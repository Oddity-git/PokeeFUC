#pragma once

// The game this build runs, chosen when configuring with -DMHFU_TITLE=FU (the
// default, USA Freedom Unite) or -DMHFU_TITLE=FUC (FUComplete, a patched
// Monster Hunter Portable 2nd G). The two have different executables, so each
// build links the corpus generated from its own and keeps its own data folder.

// The name the player sees: the window, the menu, the setup screens and the
// messages. A FUComplete build is "PokeeFUC", so it is told apart from the
// standard build installed beside it. A macro, so it joins string literals:
// MHFU_APP_NAME " needs ...". The executable and the data folder keep their
// names.
#if defined(MHFU_TITLE_FUC)
#define MHFU_APP_NAME "PokeeFUC"
#else
#define MHFU_APP_NAME "Pokee"
#endif
// The desktop's id for the window: the name of the .desktop file and icon
// the packaging installs (packaging/linux), so the two builds keep their own
// entries, icons and taskbar groups.
#if defined(MHFU_TITLE_FUC)
#define MHFU_APP_ID "io.github.teamgdb.PokeeFUC"
#else
#define MHFU_APP_ID "io.github.teamgdb.Pokee"
#endif

namespace mhfu {

enum class Title { FreedomUnite, FUComplete };

#if defined(MHFU_TITLE_FUC)
inline constexpr Title kTitle = Title::FUComplete;
#else
inline constexpr Title kTitle = Title::FreedomUnite;
#endif

// The extras that read or patch the game (the analog camera, hiding the HUD,
// layered armor, damage numbers, the gather icon and the rest) were traced in
// USA Freedom Unite and use its addresses. Under any other executable they
// stay off, so none of them reads or writes guest memory it does not know.
inline constexpr bool kFreedomUniteExtras = kTitle == Title::FreedomUnite;

// The extras ported to FUComplete: their addresses were traced again in its
// executable and overlays, so they run under both titles.
// Analog camera (camera/game_camera.cpp).
inline constexpr bool kAnalogCamera = true;
// Lock-on and its marker (camera/lock_on.cpp): the entity list, the hunter
// vtable and the camera object, at each title's addresses.
inline constexpr bool kLockOn = true;
// Hold to gather (game/gather_icon.cpp): its gather list, carve table and farm
// points.
inline constexpr bool kGatherHold = true;
// Aspect ratio's Fill (camera/game_aspect.cpp): the camera object's projection.
inline constexpr bool kGameAspect = true;
// Give Any Rarity Item (game/give_rarity.cpp): the item menu's give check.
inline constexpr bool kGiveRarity = true;
// Empty item slot (game/empty_item_slot.cpp): the item bar's step and name.
inline constexpr bool kEmptyItemSlot = true;
// Hide HUD (gpu/game_hud.cpp): the 2D packets, text queues and the HUD's
// functions in game_task and lobby_task.
inline constexpr bool kHideHud = true;
// Damage numbers (game/damage_numbers.cpp): game_task's hit and status tick.
inline constexpr bool kDamageNumbers = true;
// The extras that read the HUD's textures, which FUComplete keeps 0x4400
// lower (game::layout::kItemAtlas): UI Scaling (gpu/hud_scale), the modern
// sharpness gauge and the gather icon with its pictures (game/gather_art).
inline constexpr bool kUiScaling = true;
inline constexpr bool kSharpnessGauge = true;
inline constexpr bool kGatherIcon = true;
// Layered armor (game/layered_armor*.cpp): the armor model lookup, the
// hunter's load and the lobby's equipment calls, and the armor tables.
inline constexpr bool kLayeredArmor = true;

} // namespace mhfu
