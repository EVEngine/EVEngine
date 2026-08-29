#pragma once

#include "graphics/AtmosphereVolume.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

class VolumeDensityGraph;

/** @brief Brick-sparse, pre-bakeable participating-media texture. */
class SparseVolumeTexture {
public:
    /** @brief Allocate a logical volume; bricks are created only for non-empty voxels. */
    void resize(int width, int height, int depth, int brickSize = 8);
    void clear();

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getDepth() const { return depth_; }
    int getBrickSize() const { return brickSize_; }
    std::size_t getBrickCount() const { return bricks_.size(); }
    std::size_t getAllocatedVoxelCount() const;

    /** @brief Store one voxel; all-zero voxels do not allocate a new brick. */
    void setVoxel(int x, int y, int z, const FogFroxel &voxel);
    /** @brief Read one voxel; missing bricks return vacuum. */
    FogFroxel getVoxel(int x, int y, int z) const;
    /** @brief Nearest sample using normalized coordinates. */
    FogFroxel sample(float u, float v, float w) const;

    /** @brief Pre-bake a procedural graph into sparse bricks. */
    void bake(const VolumeDensityGraph &graph, const glm::vec3 &worldMin,
              const glm::vec3 &worldMax, float extinctionScale, const glm::vec3 &albedo,
              float emptyThreshold = 1e-5f, float time = 0.f);

    /** @brief Save the deterministic EVSV v1 binary format. */
    bool save(const std::string &path) const;
    /** @brief Load the deterministic EVSV v1 binary format, replacing current data. */
    bool load(const std::string &path);

private:
    struct Brick {
        std::vector<FogFroxel> voxels;
    };

    std::uint64_t brickKey(int bx, int by, int bz) const;
    std::size_t localIndex(int x, int y, int z) const;
    bool inBounds(int x, int y, int z) const;

    int width_ = 0;
    int height_ = 0;
    int depth_ = 0;
    int brickSize_ = 8;
    std::unordered_map<std::uint64_t, Brick> bricks_;
};

}  // namespace eve::graphics
