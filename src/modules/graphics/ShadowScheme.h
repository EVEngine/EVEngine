#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "graphics/Light.h"
#include "graphics/Shadow.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

namespace eve::graphics {

/**
 * ShadowMethod lives in Shadow.h. Auto resolves per light type:
 * dir→CSM, spot→perspective, point→cube.
 */

/** @brief Parse a script/editor method name into ShadowMethod. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<ShadowMethod> parseShadowMethod(std::string_view name);

/** @brief Canonical lowercase name for a ShadowMethod (`auto`, `none`, `csm`, …). */
[[nodiscard]] EVENGINE_API_BACKENDS const char* shadowMethodName(ShadowMethod method);

/**
 * @brief Resolve Auto / validate an explicit method for a concrete light type.
 * @param lightType `"dir"` / `"point"` / `"spot"`.
 * @param requested Method from the light (may be Auto).
 * @return Concrete method, or failure when the combination is invalid.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<ShadowMethod> resolveShadowMethod(std::string_view lightType,
                                                                             ShadowMethod requested);

/**
 * @brief Process-wide defaults for creating shadows per light family.
 *
 * Controllers (RenderControl / script) mutate this; RenderSystem3D reads it
 * each frame when selecting casters and allocating atlas slots.
 *
 * @thread Render thread only.
 */
struct EVENGINE_API_BACKENDS ShadowSchemeSettings {
    bool enableDirectionalCsm = true;
    bool enableSpotPerspective = true;
    bool enablePointCube = false;  // GPU atlas path lands later; keep off by default
    int  directionalMapSize = ShadowConfig::kMapSize;
    int  localMapSize = 1024;
    int  maxSpotShadowCasters = ShadowConfig::kLocalSlots;
    int  maxPointShadowCasters = 0;
    float defaultSpotBias = 0.002f;
    float defaultPointBias = 0.003f;

    /** @brief Borrow the process-wide settings object. */
    [[nodiscard]] static ShadowSchemeSettings& current();
};

/**
 * @brief One allocated local (spot/point-face) shadow slot for the frame.
 * @ownership Ephemeral CPU descriptor; GPU layer index is valid for the frame.
 */
struct LocalShadowSlot {
    Light3D::Data* light = nullptr;
    ShadowMethod   method = ShadowMethod::None;
    int            layer = -1;       // index into the shadow depth array
    int            lightIndex = -1;  // index in the packed Light3D list (0..7)
    glm::mat4      lightVP{1.f};
    float          bias = 0.002f;
    float          strength = 1.f;
};

/**
 * @brief Build a perspective light VP for a spot caster (Vulkan RH, ZO depth).
 * @param position World-space light origin.
 * @param direction World-space beam axis (toward the lit volume).
 * @param range Positive far plane / Light3D radius.
 * @param outerAngleDeg Spot outer half-angle in degrees.
 * @param bias Unused for the matrix itself; recorded by callers.
 */
[[nodiscard]] EVENGINE_API_BACKENDS glm::mat4 buildSpotShadowVP(const glm::vec3& position,
                                                                const glm::vec3& direction, float range,
                                                                float outerAngleDeg);

/**
 * @brief Build one cube-face light VP for a point caster.
 * @param position World-space light origin.
 * @param faceIndex 0..5 (+X,-X,+Y,-Y,+Z,-Z).
 * @param range Positive far plane / Light3D radius.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<glm::mat4> buildPointShadowFaceVP(const glm::vec3& position,
                                                                             int faceIndex, float range);

/**
 * @brief Select directional CSM caster + local spot slots for one frame.
 * @param packed Packed scene lights (already collected / sorted).
 * @param settings Active scheme settings.
 * @param[out] directionalCaster Dir light chosen for CSM, or nullptr.
 * @param[out] localSlots Filled with allocated spot slots (layer indices assigned by caller).
 */
EVENGINE_API_BACKENDS void selectShadowCasters(const std::vector<Light3D::Data*>& lights,
                                               const std::vector<bool>& isPointFlags,
                                               const ShadowSchemeSettings& settings,
                                               Light3D::Data*& directionalCaster,
                                               std::vector<LocalShadowSlot>& localSlots);

}  // namespace eve::graphics
