#include "fluids/SurfaceFluidSceneRenderer.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"

#include <algorithm>

namespace eve::fluids {

SurfaceFluidSceneRenderer::SurfaceFluidSceneRenderer(graphics::Graphics* graphics)
    : graphics_(graphics) {}

glm::mat4 SurfaceFluidSceneRenderer::modelMatrix(
    const SurfaceDropletRenderInstance& instance) {
    glm::mat4 model(1.f);
    model[0] = glm::vec4(instance.majorAxis, 0.f);
    model[1] = glm::vec4(instance.normal * instance.capHeight, 0.f);
    model[2] = glm::vec4(instance.minorAxis, 0.f);
    model[3] = glm::vec4(instance.position + instance.normal * instance.capHeight, 1.f);
    return model;
}

void SurfaceFluidSceneRenderer::draw(const SurfaceFluidRenderData& renderData,
                                     const glm::vec4& tint) {
    drawCount_ = 0;
    if (!graphics_) return;
    if (!sphere_) sphere_ = graphics_->newMeshSphere(16, 8);
    if (!sphere_) return;

    for (const SurfaceDropletRenderInstance& instance : renderData.droplets()) {
        const WetSurfaceMaterialSample material =
            SurfaceFluidRenderData::evaluateMaterial(std::max(0.35f, instance.wetness));
        graphics_->setMesh3DMaterial(0.f, material.roughness);
        graphics_->drawMesh(sphere_, modelMatrix(instance), nullptr, tint);
        ++drawCount_;
    }
}

}  // namespace eve::fluids
