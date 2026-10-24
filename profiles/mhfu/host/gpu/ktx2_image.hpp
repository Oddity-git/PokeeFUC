#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mhfu::gpu {

// KTX2 images in texture packs made for PPSSPP: Basis Universal LDR data
// (ETC1S, UASTC, and the ASTC/XUASTC LDR formats of Basis Universal 2), with or
// without zstd supercompression. HDR data is refused. They are
// read with the Basis Universal transcoder (third_party/basis_universal) and
// turned into RGBA8, red in the low byte, like the PNG images of a pack.

// True when `bytes` start with the KTX2 file identifier.
[[nodiscard]] bool is_ktx2(const std::uint8_t *bytes, std::size_t size);

struct Ktx2Image {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels; // width * height * 4
};

// The first mipmap level of the first layer and face. False, with `error`
// set, for a file that is not Basis Universal LDR data, is damaged, or is
// larger than `max_side` on either side.
bool decode_ktx2(
    const std::uint8_t *bytes, std::size_t size, std::uint32_t max_side, Ktx2Image &image, std::string &error);

} // namespace mhfu::gpu
