#pragma once

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace eve::graphics {

class FogVolume;

/** @brief Participating-media coefficients stored in one froxel. */
struct FogFroxel {
    glm::vec3 scattering{0.f};
    float extinction = 0.f;
    glm::vec3 emissive{0.f};
    float anisotropy = 0.f;
    float lightVisibility = 1.f;
};

/** @brief Camera-frustum volume used by the volumetric fog passes and CPU references. */
class AtmosphereVolume {
public:
    /** @brief Resize the froxel grid and clear all media. */
    void resize(int width, int height, int depth);
    /** @brief Remove all media and integrated lighting without changing dimensions. */
    void clear();

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getDepth() const { return depth_; }
    std::size_t getFroxelCount() const { return media_.size(); }

    /** @brief Configure the world-distance range represented by logarithmic Z slices. */
    void setDepthRange(float nearDistance, float farDistance);
    float getNearDistance() const { return nearDistance_; }
    float getFarDistance() const { return farDistance_; }
    /** @brief World distance at the center of a Z slice. */
    float sliceDistance(int z) const;
    /** @brief Convert a world distance into a clamped slice index. */
    int sliceForDistance(float distance) const;

    /** @brief Read or write one froxel; indices are clamped to the grid. */
    FogFroxel &at(int x, int y, int z);
    const FogFroxel &at(int x, int y, int z) const;

    /**
     * @brief Add a global exponential-height medium to every froxel.
     * @param baseExtinction Extinction coefficient at baseHeight.
     * @param albedo Single-scattering albedo in [0,1].
     * @param baseHeight World height where density is baseExtinction.
     * @param heightFalloff Exponential falloff per world unit.
     * @param minWorldY World height represented by the first Y row.
     * @param maxWorldY World height represented by the last Y row.
     */
    void injectHeightFog(float baseExtinction, const glm::vec3 &albedo, float baseHeight,
                         float heightFalloff, float minWorldY, float maxWorldY);

    /** @brief Voxelize one analytic local volume over the supplied world bounds. */
    void injectLocalVolume(const FogVolume &volume, const glm::vec3 &worldMin,
                           const glm::vec3 &worldMax);

    /**
     * @brief Integrate scattering and transmittance along every view ray.
     * @param lightColor Incident radiance after shadowing.
     * @param phaseScale Phase-function multiplier for this light direction.
     */
    void integrate(const glm::vec3 &lightColor, float phaseScale = 1.f);

    /** @brief Set the main-light shadow visibility for one froxel. */
    void setLightVisibility(int x, int y, int z, float visibility);

    /**
     * @brief Blend integrated lighting with a reprojected history volume.
     * @param history Previous frame already mapped into this grid.
     * @param historyWeight Base history weight in [0,1].
     * @param rejectionThreshold Relative luminance delta that rejects history.
     * @return Number of froxels whose history was rejected.
     */
    std::size_t blendHistory(const AtmosphereVolume &history, float historyWeight,
                             float rejectionThreshold);

    /** @brief Cumulative RGB in-scattering and A transmittance at a froxel. */
    const glm::vec4 &integratedAt(int x, int y, int z) const;

private:
    std::size_t index(int x, int y, int z) const;

    int width_ = 0;
    int height_ = 0;
    int depth_ = 0;
    float nearDistance_ = 0.1f;
    float farDistance_ = 100.f;
    std::vector<FogFroxel> media_;
    std::vector<glm::vec4> integrated_;
};

}  // namespace eve::graphics
