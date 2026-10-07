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
 * @brief Camera context used to score local shadow candidates for atlas paging.
 * @ownership Ephemeral; valid only for the selectShadowCasters call.
 */
struct ShadowPagingView {
    glm::vec3 eye{0.f};
    /** @brief Camera view-projection (Vulkan RH, ZO) for soft frustum scoring. */
    glm::mat4 viewProj{1.f};
    bool      valid = false;
};

/**
 * @brief Process-wide defaults for creating shadows per light family.
 *
 * Controllers (RenderControl / script) mutate this; RenderSystem3D reads it
 * each frame when selecting casters and allocating atlas slots.
 *
 * Local spot shadows use a fixed atlas (`kLocalSlots`). When more casters
 * request shadows than slots, paging scores by intensity / distance / soft
 * screen coverage and keeps sticky occupancy (hysteresis). An independent
 * update budget can time-slice redraws while still sampling cached maps.
 *
 * @thread Render thread only.
 */
struct EVENGINE_API_BACKENDS ShadowSchemeSettings {
    bool enableDirectionalCsm = true;
    bool enableSpotPerspective = true;
    bool enablePointCube = false;  // GPU atlas path lands later; keep off by default
    int  directionalMapSize = ShadowConfig::kMapSize;
    int  localMapSize = 1024;
    /** @brief Max local atlas occupants this frame (≤ kLocalSlots). */
    int  maxSpotShadowCasters = ShadowConfig::kLocalSlots;
    int  maxPointShadowCasters = 0;
    float defaultSpotBias = 0.002f;
    float defaultPointBias = 0.003f;

    /** @brief Score by camera importance and sticky-slot hysteresis (vs intensity only). */
    bool enableLocalPaging = true;
    /**
     * @brief Max local shadow layers redrawn per frame (time-slice).
     * Occupants beyond this still sample last-written depth when paging keeps them.
     */
    int maxLocalUpdatesPerFrame = ShadowConfig::kLocalSlots;
    /** @brief Multiplicative bonus for casters that already own a slot (reduces flicker). */
    float hysteresisBonus = 0.35f;
    /** @brief Drop candidates below this score when paging view is valid. */
    float minPageScore = 1e-4f;

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
    float          score = 0.f;
    /** @brief When false, skip the shadow pass and keep the cached depth layer. */
    bool           needsUpdate = true;
};

/**
 * @brief Build a perspective light VP for a spot caster (Vulkan RH, ZO depth).
 * @param position World-space light origin.
 * @param direction World-space beam axis (toward the lit volume).
 * @param range Positive far plane / Light3D radius.
 * @param outerAngleDeg Spot outer half-angle in degrees.
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
 * @brief Score a spot caster for local-shadow paging (higher = more important).
 * @param light Spot light data.
 * @param view Camera paging view (ignored when invalid / paging disabled).
 * @param settings Active scheme settings.
 * @param hadSlot True when this light occupied a local slot last frame.
 */
[[nodiscard]] EVENGINE_API_BACKENDS float scoreSpotShadowCandidate(const Light3D::Data& light,
                                                                   const ShadowPagingView& view,
                                                                   const ShadowSchemeSettings& settings,
                                                                   bool hadSlot);

/**
 * @brief Select directional CSM caster + paged local spot slots for one frame.
 * @param lights Scene lights (already collected; may exceed atlas capacity).
 * @param isPointFlags Parallel flags (true = point/spot).
 * @param settings Active scheme settings.
 * @param view Camera used for paging scores (optional when paging disabled).
 * @param[out] directionalCaster Dir light chosen for CSM, or nullptr.
 * @param[out] localSlots Allocated spot slots (cached VP when needsUpdate is false).
 */
EVENGINE_API_BACKENDS void selectShadowCasters(const std::vector<Light3D::Data*>& lights,
                                               const std::vector<bool>& isPointFlags,
                                               const ShadowSchemeSettings& settings,
                                               const ShadowPagingView& view,
                                               Light3D::Data*& directionalCaster,
                                               std::vector<LocalShadowSlot>& localSlots);

/** @brief Clear sticky local-page cache (tests / scheme resets). */
EVENGINE_API_BACKENDS void resetShadowLocalPageCache();

}  // namespace eve::graphics
