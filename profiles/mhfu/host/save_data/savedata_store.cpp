#include "save_data/savedata_store.hpp"

#include "save_data/param_sfo.hpp"
#include "save_data/savedata_crypto.hpp"

#include "platform/utf8_path.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <random>
#include <string_view>

namespace mhfu::savedata {
namespace {

constexpr const char *kParamSfo = "PARAM.SFO";
// SAVEDATA_FILE_LIST: 99 entries of a 13-byte name, its 16-byte hash and 3
// bytes of padding.
constexpr std::size_t kFileListEntries = 99u;
constexpr std::size_t kFileListEntrySize = 32u;
constexpr std::size_t kFileListNameSize = 13u;
// The mode a PSP picks for a game built with SDK 4 or later that passes a
// key, which is what this game does.
constexpr CryptMode kSaveMode = CryptMode::Mode5;

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_file(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
    // Write beside the target and rename, so an interrupted save never leaves
    // a half-written file in place of a good one.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    return !ec;
}

// A hash stored in the file list for `name`, if any.
std::optional<Block> listed_hash(const ParamSfo &sfo, const std::string &name) {
    const auto *list = sfo.binary("SAVEDATA_FILE_LIST");
    if (list == nullptr) return std::nullopt;
    for (std::size_t offset = 0; offset + kFileListEntrySize <= list->size(); offset += kFileListEntrySize) {
        std::string entry_name;
        for (std::size_t i = 0; i < kFileListNameSize && (*list)[offset + i] != 0u; ++i)
            entry_name.push_back(static_cast<char>((*list)[offset + i]));
        if (entry_name != name) continue;
        Block hash{};
        std::copy_n(list->begin() + static_cast<std::ptrdiff_t>(offset + kFileListNameSize), hash.size(), hash.begin());
        return hash;
    }
    return std::nullopt;
}

Block random_block() {
    std::random_device device;
    std::mt19937_64 engine(
        device() ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    Block block{};
    for (auto &b : block) b = static_cast<std::uint8_t>(engine());
    return block;
}

} // namespace

std::filesystem::path savedata_root(const std::filesystem::path &memory_stick) {
    return memory_stick / "PSP" / "SAVEDATA";
}

std::filesystem::path save_folder(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    return savedata_root(memory_stick) / (files.game_name + files.save_name);
}

bool save_exists(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    std::error_code ec;
    const auto folder = save_folder(memory_stick, files);
    return std::filesystem::is_directory(folder, ec) && std::filesystem::is_regular_file(folder / kParamSfo, ec);
}

LoadResult load_save(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    LoadResult result;
    const auto folder = save_folder(memory_stick, files);
    if (!save_exists(memory_stick, files)) return result;
    const auto sfo_bytes = read_file(folder / kParamSfo);
    const auto sfo = sfo_bytes ? ParamSfo::parse(*sfo_bytes) : std::nullopt;
    const auto file = read_file(folder / files.file_name);
    if (!file) return result; // the folder exists but the game's file does not
    result.status = LoadStatus::Broken;
    if (!sfo) {
        result.reason = "PARAM.SFO is unreadable";
        return result;
    }

    const auto *params = sfo->binary("SAVEDATA_PARAMS");
    const std::uint8_t flags = params != nullptr && !params->empty() ? (*params)[kParamsFlagsOffset] : 0u;
    if (flags != 0u) {
        const auto mode = mode_from_flags(flags);
        if (!mode) {
            result.reason = "unknown SAVEDATA_PARAMS flags " + std::to_string(flags);
            return result;
        }
        const Block *key = files.key ? &*files.key : nullptr;
        if (*mode != CryptMode::Mode1 && key == nullptr) {
            result.reason = "the save is encrypted with a game key but the game passed none";
            return result;
        }
        if (const auto expected = listed_hash(*sfo, files.file_name);
            expected && *expected != data_file_hash(*file, *mode, key)) {
            result.reason = files.file_name + " does not match its hash in PARAM.SFO";
            result.hash_mismatch = true;
            return result;
        }
        auto plain = decrypt_data(*file, *mode, key);
        if (!plain) {
            result.reason = files.file_name + " is too short to be encrypted";
            return result;
        }
        result.contents.data = std::move(*plain);
    } else {
        result.contents.data = *file;
    }
    result.contents.title = sfo->string("TITLE").value_or("");
    result.contents.savedata_title = sfo->string("SAVEDATA_TITLE").value_or("");
    result.contents.detail = sfo->string("SAVEDATA_DETAIL").value_or("");
    result.contents.parental_level = sfo->integer("PARENTAL_LEVEL").value_or(0u);
    result.status = LoadStatus::Ok;
    return result;
}

namespace {

constexpr const char *kKeyFile = "savedata_keys.txt";

std::filesystem::path key_file(const std::filesystem::path &memory_stick) {
    return memory_stick.parent_path() / kKeyFile;
}

struct KnownKey {
    std::string game_name;
    Block key{};
};

std::optional<Block> parse_key(std::string_view hex) {
    if (hex.size() != 32u) return std::nullopt;
    Block key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        unsigned value = 0u;
        for (std::size_t d = 0; d < 2u; ++d) {
            const char c = hex[i * 2u + d];
            value <<= 4u;
            if (c >= '0' && c <= '9')
                value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<unsigned>(c - 'A' + 10);
            else
                return std::nullopt;
        }
        key[i] = static_cast<std::uint8_t>(value);
    }
    return key;
}

std::string key_hex(const Block &key) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t byte : key) {
        out += kDigits[byte >> 4u];
        out += kDigits[byte & 0xFu];
    }
    return out;
}

std::vector<KnownKey> read_keys(const std::filesystem::path &memory_stick) {
    std::vector<KnownKey> keys;
    std::ifstream in(key_file(memory_stick));
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t space = line.find(' ');
        if (space == std::string::npos || space == 0u) continue;
        std::string_view hex = std::string_view(line).substr(space + 1u);
        while (!hex.empty() && (hex.back() == '\r' || hex.back() == ' ')) hex.remove_suffix(1u);
        if (const std::optional<Block> key = parse_key(hex)) keys.push_back({line.substr(0, space), *key});
    }
    return keys;
}

} // namespace

void note_game_key(const std::filesystem::path &memory_stick, const std::string &game_name, const Block &key) {
    if (game_name.empty() || is_zero(key) || memory_stick.empty()) return;
    // Read once per memory stick: the game passes its key with every request.
    static std::filesystem::path noted_for;
    static std::vector<KnownKey> noted;
    if (noted_for != memory_stick) {
        noted_for = memory_stick;
        noted = read_keys(memory_stick);
    }
    for (const KnownKey &known : noted)
        if (known.game_name == game_name && known.key == key) return;
    noted.push_back({game_name, key});
    std::ofstream out(key_file(memory_stick), std::ios::app);
    out << game_name << ' ' << key_hex(key) << '\n';
}

std::vector<Block> earlier_game_keys(
    const std::filesystem::path &memory_stick, const std::string &game_name, const Block &current) {
    std::vector<Block> keys;
    for (const KnownKey &known : read_keys(memory_stick))
        if (known.game_name == game_name && known.key != current &&
            std::find(keys.begin(), keys.end(), known.key) == keys.end())
            keys.push_back(known.key);
    return keys;
}

LoadResult load_save_with_earlier_key(
    const std::filesystem::path &memory_stick, const SaveFiles &files, bool &used_earlier) {
    used_earlier = false;
    LoadResult result = load_save(memory_stick, files);
    if (!result.hash_mismatch || !files.key) return result;
    for (const Block &key : earlier_game_keys(memory_stick, files.game_name, *files.key)) {
        SaveFiles earlier = files;
        earlier.key = key;
        LoadResult retry = load_save(memory_stick, earlier);
        if (retry.status == LoadStatus::Ok) {
            used_earlier = true;
            return retry;
        }
    }
    return result;
}

bool write_save(const std::filesystem::path &memory_stick, const SaveFiles &files, const SaveContents &contents,
    std::string &error) {
    const auto folder = save_folder(memory_stick, files);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        error = "cannot create " + path_to_utf8(folder) + ": " + ec.message();
        return false;
    }

    // Keep what an existing PARAM.SFO lists for other files.
    std::optional<ParamSfo> previous;
    if (const auto bytes = read_file(folder / kParamSfo)) previous = ParamSfo::parse(*bytes);
    std::vector<std::uint8_t> file_list(kFileListEntries * kFileListEntrySize, 0u);
    if (previous) {
        if (const auto *list = previous->binary("SAVEDATA_FILE_LIST"); list != nullptr)
            std::copy_n(list->begin(), std::min(list->size(), file_list.size()), file_list.begin());
    }

    std::vector<std::uint8_t> stored;
    if (files.key) {
        stored = encrypt_data(contents.data, kSaveMode, &*files.key, random_block());
        const Block hash = data_file_hash(stored, kSaveMode, &*files.key);
        // Replace this file's entry, or take the first free one.
        std::size_t slot = file_list.size();
        for (std::size_t offset = 0; offset < file_list.size(); offset += kFileListEntrySize) {
            const std::string name(reinterpret_cast<const char *>(&file_list[offset]),
                strnlen(reinterpret_cast<const char *>(&file_list[offset]), kFileListNameSize));
            if (name == files.file_name) {
                slot = offset;
                break;
            }
            if (name.empty() && slot == file_list.size()) slot = offset;
        }
        if (slot == file_list.size()) {
            error = "SAVEDATA_FILE_LIST is full";
            return false;
        }
        std::fill_n(file_list.begin() + static_cast<std::ptrdiff_t>(slot), kFileListEntrySize, 0u);
        std::copy_n(files.file_name.begin(), std::min(files.file_name.size(), kFileListNameSize - 1u),
            file_list.begin() + static_cast<std::ptrdiff_t>(slot));
        std::copy(hash.begin(), hash.end(), file_list.begin() + static_cast<std::ptrdiff_t>(slot + kFileListNameSize));
    } else {
        stored = contents.data;
    }

    ParamSfo sfo;
    sfo.set_string("CATEGORY", "MS", 4u);
    sfo.set_integer("PARENTAL_LEVEL", contents.parental_level);
    sfo.set_string("SAVEDATA_DETAIL", contents.detail, 1024u);
    sfo.set_string("SAVEDATA_DIRECTORY", files.game_name + files.save_name, 64u);
    sfo.set_binary("SAVEDATA_FILE_LIST", file_list, static_cast<std::uint32_t>(file_list.size()));
    sfo.set_binary("SAVEDATA_PARAMS", std::vector<std::uint8_t>(kParamsSize, 0u), kParamsSize);
    sfo.set_string("SAVEDATA_TITLE", contents.savedata_title, 128u);
    sfo.set_string("TITLE", contents.title, 128u);
    std::vector<std::uint8_t> sfo_bytes = sfo.serialize();
    if (files.key) sign_param_sfo(sfo_bytes, *sfo.data_offset("SAVEDATA_PARAMS"), kSaveMode);

    const std::pair<const char *, const std::vector<std::uint8_t> *> media[] = {{"ICON0.PNG", &contents.icon0},
        {"ICON1.PMF", &contents.icon1}, {"PIC1.PNG", &contents.pic1}, {"SND0.AT3", &contents.snd0}};
    for (const auto &[name, bytes] : media) {
        if (bytes->empty()) continue;
        if (!write_file(folder / name, *bytes)) {
            error = std::string("cannot write ") + name;
            return false;
        }
    }
    // The data file first and PARAM.SFO last: PARAM.SFO carries the data
    // file's hash, so a save interrupted in between is reported as broken
    // rather than loaded with mismatched contents.
    if (!write_file(folder / files.file_name, stored)) {
        error = "cannot write " + files.file_name;
        return false;
    }
    if (!write_file(folder / kParamSfo, sfo_bytes)) {
        error = "cannot write PARAM.SFO";
        return false;
    }
    return true;
}

bool delete_save(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    std::error_code ec;
    const auto removed = std::filesystem::remove_all(save_folder(memory_stick, files), ec);
    return !ec && removed > 0u;
}

std::uint64_t save_size(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    std::error_code ec;
    std::uint64_t total = 0u;
    const auto folder = save_folder(memory_stick, files);
    if (!std::filesystem::is_directory(folder, ec)) return 0u;
    for (const auto &entry : std::filesystem::directory_iterator(folder, ec))
        if (entry.is_regular_file(ec)) total += entry.file_size(ec);
    return total;
}

} // namespace mhfu::savedata
