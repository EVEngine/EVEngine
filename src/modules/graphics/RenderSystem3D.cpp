#include "graphics/RenderSystem3D.h"
#include "common/RenderTrace.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/ClusteredLight.h"
#include "graphics/Shadow.h"
#include "graphics/RenderControl.h"
#include "graphics/Material.h"
#include "graphics/AmbientOcclusion.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace eve::graphics {

namespace {

std::vector<RenderSystem3D::GBufferExtraDrawer> g_gbufferDrawers;
std::vector<RenderSystem3D::ShadowExtraDrawer> g_shadowDrawers;

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

/** mesh3d.frag always shades lights[0] as the primary directional. Keep a real
 *  directional in slot 0 whenever one exists (points stay in the remaining slots). */
void promoteDirectional(std::vector<PackedLight3D> &packed) {
    for (size_t i = 0; i < packed.size(); ++i) {
        if (packed[i].isPoint) continue;
        if (i != 0) std::swap(packed[0], packed[i]);
        return;
    }
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

void Camera3D::screenToRay(float screenX, float screenY, float viewW, float viewH) {
    auto d = data();
    d->screenRayOx = d->eyeX;
    d->screenRayOy = d->eyeY;
    d->screenRayOz = d->eyeZ;
    d->screenRayDx = 0.f;
    d->screenRayDy = 0.f;
    d->screenRayDz = -1.f;
    if (viewW <= 0.f || viewH <= 0.f) return;

    const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
    const glm::vec3 target(d->targetX, d->targetY, d->targetZ);
    const glm::vec3 up(d->upX, d->upY, d->upZ);
    const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
    const float aspect = viewW / viewH;
    const float fovRad = d->fovYDeg * 0.017453292519943295f;
    const glm::mat4 projM = perspectiveVulkanRH_ZO(fovRad, aspect, d->nearZ, d->farZ);
    const glm::mat4 invVP = glm::inverse(projM * viewM);

    // Screen pixel → Vulkan NDC (Y-down; matches perspectiveVulkanRH_ZO).
    const float ndcX = (screenX / viewW) * 2.f - 1.f;
    const float ndcY = (screenY / viewH) * 2.f - 1.f;
    auto unproject = [&](float ndcZ) -> glm::vec3 {
        glm::vec4 w = invVP * glm::vec4(ndcX, ndcY, ndcZ, 1.f);
        if (std::fabs(w.w) < 1e-8f) return eye;
        w /= w.w;
        return glm::vec3(w);
    };
    // ZO depth: near = 0, far = 1.
    const glm::vec3 nearPt = unproject(0.f);
    const glm::vec3 farPt  = unproject(1.f);
    glm::vec3 dir = farPt - nearPt;
    const float len = glm::length(dir);
    if (len > 1e-8f) dir /= len;
    else dir = glm::normalize(target - eye);

    d->screenRayOx = eye.x;
    d->screenRayOy = eye.y;
    d->screenRayOz = eye.z;
    d->screenRayDx = dir.x;
    d->screenRayDy = dir.y;
    d->screenRayDz = dir.z;
}

float Camera3D::getScreenRayOriginX() { return data()->screenRayOx; }
float Camera3D::getScreenRayOriginY() { return data()->screenRayOy; }
float Camera3D::getScreenRayOriginZ() { return data()->screenRayOz; }
float Camera3D::getScreenRayDirX() { return data()->screenRayDx; }
float Camera3D::getScreenRayDirY() { return data()->screenRayDy; }
float Camera3D::getScreenRayDirZ() { return data()->screenRayDz; }

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

void Renderable3D::setHeightTexture(Texture *texture) { meshRenderer()->heightTexture = texture; }

void Renderable3D::setShader(Shader *shader) { meshRenderer()->shader = shader; }

void Renderable3D::setMaterial(Material *material) { meshRenderer()->material = material; }

Material *Renderable3D::getMaterial() { return meshRenderer()->material; }

void Renderable3D::setXRayShader(Shader *shader) { meshRenderer()->xrayShader = shader; }

Shader *Renderable3D::getXRayShader() { return meshRenderer()->xrayShader; }

void Renderable3D::setXRayHighlight(bool on) { meshRenderer()->xrayHighlight = on; }

bool Renderable3D::getXRayHighlight() { return meshRenderer()->xrayHighlight; }

void Renderable3D::setPart(int index, const std::string &name, Mesh *mesh, Material *material) {
    auto mr = meshRenderer();
    if (index < 0 || index >= MeshRenderer::kMaxParts) return;
    mr->parts[index].name = name;
    mr->parts[index].mesh = mesh;
    mr->parts[index].material = material;
    if (mesh) {
        if (mr->partCount < index + 1) mr->partCount = index + 1;
    } else if (index + 1 == mr->partCount) {
        while (mr->partCount > 0 && !mr->parts[mr->partCount - 1].mesh) --mr->partCount;
    }
}

void Renderable3D::clearParts() {
    auto mr = meshRenderer();
    mr->partCount = 0;
    for (int i = 0; i < MeshRenderer::kMaxParts; ++i) {
        mr->parts[i] = ModelPart{};
    }
}

int Renderable3D::getPartCount() { return meshRenderer()->partCount; }

std::string Renderable3D::getPartName(int index) {
    auto mr = meshRenderer();
    if (index < 0 || index >= mr->partCount) return {};
    return mr->parts[index].name;
}

Mesh *Renderable3D::getPartMesh(int index) {
    auto mr = meshRenderer();
    if (index < 0 || index >= mr->partCount) return nullptr;
    return mr->parts[index].mesh;
}

Material *Renderable3D::getPartMaterial(int index) {
    auto mr = meshRenderer();
    if (index < 0 || index >= mr->partCount) return nullptr;
    return mr->parts[index].material;
}

void Renderable3D::setHair(bool hair) { meshRenderer()->isHair = hair; }

bool Renderable3D::getHair() { return meshRenderer()->isHair; }

void Renderable3D::setTint(float r, float g, float b, float a) {
    auto mr = meshRenderer();
    mr->r = r;
    mr->g = g;
    mr->b = b;
    mr->a = a;
}

void Renderable3D::setMetallic(float metallic) { meshRenderer()->metallic = metallic; }

void Renderable3D::setRoughness(float roughness) { meshRenderer()->roughness = roughness; }

void Renderable3D::setTexCellBomb(float cellScale, float strength, float rotAmount) {
    auto mr = meshRenderer();
    mr->texBombScale = cellScale > 1e-3f ? cellScale : 1e-3f;
    mr->texBombStrength = strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength);
    mr->texBombRot = rotAmount < 0.f ? 0.f : (rotAmount > 1.f ? 1.f : rotAmount);
}

float Renderable3D::getTexCellBombScale() { return meshRenderer()->texBombScale; }

float Renderable3D::getTexCellBombStrength() { return meshRenderer()->texBombStrength; }

float Renderable3D::getTexCellBombRotation() { return meshRenderer()->texBombRot; }

void Renderable3D::setParallax(float scale, float minLayers, float maxLayers) {
    auto mr = meshRenderer();
    mr->parallaxScale = scale < 0.f ? 0.f : (scale > 0.25f ? 0.25f : scale);
    float minL = minLayers < 1.f ? 1.f : minLayers;
    float maxL = maxLayers < minL ? minL : maxLayers;
    if (maxL > 64.f) maxL = 64.f;
    mr->parallaxMinLayers = minL;
    mr->parallaxMaxLayers = maxL;
}

float Renderable3D::getParallaxScale() { return meshRenderer()->parallaxScale; }

float Renderable3D::getParallaxMinLayers() { return meshRenderer()->parallaxMinLayers; }

float Renderable3D::getParallaxMaxLayers() { return meshRenderer()->parallaxMaxLayers; }

void Renderable3D::setVisible(bool visible) { meshRenderer()->visible = visible; }

void Renderable3D::setReceiveLight(bool receive) { meshRenderer()->receiveLight = receive; }

void Renderable3D::setCastShadow(bool cast) { meshRenderer()->castShadow = cast; }

void Renderable3D::setReceiveShadow(bool receive) { meshRenderer()->receiveShadow = receive; }

void Renderable3D::setCastOcclusion(bool cast) { meshRenderer()->castOcclusion = cast; }

bool Renderable3D::getCastOcclusion() { return meshRenderer()->castOcclusion; }

void Renderable3D::setCamera(Camera3D *camera) { meshRenderer()->camera = camera; }

void Renderable3D::setMeshLod(int index, Mesh *mesh, float switchDistance) {
    auto mr = meshRenderer();
    if (index < 0 || index >= MeshRenderer::kMaxLodLevels) return;
    mr->lodMeshes[index] = mesh;
    if (index > 0) mr->lodDistances[index - 1] = switchDistance;
    if (mesh) {
        if (mr->lodCount < index + 1) mr->lodCount = index + 1;
    } else if (index + 1 == mr->lodCount) {
        while (mr->lodCount > 0 && !mr->lodMeshes[mr->lodCount - 1]) --mr->lodCount;
    }
    // Keep primary mesh in sync with LOD0 when set.
    if (index == 0 && mesh) mr->mesh = mesh;
}

void Renderable3D::clearMeshLod() {
    auto mr = meshRenderer();
    mr->lodCount = 0;
    for (int i = 0; i < MeshRenderer::kMaxLodLevels; ++i) mr->lodMeshes[i] = nullptr;
}

int Renderable3D::getMeshLodCount() { return meshRenderer()->lodCount; }

int Renderable3D::getMeshLodLevelAtDistance(float distance) {
    return meshRenderer()->lodLevelForDistance(distance);
}

void RenderSystem3D::setDirectionalLight(float dx, float dy, float dz, float r, float g, float b) {
    glm::vec3 d(dx, dy, dz);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    gLightDir = glm::normalize(d);
    gLightColor = glm::vec3(r, g, b);
}

void RenderSystem3D::addGBufferExtraDrawer(GBufferExtraDrawer drawer) {
    if (!drawer) return;
    g_gbufferDrawers.push_back(std::move(drawer));
}

void RenderSystem3D::addShadowExtraDrawer(ShadowExtraDrawer drawer) {
    if (!drawer) return;
    g_shadowDrawers.push_back(std::move(drawer));
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
    eve::debug::RenderPassScope pass3d("RenderSystem3D");
    Camera3D *defaultCam = findDefaultCamera3D();

    RenderControl *rc = gfx.getRenderControl();
    rc->ensureCompiled();
    const bool doShadow = rc->hasPass("shadow");
    const bool doGBuffer = rc->hasPass("gbuffer");
    const bool doForward = rc->hasPass("forward");
    const bool doHair = rc->hasPass("hair");
    const bool allowClustered = rc->isEnabled("clustered");

    std::vector<PackedLight3D> packed;
    collectLights3D(packed, size_t(ClusteredLightConfig::kMaxLights));
    promoteDirectional(packed);
    Light3D::Data *shadowCaster = doShadow ? findShadowCasterDir(packed) : nullptr;
    prioritizeShadowCaster(packed, shadowCaster);
    const bool haveExtraShadowCasters = doShadow && !g_shadowDrawers.empty();

    const float aspect =
        (gfx.getHeight() > 0) ? float(gfx.getWidth()) / float(gfx.getHeight()) : 1.f;

    ShadowUpload shadowUpload{};
    shadowUpload.active = false;
    if ((shadowCaster || haveExtraShadowCasters) && defaultCam) {
        auto cd = defaultCam->data();
        glm::vec3 dir = shadowCaster ? glm::vec3(shadowCaster->dx, shadowCaster->dy, shadowCaster->dz)
                                     : gLightDir;
        if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.f, 1.f, 0.f);
        else dir = glm::normalize(dir);
        const float shadowBias = shadowCaster ? shadowCaster->shadowBias : 0.003f;
        const float shadowStrength = shadowCaster ? shadowCaster->shadowStrength : 1.f;
        const float fovRad = cd->fovYDeg * 0.017453292519943295f;
        shadowUpload =
            buildDirectionalCSM(dir, glm::vec3(cd->eyeX, cd->eyeY, cd->eyeZ),
                                glm::vec3(cd->targetX, cd->targetY, cd->targetZ),
                                glm::vec3(cd->upX, cd->upY, cd->upZ), fovRad, aspect, cd->nearZ,
                                cd->farZ, shadowBias, shadowStrength);

        if (ecs::current()->getManager<Renderable3D>() != nullptr || haveExtraShadowCasters) {
            auto casterView =
                ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
            for (int c = 0; c < ShadowConfig::kCascades; ++c) {
                eve::debug::rtPassBegin("ShadowPass");
                gfx.beginShadowPass(c);
                for (auto it = casterView.begin(); it != casterView.end(); ++it) {
                    auto [xf, mr] = *it;
                    if (!mr->visible || !mr->effectiveCastShadow()) continue;
                    const glm::mat4 model = modelFromTransform(*xf);
                    auto drawShadowMesh = [&](Mesh *drawMesh) {
                        if (!drawMesh) return;
                        eve::debug::rtBind("mesh", "shadowCaster");
                        eve::debug::rtDraw("drawMeshShadow", "cascade");
                        gfx.drawMeshShadow(drawMesh, shadowUpload.ubo.lightVP[c] * model);
                    };
                    if (mr->usesParts()) {
                        for (int p = 0; p < mr->partCount; ++p) {
                            Material *mat = mr->parts[p].material ? mr->parts[p].material : mr->material;
                            if (mat && !mat->getCastShadow()) continue;
                            drawShadowMesh(mr->parts[p].mesh);
                        }
                } else {
                    Mesh *drawMesh = mr->mesh;
                    if (defaultCam) {
                            auto cdd = defaultCam->data();
                            const float dx = xf->x - cdd->eyeX;
                            const float dy = xf->y - cdd->eyeY;
                            const float dz = xf->z - cdd->eyeZ;
                            drawMesh = mr->meshForDistance(std::sqrt(dx * dx + dy * dy + dz * dz));
                        }
                        drawShadowMesh(drawMesh);
                    }
                }
                // Extra shadow casters (billboard/card geometry not in the ECS).
                for (const auto &drawer : g_shadowDrawers)
                    drawer(gfx, shadowUpload.ubo.lightVP[c], *cd);
                gfx.endShadowPass();
                eve::debug::rtPassEnd("ShadowPass");
            }
        }
    }
    gfx.setMesh3DShadows(shadowUpload);

    const bool useClustered =
        allowClustered && packed.size() > size_t(Lighting3DPack::kMaxLights);
    if (!useClustered) {
        ClusteredLightingUpload off{};
        off.active = false;
        gfx.setMesh3DClusteredLighting(off);
        const Camera3D::Data *ambientCam = defaultCam ? defaultCam->data().operator->() : nullptr;
        gfx.setMesh3DLighting(packLights3D(packed, ambientCam));
    }

    // G-buffer fill (sampleable depth/normal) — before the forward swapchain pass.
    if (doGBuffer && defaultCam &&
        (ecs::current()->getManager<Renderable3D>() != nullptr || !g_gbufferDrawers.empty())) {
        eve::debug::rtPassBegin("GBufferPass");
        auto cd = defaultCam->data();
        const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
        const glm::vec3 target(cd->targetX, cd->targetY, cd->targetZ);
        const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
        const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
        const float fovRad = cd->fovYDeg * 0.017453292519943295f;
        const glm::mat4 projM = perspectiveVulkanRH_ZO(fovRad, aspect, cd->nearZ, cd->farZ);
        const glm::mat4 viewProj = projM * viewM;
        const int gw = std::max(1, gfx.getPixelWidth() > 0 ? gfx.getPixelWidth() : gfx.getWidth());
        const int gh = std::max(1, gfx.getPixelHeight() > 0 ? gfx.getPixelHeight() : gfx.getHeight());
        gfx.beginGBufferPass(gw, gh);
        auto gbView =
            ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
        for (auto it = gbView.begin(); it != gbView.end(); ++it) {
            auto [xf, mr] = *it;
            if (!mr->visible) continue;
            // X-ray targets are skipped so their pixels record the occluder depth
            // behind them; the X-ray shader samples that to detect occlusion.
            if (mr->xrayHighlight) continue;
            const glm::mat4 model = modelFromTransform(*xf);
            const glm::mat4 mvp = viewProj * model;
            auto emit = [&](Mesh *drawMesh, Material *mat, Texture *albedo, float tr, float tg,
                            float tb) {
                if (!drawMesh) return;
                if (mat && mat->isTransparentHair()) return;
                if (!mat && mr->isHair) return;
                eve::debug::rtDraw("drawMeshGBuffer", "gbuffer");
                gfx.drawMeshGBuffer(drawMesh, mvp, model, cd->nearZ, cd->farZ, albedo, tr, tg, tb);
            };
            if (mr->usesParts()) {
                for (int p = 0; p < mr->partCount; ++p) {
                    Material *mat = mr->parts[p].material ? mr->parts[p].material : mr->material;
                    Texture *alb = mat ? mat->getAlbedoTexture() : mr->texture;
                    float tr = mat ? mat->getTintR() : mr->r;
                    float tg = mat ? mat->getTintG() : mr->g;
                    float tb = mat ? mat->getTintB() : mr->b;
                    emit(mr->parts[p].mesh, mat, alb, tr, tg, tb);
                }
            } else {
                if (mr->effectiveHair()) continue;
                const float dx = xf->x - eye.x;
                const float dy = xf->y - eye.y;
                const float dz = xf->z - eye.z;
                Texture *alb = mr->material ? mr->material->getAlbedoTexture() : mr->texture;
                float tr = mr->material ? mr->material->getTintR() : mr->r;
                float tg = mr->material ? mr->material->getTintG() : mr->g;
                float tb = mr->material ? mr->material->getTintB() : mr->b;
                emit(mr->meshForDistance(std::sqrt(dx * dx + dy * dy + dz * dz)), mr->material, alb, tr,
                     tg, tb);
            }
        }
        // Extra G-buffer contributors (billboard/card geometry not in the ECS).
        for (const auto &drawer : g_gbufferDrawers) drawer(gfx, *cd, viewProj, aspect);
        gfx.endGBufferPass();
        eve::debug::rtPassEnd("GBufferPass");
    } else if (!doGBuffer) {
        rc->getGBuffer()->clear();
    }

    if (!doForward && !doHair) return;

    gfx.begin3DFrame();
    if (!gfx.had3DThisFrame())
        return;

    if (ecs::current()->getManager<Renderable3D>() == nullptr) return;

    struct DrawItem {
        Renderable3D::Transform3D *xf;
        Renderable3D::MeshRenderer *mr;
        Mesh *mesh;
        Material *material;
        float distSq;
    };
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> hair;
    opaque.reserve(64);
    hair.reserve(16);

    auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, mr] = *it;
        if (!mr->visible) continue;
        Camera3D *camEnt = mr->camera ? mr->camera : defaultCam;
        if (!camEnt) continue;
        auto cd = camEnt->data();
        const float dx = xf->x - cd->eyeX;
        const float dy = xf->y - cd->eyeY;
        const float dz = xf->z - cd->eyeZ;
        const float distSq = dx * dx + dy * dy + dz * dz;
        const float dist = std::sqrt(distSq);

        auto pushItem = [&](Mesh *mesh, Material *mat, bool asHair) {
            if (!mesh) return;
            DrawItem item{xf, mr, mesh, mat, distSq};
            if (asHair)
                hair.push_back(item);
            else
                opaque.push_back(item);
        };

        if (mr->usesParts()) {
            for (int p = 0; p < mr->partCount; ++p) {
                Material *mat = mr->parts[p].material ? mr->parts[p].material : mr->material;
                const bool asHair = mat ? mat->isTransparentHair() : mr->isHair;
                pushItem(mr->parts[p].mesh, mat, asHair);
            }
        } else {
            Mesh *drawMesh = mr->meshForDistance(dist);
            Material *mat = mr->material;
            pushItem(drawMesh, mat, mr->effectiveHair());
        }
    }

    std::stable_sort(hair.begin(), hair.end(),
                     [](const DrawItem &a, const DrawItem &b) { return a.distSq > b.distSq; });

    auto bindLegacyMaterial = [&](Renderable3D::MeshRenderer *mr) {
        gfx.setMesh3DMaterial(mr->metallic, mr->roughness);
        gfx.setMesh3DTexCellBomb(mr->texBombScale, mr->texBombStrength, mr->texBombRot);
        gfx.setMesh3DNormalTexture(mr->normalTexture);
        gfx.setMesh3DHeightTexture(mr->heightTexture);
        gfx.setMesh3DParallax(mr->parallaxScale, mr->parallaxMinLayers, mr->parallaxMaxLayers);
        gfx.setMesh3DShadowReceive(mr->receiveShadow);
    };

    auto applyLighting = [&](Renderable3D::MeshRenderer *mr, Material *mat, Camera3D::Data *cd,
                             const glm::mat4 &viewM, float fovRad) {
        const bool receiveLight = mat ? mat->getReceiveLight() : mr->receiveLight;
        Shader *shader = mat ? mat->effectiveShader() : mr->shader;
        if (!receiveLight) {
            ClusteredLightingUpload off{};
            off.active = false;
            gfx.setMesh3DClusteredLighting(off);
            Lighting3DPack none{};
            none.count = 0;
            none.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
            gfx.setMesh3DLighting(none);
        } else if (useClustered && !shader) {
            std::vector<ClusteredLightGpu> points, dirs;
            splitLights(packed, points, dirs);
            if (dirs.empty()) {
                ClusteredLightGpu d{};
                d.posRadius = glm::vec4(gLightDir, 0.f);
                d.color = glm::vec4(gLightColor, 1.f);
                dirs.push_back(d);
            }
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
            gfx.setMesh3DLighting(packLights3D(packed, cd));
        } else {
            ClusteredLightingUpload off{};
            off.active = false;
            gfx.setMesh3DClusteredLighting(off);
            gfx.setMesh3DLighting(packLights3D(packed, cd));
        }
    };

    auto drawMeshWithMaterial = [&](Renderable3D::Transform3D *xf, Renderable3D::MeshRenderer *mr,
                                    Mesh *drawMesh, Material *mat) {
        if (!drawMesh) return;
        Camera3D *camEnt = mr->camera ? mr->camera : defaultCam;
        if (!camEnt) return;
        auto cd = camEnt->data();

        const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
        const glm::vec3 target(cd->targetX, cd->targetY, cd->targetZ);
        const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
        const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
        const float fovRad = cd->fovYDeg * 0.017453292519943295f;
        const glm::mat4 projM = perspectiveVulkanRH_ZO(fovRad, aspect, cd->nearZ, cd->farZ);
        gfx.setMesh3DViewProj(projM * viewM);
        gfx.setMesh3DView(viewM);
        gfx.setMesh3DClip(cd->nearZ, cd->farZ);
        gfx.setMesh3DCameraPos(eye);
        gfx.setMesh3DEnv(cd->envMap, cd->envIntensity);

        Texture *albedo = mr->texture;
        Color tint(mr->r, mr->g, mr->b, mr->a);
        Shader *shader = mr->shader;
        if (mat) {
            mat->bind(gfx);
            albedo = mat->getAlbedoTexture();
            tint = Color(mat->getTintR(), mat->getTintG(), mat->getTintB(), mat->getTintA());
            shader = mat->effectiveShader();
            if (mat->getReceiveLight() && mat->getShadingModel() != "unlit") {
                applyLighting(mr, mat, cd.operator->(), viewM, fovRad);
            }
        } else {
            bindLegacyMaterial(mr);
            applyLighting(mr, nullptr, cd.operator->(), viewM, fovRad);
        }

        const glm::mat4 model = modelFromTransform(*xf);
        if (albedo) eve::debug::rtBind("texture", "albedo");
        if (shader)
            eve::debug::rtBind("shader", (mat && mat->isTransparentHair()) || mr->isHair ? "hair"
                                                                                         : "mesh");
        eve::debug::rtBind("mesh", "renderable3d");
        eve::debug::rtDraw("drawMeshShader", shader ? "custom" : "default");
        gfx.drawMeshShader(drawMesh, model, albedo, tint, shader);

        // X-ray second pass: paint only the occluded (behind-building) part over
        // the scene. The pipeline runs with depth test/write off + alpha blend and
        // the shader discards visible fragments by sampling the G-buffer depth.
        if (mr->xrayHighlight && mr->xrayShader) {
            float sw = gfx.getPixelWidth() > 0 ? float(gfx.getPixelWidth()) : float(gfx.getWidth());
            float shh =
                gfx.getPixelHeight() > 0 ? float(gfx.getPixelHeight()) : float(gfx.getHeight());
            if (mr->xrayShader->hasUniform("screenW")) mr->xrayShader->sendFloat("screenW", sw);
            if (mr->xrayShader->hasUniform("screenH")) mr->xrayShader->sendFloat("screenH", shh);
            eve::debug::rtBind("shader", "xray");
            eve::debug::rtDraw("drawMeshShader", "xray");
            gfx.drawMeshShader(drawMesh, model, albedo, tint, mr->xrayShader);
        }
    };

    // Provide the G-buffer depth to X-ray shaders for the occlusion test.
    if (doGBuffer && rc->getGBuffer() && rc->getGBuffer()->isValid()) {
        if (Texture *depth = rc->getGBuffer()->getHwDepthTexture()) gfx.setMesh3DSceneDepth(depth);
    }

    if (doForward) {
        for (const auto &item : opaque) drawMeshWithMaterial(item.xf, item.mr, item.mesh, item.material);
    }
    if (doHair) {
        for (const auto &item : hair) drawMeshWithMaterial(item.xf, item.mr, item.mesh, item.material);
    }

    const bool doAO = rc->isEnabled("ao");
    // WebGPU has no SPIR-V AO shaders; the X-ray path (below) still reads the
    // G-buffer depth directly.
    if (doAO && gfx.supportsGBufferPost() && defaultCam && gfx.had3DThisFrame()) {
        GBuffer *gb = rc->getGBuffer();
        if (gb && gb->isValid()) {
            auto cd = defaultCam->data();
            const float aspectSafe = aspect > 1e-4f ? aspect : 1.f;
            auto bindCam = [&](auto *fx) {
                fx->setCamera(cd->eyeX, cd->eyeY, cd->eyeZ, cd->targetX, cd->targetY, cd->targetZ,
                              cd->upX, cd->upY, cd->upZ, cd->fovYDeg, aspectSafe, cd->nearZ, cd->farZ);
            };
            AmbientOcclusion *ao = gfx.pipelineAmbientOcclusion();
            ao->setQuality("medium");
            ao->setIntensity(0.16f);
            ao->setPower(1.1f);
            ao->setRadius(std::clamp(cd->farZ * 0.006f, 0.18f, 0.35f));
            bindCam(ao);
            if (Texture *depth = gb->getHwDepthTexture())
                ao->applyFromGBuffer(&gfx, depth, gb->getNormalTexture());
            // Fullscreen SSGI from lit scene color reprints nearby props
            // (curtains, planters) onto the floor as multiple swimming ghosts.
            // Mesh shaders still add hemispheric sky/ground + wrap fill.
        }
    }

    const bool doOutline = rc->isEnabled("outline");
    if (doOutline && defaultCam && gfx.had3DThisFrame()) {
        GBuffer *gb = rc->getGBuffer();
        if (gb && gb->isValid()) {
            Outline *outline = gfx.pipelineOutline();
            auto cd = defaultCam->data();
            outline->setClip(cd->nearZ, cd->farZ);
            if (Texture *depth = gb->getHwDepthTexture())
                outline->apply(&gfx, depth, gb->getNormalTexture());
        }
    }
}

void RenderSystem3D::renderToCanvas(Graphics &gfx, Canvas *target, Camera3D *camera) {
    if (!target || !camera) return;
    auto cd = camera->data();
    const float aspect = target->getWidth() > 0
                             ? float(target->getWidth()) / float(target->getHeight())
                             : 1.f;

    // Preview-quality forward pass: no shadow / G-buffer / AO passes.
    gfx.begin3DFrameToCanvas(target);

    const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
    const glm::vec3 look(cd->targetX, cd->targetY, cd->targetZ);
    const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
    const glm::mat4 viewM = glm::lookAtRH(eye, look, up);
    const float fovRad = cd->fovYDeg * 0.017453292519943295f;
    const glm::mat4 projM = perspectiveVulkanRH_ZO(fovRad, aspect, cd->nearZ, cd->farZ);
    gfx.setMesh3DViewProj(projM * viewM);
    gfx.setMesh3DView(viewM);
    gfx.setMesh3DClip(cd->nearZ, cd->farZ);
    gfx.setMesh3DCameraPos(eye);
    gfx.setMesh3DEnv(cd->envMap, cd->envIntensity);

    // Lighting: real lights (if any) + camera ambient; shadows/clustered off.
    std::vector<PackedLight3D> packed;
    collectLights3D(packed, size_t(ClusteredLightConfig::kMaxLights));
    promoteDirectional(packed);
    ClusteredLightingUpload noClustered{};
    noClustered.active = false;
    gfx.setMesh3DClusteredLighting(noClustered);
    gfx.setMesh3DLighting(packLights3D(packed, cd.operator->()));
    ShadowUpload noShadow{};
    noShadow.active = false;
    gfx.setMesh3DShadows(noShadow);

    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [xf, mr] = *it;
            if (!mr->visible) continue;
            // Legacy per-entity material path (no parts / materials / hair).
            gfx.setMesh3DMaterial(mr->metallic, mr->roughness);
            gfx.setMesh3DTexCellBomb(mr->texBombScale, mr->texBombStrength, mr->texBombRot);
            gfx.setMesh3DNormalTexture(mr->normalTexture);
            gfx.setMesh3DHeightTexture(mr->heightTexture);
            gfx.setMesh3DParallax(mr->parallaxScale, mr->parallaxMinLayers, mr->parallaxMaxLayers);
            gfx.setMesh3DShadowReceive(false);
            Texture *albedo = mr->texture;
            Color tint(mr->r, mr->g, mr->b, mr->a);
            Shader *shader = mr->shader;
            const glm::mat4 model = modelFromTransform(*xf);
            eve::debug::rtDraw("drawMeshShader", shader ? "custom" : "default");
            gfx.drawMeshShader(mr->mesh, model, albedo, tint, shader);
        }
    }

    gfx.end3DFrameToCanvas();
}

} // namespace eve::graphics
