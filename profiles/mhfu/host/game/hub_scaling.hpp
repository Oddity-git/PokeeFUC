#pragma once

// Gathering Hall scaling, offline only. The Guild's quests are tuned for four
// hunters; a hunter alone in the offline hall meets the same large monsters.
// With the switch on, a large monster's health is cut by a flat percentage
// when the quest starts, and put back when the switch goes off.
//
// The quest itself carries no mark the port can read: the id is not kept in
// guest memory during the hunt (traced). The hall is its own stage, so the
// gate is the stage that was loaded just before the quest started
// (MHFU_TRACE_HUB_SCALING=1 prints it). Only the offline hall counts: while
// an ad hoc session is active nothing changes. Village quests never change.
//
// The health fields are the entity list's own (game/guest_layout.hpp), the
// ones damage numbers already read: current at +0x2E4, most at +0x41E.

#include "psprecomp/guest_memory.hpp"

#include <cstdint>

namespace mhfu::game::hub_scaling {

// The offline Gathering Hall's stage, traced in FUComplete: the stage the
// game has loaded while the hunter stands in the hall. Pokke Village is 139.
inline constexpr std::uint16_t kGatheringHallStage = 85u;
// Large monsters have hundreds or thousands of health; a small monster's
// pool is below this, and it is not worth scaling.
inline constexpr int kMinLargeHealth = 300;
// Health left, in percent, unless MHFU_HUB_SCALING_HEALTH says otherwise.
inline constexpr int kDefaultHealthPercent = 65;

// At the flip: follows the switch and the quest. `enabled` is the setting,
// `offline` is true while no ad hoc session is active.
void frame(psprecomp::GuestMemory &memory, bool enabled, bool offline);

// Health left, in percent, from MHFU_HUB_SCALING_HEALTH, or the default.
[[nodiscard]] int health_percent() noexcept;

// Forgets the quest, as at start; for tests.
void reset();

} // namespace mhfu::game::hub_scaling
