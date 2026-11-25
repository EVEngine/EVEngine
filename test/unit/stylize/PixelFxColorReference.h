#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace test_pixelfx_reference {
// A measured reference pixel, packed as AABBGGRR. Bits 0/1/2 select R/G/B.
// Only the observed directional one-level quantization boundary is admitted.
struct RoundingPixel {
    size_t   index;
    uint32_t rgba;
    uint8_t  upperChannels;
    uint8_t  lowerChannels = 0;
};

// Validate measured pixels channel-by-channel, then hash their canonical
// reference values with every unchanged actual pixel. Never mutate the frame.
// Alpha and all unselected channels/pixels retain the original exact oracle.
[[nodiscard]] inline bool matchesColorReference(std::span<const uint32_t> pixels, uint32_t expectedHash,
                                                std::span<const RoundingPixel> references) {
    for (size_t i = 0; i < references.size(); ++i) {
        const auto& reference = references[i];
        if (reference.index >= pixels.size() || ((reference.upperChannels | reference.lowerChannels) & ~7u) != 0 ||
            (reference.upperChannels & reference.lowerChannels) != 0 ||
            (i > 0 && references[i - 1].index >= reference.index))
            return false;
        const auto actual = pixels[reference.index];
        for (unsigned channel = 0; channel < 4; ++channel) {
            const auto shift = channel * 8;
            const int  delta = int((actual >> shift) & 255u) - int((reference.rgba >> shift) & 255u);
            if (delta == 0) continue;
            if (channel == 3) return false;
            const bool upper = delta == 1 && (reference.upperChannels & (1u << channel)) != 0;
            const bool lower = delta == -1 && (reference.lowerChannels & (1u << channel)) != 0;
            if (!upper && !lower) return false;
        }
    }
    uint32_t hash = 2166136261u;
    size_t   next = 0;
    for (size_t i = 0; i < pixels.size(); ++i) {
        auto color = pixels[i];
        if (next < references.size() && i == references[next].index) {
            color = references[next].rgba;
            ++next;
        }
        for (unsigned shift = 0; shift < 32; shift += 8) hash = (hash ^ ((color >> shift) & 255u)) * 16777619u;
    }
    return hash == expectedHash;
}
[[nodiscard]] inline bool matchesColorReference(std::span<const uint32_t> pixels, uint32_t expectedHash,
                                                RoundingPixel reference) {
    return matchesColorReference(pixels, expectedHash, std::span<const RoundingPixel>(&reference, 1));
}
}  // namespace test_pixelfx_reference
