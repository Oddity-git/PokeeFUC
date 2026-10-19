#pragma once

// A minimal little-endian MIPS executable for tests that need one: a
// program segment of `code` loaded at `load_address`, and empty (NOBITS)
// sections with the given names and addresses, as a game's executable names
// its overlays. No game data.
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mhfu::test {

struct NamedSection {
    std::string name;
    std::uint32_t address{};
};

inline std::vector<std::uint8_t> synthetic_elf(
    std::uint32_t load_address, const std::vector<std::uint8_t> &code, const std::vector<NamedSection> &sections = {}) {
    constexpr std::uint32_t kHeader = 52u, kProgramHeader = 32u, kSectionHeader = 40u;
    std::vector<std::uint8_t> out;
    const auto put16 = [&out](std::size_t at, std::uint32_t value) {
        out[at] = static_cast<std::uint8_t>(value);
        out[at + 1u] = static_cast<std::uint8_t>(value >> 8u);
    };
    const auto put32 = [&out](std::size_t at, std::uint32_t value) {
        for (std::size_t i = 0; i < 4u; ++i) out[at + i] = static_cast<std::uint8_t>(value >> (8u * i));
    };

    // Section names: index 0 is the empty name, then each section, then
    // .shstrtab itself.
    std::string names(1u, '\0');
    std::vector<std::uint32_t> name_offsets;
    for (const NamedSection &section : sections) {
        name_offsets.push_back(static_cast<std::uint32_t>(names.size()));
        names += section.name;
        names += '\0';
    }
    const auto strtab_name = static_cast<std::uint32_t>(names.size());
    names += ".shstrtab";
    names += '\0';

    const std::uint32_t code_offset = kHeader + kProgramHeader;
    const auto names_offset = static_cast<std::uint32_t>(code_offset + code.size());
    const auto section_offset = static_cast<std::uint32_t>((names_offset + names.size() + 3u) & ~3u);
    const auto section_count = static_cast<std::uint32_t>(sections.size() + 2u);
    out.assign(section_offset + section_count * kSectionHeader, 0u);

    out[0] = 0x7Fu;
    out[1] = 'E';
    out[2] = 'L';
    out[3] = 'F';
    out[4] = 1u; // 32-bit
    out[5] = 1u; // little-endian
    out[6] = 1u;
    put16(16u, 2u); // ET_EXEC
    put16(18u, 8u); // MIPS
    put32(20u, 1u);
    put32(24u, load_address);
    put32(28u, kHeader);
    put32(32u, section_offset);
    put16(40u, kHeader);
    put16(42u, kProgramHeader);
    put16(44u, 1u);
    put16(46u, kSectionHeader);
    put16(48u, section_count);
    put16(50u, section_count - 1u);

    put32(kHeader + 0u, 1u); // PT_LOAD
    put32(kHeader + 4u, code_offset);
    put32(kHeader + 8u, load_address);
    put32(kHeader + 12u, load_address);
    put32(kHeader + 16u, static_cast<std::uint32_t>(code.size()));
    put32(kHeader + 20u, static_cast<std::uint32_t>(code.size()));
    put32(kHeader + 24u, 7u);
    put32(kHeader + 28u, 16u);
    std::copy(code.begin(), code.end(), out.begin() + code_offset);
    std::copy(names.begin(), names.end(), out.begin() + names_offset);

    for (std::size_t i = 0; i < sections.size(); ++i) {
        const std::size_t at = section_offset + (i + 1u) * kSectionHeader;
        put32(at + 0u, name_offsets[i]);
        put32(at + 4u, 8u); // SHT_NOBITS: named, with no bytes in the file
        put32(at + 8u, 3u);
        put32(at + 12u, sections[i].address);
        put32(at + 16u, names_offset);
    }
    const std::size_t strtab = section_offset + (section_count - 1u) * kSectionHeader;
    put32(strtab + 0u, strtab_name);
    put32(strtab + 4u, 3u); // SHT_STRTAB
    put32(strtab + 16u, names_offset);
    put32(strtab + 20u, static_cast<std::uint32_t>(names.size()));
    return out;
}

} // namespace mhfu::test
