#pragma once

#include "graphics/Light.h"

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace eve::graphics {

/** @brief Clustered forward grid (CPU assign, GPU consume). */
struct ClusteredLightConfig {
    static constexpr int kTilesX = 16;
    static constexpr int kTilesY = 9;
    static constexpr int kSlices = 24;
    static constexpr int kClusterCount = kTilesX * kTilesY * kSlices;
    static constexpr int kMaxLights = 256;
    static constexpr int kMaxLightsPerCluster = 32;
};

using ClusteredLightGpu = Light3DGpu;

struct ClusterTableEntry {
    uint32_t offset = 0;
    uint32_t count = 0;
};

/**
 * @brief CPU-built clustered lighting upload for one frame/camera.
 * Point lights are clustered; directional lights become the primary dir (first by intensity).
 */
struct ClusteredLightingUpload {
    glm::vec4 ambient{0.12f, 0.12f, 0.14f, 0.f};
    glm::vec4 gridInfo{float(ClusteredLightConfig::kTilesX), float(ClusteredLightConfig::kTilesY),
                       float(ClusteredLightConfig::kSlices), 0.f};  // w = point light count
    glm::vec4 clipInfo{0.1f, 100.f, 1.f, 1.f};  // near, far, screenW, screenH
    glm::mat4 view{1.f};
    glm::vec4 primaryDir{0.4f, 1.f, 0.3f, 0.f};  // toward surface; w=1 if valid
    glm::vec4 primaryColor{1.f, 1.f, 1.f, 1.f};
    std::vector<ClusteredLightGpu> lights;           // point lights only (world space)
    std::vector<ClusterTableEntry> clusterTable;     // size = kClusterCount
    std::vector<uint32_t> lightIndices;
    bool active = false;
};

/**
 * @brief Build clustered tables for point lights in view space.
 * @param points  world-space point lights (posRadius.w = radius > 0)
 * @param dirs    world-space directional lights (optional; first becomes primary)
 */
ClusteredLightingUpload buildClusteredLighting(const std::vector<ClusteredLightGpu> &points,
                                               const std::vector<ClusteredLightGpu> &dirs,
                                               const glm::mat4 &view, float nearZ, float farZ,
                                               int screenW, int screenH, float fovYRad,
                                               const glm::vec4 &ambient);

}  // namespace eve::graphics
