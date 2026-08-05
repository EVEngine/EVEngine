#include "graphics/RenderSystem3D.h"
#include "graphics/Graphics.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace eve::graphics {

namespace {

glm::vec3 gLightDir = glm::normalize(glm::vec3(0.4f, 1.f, 0.3f));
glm::vec3 gLightColor = glm::vec3(1.f);

glm::mat4 modelFromTransform(const Renderable3D::Transform3D &t) {
    glm::mat4 m(1.f);
    m = glm::translate(m, glm::vec3(t.x, t.y, t.z));
    m = glm::rotate(m, t.yaw, glm::vec3(0.f, 1.f, 0.f));
    m = glm::rotate(m, t.pitch, glm::vec3(1.f, 0.f, 0.f));
    m = glm::rotate(m, t.roll, glm::vec3(0.f, 0.f, 1.f));
    m = glm::scale(m, glm::vec3(t.sx, t.sy, t.sz));
    return m;
}

Camera3D *findDefaultCamera3D() {
    if (ecs::ComponentManager<Camera3D>::inst().registy == nullptr) return nullptr;
    auto camView = ecs::View<Camera3D, Camera3D::Data>();
    for (auto it = camView.begin(); it != camView.end(); ++it) {
        auto [data] = *it;
        if (!data->active || !data->entity) continue;
        return data->entity;
    }
    return nullptr;
}

} // namespace

void RenderSystem3D::setDirectionalLight(float dx, float dy, float dz, float r, float g, float b) {
    glm::vec3 d(dx, dy, dz);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    gLightDir = glm::normalize(d);
    gLightColor = glm::vec3(r, g, b);
}

void RenderSystem3D::render(Graphics &gfx) {
    Camera3D *defaultCam = findDefaultCamera3D();

    gfx.setMesh3DLight(gLightDir, gLightColor);
    gfx.begin3DFrame();

    if (ecs::ComponentManager<Renderable3D>::inst().registy == nullptr) return;

    const float aspect =
        (gfx.getHeight() > 0) ? float(gfx.getWidth()) / float(gfx.getHeight()) : 1.f;

    auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, mr] = *it;
        if (!mr->visible || !mr->mesh) continue;

        Camera3D *camEnt = mr->camera ? mr->camera : defaultCam;
        if (!camEnt) continue;
        auto cd = camEnt->data();

        // Right-handed view, Y-up; Vulkan depth via perspectiveRH_ZO.
        const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
        const glm::vec3 target(cd->targetX, cd->targetY, cd->targetZ);
        const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
        const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
        const float fovRad = cd->fovYDeg * 0.017453292519943295f;
        const glm::mat4 projM = glm::perspectiveRH_ZO(fovRad, aspect, cd->nearZ, cd->farZ);
        gfx.setMesh3DViewProj(projM * viewM);

        const glm::mat4 model = modelFromTransform(*xf);
        const Color tint(mr->r, mr->g, mr->b, mr->a);
        gfx.drawMesh(mr->mesh, model, mr->texture, tint);
    }
}

} // namespace eve::graphics
