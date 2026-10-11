#pragma once

#include "common/Result.h"

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace ssq { class Table; }

namespace eve::graphics {

/** @brief Pcg-compatible render-size multiplier for planar water reflections. */
enum class WaterReflectionResolution : std::uint8_t { Full = 0, Half = 1, Third = 2, Quarter = 3 };

/** @brief Authored planar-reflection policy with no camera or texture ownership. */
struct WaterPlanarReflectionSettings {
    bool enabled = true;
    bool disableSkyboxReflections = false;
    WaterReflectionResolution resolutionMultiplier = WaterReflectionResolution::Half;
    float clipPlaneOffset = 0.0F;
    std::uint32_t reflectLayers = 0xffffffffU;
    bool shadows = false;
    bool enableRenderDistance = false;
    bool enablePerLayerDistances = false;
    float customRenderDistance = 500.0F;
    float lodBias = 1.0F;
    std::array<float, 32> customRenderDistances{};
    int textureResolution = 512;
};

/** @brief Immutable camera and horizontal water-plane inputs for a reflection capture. */
struct WaterPlanarReflectionInput {
    glm::vec3 cameraPosition{};
    glm::vec3 cameraForward{0.0F, 0.0F, -1.0F};
    float waterPlaneY = 0.0F;
    float renderScale = 1.0F;
    bool orthographic = false;
    bool reflectionOrPreviewCamera = false;
};

/** @brief Complete, caller-owned plan for configuring a mirrored capture camera. */
struct WaterPlanarReflectionPlan {
    glm::mat4 reflectionMatrix{1.0F};
    glm::vec3 cameraPosition{};
    glm::vec3 cameraForward{0.0F, 0.0F, -1.0F};
    glm::vec4 worldClipPlane{0.0F, 1.0F, 0.0F, 0.0F};
    std::array<float, 32> layerCullDistances{};
    std::uint32_t cullingMask = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    bool renderShadows = false;
    bool shouldRender = false;
};

/**
 * @brief Build the mirror-camera plan used by PcgPlanarReflections without retaining scene objects.
 * @param settings Immutable authored settings; distances must be finite and nonnegative.
 * @param input Immutable finite camera and water-plane snapshot.
 * @return A complete plan, or InvalidArgument without publishing partial state.
 * @thread Pure, deterministic and reentrant for the supplied inputs.
 */
[[nodiscard]] Result<WaterPlanarReflectionPlan> buildWaterPlanarReflectionPlan(
    const WaterPlanarReflectionSettings& settings, const WaterPlanarReflectionInput& input);

/** @brief Register planar water-reflection value types and checked planner with the VM owner thread. */
void exposeWaterPlanarReflectionBindings(ssq::Table& table);

}  // namespace eve::graphics
