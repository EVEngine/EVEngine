#pragma once

#include "voxel/CubeTypeRegistry.h"
#include "voxel/FaceDir.h"
#include "voxel/VoxelPack.h"

#include <cstdint>
#include <vector>

namespace eve::voxel {

/**
 * Greedy rectangle mesher for one 32³ dense voxel volume.
 * Voxel storage: index = x + y*32 + z*32*32. Value 0 = air; 1..255 = type id.
 *
 * 每个体素的类型 id 通过注册表解析为“各面纹理 id”后合并；输出 PackedRect.tex
 * 始终是纹理 id（0..127），渲染端无需感知“类型”概念。
 * 空注册表下 resolveFaceTex 退化为 id，行为与旧的“体素值 == 纹理 id”一致。
 *
 * Emits packed rects into six face buffers (caller-provided).
 * Out-of-chunk neighbors are treated as air (chunk seams get both sides until
 * a future neighbor-aware pass).
 */
class GreedyMesher {
public:
    /** Mesh all six faces. Clears and fills `outFaces[6]`. */
    static void meshChunk(const uint8_t *voxels, std::vector<PackedRect> outFaces[6],
                          const CubeTypeRegistry &types = CubeTypeRegistry::empty());

    /** Mesh a single face direction into `out` (appended; caller may clear). */
    static void meshFace(const uint8_t *voxels, FaceDir dir, std::vector<PackedRect> &out,
                         const CubeTypeRegistry &types = CubeTypeRegistry::empty());

private:
    static uint8_t at(const uint8_t *v, int x, int y, int z);
    static bool isSolid(uint8_t id) { return id != 0; }
    static void greedy2D(int mask[kChunkSize][kChunkSize], FaceDir dir, int slice,
                         std::vector<PackedRect> &out);
};

}  // namespace eve::voxel
