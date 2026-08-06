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

void Camera3D::setEye(float x, float y, float z) {
    auto d = data();
    d->eyeX = x;
    d->eyeY = y;
    d->eyeZ = z;
}

void Camera3D::setTarget(float x, float y, float z) {
    auto d = data();
    d->targetX = x;
    d->targetY = y;
    d->targetZ = z;
}

void Camera3D::setUp(float x, float y, float z) {
    auto d = data();
    d->upX = x;
    d->upY = y;
    d->upZ = z;
}

void Camera3D::setFov(float fovYDeg) { data()->fovYDeg = fovYDeg; }

void Camera3D::setActive(bool active) { data()->active = active; }

void Renderable3D::setPosition(float x, float y, float z) {
    auto t = transform();
    t->x = x;
    t->y = y;
    t->z = z;
}

void Renderable3D::setRotation(float yaw, float pitch, float roll) {
    auto t = transform();
    t->yaw = yaw;
    t->pitch = pitch;
    t->roll = roll;
}

void Renderable3D::setYaw(float yaw) { transform()->yaw = yaw; }

float Renderable3D::getYaw() { return transform()->yaw; }

void Renderable3D::setScale(float sx, float sy, float sz) {
    auto t = transform();
    t->sx = sx;
    t->sy = sy;
    t->sz = sz;
}

void Renderable3D::setMesh(Mesh *mesh) { meshRenderer()->mesh = mesh; }

void Renderable3D::setTexture(Texture *texture) { meshRenderer()->texture = texture; }

void Renderable3D::setShader(Shader *shader) { meshRenderer()->shader = shader; }

void Renderable3D::setTint(float r, float g, float b, float a) {
    auto mr = meshRenderer();
    mr->r = r;
    mr->g = g;
    mr->b = b;
    mr->a = a;
}

void Renderable3D::setVisible(bool visible) { meshRenderer()->visible = visible; }

void Renderable3D::setCamera(Camera3D *camera) { meshRenderer()->camera = camera; }

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
    if (!gfx.had3DThisFrame())
        return;

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
        gfx.setMesh3DCameraPos(eye);

        const glm::mat4 model = modelFromTransform(*xf);
        const Color tint(mr->r, mr->g, mr->b, mr->a);
        gfx.drawMeshShader(mr->mesh, model, mr->texture, tint, mr->shader);
    }
}

} // namespace eve::graphics
