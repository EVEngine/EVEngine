#pragma once

#include "voxel/FaceDir.h"
#include "voxel/VoxelPack.h"

#include <cstdint>
#include <vector>

namespace eve::voxel {

/**
 * Greedy rectangle mesher for one 32³ dense voxel volume.
 * Voxel storage: index = x + y*32 + z*32*32. Value 0 = air; 1..127 = texture id.
 *
 * Emits packed rects into six face buffers (caller-provided).
 * Out-of-chunk neighbors are treated as air (chunk seams get both sides until
 * a future neighbor-aware pass).
 */
class GreedyMesher {
public:
    /** Mesh all six faces. Clears and fills `outFaces[6]`. */
    static void meshChunk(const uint8_t *voxels, std::vector<PackedRect> outFaces[6]);

    /** Mesh a single face direction into `out` (appended; caller may clear). */
    static void meshFace(const uint8_t *voxels, FaceDir dir, std::vector<PackedRect> &out);

private:
    static uint8_t at(const uint8_t *v, int x, int y, int z);
    static bool isSolid(uint8_t id) { return id != 0; }
    static void greedy2D(uint16_t mask[kChunkSize][kChunkSize], FaceDir dir, int slice,
                         std::vector<PackedRect> &out);
};

}  // namespace eve::voxel
