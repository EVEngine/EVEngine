#include "graphics/AtmosphereClipmap.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>

namespace eve::graphics {

void AtmosphereClipmap::configure(int levelCount, int resolution, float firstExtent) {
    const int count = std::max(levelCount, 1);
    const int res = std::max(resolution, 2);
    const float baseExtent = std::max(firstExtent, 1e-3f);
    levels_.resize(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        AtmosphereClipmapLevel &level = levels_[std::size_t(i)];
        level.extent = baseExtent * std::pow(2.f, float(i));
        level.voxelSize = (level.extent * 2.f) / float(res);
        level.center = glm::vec3(0.f);
        level.volume.resize(res, res, res);
        level.volume.setDepthRange(level.voxelSize, level.extent * 2.f);
    }
}

void AtmosphereClipmap::updateCenter(const glm::vec3 &cameraPosition) {
    for (AtmosphereClipmapLevel &level : levels_) {
        level.center = glm::floor(cameraPosition / level.voxelSize) * level.voxelSize;
    }
}

AtmosphereClipmapLevel &AtmosphereClipmap::getLevel(int level) {
    return levels_[std::size_t(std::clamp(level, 0, int(levels_.size()) - 1))];
}

const AtmosphereClipmapLevel &AtmosphereClipmap::getLevel(int level) const {
    return levels_[std::size_t(std::clamp(level, 0, int(levels_.size()) - 1))];
}

int AtmosphereClipmap::levelForPosition(const glm::vec3 &worldPosition) const {
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        const AtmosphereClipmapLevel &level = levels_[i];
        const glm::vec3 d = glm::abs(worldPosition - level.center);
        if (d.x <= level.extent && d.y <= level.extent && d.z <= level.extent) return int(i);
    }
    return -1;
}

FogFroxel AtmosphereClipmap::sample(const glm::vec3 &worldPosition) const {
    const int index = levelForPosition(worldPosition);
    if (index < 0) return {};
    const AtmosphereClipmapLevel &level = levels_[std::size_t(index)];
    const glm::vec3 minCorner = level.center - glm::vec3(level.extent);
    const glm::vec3 uvw = (worldPosition - minCorner) / (level.extent * 2.f);
    return level.volume.sampleMedia(uvw.x, uvw.y, uvw.z);
}

}  // namespace eve::graphics
