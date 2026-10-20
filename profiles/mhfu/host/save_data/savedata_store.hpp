#pragma once

// Saves on the host, laid out as on a PSP memory stick:
//   <ms0>/PSP/SAVEDATA/<gameName><saveName>/PARAM.SFO, ICON0.PNG, ..., <fileName>
// so a folder can be copied to or from a real memory stick unchanged.
#include "save_data/aes128.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mhfu::savedata {

struct SaveFiles {
    std::string game_name;    // e.g. "ULUS10391"
    std::string save_name;    // appended to the game name; may be empty
    std::string file_name;    // the data file, e.g. "MHP2NDG.BIN"
    std::optional<Block> key; // game key; saves are encrypted with it, and plain without one
};

struct SaveContents {
    std::vector<std::uint8_t> data; // plaintext
    std::string title;
    std::string savedata_title;
    std::string detail;
    std::uint32_t parental_level{};
    std::vector<std::uint8_t> icon0; // ICON0.PNG
    std::vector<std::uint8_t> icon1; // ICON1.PMF
    std::vector<std::uint8_t> pic1;  // PIC1.PNG
    std::vector<std::uint8_t> snd0;  // SND0.AT3
};

enum class LoadStatus { Ok, NoData, Broken };

struct LoadResult {
    LoadStatus status{LoadStatus::NoData};
    SaveContents contents;
    std::string reason;   // why a save is broken, for the log
    bool hash_mismatch{}; // broken because its data does not match the hash the key gives
};

// The game keys a game has passed with its saves, kept in savedata_keys.txt
// beside the memory stick folder (one "<gameName> <32 hex digits>" a line).
// A game whose code changes its key, as FUComplete's Save Region option
// does, then still finds the saves it wrote with the key it used before:
// load_save_with_earlier_key tries those when the current key's hash does not
// match, and the game's next save writes the folder with the current key.
void note_game_key(const std::filesystem::path &memory_stick, const std::string &game_name, const Block &key);
[[nodiscard]] std::vector<Block> earlier_game_keys(
    const std::filesystem::path &memory_stick, const std::string &game_name, const Block &current);
// load_save, and when that finds the data does not match the hash the
// current key gives, load_save with each earlier key of the game until one
// matches. `used_earlier` says one did.
[[nodiscard]] LoadResult load_save_with_earlier_key(
    const std::filesystem::path &memory_stick, const SaveFiles &files, bool &used_earlier);

// PSP/SAVEDATA under the memory stick root.
[[nodiscard]] std::filesystem::path savedata_root(const std::filesystem::path &memory_stick);
[[nodiscard]] std::filesystem::path save_folder(const std::filesystem::path &memory_stick, const SaveFiles &files);

[[nodiscard]] bool save_exists(const std::filesystem::path &memory_stick, const SaveFiles &files);
[[nodiscard]] LoadResult load_save(const std::filesystem::path &memory_stick, const SaveFiles &files);
// Writes the folder; returns false with `error` set on failure.
bool write_save(const std::filesystem::path &memory_stick, const SaveFiles &files, const SaveContents &contents,
    std::string &error);
bool delete_save(const std::filesystem::path &memory_stick, const SaveFiles &files);

// Total size of the folder's files in bytes, 0 when it does not exist.
[[nodiscard]] std::uint64_t save_size(const std::filesystem::path &memory_stick, const SaveFiles &files);

} // namespace mhfu::savedata
