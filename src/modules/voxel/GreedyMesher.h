#pragma once

#include "voxel/CubeTypeRegistry.h"
#include "voxel/FaceDir.h"
#include "voxel/VoxelPack.h"

#include <cstdint>
#include <vector>

namespace eve::voxel {

/**
 * Optional out-of-chunk neighbor sampler for seam culling.
 * Receives chunk coordinates plus the (possibly out-of-range) local voxel
 * coords and returns the voxel id there (0 = air / unknown).
 * Null sampler → out-of-chunk neighbors are treated as air (old behavior).
 */
using ChunkSampler = uint8_t (*)(void *userData, int chunkX, int chunkY, int chunkZ,
                                 int localX, int localY, int localZ);

/**
 * Greedy rectangle mesher for one 32³ dense voxel volume.
 * Voxel storage: index = x + y*32 + z*32*32. Value 0 = air; 1..255 = type id.
 *
 * 每个体素的类型 id 通过注册表解析为“各面纹理 id”后合并；输出 PackedRect.tex
 * 始终是纹理 id（0..127），渲染端无需感知“类型”概念。
 * 空注册表下 resolveFaceTex 退化为 id，行为与旧的“体素值 == 纹理 id”一致。
 *
 * Emits packed rects into six face buffers (caller-provided).
 * Out-of-chunk neighbors consult the optional ChunkSampler so shared faces
 * between adjacent chunks are culled (no duplicate interior faces).
 */
class GreedyMesher {
public:
    /** Mesh all six faces. Clears and fills `outFaces[6]`. */
    static void meshChunk(const uint8_t *voxels, std::vector<PackedRect> outFaces[6],
                          const CubeTypeRegistry &types = CubeTypeRegistry::empty(),
                          ChunkSampler sampler = nullptr, void *samplerUserData = nullptr,
                          int chunkX = 0, int chunkY = 0, int chunkZ = 0,
                          std::vector<uint32_t> aoOut[6] = nullptr);

    /** Mesh a single face direction into `out` (appended; caller may clear). */
    static void meshFace(const uint8_t *voxels, FaceDir dir, std::vector<PackedRect> &out,
                         const CubeTypeRegistry &types = CubeTypeRegistry::empty(),
                         ChunkSampler sampler = nullptr, void *samplerUserData = nullptr,
                         int chunkX = 0, int chunkY = 0, int chunkZ = 0,
                         std::vector<uint32_t> *aoOut = nullptr);

private:
    static uint8_t at(const uint8_t *v, int x, int y, int z);
    /** In-chunk lookup, or sampler for out-of-chunk coords (null → air). */
    static uint8_t neighborAt(const uint8_t *voxels, ChunkSampler sampler, void *userData,
                              int chunkX, int chunkY, int chunkZ, int nx, int ny, int nz);
    /** Outside-layer occluder for a face grid vertex (0 = air). */
    static uint8_t outsideVoxel(const uint8_t *voxels, ChunkSampler sampler, void *userData,
                                int chunkX, int chunkY, int chunkZ, FaceDir dir, int slice,
                                int gu, int gv);
    /** Classic Minecraft-style vertex AO (0..3) for a face grid vertex. */
    static uint8_t vertexAO(const uint8_t *voxels, ChunkSampler sampler, void *userData,
                            int chunkX, int chunkY, int chunkZ, FaceDir dir, int slice, int gu,
                            int gv);
    /** 4-corner AO word (2 bits per corner, shader vertex order). */
    static uint32_t rectAOWord(const uint8_t *voxels, ChunkSampler sampler, void *userData,
                               int chunkX, int chunkY, int chunkZ, FaceDir dir, int slice, int x,
                               int y, int z, int w, int h);
    static bool isSolid(uint8_t id) { return id != 0; }
    static void greedy2D(int mask[kChunkSize][kChunkSize], FaceDir dir, int slice,
                         std::vector<PackedRect> &out, std::vector<uint32_t> *aoOut,
                         const uint8_t *voxels, ChunkSampler sampler, void *userData,
                         int chunkX, int chunkY, int chunkZ);
};

}  // namespace eve::voxel
