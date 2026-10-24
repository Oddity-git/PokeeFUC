// The installer's disc checks, against small ISO9660 images built here from
// public bytes. Each image is wrong in one way, and the message must say how.
// No game data: the executables are synthetic or a few placeholder bytes.
#include "install/game_identity.hpp"
#include "install/installer.hpp"
#include "save_data/param_sfo.hpp"
#include "synthetic_elf.hpp"
#include "title.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string &message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using Files = std::map<std::string, std::vector<std::uint8_t>>;

constexpr std::size_t kBlock = 2048u;

// An ISO9660 image holding `files` (paths with '/'), one block per directory.
std::vector<std::uint8_t> iso_image(const Files &files) {
    struct Node {
        std::map<std::string, Node> children;
        const std::vector<std::uint8_t> *data = nullptr;
        std::uint32_t lba = 0u;
        std::uint32_t size = 0u;
    };
    Node root;
    for (const auto &[path, data] : files) {
        Node *node = &root;
        std::size_t start = 0u;
        for (std::size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', start)) {
            node = &node->children[path.substr(start, slash - start)];
            start = slash + 1u;
        }
        node->children[path.substr(start)].data = &data;
    }
    std::uint32_t next = 17u;
    const auto place = [&](auto &self, Node &node) -> void {
        node.lba = next;
        node.size = node.data ? static_cast<std::uint32_t>(node.data->size()) : static_cast<std::uint32_t>(kBlock);
        next += node.data ? static_cast<std::uint32_t>((node.data->size() + kBlock - 1u) / kBlock) : 1u;
        if (node.data && node.data->empty()) ++next;
        for (auto &[name, child] : node.children) self(self, child);
    };
    place(place, root);
    std::vector<std::uint8_t> image(static_cast<std::size_t>(next) * kBlock);
    const auto store = [&](std::size_t at, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) image[at + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    };
    const auto record = [&](std::size_t at, const std::string &name, const Node &node) {
        image[at] = static_cast<std::uint8_t>((33u + name.size() + 1u) & ~1u);
        store(at + 2u, node.lba);
        store(at + 10u, node.size);
        image[at + 25u] = node.data ? 0u : 2u;
        image[at + 32u] = static_cast<std::uint8_t>(name.size());
        std::copy(name.begin(), name.end(), image.begin() + static_cast<std::ptrdiff_t>(at + 33u));
        return static_cast<std::size_t>(image[at]);
    };
    const auto write = [&](auto &self, const Node &node) -> void {
        if (node.data) {
            std::copy(
                node.data->begin(), node.data->end(), image.begin() + static_cast<std::ptrdiff_t>(node.lba * kBlock));
            return;
        }
        std::size_t at = node.lba * kBlock;
        for (const auto &[name, child] : node.children) {
            at += record(at, child.data ? name + ";1" : name, child);
            self(self, child);
        }
    };
    image[16u * kBlock] = 1u;
    std::copy_n("CD001", 5, image.begin() + 16 * kBlock + 1);
    image[16u * kBlock + 6u] = 1u;
    record(16u * kBlock + 156u, std::string(1, '\0'), root);
    write(write, root);
    return image;
}

std::vector<std::uint8_t> sfo(const std::map<std::string, std::string> &values) {
    mhfu::savedata::ParamSfo param;
    for (const auto &[key, value] : values) param.set_string(key, value, 128u);
    return param.serialize();
}

std::filesystem::path write(
    const std::filesystem::path &dir, const std::string &name, const std::vector<std::uint8_t> &bytes) {
    const auto path = dir / name;
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

// The message check_image throws, or "" when it accepts the image.
std::string refusal(const std::filesystem::path &path) {
    try {
        (void)mhfu::install::check_image(path);
    } catch (const mhfu::install::InstallError &error) {
        return error.what();
    }
    return {};
}

bool says(const std::string &message, const std::string &part) {
    return message.find(part) != std::string::npos;
}

void refusals(const std::filesystem::path &dir) {
    using namespace mhfu::install;
    check(says(refusal(dir / "missing.iso"), "is not a file"), "a missing path is not a file");
    check(says(refusal(write(dir, "empty.iso", {})), "is empty or cannot be read"), "an empty file is refused");
    check(says(refusal(write(dir, "game.cso", {'C', 'I', 'S', 'O', 0u})), "is a compressed image"),
        "a CSO image asks to be decompressed");
    check(says(refusal(write(dir, "game.zso", {'Z', 'I', 'S', 'O', 0u})), "is a compressed image"),
        "a ZSO image asks to be decompressed");
    check(says(refusal(write(dir, "noise.iso", std::vector<std::uint8_t>(64u, 0x41u))), "is not a disc image"),
        "a file that is not ISO9660 is refused");
    check(says(refusal(write(dir, "data.iso", iso_image({{"README.TXT", {'h', 'i'}}}))), "has no PSP_GAME/PARAM.SFO"),
        "a disc without PSP_GAME/PARAM.SFO is not a PSP game");
    check(says(refusal(write(dir, "ps3.iso", iso_image({{"PS3_GAME/PARAM.SFO", sfo({{"TITLE", "x"}})}}))),
              "is a PlayStation 3 disc image"),
        "a PS3 disc is named as one");
    check(says(refusal(write(dir, "noid.iso", iso_image({{"PSP_GAME/PARAM.SFO", sfo({{"TITLE", "x"}})}}))),
              "has no disc id"),
        "a PARAM.SFO without DISC_ID is refused");

    const auto other = [&](const std::string &id, const std::string &title) {
        Files files{{"PSP_GAME/PARAM.SFO", sfo({{"DISC_ID", id}, {"TITLE", title}})}};
        return refusal(write(dir, id + ".iso", iso_image(files)));
    };
    const std::string p3 = other("ULJM05800", "Portable 3rd");
    check(says(p3, "Portable 3rd (ULJM05800)") && says(p3, "original PSP release of Portable 3rd"),
        "the PSP Portable 3rd disc is named and refused");
    check(says(other("NPJB40001", ""), "Portable 3rd HD"), "Portable 3rd HD is named and refused");
    check(says(other("ULES01213", "Freedom Unite"), "Other releases and regions are not supported"),
        "another region is refused");
    if constexpr (mhfu::kTitle == mhfu::Title::FUComplete)
        check(says(other(kFreedomUniteUsa.disc_id, "MHFU"), "runs in the standard build of Pokee"),
            "the FUComplete build points a Freedom Unite disc to the standard build");
    else
        check(says(other(kFUComplete.disc_id, "MHP2G"), "run in a FUComplete build of Pokee"),
            "the standard build points a Portable 2nd G disc to PokeeFUC");

    const std::vector<std::uint8_t> id_sfo = sfo({{"DISC_ID", kBuildRelease.disc_id}, {"TITLE", "Game"}});
    check(says(refusal(write(dir, "noeboot.iso", iso_image({{"PSP_GAME/PARAM.SFO", id_sfo}}))),
              "has no PSP_GAME/SYSDIR/EBOOT.BIN"),
        "a disc without its executable is refused");

    const std::vector<std::uint8_t> elf = mhfu::test::synthetic_elf(0x08804000u, std::vector<std::uint8_t>(64u));
    const std::vector<std::uint8_t> encrypted{'~', 'P', 'S', 'P', 1u, 2u, 3u, 4u};
    if constexpr (mhfu::kTitle == mhfu::Title::FUComplete) {
        Files unpatched{{"PSP_GAME/PARAM.SFO", id_sfo}, {"PSP_GAME/SYSDIR/EBOOT.BIN", encrypted}};
        check(says(refusal(write(dir, "unpatched.iso", iso_image(unpatched))), "is not patched with FUComplete"),
            "an unpatched Portable 2nd G is sent to FUCTool");
        Files no_marker{{"PSP_GAME/PARAM.SFO", id_sfo}, {"PSP_GAME/SYSDIR/EBOOT.BIN", elf}};
        check(says(refusal(write(dir, "nomarker.iso", iso_image(no_marker))), "is not patched with FUComplete"),
            "a plain executable without the FUC folder is refused");
        Files patched{{"PSP_GAME/PARAM.SFO", id_sfo}, {"PSP_GAME/SYSDIR/EBOOT.BIN", elf},
            {kFUComplete.marker_on_disc, {1u, 2u, 3u}}};
        const auto path = write(dir, "patched.iso", iso_image(patched));
        check(refusal(path).empty(), "a FUComplete image is accepted");
        const ImageInfo info = check_image(path);
        check(info.title == kFUComplete.title && info.disc_id_display == kFUComplete.disc_id_display &&
                info.size_bytes == std::filesystem::file_size(path),
            "an accepted image reports its release and size");
    } else {
        Files changed{{"PSP_GAME/PARAM.SFO", id_sfo}, {"PSP_GAME/SYSDIR/EBOOT.BIN", encrypted}};
        check(says(refusal(write(dir, "changed.iso", iso_image(changed))), "is not the version"),
            "an executable that is not the supported version is refused");
    }
}

} // namespace

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "mhfu_installer_inspect_tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    refusals(dir);
    std::filesystem::remove_all(dir);
    if (failures != 0) return 1;
    std::cout << "installer inspect tests passed\n";
    return 0;
}
