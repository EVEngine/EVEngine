#pragma once

#include "common/Module.h"
#include "voxel/Chunk.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelWorld.h"

#include <string>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::voxel {

/**
 * High-performance voxel rendering module.
 *
 * - 32³ chunks, greedy-meshed into face rectangles
 * - Each rect packed into one uint32 (xyz + wh + tex)
 * - Six per-chunk face buffers (pos/neg X/Y/Z)
 * - Camera frustum + view-range + face-orientation cull → instanced draws
 *
 * Script: `voxel <- eve.Voxel(); world <- voxel.newWorld();`
 */
class Voxel : public Module {
public:
    Module_REG(Voxel);
    Voxel() = default;
    ~Voxel() override = default;

    int getChunkSize() const { return kChunkSize; }

    VoxelWorld *newWorld();
    Chunk *newChunk(int cx = 0, int cy = 0, int cz = 0);

    /** Pack helpers for tests / tooling (no overloads). */
    uint32_t packRect(int x, int y, int z, int width, int height, int tex) const;
    int unpackRectX(uint32_t bits) const;
    int unpackRectY(uint32_t bits) const;
    int unpackRectZ(uint32_t bits) const;
    int unpackRectWidth(uint32_t bits) const;
    int unpackRectHeight(uint32_t bits) const;
    int unpackRectTex(uint32_t bits) const;

    /**
     * Run greedy meshing on a flat voxel buffer (length >= 32³).
     * Results stored in module-owned face buffers; query via getMeshFace*.
     */
    void meshVoxels(const uint8_t *voxels, int byteCount);
    int getMeshFaceCount(const std::string &faceDir) const;
    uint32_t getMeshFacePacked(const std::string &faceDir, int index) const;
};

}  // namespace eve::voxel
