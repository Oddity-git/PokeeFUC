// The four overlay bases read from USA Freedom Unite's DATA.BIN. Addresses
// seen interpreted during a village boot must land in the slot the archive
// names, and the Portable 3rd bases must not.
#include "mhfu_profile.hpp"
#include "synthetic_elf.hpp"

#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void slots() {
    using mhfu::kOverlaySlots;
    using mhfu::overlay_slot_of;
    check(std::size(kOverlaySlots) == 5u, "four overlay bases and the end of user memory");
    check(kOverlaySlots[0] == 0x09A5F300u, "task overlays");
    check(kOverlaySlots[1] == 0x09C19100u, "sub overlays");
    check(kOverlaySlots[2] == 0x09D1A280u, "monster overlays");
    check(kOverlaySlots[3] == 0x09D63100u, "stage overlays");
    check(kOverlaySlots[4] == 0x0A000000u, "slots stop at the end of 32 MiB user memory");
    for (std::size_t i = 0; i + 1u < std::size(kOverlaySlots); ++i)
        check(kOverlaySlots[i] < kOverlaySlots[i + 1u], "overlay bases increase");

    check(overlay_slot_of(0x09AB8690u).first == 0x09A5F300u, "village game_task code is in the task slot");
    check(overlay_slot_of(0x09C1A1E8u).first == 0x09C19100u, "the story movie player is in the sub slot");
    check(overlay_slot_of(0x09C38CA8u).first == 0x09C19100u, "game_sub code is in the sub slot");
    check(overlay_slot_of(0x09D1A280u).first == 0x09D1A280u, "a monster overlay starts its slot");
    check(overlay_slot_of(0x09D63100u).first == 0x09D63100u, "a stage overlay starts its slot");
    check(overlay_slot_of(0x08804000u).first == 0u, "the executable's load base is not an overlay");
    check(overlay_slot_of(0x0A000000u).first == 0u, "the end address is not itself a slot");
    check(overlay_slot_of(0x0A001780u).first == 0u, "a Portable 3rd overlay base is not a slot");
}

// Another executable (FUComplete's Portable 2nd G) names its slots in its
// *.ovl sections. The relocation sections beside them, other sections and
// addresses past user memory are not slots.
void slots_from_sections() {
    using mhfu::test::NamedSection;
    const std::vector<std::uint8_t> bytes = mhfu::test::synthetic_elf(0x08804000u, std::vector<std::uint8_t>(16u, 0u),
        {{"demo_task.ovl", 0x09A5A580u}, {"game_task.ovl", 0x09A5A580u}, {".reldemo_task.ovl", 0x08900000u},
            {"game_sub.ovl", 0x09C14280u}, {"em01.ovl", 0x09D15500u}, {".text", 0x08804000u},
            {"stage00.ovl", 0x09D5E380u}, {"beyond.ovl", 0x0A000000u}, {".ovl", 0x09000000u}});
    const psprecomp::Elf32Image elf = psprecomp::Elf32Image::from_bytes(bytes, "slots.elf");
    const std::vector<std::uint32_t> slots = mhfu::overlay_slots_from_sections(elf, 0x08804000u, 0x0A000000u);
    const std::vector<std::uint32_t> expected{0x09A5A580u, 0x09C14280u, 0x09D15500u, 0x09D5E380u, 0x0A000000u};
    check(slots == expected, "distinct overlay section addresses, ascending, then the end of user memory");

    const psprecomp::Elf32Image none =
        psprecomp::Elf32Image::from_bytes(mhfu::test::synthetic_elf(0x08804000u, {0u, 0u, 0u, 0u}), "none.elf");
    check(mhfu::overlay_slots_from_sections(none, 0x08804000u, 0x0A000000u).empty(), "no overlay sections, no slots");

    mhfu::set_overlay_slots({0x0A000000u});
    check(mhfu::overlay_slots().size() == std::size(mhfu::kOverlaySlots), "a table without a base is ignored");
    mhfu::set_overlay_slots(slots);
    check(mhfu::overlay_slot_of(0x09A5A580u).first == 0x09A5A580u, "the task slot starts the table");
    check(mhfu::overlay_slot_of(0x09A5F300u) == std::pair{0x09A5A580u, 0x09C14280u},
        "Freedom Unite's task base falls in Portable 2nd G's task slot");
    check(mhfu::overlay_slot_of(0x09D60000u).first == 0x09D5E380u, "the last slot runs to the end of user memory");
    check(mhfu::overlay_slot_of(0x09A5A57Cu).first == 0u, "below the first slot is no slot");
    mhfu::set_overlay_slots({std::begin(mhfu::kOverlaySlots), std::end(mhfu::kOverlaySlots)});
    check(mhfu::overlay_slot_of(0x09A5F300u).first == 0x09A5F300u, "Freedom Unite's table comes back");
}
} // namespace

int main() {
    slots();
    slots_from_sections();
    if (failures != 0) {
        std::cerr << failures << " overlay slot check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "overlay slot tests passed\n";
    return EXIT_SUCCESS;
}
