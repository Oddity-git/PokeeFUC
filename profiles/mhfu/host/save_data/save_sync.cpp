#include "save_data/save_sync.hpp"

#include "save_data/param_sfo.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <vector>

namespace mhfu::savedata::sync {
namespace fs = std::filesystem;
namespace {

bool is_game_folder_name(std::string_view name) {
    return name == "ULUS10391" || name == "ULJM05500" || name == "ULES01213";
}

// The files that make up the save: PARAM.SFO and the data files it lists.
// None when PARAM.SFO is missing or cannot be read; the folder then hashes
// every file it holds, which is right for a folder being filled.
std::optional<std::vector<std::string>> save_files(const fs::path &folder) {
    std::ifstream in(folder / "PARAM.SFO", std::ios::binary);
    if (!in) return std::nullopt;
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const std::optional<ParamSfo> sfo = ParamSfo::parse(bytes);
    if (!sfo) return std::nullopt;
    const std::vector<std::uint8_t> *list = sfo->binary("SAVEDATA_FILE_LIST");
    if (list == nullptr) return std::nullopt;
    std::vector<std::string> names{"PARAM.SFO"};
    std::string name;
    for (const std::uint8_t byte : *list) {
        if (byte != 0u) {
            name.push_back(static_cast<char>(byte));
            continue;
        }
        if (!name.empty()) {
            names.push_back(name);
            name.clear();
        }
    }
    if (!name.empty()) names.push_back(name);
    if (names.size() <= 1u) return std::nullopt;
    return names;
}

} // namespace

Decision decide(std::string_view local_hash, std::string_view remote_hash, std::string_view last_hash) {
    if (local_hash.empty()) return Decision::NoLocal;
    if (remote_hash.empty()) return Decision::NoRemote;
    if (local_hash == remote_hash) return Decision::InSync;
    if (last_hash.empty()) return Decision::FirstSync;
    if (remote_hash == last_hash) return Decision::Upload;
    if (local_hash == last_hash) return Decision::Download;
    return Decision::Conflict;
}

std::string hash_folder(const fs::path &folder) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return {};
    std::vector<std::string> names;
    if (const std::optional<std::vector<std::string>> listed = save_files(folder))
        names = *listed;
    else
        for (const fs::directory_entry &entry : fs::directory_iterator(folder, ec))
            if (entry.is_regular_file(ec)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::string text;
    for (const std::string &name : names) {
        if (name.size() >= 5u && name.compare(name.size() - 5u, 5u, ".part") == 0) continue;
        text += name;
        text += '\0';
        const fs::path file = folder / name;
        text += fs::is_regular_file(file, ec) ? psprecomp::sha256_file(file) : std::string("missing");
        text += '\n';
    }
    if (text.empty()) return {};
    const std::string bytes = psprecomp::sha256_bytes(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
    return bytes;
}

bool copy_folder(const fs::path &from, const fs::path &to, std::string &error) {
    std::error_code ec;
    if (!fs::is_directory(from, ec)) {
        error = "there is no save folder to copy";
        return false;
    }
    fs::create_directories(to, ec);
    if (ec) {
        error = "cannot create the folder";
        return false;
    }
    for (const fs::directory_entry &entry : fs::directory_iterator(from, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const fs::path source = entry.path();
        const fs::path target = to / source.filename();
        const fs::path part = to / (source.filename().string() + ".part");
        {
            std::ifstream in(source, std::ios::binary);
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            if (!in || !out) {
                error = "cannot read " + source.filename().string();
                fs::remove(part, ec);
                return false;
            }
            out << in.rdbuf();
            if (!out) {
                error = "cannot write " + target.filename().string();
                fs::remove(part, ec);
                return false;
            }
        }
        fs::remove(target, ec);
        fs::rename(part, target, ec);
        if (ec) {
            error = "cannot replace " + target.filename().string();
            fs::remove(part, ec);
            return false;
        }
    }
    if (ec) {
        error = "cannot read the save folder";
        return false;
    }
    return true;
}

const char *decision_text(Decision decision) {
    switch (decision) {
    case Decision::NoLocal:
        return "This device has no save yet.";
    case Decision::NoRemote:
        return "The folder has no save yet; upload this device's save.";
    case Decision::InSync:
        return "Both have the same save.";
    case Decision::Upload:
        return "This device has a save the folder does not; sync uploads it.";
    case Decision::Download:
        return "The folder has a save this device does not; sync downloads it.";
    case Decision::FirstSync:
        return "First sync: the two saves differ. Choose which one to keep.";
    case Decision::Conflict:
        return "Both changed since the last sync. Choose which one to keep.";
    }
    return "";
}

fs::path game_save_folder(const fs::path &memory_stick) {
    std::error_code ec;
    const fs::path savedata = memory_stick / "PSP" / "SAVEDATA";
    if (!fs::is_directory(savedata, ec)) return {};
    for (const fs::directory_entry &entry : fs::directory_iterator(savedata, ec)) {
        if (!entry.is_directory(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (!is_game_folder_name(name)) continue;
        if (fs::is_regular_file(entry.path() / "PARAM.SFO", ec)) return entry.path();
    }
    return {};
}

} // namespace mhfu::savedata::sync
