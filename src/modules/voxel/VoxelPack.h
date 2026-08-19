#pragma once

#include <cstdint>

namespace eve::voxel {

/** Chunk edge length in voxels (fixed). */
constexpr int kChunkSize = 32;

/**
 * Packed face-rectangle instance (exactly 32 bits):
 *   bits  0..4  : x (0..31)
 *   bits  5..9  : y (0..31)
 *   bits 10..14 : z (0..31)
 *   bits 15..19 : width  - 1 (0..31 → size 1..32)
 *   bits 20..24 : height - 1 (0..31 → size 1..32)
 *   bits 25..31 : texture index (0..127)
 *
 * Origin is the min corner of the rectangle in chunk-local voxel coords.
 * Width/height span the face tangent axes (see FaceDir).
 */
struct PackedRect {
    uint32_t bits = 0;

    static PackedRect pack(int x, int y, int z, int width, int height, int tex) {
        PackedRect r;
        r.bits = (uint32_t(x) & 31u) | ((uint32_t(y) & 31u) << 5) | ((uint32_t(z) & 31u) << 10) |
                 ((uint32_t(width - 1) & 31u) << 15) | ((uint32_t(height - 1) & 31u) << 20) |
                 ((uint32_t(tex) & 127u) << 25);
        return r;
    }

    int x() const { return int(bits & 31u); }
    int y() const { return int((bits >> 5) & 31u); }
    int z() const { return int((bits >> 10) & 31u); }
    int width() const { return int((bits >> 15) & 31u) + 1; }
    int height() const { return int((bits >> 20) & 31u) + 1; }
    int tex() const { return int((bits >> 25) & 127u); }
};

static_assert(sizeof(PackedRect) == 4, "PackedRect must be 32-bit");

}  // namespace eve::voxel
