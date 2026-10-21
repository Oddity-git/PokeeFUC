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
// the loader runs. When they differ from an executable no corpus was generated
// from, it writes that executable with the code as the loader left it,
// EBOOT.FUC.ELF beside it, and says so: a corpus generated from that file runs
// FUComplete's patches as compiled code. main.cpp starts from EBOOT.FUC.ELF
// whenever the linked corpus was generated from it.
//
// Running from the file the corpus was generated from, a FUComplete
// configuration other than the one it was generated with changes other words.
// The file is then left alone, and each changed word's code goes to the
// interpreter instead (run_interpreted), so CONFIG.BIN's options work without
// generating the corpus again. The corpus is cut into basic blocks that jump to
// each other directly within a 16 KiB unit, so every block of the unit around a
// changed word is interpreted, not only the block that holds it; the rest of
// the game stays compiled. MHFU_FUC_INTERPRET_CHANGES=0 leaves the compiled code
// as it is.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <span>
#include <vector>

namespace psprecomp {
class Elf32Image;
class GuestMemory;
class Runtime;
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

// The addresses of those words.
[[nodiscard]] std::vector<std::uint32_t> changed_addresses(
    const std::vector<CodeRange> &ranges, const psprecomp::GuestMemory &memory);

// The span a generated unit covers: every compiled block that can jump to a
// block holding a changed word, without passing through dispatch, starts
// within this distance of it.
inline constexpr std::uint32_t kUnitSpan = 0x4000u;

// Puts the interpreter in place of every compiled function registered within
// kUnitSpan of each address, except at addresses in `done`, which it adds to.
// Returns how many registrations it replaced.
std::size_t run_interpreted(
    psprecomp::Runtime &runtime, std::span<const std::uint32_t> addresses, std::set<std::uint32_t> &done);

// Whether MHFU_FUC_INTERPRET_CHANGES leaves changed code to the interpreter (the default).
[[nodiscard]] bool interpret_changes();

// The executable's bytes with every code range taken from guest memory.
[[nodiscard]] std::vector<std::uint8_t> patched_executable(
    const std::vector<std::uint8_t> &file, const std::vector<CodeRange> &ranges, const psprecomp::GuestMemory &memory);

// EBOOT.FUC.ELF beside `executable`.
[[nodiscard]] std::filesystem::path patched_path(const std::filesystem::path &executable);

// Starts watching the code of `elf`, loaded from `executable`. `compiled`:
// the linked corpus was generated from that very file.
void watch(
    const psprecomp::Elf32Image &elf, std::filesystem::path executable, std::uint32_t load_base, bool compiled = false);

// At an instruction cache flush, which the loader does after writing code:
// with a compiled executable, interprets the code of the words changed since.
void code_flushed(psprecomp::Runtime &runtime);

// Once a game frame. Compares the code every second for the first minute.
// With a compiled executable, interprets the code of the words changed since.
// Otherwise it writes EBOOT.FUC.ELF once the loader's changes have held for a
// second, and stops; it returns true on the frame it writes the file.
// MHFU_FUC_CODE_PATCHES=0 leaves the code unwatched.
bool frame(psprecomp::Runtime &runtime);

} // namespace mhfu::fuc
