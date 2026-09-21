#pragma once

/**
 * @brief Capability for the camera math a scene picking / culling call needs.
 *
 * @ownership Implementations are registered by the module that owns the camera
 * (graphics) and borrow every argument for the duration of one call; nothing is
 * retained.
 * @threadsafety `screenRay` writes the ray back into the borrowed camera, so it
 * inherits that call's caller-side serialization; `clipForViewport` is
 * reentrant.
 *
 * `scene` (L1) exposes picking and frustum-culling entry points that take a
 * `graphics::Camera3D`; turning a screen pixel into a world ray, and building
 * the view-projection matrix, needs a complete `Camera3D`, which is graphics
 * (L4) territory. Implementing those calls inside scene would force an upward
 * include, so the projection lives with graphics and reaches scene through this
 * capability, declared here by the consumer as common/Capability.h prescribes.
 *
 * The implementation is registered by graphics/ScenePicking.cpp; scene queries
 * it per call and degrades explicitly when nothing provides it:
 * `Scene::pickScreenAt` returns an empty id, `Scene::collectFrustumIdsAt` an
 * empty list and `Scene::applyPcgTerrainCullingAt` a
 * `DiagnosticCode::Unsupported` failure. Callers that must tell "no provider"
 * apart from "nothing hit" can query this capability themselves.
 *
 * @cost One `cap::query` per call, then the caller's own traversal; both entry
 * points run per user action, not per frame.
 */

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <optional>

namespace eve::graphics {
class Camera3D;
}

namespace eve::scene {

/** @brief World-space ray through one screen pixel. */
struct ScreenRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

/** @brief Camera view-projection matrix and its inverse for one viewport. */
struct CameraClip {
    glm::mat4 clip{1.0f};
    glm::mat4 inverse{1.0f};
};

/**
 * @brief Camera projection queries answered by the module that owns `Camera3D`.
 *
 * @ownership The caller owns the camera it passes and keeps it alive for the
 * duration of each call; the implementation never stores it.
 * @lifetime A borrowed camera must outlive the call only; the returned ray or
 * matrices are values, so no pointer to either is retained.
 * @threadsafety Stateless implementations; `screenRay` carries the same
 * caller-side serialization requirement as `Camera3D::screenToRay`.
 */
class ISceneCameraProjection {
public:
    static constexpr const char* capabilityName = "ISceneCameraProjection";

    virtual ~ISceneCameraProjection() = default;

    /**
     * @brief Ray through a screen pixel.
     * @param cam Borrowed camera; updated in place with the ray it computed.
     * @param screenX,screenY Pixel in the viewport.
     * @param viewW,viewH Viewport size.
     * @return The ray, or `std::nullopt` when the camera cannot produce one.
     * @ownership The caller retains the camera; no pointer is stored.
     * @lifetime The camera must stay valid for this call only.
     * @cost O(1).
     */
    virtual std::optional<ScreenRay> screenRay(graphics::Camera3D& cam, float screenX, float screenY, float viewW,
                                               float viewH) const = 0;

    /**
     * @brief Clip matrix and its inverse for a camera and viewport.
     * @param cam Borrowed camera.
     * @param viewW,viewH Viewport size; must be positive.
     * @return The matrices, or `std::nullopt` for a degenerate viewport.
     * @ownership The caller retains the camera; no pointer is stored.
     * @lifetime The camera must stay valid for this call only.
     * @cost O(1) plus one 4x4 inverse.
     */
    virtual std::optional<CameraClip> clipForViewport(graphics::Camera3D& cam, float viewW, float viewH) const = 0;
};

}  // namespace eve::scene
