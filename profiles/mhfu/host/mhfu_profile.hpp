#pragma once

#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace mhfu {

inline constexpr std::uint32_t kLoadBase = psprecomp::kDefaultPspUserLoadBase;
inline constexpr std::uint32_t kGuestRamBytes = 64u * 1024u * 1024u;

struct ProfilePaths {
    std::filesystem::path disc_image;   // UMD ISO; empty disables disc0:
    std::filesystem::path memory_stick; // host directory backing ms0:
};

// Installs the kernel and HLE modules, binds logging stubs for the remaining
// imports (unless MHFU_STRICT_HLE is set) and prepares the loader thread
// that runs module_start.
void install_profile(psprecomp::Runtime &runtime, const psprecomp::Elf32Image &elf, const ProfilePaths &paths);

// The host directory that backs ms0:, where the game's saves live under
// PSP/SAVEDATA. This is the only place that decides it; the rest of the host
// receives the result, so moving saves to a per-user location changes only
// this function.
[[nodiscard]] std::filesystem::path memory_stick_directory(const std::filesystem::path &game_dir);

// Load addresses of the MWo3 overlays in USA Freedom Unite's DATA.BIN, read
// from that archive: 298 images, four bases. Each image's header, code and
// data end before the next base. The last value is the end of the 32 MiB
// user map, not another overlay. The game copies one image into a base at
// a time; a jump there has no corpus until that image is recompiled.
inline constexpr std::uint32_t kOverlaySlots[] = {
    0x09A5F300u, // *_task.ovl (demo_task, game_task, movie_task, ...)
    0x09C19100u, // demo_sub.ovl, game_sub.ovl
    0x09D1A280u, // em*.ovl
    0x09D63100u, // stage*.ovl
    0x0A000000u, // end of user memory
};

// The overlay bases this run uses, ending with the end of user memory:
// kOverlaySlots until set_overlay_slots names others.
[[nodiscard]] inline std::vector<std::uint32_t> &overlay_slot_table() {
    static std::vector<std::uint32_t> table(std::begin(kOverlaySlots), std::end(kOverlaySlots));
    return table;
}
[[nodiscard]] inline std::span<const std::uint32_t> overlay_slots() {
    return overlay_slot_table();
}

// Replaces the table: ascending bases followed by the end of user memory. A
// table with no base below its end is ignored.
inline void set_overlay_slots(std::vector<std::uint32_t> slots) {
    if (slots.size() < 2u) return;
    overlay_slot_table() = std::move(slots);
}

// The overlay bases another executable names: the distinct load addresses of
// its *.ovl sections, which carry an overlay's name and address but no bytes
// (Freedom Unite's do, and so does Portable 2nd G's), below user_end, with
// user_end appended. Empty when it names none.
[[nodiscard]] inline std::vector<std::uint32_t> overlay_slots_from_sections(
    const psprecomp::Elf32Image &elf, std::uint32_t load_base, std::uint32_t user_end) {
    std::vector<std::uint32_t> bases;
    for (const psprecomp::ElfSection &section : elf.sections()) {
        if (section.name.size() <= 4u || !section.name.ends_with(".ovl") || section.name.starts_with(".rel")) continue;
        const std::uint32_t address = elf.section_runtime_address(section, load_base);
        if (address != 0u && address < user_end) bases.push_back(address);
    }
    std::sort(bases.begin(), bases.end());
    bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
    if (bases.empty()) return {};
    bases.push_back(user_end);
    return bases;
}

// The slot containing `address`, or {0, 0} when it is outside every slot.
[[nodiscard]] inline std::pair<std::uint32_t, std::uint32_t> overlay_slot_of(std::uint32_t address) {
    const std::span<const std::uint32_t> slots = overlay_slots();
    for (std::size_t i = 0; i + 1u < slots.size(); ++i) {
        if (address >= slots[i] && address < slots[i + 1u]) return {slots[i], slots[i + 1u]};
    }
    return {0u, 0u};
}

} // namespace mhfu
