#define MHFU_OVERLAY_MODULE 1
#include "overlay_module.hpp"
#include "overlay_swap_fixture.hpp"

#include "psprecomp/runtime.hpp"

namespace {

// Only the module needs the image's identity hash; keeping it out of the
// shared header leaves the test executable without an unused inline copy.
std::uint64_t overlay_fixture_hash(const std::uint8_t *data, std::size_t size) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (std::size_t index = 0u; index < size; ++index) {
        hash ^= data[index];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

void fixture_body(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ctx.gpr[2] = MHFU_OVERLAY_FIXTURE_MARK;
    ctx.pc = kOverlayFixtureEntry + 4u;
}

mhfu::OverlayModuleInfo fixture_info() {
    const auto image = overlay_fixture_image(MHFU_OVERLAY_FIXTURE_MARK);
    mhfu::OverlayModuleInfo info{};
    info.abi_version = mhfu::kOverlayAbiVersion;
    info.base = kOverlayFixtureBase;
    info.size = static_cast<std::uint32_t>(kOverlayFixtureBytes);
    info.code_size = kOverlayFixtureCodeBytes;
    info.hash = overlay_fixture_hash(image.data(), image.size());
    info.name = MHFU_OVERLAY_FIXTURE_MARK == 1u ? "stageA" : "stageB";
    return info;
}

const mhfu::OverlayModuleInfo kInfo = fixture_info();

} // namespace

extern "C" MHFU_OVERLAY_EXPORT const mhfu::OverlayModuleInfo *mhfu_overlay_info() {
    return &kInfo;
}

extern "C" MHFU_OVERLAY_EXPORT void mhfu_register_overlay(psprecomp::Runtime &runtime) {
    // Chained calls only enter bodies whose names start with recomp_unit_, which
    // is how generated overlay code is registered.
    const char *unit_name = MHFU_OVERLAY_FIXTURE_MARK == 1 ? "recomp_unit_stageA" : "recomp_unit_stageB";
    runtime.register_function(kOverlayFixtureEntry, &fixture_body, unit_name);
}
