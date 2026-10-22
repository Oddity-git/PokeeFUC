// Guild Hall scaling: the offline hall's stage, the flat cut, and putting the
// monsters back. No game data; guest memory is a stand-in.
#include "game/guest_layout.hpp"
#include "game/hub_scaling.hpp"

#include "psprecomp/guest_memory.hpp"

#include <cstdint>
#include <iostream>

namespace {
using namespace mhfu::game;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

constexpr std::uint32_t kManager = 0x09800000u;
constexpr std::uint32_t kNode = 0x09810000u;
constexpr std::uint32_t kLarge = 0x09830000u;
constexpr std::uint32_t kSmall = 0x09840000u;
constexpr std::uint32_t kHunter = 0x09820000u;
constexpr std::uint32_t kGameSys = 0x09700000u;

void start_quest(psprecomp::GuestMemory &memory) {
    namespace layout = mhfu::game::layout;
    memory.store32(layout::kTaskSlot, layout::kOverlayMagic);
    memory.store32(layout::kTaskSlot + 8u, layout::kTaskSlot);
    const char name[] = "game_task.ovl";
    for (std::uint32_t i = 0; i < sizeof(name); ++i)
        memory.store8(layout::kTaskSlot + layout::kOverlayName + i, static_cast<std::uint8_t>(name[i]));
}

void end_quest(psprecomp::GuestMemory &memory) {
    memory.store32(mhfu::game::layout::kTaskSlot, 0u);
}

void stand_in(psprecomp::GuestMemory &memory, std::uint16_t stage) {
    namespace layout = mhfu::game::layout;
    memory.store32(layout::kGameSysPointer, kGameSys);
    memory.store16(kGameSys + layout::kGameSysStage, stage);

    memory.store32(layout::kEntityList, kManager);
    memory.store32(kManager + layout::kListHead, kNode);
    const std::uint32_t entities[] = {kLarge, kSmall, kHunter};
    const std::uint8_t types[] = {0u, 0u, layout::kPlayerNode};
    for (std::uint32_t i = 0; i < 3u; ++i) {
        const std::uint32_t node = kNode + i * 0x100u;
        memory.store32(node + layout::kNodeEntity, entities[i]);
        memory.store8(node + layout::kNodeType, types[i]);
        memory.store32(node + layout::kNodeOlder, i + 2u < 3u ? node + 0x100u : 0u);
    }
    memory.store32(kHunter + layout::kVtable, layout::kHunterVtable);
    memory.store32(kLarge + layout::kVtable, 0x09D20000u);
    memory.store32(kSmall + layout::kVtable, 0x09D20000u);
    memory.store16(kLarge + layout::kHealth, 2000u);
    memory.store16(kLarge + layout::kMostHealth, 2000u);
    memory.store16(kSmall + layout::kHealth, 150u);
    memory.store16(kSmall + layout::kMostHealth, 150u);
    memory.store16(kHunter + layout::kHealth, 150u);
    memory.store16(kHunter + layout::kMostHealth, 150u);
}

std::int16_t health(psprecomp::GuestMemory &memory, std::uint32_t entity) {
    return static_cast<std::int16_t>(memory.load16(entity + mhfu::game::layout::kHealth));
}

std::int16_t most(psprecomp::GuestMemory &memory, std::uint32_t entity) {
    return static_cast<std::int16_t>(memory.load16(entity + mhfu::game::layout::kMostHealth));
}

void test_hall_quest_is_scaled() {
    namespace layout = mhfu::game::layout;
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    hub_scaling::reset();
    stand_in(memory, hub_scaling::kGatheringHallStage);
    hub_scaling::frame(memory, true, true); // the hall, before the quest
    check(!layout::quest_running(memory), "no quest yet");

    start_quest(memory);
    hub_scaling::frame(memory, true, true);
    check(most(memory, kLarge) == 1300 && health(memory, kLarge) == 1300, "65% of 2000 is 1300");
    check(most(memory, kSmall) == 150 && health(memory, kSmall) == 150, "a small monster is left alone");
    check(most(memory, kHunter) == 150 && health(memory, kHunter) == 150, "a hunter is left alone");

    hub_scaling::frame(memory, true, true);
    check(most(memory, kLarge) == 1300 && health(memory, kLarge) == 1300, "the cut is made once");
}

void test_restore() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    hub_scaling::reset();
    stand_in(memory, hub_scaling::kGatheringHallStage);
    hub_scaling::frame(memory, true, true);
    start_quest(memory);
    hub_scaling::frame(memory, true, true);
    check(most(memory, kLarge) == 1300, "scaled first");

    hub_scaling::frame(memory, false, true);
    check(most(memory, kLarge) == 2000 && health(memory, kLarge) == 2000, "off puts the health back");
    check(most(memory, kSmall) == 150, "and leaves the small monster as it was");
}

void test_village_and_online_are_not_scaled() {
    psprecomp::GuestMemory memory(32u * 1024u * 1024u);
    hub_scaling::reset();
    stand_in(memory, 139u); // Pokke Village
    hub_scaling::frame(memory, true, true);
    start_quest(memory);
    hub_scaling::frame(memory, true, true);
    check(most(memory, kLarge) == 2000, "a village quest keeps its health");

    end_quest(memory);
    hub_scaling::frame(memory, true, true);
    stand_in(memory, hub_scaling::kGatheringHallStage);
    hub_scaling::reset();
    hub_scaling::frame(memory, true, false); // online
    start_quest(memory);
    hub_scaling::frame(memory, true, false);
    check(most(memory, kLarge) == 2000, "an online hall keeps its health");
}

void test_health_percent() {
    check(hub_scaling::health_percent() >= 1 && hub_scaling::health_percent() <= 100, "the percentage is a percentage");
}

} // namespace

int main() {
    test_hall_quest_is_scaled();
    test_restore();
    test_village_and_online_are_not_scaled();
    test_health_percent();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "hub scaling tests passed\n";
    return 0;
}
