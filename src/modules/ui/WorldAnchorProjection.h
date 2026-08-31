#pragma once

#include "ui/UIHost.h"

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <vector>

namespace eve::ui {

/** @brief Immutable result of projecting a UI world anchor into a viewport. */
struct WorldAnchorProjection {
    WorldAnchorState state = WorldAnchorState::Disabled;
    float screenX = 0.f;
    float screenY = 0.f;
    float depth = 0.f;
    float scale = 1.f;
    bool render = false;
};

/** @brief One projected host participating in deterministic screen-space placement. */
struct WorldAnchorLayoutItem {
    std::size_t stableIndex = 0;
    float screenX = 0.f;
    float screenY = 0.f;
    float width = 0.f;
    float height = 0.f;
    float pivotX = 0.f;
    float pivotY = 0.f;
    float depth = 0.f;
    float safeMargin = 0.f;
    float padding = 0.f;
    float maxDisplacement = 0.f;
    int priority = 0;
    bool avoidOverlap = false;
};

/** @brief Owning placement result corresponding to WorldAnchorLayoutItem::stableIndex. */
struct WorldAnchorLayoutResult {
    std::size_t stableIndex = 0;
    float screenX = 0.f;
    float screenY = 0.f;
    float displacementX = 0.f;
    float displacementY = 0.f;
    bool render = false;
};

/**
 * @brief Projects one world-space UI anchor using a Vulkan RH-ZO view-projection matrix.
 * @param anchor Authoritative anchor settings; derived fields are ignored.
 * @param viewProjection Camera projection multiplied by view.
 * @param cameraX Camera world X used only for distance scaling.
 * @param cameraY Camera world Y used only for distance scaling.
 * @param cameraZ Camera world Z used only for distance scaling.
 * @param viewportWidth Positive drawable width in pixels.
 * @param viewportHeight Positive drawable height in pixels.
 * @return Owning, deterministic projection snapshot. Invalid viewports produce NoCamera.
 * @thread Pure and thread-safe; invokes no callbacks and retains no references.
 */
[[nodiscard]] WorldAnchorProjection projectWorldAnchor(const UIHost::WorldAnchor &anchor,
                                                       const glm::mat4 &viewProjection,
                                                       float cameraX, float cameraY, float cameraZ,
                                                       float viewportWidth, float viewportHeight);

/**
 * @brief Resolves projected host overlap with bounded deterministic vertical displacement.
 * @param items Owning snapshots. Higher priority wins, then nearer depth, then stableIndex.
 * @param viewportWidth Positive viewport width.
 * @param viewportHeight Positive viewport height.
 * @return One owning result per input item, sorted by stableIndex.
 * @thread Pure and thread-safe; invokes no callbacks and retains no references.
 */
[[nodiscard]] std::vector<WorldAnchorLayoutResult> resolveWorldAnchorOverlaps(
    std::vector<WorldAnchorLayoutItem> items, float viewportWidth, float viewportHeight);

}  // namespace eve::ui
