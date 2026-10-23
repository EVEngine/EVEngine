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
    /** @brief Constructs a Chunk. */
    Chunk(int cx = 0, int cy = 0, int cz = 0) : cx_(cx), cy_(cy), cz_(cz) {
        /** @brief Memset. */
        std::memset(voxels_, 0, sizeof(voxels_));
    }

    /** @brief Cx. */
    int cx() const { return cx_; }
    /** @brief Cy. */
    int cy() const { return cy_; }
    /** @brief Cz. */
    int cz() const { return cz_; }

    /** @brief Origin x. */
    float originX() const { return float(cx_ * kChunkSize); }
    /** @brief Origin y. */
    float originY() const { return float(cy_ * kChunkSize); }
    /** @brief Origin z. */
    float originZ() const { return float(cz_ * kChunkSize); }

    /** @brief World aabb. */
    void worldAABB(float &minX, float &minY, float &minZ, float &maxX, float &maxY,
                   float &maxZ) const {
        minX = originX();
        minY = originY();
        minZ = originZ();
        maxX = minX + float(kChunkSize);
        maxY = minY + float(kChunkSize);
        maxZ = minZ + float(kChunkSize);
    }

    /** @brief Returns the value. */
    uint8_t get(int x, int y, int z) const {
        if (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize)
            return 0;
        return voxels_[index(x, y, z)];
    }

    /** @brief Sets the value. */
    void set(int x, int y, int z, uint8_t texId) {
        if (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize) return;
        voxels_[index(x, y, z)] = texId;
        dirty_ = true;
    }

    /** @brief Fill. */
    void fill(uint8_t texId) {
        /** @brief Memset. */
        std::memset(voxels_, texId, sizeof(voxels_));
        dirty_ = true;
    }

    /** @brief Clears . */
    void clear() { fill(0); }

    /** Replace the whole 32³ voxel storage (e.g. deserialization). */
    /** @brief Sets the voxel data. */
    void setVoxelData(const uint8_t *src) {
        /** @brief Memcpy. */
        std::memcpy(voxels_, src, sizeof(voxels_));
        dirty_ = true;
    }

    /** @brief True when dirty. */
    bool isDirty() const { return dirty_; }

    /** Explicitly invalidate the mesh (e.g. neighbor edit on a shared border). */
    /** @brief Mark dirty. */
    void markDirty() { dirty_ = true; }

    /** @brief Rebuild six face instance buffers via greedy meshing. */
    void remesh(const CubeTypeRegistry &types = CubeTypeRegistry::empty(),
                ChunkSampler sampler = nullptr,
                void *samplerUserData = nullptr) {
        /** @brief Mesh chunk. */
        GreedyMesher::meshChunk(voxels_, faces_, types, sampler, samplerUserData, cx_, cy_, cz_,
                                ao_);
        dirty_ = false;
    }

    /** @brief Ensure mesh is up to date; remesh if dirty. */
    void ensureMeshed(const CubeTypeRegistry &types = CubeTypeRegistry::empty(),
                      ChunkSampler sampler = nullptr,
                      void *samplerUserData = nullptr) {
        if (dirty_) remesh(types, sampler, samplerUserData);
    }

    /** @brief Face rects. */
    const std::vector<PackedRect> &faceRects(FaceDir dir) const {
        return faces_[int(dir)];
    }

    /** @brief Face rect count. */
    int faceRectCount(FaceDir dir) const { return int(faces_[int(dir)].size()); }

    /** @brief Face packed data. */
    const uint32_t *facePackedData(FaceDir dir) const {
        const auto &v = faces_[int(dir)];
        return v.empty() ? nullptr : reinterpret_cast<const uint32_t *>(v.data());
    }

    /**
     * Per-rect ambient-occlusion words (2 bits per corner, 0..3, shader
     * corner order). Parallel to faceRects(dir).
     */
    /** @brief Face ao packed data. */
    const uint32_t *faceAOPackedData(FaceDir dir) const {
        const auto &v = ao_[int(dir)];
        return v.empty() ? nullptr : v.data();
    }

    /** @brief Total rect count. */
    int totalRectCount() const {
        int n = 0;
        for (int i = 0; i < faceDirCount(); ++i) n += int(faces_[i].size());
        return n;
    }

    /** @brief Raw voxels. */
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
    std::vector<uint32_t> ao_[6];
    bool dirty_ = true;
};

}  // namespace eve::voxel
