#pragma once

// FUComplete's options, as its loader reads them from CONFIG.BIN in
// ms0:/PSP/SAVEDATA/FUCDAT when the game starts. The menu shows them in the
// Game tab of the FUComplete build and writes the file; a change applies at
// the next start. Most options make the loader change the game's code, which
// the compiled code runs through the interpreter (fuc_code_patches.hpp), so no
// corpus has to be generated for them.
//
// The file is 64 bytes: a version string ("1.4.0 ENG") and from 0x10 one byte
// per option, the field of view a halfword. The offsets and values are the
// file format's, as FUComplete 1.4.0's configuration tool writes them; the
// descriptions here are this project's.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mhfu::fuc_config {

struct Choice {
    const char *label;
    std::uint16_t value; // the option's bytes in file order, high byte first for two
};

struct Option {
    const char *label;
    std::uint32_t offset;
    std::uint32_t bytes; // 1, or 2 for the field of view
    std::span<const Choice> choices;
    const char *description;
    // Shown, but not changed from the menu: Save Region picks which save the
    // game loads, and a wrong one shows no character.
    bool locked{};
};

[[nodiscard]] std::span<const Option> options();

inline constexpr std::size_t kFileBytes = 64u;

// CONFIG.BIN under a memory stick directory.
[[nodiscard]] std::filesystem::path config_path(const std::filesystem::path &memory_stick);

// The file's bytes. None when it is missing or too short to hold every option.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> read(const std::filesystem::path &path);

// The option's value in `bytes`.
[[nodiscard]] std::uint16_t value(const std::vector<std::uint8_t> &bytes, const Option &option);
// Its index among the option's choices, or -1 for a value none of them has.
[[nodiscard]] int choice(const std::vector<std::uint8_t> &bytes, const Option &option);
// Sets the option to choice `index` in `bytes`.
void set(std::vector<std::uint8_t> &bytes, const Option &option, std::size_t index);

// Writes `bytes` to `path` through a temporary file. False on failure.
bool write(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes);

} // namespace mhfu::fuc_config
