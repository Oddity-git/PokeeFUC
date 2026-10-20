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
// Analog camera (camera/game_camera.cpp), without lock-on, which is not ported.
inline constexpr bool kAnalogCamera = true;
// Lock-on and its marker (camera/lock_on.cpp) read Freedom Unite's monsters.
inline constexpr bool kLockOn = kFreedomUniteExtras;
// Hold to gather (game/gather_icon.cpp): its gather list, carve table and farm
// points. The gather icon itself is drawn with Freedom Unite's item art.
inline constexpr bool kGatherHold = true;
// Not ported yet: they stay off, and their rows stay out of the menu.
inline constexpr bool kGameAspect = kFreedomUniteExtras;    // Aspect ratio
inline constexpr bool kUiScaling = kFreedomUniteExtras;     // UI Scaling (gpu/hud_scale)
inline constexpr bool kHideHud = kFreedomUniteExtras;       // Hide HUD (gpu/game_hud)
inline constexpr bool kEmptyItemSlot = kFreedomUniteExtras; // Empty item slot
inline constexpr bool kDamageNumbers = kFreedomUniteExtras;
inline constexpr bool kSharpnessGauge = kFreedomUniteExtras; // Modern sharpness gauge
inline constexpr bool kGatherIcon = kFreedomUniteExtras;
inline constexpr bool kGiveRarity = kFreedomUniteExtras;   // Give Any Rarity Item
inline constexpr bool kLayeredArmor = kFreedomUniteExtras; // Layered armor and equipment models

} // namespace mhfu
