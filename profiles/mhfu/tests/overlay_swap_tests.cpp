// A second overlay copied into an occupied slot must not run the first
// overlay's compiled body. The game does this before it flushes the
// instruction cache, so the swap has to be noticed on the call itself.

#include "overlay_swap_fixture.hpp"
#include "overlays.hpp"
#include "title.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {

void check(bool condition, const char *contract) {
    if (!condition) throw std::runtime_error(contract);
}

void outside_slot(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ctx.gpr[2] = 9u;
}

void swapped_slot_does_not_run_the_old_body() {
    if (setenv("MHFU_OVERLAY_DIR", MHFU_OVERLAY_FIXTURE_DIR, 1) != 0)
        throw std::runtime_error("could not point the overlay loader at the fixtures");

    psprecomp::Runtime runtime;
    mhfu::install_overlay_support(runtime);
    const auto first = overlay_fixture_image(1u);
    runtime.memory().copy_in(kOverlayFixtureBase, std::span<const std::uint8_t>(first.data(), first.size()));
    runtime.cpu().gpr[2] = 0u;
    runtime.run(kOverlayFixtureEntry, 1u);
    check(runtime.cpu().gpr[2] == 1u, "the first image's body did not run");

    runtime.cpu().gpr[2] = 0u;
    runtime.cpu().pc = kOverlayFixtureEntry;
    check(runtime.invoke_chained_call(runtime.cpu()), "a call into the unchanged image was rejected");
    check(runtime.cpu().gpr[2] == 1u, "the unchanged image did not run its own body");

    const auto second = overlay_fixture_image(2u);
    runtime.memory().copy_in(kOverlayFixtureBase, std::span<const std::uint8_t>(second.data(), second.size()));
    runtime.cpu().gpr[2] = 0u;
    runtime.cpu().pc = kOverlayFixtureEntry;
    check(!runtime.invoke_chained_call(runtime.cpu()), "the replaced image still ran the previous body");
    check(runtime.cpu().gpr[2] == 0u, "the previous body executed after the slot was overwritten");
    check(runtime.cpu().pc == kOverlayFixtureEntry, "rejecting the stale body moved the guest PC");

    runtime.cpu().gpr[2] = 0u;
    runtime.run(kOverlayFixtureEntry, 1u);
    check(runtime.cpu().gpr[2] == 2u, "the image now in the slot did not run");

    runtime.cpu().gpr[2] = 0u;
    runtime.register_function(0x08804000u, &outside_slot, "recomp_unit_not_an_overlay");
    runtime.cpu().pc = 0x08804000u;
    check(runtime.invoke_chained_call(runtime.cpu()) && runtime.cpu().gpr[2] == 9u,
        "a call outside every overlay slot was treated as stale");
}

// An overlay with no library is dumped under a name of its own, since the
// slot is shared, and the FUComplete build dumps by default: its loader
// patches overlays as they load.
void dump_names_and_directory() {
    check(mhfu::overlay_dump_name(0x09A5A580u, "game_task.ovl") == "overlay_09A5A580_game_task.bin",
        "a dump is named after its slot and its image");
    check(
        mhfu::overlay_dump_name(0x09A5A580u, "lobby_task.ovl") != mhfu::overlay_dump_name(0x09A5A580u, "game_task.ovl"),
        "two overlays of one slot do not share a dump");
    check(mhfu::overlay_dump_name(0x09C14280u, "") == "overlay_09C14280.bin", "an image with no name keeps the slot");
    check(mhfu::overlay_dump_name(0x09C14280u, "a-b c.ovl") == "overlay_09C14280_a_b_c.bin",
        "characters a file name should not have become '_'");

    setenv("MHFU_DUMP_OVERLAYS", "0", 1);
    check(!mhfu::overlay_dump_directory(), "MHFU_DUMP_OVERLAYS=0 dumps nothing");
    setenv("MHFU_DUMP_OVERLAYS", "dumps-here", 1);
    check(mhfu::overlay_dump_directory() == std::filesystem::path("dumps-here"), "MHFU_DUMP_OVERLAYS is the directory");
    unsetenv("MHFU_DUMP_OVERLAYS");
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "pokee_overlay_dump_tests";
    setenv("MHFU_DATA_DIR", data.string().c_str(), 1);
    const std::optional<std::filesystem::path> unset = mhfu::overlay_dump_directory();
    if constexpr (mhfu::kTitle == mhfu::Title::FUComplete)
        check(unset && unset->filename() == "overlay_dumps", "the FUComplete build dumps into its data folder");
    else
        check(!unset, "the Freedom Unite build dumps only when asked");
    unsetenv("MHFU_DATA_DIR");
}

} // namespace

int main() {
    try {
        dump_names_and_directory();
        swapped_slot_does_not_run_the_old_body();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "overlay swap tests passed\n";
    return 0;
}
