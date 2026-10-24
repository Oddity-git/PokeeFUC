#pragma once

#include "mhfu_profile.hpp"

#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace mhfu {

// Loads the overlay libraries and installs the dispatch-miss hook that
// recognises the overlay currently in a slot and registers its recompiled
// corpus. The libraries are read from MHFU_OVERLAY_DIR, or from overlays/
// next to the executable. With MHFU_DUMP_OVERLAYS set, an unknown overlay is
// written out instead so it can be recompiled.
void install_overlay_support(psprecomp::Runtime &runtime);

// Drops the corpus of any slot whose contents no longer match it. The guest
// flushes the instruction cache right after loading an overlay, and a chained
// call into a slot whose header has already changed does the same check first,
// because that jump can happen before the flush. The next lookup then installs
// the image that is in memory now.
void revalidate_overlays(psprecomp::Runtime &runtime);

// The guest loaded code: a slot whose image matched no corpus is searched
// again at the next call into it.
void forget_unmatched_overlays();

// Where an overlay with no library is dumped for recompilation, if anywhere:
// MHFU_DUMP_OVERLAYS (0: nowhere), or, in the FUComplete build,
// overlay_dumps in the data folder, because FUComplete's loader patches
// overlays as they load.
[[nodiscard]] std::optional<std::filesystem::path> overlay_dump_directory();
// overlay_<slot>[_<name>].bin, the name without its extension and with any
// character but a letter or digit made '_'. The slot is shared, so the name
// keeps one overlay's dump from another's.
[[nodiscard]] std::string overlay_dump_name(std::uint32_t slot, const std::string &image_name);

} // namespace mhfu
