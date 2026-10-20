#include "fuc_code_patches.hpp"

#include "platform/utf8_path.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/guest_memory.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

namespace mhfu::fuc {
namespace {

constexpr std::uint32_t kSectionProgbits = 1u;
constexpr std::uint32_t kSectionExecutable = 0x4u;
// The loader runs during the title screen. The code is compared once a
// second (30 game frames) for a minute, and written once a change has held
// for a whole second, so a loader still writing is not caught halfway.
constexpr std::uint64_t kCheckEvery = 30u;
constexpr std::uint64_t kWatchFrames = 30u * 60u;

std::uint32_t word_in(const std::vector<std::uint8_t> &bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) | static_cast<std::uint32_t>(bytes[at + 1u]) << 8u |
        static_cast<std::uint32_t>(bytes[at + 2u]) << 16u | static_cast<std::uint32_t>(bytes[at + 3u]) << 24u;
}

struct Watch {
    bool on{};
    std::vector<CodeRange> ranges;
    std::vector<std::uint8_t> file;
    std::filesystem::path executable;
    std::uint64_t frame{};
    std::size_t last_changed{};
    bool done{};
};

Watch &watched() {
    static Watch value;
    return value;
}

bool write_file(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
    std::filesystem::path temporary = path;
    temporary += ".part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) std::filesystem::remove(temporary, ec);
    return !ec;
}

} // namespace

std::vector<CodeRange> code_ranges(const psprecomp::Elf32Image &elf, std::uint32_t load_base) {
    std::vector<CodeRange> ranges;
    const std::vector<std::uint8_t> &bytes = elf.bytes();
    for (const psprecomp::ElfSection &section : elf.sections()) {
        if (section.type != kSectionProgbits || (section.flags & kSectionExecutable) == 0u || section.size < 4u)
            continue;
        if (section.offset > bytes.size() || section.size > bytes.size() - section.offset) continue;
        CodeRange range;
        range.address = elf.section_runtime_address(section, load_base);
        range.file_offset = section.offset;
        range.words.reserve(section.size / 4u);
        for (std::uint32_t at = 0; at + 4u <= section.size; at += 4u)
            range.words.push_back(word_in(bytes, section.offset + at));
        ranges.push_back(std::move(range));
    }
    return ranges;
}

std::size_t changed_words(const std::vector<CodeRange> &ranges, const psprecomp::GuestMemory &memory) {
    std::size_t changed = 0;
    for (const CodeRange &range : ranges) {
        const auto bytes = static_cast<std::uint32_t>(range.words.size() * 4u);
        if (memory.raw_pointer(range.address, bytes) == nullptr) continue;
        for (std::size_t i = 0; i < range.words.size(); ++i)
            if (memory.load32(range.address + static_cast<std::uint32_t>(i * 4u)) != range.words[i]) ++changed;
    }
    return changed;
}

std::vector<std::uint8_t> patched_executable(
    const std::vector<std::uint8_t> &file, const std::vector<CodeRange> &ranges, const psprecomp::GuestMemory &memory) {
    std::vector<std::uint8_t> out = file;
    for (const CodeRange &range : ranges) {
        const auto bytes = static_cast<std::uint32_t>(range.words.size() * 4u);
        if (memory.raw_pointer(range.address, bytes) == nullptr || range.file_offset + bytes > out.size()) continue;
        for (std::size_t i = 0; i < range.words.size(); ++i) {
            const std::uint32_t word = memory.load32(range.address + static_cast<std::uint32_t>(i * 4u));
            const std::size_t at = range.file_offset + i * 4u;
            for (std::size_t b = 0; b < 4u; ++b) out[at + b] = static_cast<std::uint8_t>(word >> (8u * b));
        }
    }
    return out;
}

std::filesystem::path patched_path(const std::filesystem::path &executable) {
    return executable.parent_path() / "EBOOT.FUC.ELF";
}

void watch(const psprecomp::Elf32Image &elf, std::filesystem::path executable, std::uint32_t load_base) {
    Watch &w = watched();
    w = Watch{};
    if (const char *value = std::getenv("MHFU_FUC_CODE_PATCHES"); value != nullptr && std::string_view(value) == "0")
        return;
    w.ranges = code_ranges(elf, load_base);
    w.file = elf.bytes();
    w.executable = std::move(executable);
    w.on = !w.ranges.empty();
}

bool frame(const psprecomp::GuestMemory &memory) {
    Watch &w = watched();
    if (!w.on || w.done) return false;
    ++w.frame;
    if (w.frame > kWatchFrames) {
        w.done = true;
        w.ranges.clear();
        w.file.clear();
        return false;
    }
    if (w.frame % kCheckEvery != 0u) return false;
    const std::size_t changed = changed_words(w.ranges, memory);
    const bool settled = changed != 0u && changed == w.last_changed;
    w.last_changed = changed;
    if (!settled) return false;
    w.done = true;
    const std::filesystem::path path = patched_path(w.executable);
    const bool written = write_file(path, patched_executable(w.file, w.ranges, memory));
    std::cout << "[fuc] FUComplete's loader changed " << changed << (changed == 1u ? " word" : " words")
              << " of the game's code, which the compiled code does not run. ";
    if (written) {
        std::cout << "The executable with its changes is " << path_to_utf8(path)
                  << ". Generate the corpus from it (MHFU_EXECUTABLE=<that file> "
                     "profiles/mhfu/scripts/generate.sh <build dir>) and rebuild; the game then starts from it.";
    } else {
        std::cout << "Could not write " << path_to_utf8(path) << ".";
    }
    std::cout << std::endl;
    w.ranges.clear();
    w.file.clear();
    return written;
}

} // namespace mhfu::fuc
