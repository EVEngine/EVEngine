#pragma once

#include "voxel/CubeTypeRegistry.h"
#include "voxel/FaceDir.h"
#include "voxel/GreedyMesher.h"
#include "voxel/VoxelPack.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace eve::voxel {

/**
 * @brief One 32³ voxel chunk with six direction-sorted packed-rect instance buffers.
 * Coordinates: chunk (cx,cy,cz) → world origin (cx*32, cy*32, cz*32).
 */
class Chunk {
public:
    Chunk(int cx = 0, int cy = 0, int cz = 0) : cx_(cx), cy_(cy), cz_(cz) {
        std::memset(voxels_, 0, sizeof(voxels_));
    }

    int cx() const { return cx_; }
    int cy() const { return cy_; }
    int cz() const { return cz_; }

    float originX() const { return float(cx_ * kChunkSize); }
    float originY() const { return float(cy_ * kChunkSize); }
    float originZ() const { return float(cz_ * kChunkSize); }

    void worldAABB(float &minX, float &minY, float &minZ, float &maxX, float &maxY,
                   float &maxZ) const {
        minX = originX();
        minY = originY();
        minZ = originZ();
        maxX = minX + float(kChunkSize);
        maxY = minY + float(kChunkSize);
        maxZ = minZ + float(kChunkSize);
    }

    uint8_t get(int x, int y, int z) const {
        if (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize)
            return 0;
        return voxels_[index(x, y, z)];
    }

    void set(int x, int y, int z, uint8_t texId) {
        if (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize) return;
        voxels_[index(x, y, z)] = texId;
        dirty_ = true;
    }

    void fill(uint8_t texId) {
        std::memset(voxels_, texId, sizeof(voxels_));
        dirty_ = true;
    }

    void clear() { fill(0); }

    bool isDirty() const { return dirty_; }

    /** @brief Rebuild six face instance buffers via greedy meshing. */
    void remesh(const CubeTypeRegistry &types = CubeTypeRegistry::empty()) {
        GreedyMesher::meshChunk(voxels_, faces_, types);
        dirty_ = false;
    }

    /** @brief Ensure mesh is up to date; remesh if dirty. */
    void ensureMeshed(const CubeTypeRegistry &types = CubeTypeRegistry::empty()) {
        if (dirty_) remesh(types);
    }

    const std::vector<PackedRect> &faceRects(FaceDir dir) const {
        return faces_[int(dir)];
    }

    int faceRectCount(FaceDir dir) const { return int(faces_[int(dir)].size()); }

    const uint32_t *facePackedData(FaceDir dir) const {
        const auto &v = faces_[int(dir)];
        return v.empty() ? nullptr : reinterpret_cast<const uint32_t *>(v.data());
    }

    int totalRectCount() const {
        int n = 0;
        for (int i = 0; i < faceDirCount(); ++i) n += int(faces_[i].size());
        return n;
    }

    const uint8_t *rawVoxels() const { return voxels_; }

private:
    static int index(int x, int y, int z) {
        return x + y * kChunkSize + z * kChunkSize * kChunkSize;
    }

    int cx_ = 0;
    int cy_ = 0;
    int cz_ = 0;
    uint8_t voxels_[kChunkSize * kChunkSize * kChunkSize]{};
    std::vector<PackedRect> faces_[6];
    bool dirty_ = true;
};

}  // namespace eve::voxel
