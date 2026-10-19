#pragma once

// The game this build runs, chosen when configuring with -DMHFU_TITLE=FU (the
// default, USA Freedom Unite) or -DMHFU_TITLE=FUC (FUComplete, a patched
// Monster Hunter Portable 2nd G). The two have different executables, so each
// build links the corpus generated from its own and keeps its own data folder.
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

} // namespace mhfu
