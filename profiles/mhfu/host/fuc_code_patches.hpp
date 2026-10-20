#pragma once

// FUComplete's loader (PRELOAD.BIN and MODLOAD.BIN, run from
// ms0:/PSP/SAVEDATA/FUCDAT) rewrites words of the executable's code when the
// game starts: jumps from the file I/O into its own code at 0x08A35910 for its
// file replacer, and the immediates of the save keys for its Save Region
// option. The generated corpus is translated from the executable as it is on
// disc, and a generated unit runs the words it was translated from, so a word
// changed in memory never runs.
//
// The FUComplete build therefore watches the executable's code sections while
// the loader runs. When they differ from the executable it started from, it
// writes that executable with the code as the loader left it, EBOOT.FUC.ELF
// beside it, and says so: a corpus generated from that file runs FUComplete's
// patches as compiled code. main.cpp starts from EBOOT.FUC.ELF whenever the
// linked corpus was generated from it. A FUComplete configuration that patches
// other words writes the file again, and the corpus has to be generated again.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace psprecomp {
class Elf32Image;
class GuestMemory;
} // namespace psprecomp

namespace mhfu::fuc {

// The code of one executable section: where it is loaded, where its bytes are
// in the file, and the words the file holds.
struct CodeRange {
    std::uint32_t address{};
    std::uint32_t file_offset{};
    std::vector<std::uint32_t> words;
};

// The executable's code sections (SHF_EXECINSTR with bytes in the file).
[[nodiscard]] std::vector<CodeRange> code_ranges(const psprecomp::Elf32Image &elf, std::uint32_t load_base);

// The words of `ranges` that differ in guest memory from the file.
[[nodiscard]] std::size_t changed_words(const std::vector<CodeRange> &ranges, const psprecomp::GuestMemory &memory);

// The executable's bytes with every code range taken from guest memory.
[[nodiscard]] std::vector<std::uint8_t> patched_executable(
    const std::vector<std::uint8_t> &file, const std::vector<CodeRange> &ranges, const psprecomp::GuestMemory &memory);

// EBOOT.FUC.ELF beside `executable`.
[[nodiscard]] std::filesystem::path patched_path(const std::filesystem::path &executable);

// Starts watching the code of `elf`, loaded from `executable`.
void watch(const psprecomp::Elf32Image &elf, std::filesystem::path executable, std::uint32_t load_base);

// Once a game frame. Compares the code every second for the first minute,
// writes EBOOT.FUC.ELF once the loader's changes have held for a second, and
// stops. Returns true on the frame it writes the file. MHFU_FUC_CODE_PATCHES=0
// leaves the code unwatched.
bool frame(const psprecomp::GuestMemory &memory);

} // namespace mhfu::fuc
