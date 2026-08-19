#pragma once

#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"

#include <glm/glm.hpp>
#include <vector>

namespace eve::graphics {

/**
 * @brief One resolved 3D draw, produced by the frame data prep jobs.
 *
 * All CPU work that used to happen inside the render loop (ECS traversal,
 * model matrix, LOD selection, distance) is done once per frame in the prep
 * phase; the render thread only consumes these items.
 */
struct RenderItem3D {
    Renderable3D::MeshRenderer *mr = nullptr;
    /** @brief Mesh to draw in the camera pass, LOD-resolved for this item's camera. */
    Mesh *meshForward = nullptr;
    /** @brief Mesh to draw in shadow / G-buffer passes, LOD-resolved for the main camera. */
    Mesh *meshShadow = nullptr;
    Material *material = nullptr;
    /** @brief Precomputed model matrix (translation/rotation/scale). */
    glm::mat4 model{1.f};
    /** @brief Squared distance to this item's camera (hair sort order). */
    float distSq = 0.f;
    /** @brief Squared distance to the main camera (shadow / G-buffer LOD). */
    float distSqMain = 0.f;
    Camera3D *camera = nullptr;
    int partIndex = -1;  // -1 = whole-entity item
    bool hair = false;
    bool shadowCastable = false;
    /** @brief Set by the prep when "frustumCull" is enabled and the item is outside the view. */
    bool culled = false;
};

/**
 * @brief Double-buffered per-frame draw list written by the prep jobs and only
 * read by the render thread. Vectors keep their capacity across frames, so the
 * steady state does not allocate per frame.
 */
struct FrameDrawList3D {
    /** @brief Clear both lists, keeping capacity for the next frame. */
    void reset() {
        opaque.clear();
        hair.clear();
        hasAny = false;
    }

    std::vector<RenderItem3D> opaque;
    std::vector<RenderItem3D> hair;
    bool hasAny = false;
};

}  // namespace eve::graphics
