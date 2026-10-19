// Installer identities and executable preparation. The disc image is optional:
// pass its path to also prepare that image's executable and check the hash.
#include "corpus_identity.hpp"
#include "install/executable_preparation.hpp"
#include "install/game_identity.hpp"
#include "kernel/iso_image.hpp"
#include "synthetic_elf.hpp"

#include "psprecomp/common.hpp"
#include "psprecomp/sha256.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void identities() {
    using namespace mhfu::install;
    check(find_release_by_disc_id("NPJB40001") == nullptr, "Portable 3rd HD is not accepted");
    check(find_release_by_disc_id("ULJM05800") == nullptr, "the original PSP Portable 3rd disc is not accepted");
    check(std::size(kSupportedReleases) == 1u, "a build accepts one release");
    check(find_release_by_encrypted_hash(kPortable3rdHd.encrypted_sha256) == nullptr,
        "Portable 3rd's encrypted executable is refused");
    // FUComplete pins no hash, so an empty one must never match it.
    check(find_release_by_encrypted_hash("") == nullptr && find_release_by_executable_hash("") == nullptr,
        "an empty hash names no release");
    check(kFreedomUniteUsa.guest_ram_bytes == 32u * 1024u * 1024u, "Freedom Unite uses 32 MiB");
    check(kFUComplete.guest_ram_bytes == 32u * 1024u * 1024u, "FUComplete's Portable 2nd G uses 32 MiB");
    check(!kFreedomUniteUsa.plain_executable && kFUComplete.plain_executable,
        "only FUComplete's disc holds a plain executable");
    check(
        std::string(kFUComplete.disc_id) == "ULJM05500" && std::string(kFUComplete.marker_on_disc) == "FUC/PRELOAD.BIN",
        "FUComplete is Portable 2nd G with the FUC folder");
    check(std::string(kFreedomUniteUsa.data_folder) == "MHFU" && std::string(kFUComplete.data_folder) == "FUC",
        "the two keep their data apart");
    const std::string corpus = mhfu::kCorpusExecutableSha256;
    const std::string listed = supported_release_list();
    if constexpr (mhfu::kTitle == mhfu::Title::FreedomUnite) {
        check(find_release_by_disc_id("ULUS10391") == &kSupportedReleases[0], "USA Freedom Unite is the accepted disc");
        check(find_release_by_disc_id("ULJM05500") == nullptr, "Portable 2nd G needs a FUComplete build");
        check(find_release_by_encrypted_hash(kFreedomUniteUsa.encrypted_sha256) == &kSupportedReleases[0],
            "Freedom Unite encrypted hash");
        check(find_release_by_executable_hash(kFreedomUniteUsa.executable_sha256) == &kSupportedReleases[0],
            "Freedom Unite prepared hash");
        check(std::string(kDiscId) == kFreedomUniteUsa.disc_id, "the running disc id is Freedom Unite");
        check(corpus.empty() || corpus == kFreedomUniteUsa.executable_sha256,
            "a linked corpus names the accepted executable, or none");
        check(listed.find("ULUS-10391") != std::string::npos && listed.find("NPJB-40001") == std::string::npos,
            "the supported list names only USA Freedom Unite");
        check(mhfu::kFreedomUniteExtras, "the Freedom Unite extras run in a Freedom Unite build");
    } else {
        check(find_release_by_disc_id("ULJM05500") == &kSupportedReleases[0], "FUComplete is the accepted disc");
        check(find_release_by_disc_id("ULUS10391") == nullptr, "Freedom Unite needs the standard build");
        check(find_release_by_encrypted_hash(kFreedomUniteUsa.encrypted_sha256) == nullptr,
            "Freedom Unite's executable is refused");
        check(std::string(kDiscId) == "ULJM05500", "the running disc id is Portable 2nd G's");
        check(listed.find("ULJM-05500") != std::string::npos && listed.find("ULUS-10391") == std::string::npos,
            "the supported list names only FUComplete");
        check(!mhfu::kFreedomUniteExtras, "the Freedom Unite extras stay off");
    }
}

// FUComplete's disc holds an ELF already; a Freedom Unite build refuses one.
void plain_executable() {
    const std::vector<std::uint8_t> elf = mhfu::test::synthetic_elf(0x08804000u, {0x08u, 0x00u, 0xE0u, 0x03u});
    bool threw = false;
    std::vector<std::uint8_t> prepared;
    try {
        prepared = mhfu::install::prepare_executable(elf);
    } catch (const psprecomp::Error &) {
        threw = true;
    }
    if constexpr (mhfu::install::kBuildRelease.plain_executable) {
        check(!threw && prepared == elf, "a plain executable is kept as it is");
        std::vector<std::uint8_t> broken = elf;
        broken[18] = 3u; // not MIPS
        threw = false;
        try {
            (void)mhfu::install::prepare_executable(broken);
        } catch (const psprecomp::Error &) {
            threw = true;
        }
        check(threw, "an ELF the loader cannot use is refused");
        std::vector<std::uint8_t> empty = mhfu::test::synthetic_elf(0x08804000u, {});
        empty[44] = 0u; // no program headers
        threw = false;
        try {
            (void)mhfu::install::prepare_executable(empty);
        } catch (const psprecomp::Error &) {
            threw = true;
        }
        check(threw, "an ELF with nothing to load is refused");
    } else {
        check(threw, "a Freedom Unite build refuses a plain executable");
    }
    check(mhfu::install::looks_like_elf(elf.data(), elf.size()) && !mhfu::install::looks_like_elf(elf.data(), 3u),
        "an ELF is told by its magic");
}

void rejects_unknown_executable() {
    const std::vector<std::uint8_t> junk(32u, 0u);
    bool threw = false;
    try {
        (void)mhfu::install::prepare_executable(junk);
    } catch (const psprecomp::Error &error) {
        threw = true;
        check(std::string(error.what()).find("not a supported executable") != std::string::npos,
            "an unknown executable says it is not supported");
    }
    check(threw, "an unknown executable is refused");
}

void prepare_image(const std::string &path) {
    mhfu::IsoImage image(path);
    const auto entry = image.find(mhfu::install::kExecutablePathOnDisc);
    check(entry && !entry->directory, "the image has EBOOT.BIN");
    if (!entry || entry->directory) return;
    std::vector<std::uint8_t> eboot(entry->size);
    check(image.read(static_cast<std::uint64_t>(entry->lba) * mhfu::IsoImage::kSectorSize, eboot) == eboot.size(),
        "EBOOT.BIN was read");
    const mhfu::install::SupportedRelease *release =
        mhfu::install::find_release_by_encrypted_hash(psprecomp::sha256_bytes(eboot));
    check(release != nullptr, "the image's executable is a supported one");
    if (release == nullptr) return;
    const std::vector<std::uint8_t> executable = mhfu::install::prepare_executable(eboot);
    check(psprecomp::sha256_bytes(executable) == release->executable_sha256, "the prepared executable matches");
    check(executable.size() >= 4u && executable[0] == 0x7fu && executable[1] == 'E' && executable[2] == 'L' &&
            executable[3] == 'F',
        "the prepared executable is an ELF");
    std::cout << "prepared " << release->disc_id_display << "\n";
}
} // namespace

int main(int argc, char **argv) {
    identities();
    rejects_unknown_executable();
    plain_executable();
    if (argc == 2) prepare_image(argv[1]);
    if (failures != 0) return 1;
    std::cout << "install identity tests passed\n";
    return 0;
}
