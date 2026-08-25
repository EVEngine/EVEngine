#pragma once

#include "graphics/AtmosphereVolume.h"

#include <vector>

namespace eve::graphics {

/** @brief One camera-centered, world-space atmosphere clipmap level. */
struct AtmosphereClipmapLevel {
    AtmosphereVolume volume;
    glm::vec3 center{0.f};
    float extent = 1.f;
    float voxelSize = 1.f;
};

/**
 * @brief Nested world-space volume grids for stable large-range participating media.
 *
 * Level zero has the finest voxels. Each following level doubles its world extent.
 * Centers snap to whole voxels so sub-voxel camera movement does not resample media.
 */
class AtmosphereClipmap {
public:
    /** @brief Allocate nested cubic levels and center them at the origin. */
    void configure(int levelCount, int resolution, float firstExtent);
    /** @brief Snap all levels around a new camera position. */
    void updateCenter(const glm::vec3 &cameraPosition);

    int getLevelCount() const { return int(levels_.size()); }
    AtmosphereClipmapLevel &getLevel(int level);
    const AtmosphereClipmapLevel &getLevel(int level) const;

    /** @brief Select the finest level containing a world position, or -1. */
    int levelForPosition(const glm::vec3 &worldPosition) const;
    /** @brief Sample raw media from the finest containing level. */
    FogFroxel sample(const glm::vec3 &worldPosition) const;

private:
    std::vector<AtmosphereClipmapLevel> levels_;
};

}  // namespace eve::graphics
