#include "graphics/RenderSystem3D.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/ClusteredLight.h"
#include "graphics/Shadow.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

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
    if (ecs::current()->getManager<Camera3D>() == nullptr) return nullptr;
    auto camView = ecs::View<Camera3D, Camera3D::Data>();
    for (auto it = camView.begin(); it != camView.end(); ++it) {
        auto [data] = *it;
        if (!data->active || !data->entity) continue;
        return data->entity;
    }
    return nullptr;
}

struct PackedLight3D {
    Light3D::Data *data = nullptr;
    bool isPoint = true;
};

void collectLights3D(std::vector<PackedLight3D> &out, size_t maxCount) {
    out.clear();
    if (ecs::current()->getManager<Light3D>() == nullptr) return;
    auto view = ecs::View<Light3D, Light3D::Data>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [d] = *it;
        if (!d->enabled) continue;
        PackedLight3D pl;
        pl.data = d;
        pl.isPoint = (d->type != "dir");
        out.push_back(pl);
    }
    std::stable_sort(out.begin(), out.end(), [](const PackedLight3D &a, const PackedLight3D &b) {
        if (a.isPoint != b.isPoint) return a.isPoint && !b.isPoint;
        return a.data->intensity > b.data->intensity;
    });
    if (out.size() > maxCount) out.resize(maxCount);
}

Lighting3DPack packLights3D(const std::vector<PackedLight3D> &lights, const Camera3D::Data *cam) {
    Lighting3DPack pack{};
    if (cam) {
        pack.ambient = glm::vec4(cam->ambientR, cam->ambientG, cam->ambientB, 0.f);
    }
    if (lights.empty()) {
        pack.count = 1;
        pack.lights[0].posRadius = glm::vec4(gLightDir, 0.f);
        pack.lights[0].color = glm::vec4(gLightColor, 1.f);
        return pack;
    }
    const int n = std::min(int(lights.size()), Lighting3DPack::kMaxLights);
    pack.count = n;
    for (int i = 0; i < n; ++i) {
        const auto *d = lights[size_t(i)].data;
        Light3DGpu &g = pack.lights[i];
        g.color = glm::vec4(d->r * d->intensity, d->g * d->intensity, d->b * d->intensity, 1.f);
        if (lights[size_t(i)].isPoint) {
            g.posRadius = glm::vec4(d->x, d->y, d->z, d->radius);
        } else {
            glm::vec3 dir(d->dx, d->dy, d->dz);
            if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.f, 1.f, 0.f);
            else dir = glm::normalize(dir);
            g.posRadius = glm::vec4(dir, 0.f);
        }
    }
    return pack;
}

void splitLights(const std::vector<PackedLight3D> &packed, std::vector<ClusteredLightGpu> &points,
                 std::vector<ClusteredLightGpu> &dirs) {
    points.clear();
    dirs.clear();
    for (const auto &pl : packed) {
        const auto *d = pl.data;
        ClusteredLightGpu g{};
        g.color = glm::vec4(d->r * d->intensity, d->g * d->intensity, d->b * d->intensity, 1.f);
        if (pl.isPoint) {
            g.posRadius = glm::vec4(d->x, d->y, d->z, d->radius);
            points.push_back(g);
        } else {
            glm::vec3 dir(d->dx, d->dy, d->dz);
            if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.f, 1.f, 0.f);
            else dir = glm::normalize(dir);
            g.posRadius = glm::vec4(dir, 0.f);
            dirs.push_back(g);
        }
    }
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

void Camera3D::setAmbient(float r, float g, float b) {
    auto d = data();
    d->ambientR = r;
    d->ambientG = g;
    d->ambientB = b;
}

void Camera3D::setEnvMap(Texture *cube) { data()->envMap = cube; }

void Camera3D::setEnvIntensity(float intensity) {
    data()->envIntensity = intensity < 0.f ? 0.f : intensity;
}

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

void Renderable3D::setNormalTexture(Texture *texture) { meshRenderer()->normalTexture = texture; }

void Renderable3D::setShader(Shader *shader) { meshRenderer()->shader = shader; }

void Renderable3D::setTint(float r, float g, float b, float a) {
    auto mr = meshRenderer();
    mr->r = r;
    mr->g = g;
    mr->b = b;
    mr->a = a;
}

void Renderable3D::setMetallic(float metallic) { meshRenderer()->metallic = metallic; }

void Renderable3D::setRoughness(float roughness) { meshRenderer()->roughness = roughness; }

void Renderable3D::setVisible(bool visible) { meshRenderer()->visible = visible; }

void Renderable3D::setReceiveLight(bool receive) { meshRenderer()->receiveLight = receive; }

void Renderable3D::setCastShadow(bool cast) { meshRenderer()->castShadow = cast; }

void Renderable3D::setReceiveShadow(bool receive) { meshRenderer()->receiveShadow = receive; }

void Renderable3D::setCamera(Camera3D *camera) { meshRenderer()->camera = camera; }

void RenderSystem3D::setDirectionalLight(float dx, float dy, float dz, float r, float g, float b) {
    glm::vec3 d(dx, dy, dz);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    gLightDir = glm::normalize(d);
    gLightColor = glm::vec3(r, g, b);
}

namespace {

Light3D::Data *findShadowCasterDir(const std::vector<PackedLight3D> &packed) {
    Light3D::Data *best = nullptr;
    float bestI = -1.f;
    for (const auto &pl : packed) {
        if (pl.isPoint || !pl.data || !pl.data->castShadow) continue;
        if (pl.data->intensity > bestI) {
            bestI = pl.data->intensity;
            best = pl.data;
        }
    }
    return best;
}

void prioritizeShadowCaster(std::vector<PackedLight3D> &packed, Light3D::Data *caster) {
    if (!caster || packed.empty()) return;
    for (size_t i = 0; i < packed.size(); ++i) {
        if (packed[i].data != caster) continue;
        if (i != 0) std::swap(packed[0], packed[i]);
        return;
    }
}

}  // namespace

void RenderSystem3D::render(Graphics &gfx) {
    Camera3D *defaultCam = findDefaultCamera3D();

    std::vector<PackedLight3D> packed;
    collectLights3D(packed, size_t(ClusteredLightConfig::kMaxLights));
    Light3D::Data *shadowCaster = findShadowCasterDir(packed);
    prioritizeShadowCaster(packed, shadowCaster);

    const float aspect =
        (gfx.getHeight() > 0) ? float(gfx.getWidth()) / float(gfx.getHeight()) : 1.f;

    ShadowUpload shadowUpload{};
    shadowUpload.active = false;
    if (shadowCaster && defaultCam) {
        auto cd = defaultCam->data();
        glm::vec3 dir(shadowCaster->dx, shadowCaster->dy, shadowCaster->dz);
        if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.f, 1.f, 0.f);
        else dir = glm::normalize(dir);
        const float fovRad = cd->fovYDeg * 0.017453292519943295f;
        shadowUpload =
            buildDirectionalCSM(dir, glm::vec3(cd->eyeX, cd->eyeY, cd->eyeZ),
                                glm::vec3(cd->targetX, cd->targetY, cd->targetZ),
                                glm::vec3(cd->upX, cd->upY, cd->upZ), fovRad, aspect, cd->nearZ,
                                cd->farZ, shadowCaster->shadowBias, shadowCaster->shadowStrength);

        if (ecs::current()->getManager<Renderable3D>() != nullptr) {
            auto casterView =
                ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
            for (int c = 0; c < ShadowConfig::kCascades; ++c) {
                gfx.beginShadowPass(c);
                for (auto it = casterView.begin(); it != casterView.end(); ++it) {
                    auto [xf, mr] = *it;
                    if (!mr->visible || !mr->mesh || !mr->castShadow) continue;
                    const glm::mat4 model = modelFromTransform(*xf);
                    gfx.drawMeshShadow(mr->mesh, shadowUpload.ubo.lightVP[c] * model);
                }
                gfx.endShadowPass();
            }
        }
    }
    gfx.setMesh3DShadows(shadowUpload);

    const bool useClustered = packed.size() > size_t(Lighting3DPack::kMaxLights);
    if (!useClustered) {
        ClusteredLightingUpload off{};
        off.active = false;
        gfx.setMesh3DClusteredLighting(off);
        const Camera3D::Data *ambientCam = defaultCam ? defaultCam->data().operator->() : nullptr;
        gfx.setMesh3DLighting(packLights3D(packed, ambientCam));
    }

    gfx.begin3DFrame();
    if (!gfx.had3DThisFrame())
        return;

    if (ecs::current()->getManager<Renderable3D>() == nullptr) return;

    auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, mr] = *it;
        if (!mr->visible || !mr->mesh) continue;

        Camera3D *camEnt = mr->camera ? mr->camera : defaultCam;
        if (!camEnt) continue;
        auto cd = camEnt->data();

        const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
        const glm::vec3 target(cd->targetX, cd->targetY, cd->targetZ);
        const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
        const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
        const float fovRad = cd->fovYDeg * 0.017453292519943295f;
        const glm::mat4 projM = glm::perspectiveRH_ZO(fovRad, aspect, cd->nearZ, cd->farZ);
        gfx.setMesh3DViewProj(projM * viewM);
        gfx.setMesh3DCameraPos(eye);
        gfx.setMesh3DMaterial(mr->metallic, mr->roughness);
        gfx.setMesh3DNormalTexture(mr->normalTexture);
        gfx.setMesh3DEnv(cd->envMap, cd->envIntensity);
        gfx.setMesh3DShadowReceive(mr->receiveShadow);

        if (!mr->receiveLight) {
            ClusteredLightingUpload off{};
            off.active = false;
            gfx.setMesh3DClusteredLighting(off);
            Lighting3DPack none{};
            none.count = 0;
            none.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
            gfx.setMesh3DLighting(none);
        } else if (useClustered && !mr->shader) {
            std::vector<ClusteredLightGpu> points, dirs;
            splitLights(packed, points, dirs);
            if (dirs.empty()) {
                ClusteredLightGpu d{};
                d.posRadius = glm::vec4(gLightDir, 0.f);
                d.color = glm::vec4(gLightColor, 1.f);
                dirs.push_back(d);
            }
            // packed[0] is shadow caster when present — match its direction as primary.
            if (shadowCaster && !dirs.empty()) {
                glm::vec3 dir(shadowCaster->dx, shadowCaster->dy, shadowCaster->dz);
                if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.f, 1.f, 0.f);
                else dir = glm::normalize(dir);
                for (size_t i = 0; i < dirs.size(); ++i) {
                    if (glm::length(glm::vec3(dirs[i].posRadius) - dir) < 1e-3f) {
                        if (i != 0) std::swap(dirs[0], dirs[i]);
                        break;
                    }
                }
            }
            glm::vec4 ambient(cd->ambientR, cd->ambientG, cd->ambientB, 0.f);
            auto upload = buildClusteredLighting(points, dirs, viewM, cd->nearZ, cd->farZ,
                                                 gfx.getWidth(), gfx.getHeight(), fovRad, ambient);
            gfx.setMesh3DClusteredLighting(upload);
            gfx.setMesh3DLighting(packLights3D(packed, cd.operator->()));
        } else {
            ClusteredLightingUpload off{};
            off.active = false;
            gfx.setMesh3DClusteredLighting(off);
            gfx.setMesh3DLighting(packLights3D(packed, cd.operator->()));
        }

        const glm::mat4 model = modelFromTransform(*xf);
        const Color tint(mr->r, mr->g, mr->b, mr->a);
        gfx.drawMeshShader(mr->mesh, model, mr->texture, tint, mr->shader);
    }
}

} // namespace eve::graphics
