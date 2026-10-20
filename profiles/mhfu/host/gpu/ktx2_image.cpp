#include "ktx2_image.hpp"

// The transcoder's headers mix enumeration types in arithmetic, which C++20
// deprecates; they are third-party code and stay unmodified.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#endif
#include "basis_universal/transcoder/basisu_transcoder.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <cstring>
#include <limits>
#include <mutex>

namespace mhfu::gpu {
namespace {

constexpr std::uint8_t kIdentifier[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};

void init_transcoder() {
    static std::once_flag once;
    std::call_once(once, [] { basist::basisu_transcoder_init(); });
}

} // namespace

bool is_ktx2(const std::uint8_t *bytes, std::size_t size) {
    return bytes != nullptr && size >= sizeof(kIdentifier) && std::memcmp(bytes, kIdentifier, sizeof(kIdentifier)) == 0;
}

bool decode_ktx2(
    const std::uint8_t *bytes, std::size_t size, std::uint32_t max_side, Ktx2Image &image, std::string &error) {
    image = Ktx2Image{};
    if (!is_ktx2(bytes, size)) {
        error = "not a KTX2 file";
        return false;
    }
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        error = "KTX2 file too large";
        return false;
    }
    init_transcoder();
    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(bytes, static_cast<std::uint32_t>(size))) {
        // init() also refuses KTX2 files that hold plain Vulkan formats
        // rather than Basis Universal data.
        error = "KTX2 file is damaged or not Basis Universal";
        return false;
    }
    if (!transcoder.is_ldr()) {
        error = "KTX2 file holds HDR data";
        return false;
    }
    const std::uint32_t width = transcoder.get_width();
    const std::uint32_t height = transcoder.get_height();
    if (width == 0u || height == 0u || width > max_side || height > max_side) {
        error = "too large";
        return false;
    }
    if (!transcoder.start_transcoding()) {
        error = "KTX2 data is damaged";
        return false;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    // RGBA32 counts its output buffer in pixels.
    if (!transcoder.transcode_image_level(
            0, 0, 0, pixels.data(), width * height, basist::transcoder_texture_format::cTFRGBA32)) {
        error = "KTX2 data cannot be transcoded";
        return false;
    }
    image.width = width;
    image.height = height;
    image.pixels = std::move(pixels);
    return true;
}

} // namespace mhfu::gpu
