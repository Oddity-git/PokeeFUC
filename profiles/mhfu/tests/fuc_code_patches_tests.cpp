// FUComplete's loader changes words of the executable's code in memory. The
// watch finds them and writes the executable with them. No game data: the
// executable is a synthetic one.
#include "fuc_code_patches.hpp"
#include "synthetic_elf.hpp"

#include "psprecomp/elf32.hpp"
#include "psprecomp/guest_memory.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void set_environment(const char *name, const std::string &value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

constexpr std::uint32_t kLoad = 0x08804000u;

// 16 words of "code", then 8 of data the game writes as it runs.
std::vector<std::uint8_t> image_bytes() {
    std::vector<std::uint8_t> code(24u * 4u);
    for (std::size_t i = 0; i < code.size(); ++i) code[i] = static_cast<std::uint8_t>(i);
    return mhfu::test::synthetic_elf(kLoad, code, {{".text", kLoad, true, 16u * 4u}, {"game_task.ovl", 0x09A5A580u}});
}

std::vector<std::uint8_t> read(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void test_ranges_and_changes() {
    const psprecomp::Elf32Image elf = psprecomp::Elf32Image::from_bytes(image_bytes(), "synthetic");
    const std::vector<mhfu::fuc::CodeRange> ranges = mhfu::fuc::code_ranges(elf, kLoad);
    check(ranges.size() == 1u, "the one code section is found, not the data or the empty section");
    check(ranges.size() == 1u && ranges[0].address == kLoad && ranges[0].words.size() == 16u,
        "it starts at the load address and holds its 16 words");
    if (ranges.size() != 1u) return;
    check(ranges[0].words[1] == 0x07060504u, "the words are read little-endian from the file");

    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    (void)elf.load_and_relocate(memory, kLoad);
    check(mhfu::fuc::changed_words(ranges, memory) == 0u, "as loaded, nothing has changed");
    memory.store32(kLoad + 16u * 4u, 0xDEADBEEFu);
    check(mhfu::fuc::changed_words(ranges, memory) == 0u, "a write to data is not a change to the code");
    memory.store32(kLoad + 4u, 0x0A28D644u); // j 0x08A35910, as the loader hooks the file I/O
    memory.store32(kLoad + 8u, 0u);
    check(mhfu::fuc::changed_words(ranges, memory) == 2u, "the loader's two words are found");

    const std::vector<std::uint8_t> patched = mhfu::fuc::patched_executable(elf.bytes(), ranges, memory);
    check(patched.size() == elf.bytes().size(), "the patched executable keeps its size");
    const psprecomp::Elf32Image again = psprecomp::Elf32Image::from_bytes(patched, "patched");
    check(again.read_word_at_vaddr(kLoad + 4u) == 0x0A28D644u && again.read_word_at_vaddr(kLoad + 8u) == 0u,
        "the patched executable holds the loader's words");
    check(again.read_word_at_vaddr(kLoad + 16u * 4u) == elf.read_word_at_vaddr(kLoad + 16u * 4u),
        "and the file's data, not what the game wrote there");
    check(mhfu::fuc::changed_words(mhfu::fuc::code_ranges(again, kLoad), memory) == 0u,
        "a game started from it finds nothing changed");
}

void test_watch_writes_once_settled() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "pokee_fuc_code_patches_tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path executable = dir / "EBOOT.ELF";
    {
        const std::vector<std::uint8_t> bytes = image_bytes();
        std::ofstream(executable, std::ios::binary)
            .write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    check(mhfu::fuc::patched_path(executable) == dir / "EBOOT.FUC.ELF", "the patched file sits beside the executable");
    const psprecomp::Elf32Image elf = psprecomp::Elf32Image::from_file(executable);
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    (void)elf.load_and_relocate(memory, kLoad);
    mhfu::fuc::watch(elf, executable, kLoad);

    bool written = false;
    for (int frame = 0; frame < 60 && !written; ++frame) written = mhfu::fuc::frame(memory);
    check(!written && !std::filesystem::exists(dir / "EBOOT.FUC.ELF"), "unchanged code writes nothing");

    memory.store32(kLoad + 12u, 0x0A28D67Bu);
    int frames = 0;
    while (!written && frames < 200) {
        written = mhfu::fuc::frame(memory);
        ++frames;
    }
    check(written, "a change that holds is written");
    check(frames > 30, "only after it has held for a check");
    const std::vector<std::uint8_t> file = read(dir / "EBOOT.FUC.ELF");
    check(!file.empty() &&
            psprecomp::Elf32Image::from_bytes(file, "written").read_word_at_vaddr(kLoad + 12u) == 0x0A28D67Bu,
        "the file holds the changed word");
    for (int frame = 0; frame < 120; ++frame) check(!mhfu::fuc::frame(memory), "and is written once");

    set_environment("MHFU_FUC_CODE_PATCHES", "0");
    std::filesystem::remove(dir / "EBOOT.FUC.ELF");
    mhfu::fuc::watch(elf, executable, kLoad);
    for (int frame = 0; frame < 200; ++frame) (void)mhfu::fuc::frame(memory);
    check(!std::filesystem::exists(dir / "EBOOT.FUC.ELF"), "MHFU_FUC_CODE_PATCHES=0 leaves the code unwatched");
    set_environment("MHFU_FUC_CODE_PATCHES", "");
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    test_ranges_and_changes();
    test_watch_writes_once_settled();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "fuc code patches: ok\n";
    return 0;
}
