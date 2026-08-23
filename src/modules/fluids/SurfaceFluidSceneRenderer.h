#pragma once

#include "fluids/SurfaceFluidRenderData.h"

#include <glm/glm.hpp>

namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics

namespace eve::fluids {

/**
 * @brief Draws surface-droplet cap instances through the engine 3D backend.
 *
 * The renderer is backend-neutral at its public boundary and uses Graphics'
 * sphere mesh + PBR path. On Vulkan this writes regular scene depth and receives
 * environment lighting; WebGPU consumes the same transforms as a fallback.
 */
class SurfaceFluidSceneRenderer {
public:
    /** @param graphics initialized graphics backend; it must outlive this object. */
    explicit SurfaceFluidSceneRenderer(graphics::Graphics* graphics);

    /**
     * @brief Draw all attached droplet caps into an already-open 3D frame.
     * @param renderData oriented cap instances generated for the current pose.
     * @param tint linear water tint; alpha is forwarded to the mesh material.
     */
    void draw(const SurfaceFluidRenderData& renderData,
              const glm::vec4& tint = glm::vec4(0.72f, 0.88f, 0.96f, 0.78f));

    /** @return number of caps submitted by the most recent draw call. */
    int getDrawCount() const { return drawCount_; }

    /** @brief Build a unit-sphere model matrix whose lower pole touches the surface. */
    static glm::mat4 modelMatrix(const SurfaceDropletRenderInstance& instance);

private:
    graphics::Graphics* graphics_ = nullptr;
    graphics::Mesh*     sphere_ = nullptr;
    int                 drawCount_ = 0;
};

}  // namespace eve::fluids
