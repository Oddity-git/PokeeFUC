#include "fuc_code_patches.hpp"

#include "platform/utf8_path.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/guest_memory.hpp"
#include "psprecomp/interpreter.hpp"
#include "psprecomp/common.hpp"
#include "psprecomp/runtime.hpp"

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
    bool compiled{};
    std::vector<CodeRange> ranges;
    std::vector<std::uint8_t> file;
    std::filesystem::path executable;
    std::uint64_t frame{};
    std::size_t last_changed{};
    bool done{};
    // Words already handed to the interpreter, and the registrations replaced.
    std::set<std::uint32_t> interpreted_words;
    std::set<std::uint32_t> interpreted;
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

// Runs the guest code at ctx.pc, as it is in memory, until it reaches another
// registered address. The dispatcher calls it in place of a compiled function.
void interpret(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    (void)psprecomp::interpret_allegrex(runtime, ctx);
}

// With a compiled executable: the changed words not yet interpreted go to the
// interpreter, with the code around them.
void interpret_new_changes(psprecomp::Runtime &runtime, Watch &w) {
    if (!w.compiled || !interpret_changes()) return;
    std::vector<std::uint32_t> fresh;
    for (const std::uint32_t address : changed_addresses(w.ranges, runtime.memory()))
        if (w.interpreted_words.insert(address).second) fresh.push_back(address);
    if (fresh.empty()) return;
    const std::size_t replaced = run_interpreted(runtime, fresh, w.interpreted);
    std::cout << "[fuc] FUComplete's loader changed " << fresh.size() << (fresh.size() == 1u ? " word" : " words")
              << " of the code compiled from " << path_to_utf8(w.executable.filename()) << " (first at "
              << psprecomp::hex32(fresh.front()) << "); " << replaced << " compiled blocks around them run interpreted"
              << std::endl;
}

} // namespace

bool interpret_changes() {
    static const bool on = [] {
        const char *value = std::getenv("MHFU_FUC_INTERPRET_CHANGES");
        return value == nullptr || std::string_view(value) != "0";
    }();
    return on;
}

std::vector<std::uint32_t> changed_addresses(
    const std::vector<CodeRange> &ranges, const psprecomp::GuestMemory &memory) {
    std::vector<std::uint32_t> changed;
    for (const CodeRange &range : ranges) {
        const auto bytes = static_cast<std::uint32_t>(range.words.size() * 4u);
        if (memory.raw_pointer(range.address, bytes) == nullptr) continue;
        for (std::size_t i = 0; i < range.words.size(); ++i) {
            const std::uint32_t address = range.address + static_cast<std::uint32_t>(i * 4u);
            if (memory.load32(address) != range.words[i]) changed.push_back(address);
        }
    }
    return changed;
}

std::size_t run_interpreted(
    psprecomp::Runtime &runtime, std::span<const std::uint32_t> addresses, std::set<std::uint32_t> &done) {
    std::size_t replaced = 0;
    for (const std::uint32_t word : addresses) {
        const std::uint32_t first = word >= kUnitSpan ? word - kUnitSpan + 4u : 0u;
        for (std::uint32_t at = first; at < word + kUnitSpan; at += 4u) {
            if (done.contains(at) || !runtime.has_function(at)) continue;
            runtime.register_function(at, &interpret, "mhfu_fuc_interpreted");
            done.insert(at);
            ++replaced;
        }
    }
    return replaced;
}

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

void watch(const psprecomp::Elf32Image &elf, std::filesystem::path executable, std::uint32_t load_base, bool compiled) {
    Watch &w = watched();
    w = Watch{};
    w.compiled = compiled;
    if (const char *value = std::getenv("MHFU_FUC_CODE_PATCHES"); value != nullptr && std::string_view(value) == "0")
        return;
    w.ranges = code_ranges(elf, load_base);
    w.file = elf.bytes();
    w.executable = std::move(executable);
    w.on = !w.ranges.empty();
}

void code_flushed(psprecomp::Runtime &runtime) {
    Watch &w = watched();
    if (w.on && !w.done) interpret_new_changes(runtime, w);
}

bool frame(psprecomp::Runtime &runtime) {
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
    // The corpus was generated from this file: it is not written again, since
    // the next start would then match no corpus at all.
    if (w.compiled) {
        interpret_new_changes(runtime, w);
        return false;
    }
    const psprecomp::GuestMemory &memory = runtime.memory();
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
