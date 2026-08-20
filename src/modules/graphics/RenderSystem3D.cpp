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
#include "graphics/FrameDrawList.h"
#include "thread/Thread.h"

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

/**
 * @brief Six normalized view-frustum planes (Gribb–Hartmann) for sphere culling.
 * Plane convention: point is inside when dot(plane.xyz, p) + plane.w >= 0.
 */
struct FrustumPlanes {
    glm::vec4 p[6]{};

    bool sphereVisible(const glm::vec3 &center, float radius) const {
        for (const auto &pl : p) {
            const float d = pl.x * center.x + pl.y * center.y + pl.z * center.z + pl.w;
            if (d < -radius) return false;
        }
        return true;
    }
};

FrustumPlanes extractFrustum(const glm::mat4 &m) {
    FrustumPlanes f;
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    auto norm = [](glm::vec4 &v) {
        const float l = glm::length(glm::vec3(v));
        if (l > 1e-8f) v /= l;
    };
    f.p[0] = r3 + r0;  // left
    f.p[1] = r3 - r0;  // right
    f.p[2] = r3 + r1;  // bottom
    f.p[3] = r3 - r1;  // top
    // Vulkan clip space uses zero-to-one depth: near plane is z_clip = 0
    // (plane r2), not the z = -w plane used by OpenGL-style [-1,1] depth.
    f.p[4] = r2;       // near
    f.p[5] = r3 - r2;  // far
    for (auto &pl : f.p) norm(pl);
    return f;
}

/** @brief Cheap entity snapshot handed to the parallel prep jobs. */
struct EntityRef3D {
    Renderable3D::Transform3D *xf = nullptr;
    Renderable3D::MeshRenderer *mr = nullptr;
};

/**
 * @brief Per-frame prep result (double-buffered): what the render thread
 * consumes. The prep jobs write here; the render loop only reads it.
 */
struct FrameState3D {
    FrameDrawList3D drawList;
    std::vector<PackedLight3D> packed;
    Lighting3DPack lighting;
    std::vector<ClusteredLightGpu> clusteredPoints;
    std::vector<ClusteredLightGpu> clusteredDirs;
    ShadowUpload shadowUpload;
    /** @brief Per-cascade light frustums for caster culling (computed by the
     *  light job from the CSM lightVP matrices). */
    FrustumPlanes cascadeFrustums[ShadowConfig::kCascades]{};
    Light3D::Data *shadowCaster = nullptr;
    bool haveExtraShadowCasters = false;
    bool useClustered = false;
    bool hadRenderables = false;
    float aspect = 1.f;
};

FrameState3D g_frameStates[2];
size_t g_frameIndex = 0;

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

/**
 * @brief Conservative sphere-vs-frustum test (Gribb-Hartmann plane extraction
 * from a column-major clip matrix).
 * @return false when the sphere is fully outside at least one plane.
 */
bool sphereInFrustum(const glm::mat4 &viewProj, const glm::vec3 &center, float radius) {
    const glm::vec4 c0 = viewProj[0];
    const glm::vec4 c1 = viewProj[1];
    const glm::vec4 c2 = viewProj[2];
    const glm::vec4 c3 = viewProj[3];
    const glm::vec4 planes[6] = {
        c3 + c0, c3 - c0,  // left, right
        c3 + c1, c3 - c1,  // bottom, top
        c3 + c2, c3 - c2,  // near, far
    };
    for (const glm::vec4 &p : planes) {
        const float dist = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
        if (dist < -radius) return false;
    }
    return true;
}

/**
 * @brief Job-ified frame data prep: ECS traversal, model matrices, LOD
 * selection, (optional) frustum culling, light packing and sorting run on the
 * JobSystem workers and land in `frame.drawList`; the render loop only
 * consumes the prebuilt list afterwards.
 */
void prepareFrame3D(FrameState3D &frame, RenderControl *rc, Camera3D *defaultCam, bool doShadow,
                    bool frustumCull) {
    frame.drawList.reset();
    frame.packed.clear();
    frame.clusteredPoints.clear();
    frame.clusteredDirs.clear();
    frame.shadowUpload = ShadowUpload{};
    frame.shadowUpload.active = false;
    frame.shadowCaster = nullptr;
    frame.useClustered = false;
    frame.hadRenderables = ecs::current()->getManager<Renderable3D>() != nullptr;

    if (!frame.hadRenderables) return;

    const bool haveExtraShadowCasters = doShadow && !g_shadowDrawers.empty();
    frame.haveExtraShadowCasters = haveExtraShadowCasters;

    // ---- light + CSM prep job (independent of the entity prep) ----
    auto *jobs = thread::Thread::create()->getJobSystem();
    jobs->beginFrame();
    thread::Job *lightJob = jobs->submitFrame([&] {
        collectLights3D(frame.packed, size_t(ClusteredLightConfig::kMaxLights));
        promoteDirectional(frame.packed);
        frame.shadowCaster = doShadow ? findShadowCasterDir(frame.packed) : nullptr;
        prioritizeShadowCaster(frame.packed, frame.shadowCaster);
        frame.useClustered =
            rc->isEnabled("clustered") && frame.packed.size() > size_t(Lighting3DPack::kMaxLights);

        const Camera3D::Data *ambientCam = defaultCam ? defaultCam->data().operator->() : nullptr;
        frame.lighting = packLights3D(frame.packed, ambientCam);
        splitLights(frame.packed, frame.clusteredPoints, frame.clusteredDirs);
        if (frame.clusteredDirs.empty()) {
            ClusteredLightGpu d{};
            d.posRadius = glm::vec4(gLightDir, 0.f);
            d.color = glm::vec4(gLightColor, 1.f);
            frame.clusteredDirs.push_back(d);
        }
        if (frame.shadowCaster && !frame.clusteredDirs.empty()) {
            glm::vec3 dir(frame.shadowCaster->dx, frame.shadowCaster->dy, frame.shadowCaster->dz);
            if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.f, 1.f, 0.f);
            else dir = glm::normalize(dir);
            for (size_t i = 0; i < frame.clusteredDirs.size(); ++i) {
                if (glm::length(glm::vec3(frame.clusteredDirs[i].posRadius) - dir) < 1e-3f) {
                    if (i != 0) std::swap(frame.clusteredDirs[0], frame.clusteredDirs[i]);
                    break;
                }
            }
        }

        ShadowUpload su{};
        su.active = false;
        if ((frame.shadowCaster || haveExtraShadowCasters) && defaultCam) {
            auto cd = defaultCam->data();
            glm::vec3 dir = frame.shadowCaster
                                ? glm::vec3(frame.shadowCaster->dx, frame.shadowCaster->dy,
                                            frame.shadowCaster->dz)
                                : gLightDir;
            if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.f, 1.f, 0.f);
            else dir = glm::normalize(dir);
            const float shadowBias = frame.shadowCaster ? frame.shadowCaster->shadowBias : 0.003f;
            const float shadowStrength =
                frame.shadowCaster ? frame.shadowCaster->shadowStrength : 1.f;
            const float fovRad = cd->fovYDeg * 0.017453292519943295f;
            su = buildDirectionalCSM(dir, glm::vec3(cd->eyeX, cd->eyeY, cd->eyeZ),
                                     glm::vec3(cd->targetX, cd->targetY, cd->targetZ),
                                     glm::vec3(cd->upX, cd->upY, cd->upZ), fovRad, frame.aspect,
                                     cd->nearZ, cd->farZ, shadowBias, shadowStrength);
        }
        frame.shadowUpload = su;
        if (su.active) {
            for (int c = 0; c < ShadowConfig::kCascades; ++c)
                frame.cascadeFrustums[c] = extractFrustum(su.ubo.lightVP[c]);
        }
    });

    // Camera snapshot: deduplicated camera data pointers plus their view-proj
    // matrices, computed on the main thread before the parallel job forks so
    // the workers only read immutable data. Slot 0 is the default camera so
    // the G-buffer pass reuses its view.
    std::vector<Camera3D::Data *> camData;
    std::vector<glm::mat4> camViewProj;
    auto camIndexFor = [&](Camera3D *camEnt) -> int {
        if (!camEnt) return -1;
        Camera3D::Data *d = camEnt->data().operator->();
        for (size_t i = 0; i < camData.size(); ++i) {
            if (camData[i] == d) return int(i);
        }
        const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
        const glm::vec3 target(d->targetX, d->targetY, d->targetZ);
        const glm::vec3 up(d->upX, d->upY, d->upZ);
        const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
        const float fovRad = d->fovYDeg * 0.017453292519943295f;
        const glm::mat4 projM = perspectiveVulkanRH_ZO(fovRad, frame.aspect, d->nearZ, d->farZ);
        camData.push_back(d);
        camViewProj.push_back(projM * viewM);
        return int(camData.size()) - 1;
    };
    const int defaultCamIdx = defaultCam ? camIndexFor(defaultCam) : -1;

    // ---- entity snapshot (single pass; matrix / LOD / cull math is parallel) ----
    std::vector<EntityRef3D> entities;
    {
        auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [xf, mr] = *it;
            if (!mr->visible) continue;
            // Register every referenced camera on the main thread so the
            // parallel workers below only do const lookups into camViewProj.
            (void)camIndexFor(mr->camera ? mr->camera : defaultCam);
            entities.push_back(EntityRef3D{xf, mr});
        }
    }
    if (entities.empty()) {
        lightJob->wait();
        jobs->endFrame();
        return;
    }
    // The entity job below consumes the light job's outputs (CSM cascade
    // frustums and shadow-active flag) for per-cascade caster culling, so the
    // light job must finish first. It is a handful of lights + CSM matrix
    // math, so the wait costs microseconds while the expensive parallel_for
    // still runs on all workers.
    lightJob->wait();

    auto camViewProjOf = [&](Camera3D *camEnt) -> const glm::mat4 * {
        if (!camEnt) return nullptr;
        Camera3D::Data *d = camEnt->data().operator->();
        for (size_t i = 0; i < camData.size(); ++i) {
            if (camData[i] == d) return &camViewProj[i];
        }
        return nullptr;
    };

    const int count = int(entities.size());
    constexpr int kChunk = 64;
    std::vector<std::vector<RenderItem3D>> chunks(size_t((count + kChunk - 1) / kChunk));
    const bool shadowActive = doShadow && frame.shadowUpload.active;
    thread::Job *entityJob = jobs->parallelForFrame(
        0, count,
        [&](int first, int last) {
            const int chunkIndex = first / kChunk;
            std::vector<RenderItem3D> &out = chunks[size_t(chunkIndex)];
            for (int i = first; i < last; ++i) {
                const EntityRef3D &e = entities[size_t(i)];
                Renderable3D::Transform3D *xf = e.xf;
                Renderable3D::MeshRenderer *mr = e.mr;

                Camera3D *camEnt = mr->camera ? mr->camera : defaultCam;
                const glm::mat4 model = modelFromTransform(*xf);

                auto distTo = [](const Renderable3D::Transform3D &t, const Camera3D::Data *cd) {
                    const float dx = t.x - cd->eyeX;
                    const float dy = t.y - cd->eyeY;
                    const float dz = t.z - cd->eyeZ;
                    return dx * dx + dy * dy + dz * dz;
                };
                float distSq = 0.f;
                if (camEnt) distSq = distTo(*xf, camEnt->data().operator->());
                float distSqMain = 0.f;
                if (defaultCam) distSqMain = distTo(*xf, defaultCam->data().operator->());
                const float dist = std::sqrt(distSq);
                const float distMain = std::sqrt(distSqMain);

                auto pushItem = [&](Mesh *mesh, Mesh *meshShadow, Material *mat, int partIndex,
                                    bool asHair) {
                    if (!mesh) return;
                    RenderItem3D item;
                    item.mr = mr;
                    item.meshForward = mesh;
                    item.meshShadow = meshShadow;
                    item.material = mat;
                    item.model = model;
                    item.distSq = distSq;
                    item.distSqMain = distSqMain;
                    item.camera = camEnt;
                    item.partIndex = partIndex;
                    item.hair = asHair;
                    item.shadowCastable = mr->effectiveCastShadow();
                    const bool shadowOk = item.shadowCastable && !(mat && !mat->getCastShadow());
                    const float maxScale =
                        std::max(std::abs(xf->sx), std::max(std::abs(xf->sy), std::abs(xf->sz)));
                    const glm::vec3 worldC(xf->x, xf->y, xf->z);
                    const float worldR = mesh->boundsRadius > 0.f ? maxScale * mesh->boundsRadius
                                                                  : 0.f;
                    if (frustumCull && worldR > 0.f) {
                        if (const glm::mat4 *vp = camViewProjOf(camEnt)) {
                            item.culled = !sphereInFrustum(*vp, worldC, worldR);
                        }
                    }
                    if (frustumCull && defaultCamIdx >= 0 && worldR > 0.f) {
                        item.culledMain =
                            !sphereInFrustum(camViewProj[size_t(defaultCamIdx)], worldC, worldR);
                    }
                    if (shadowActive && shadowOk) {
                        if (worldR > 0.f) {
                            for (int c = 0; c < ShadowConfig::kCascades; ++c) {
                                if (frame.cascadeFrustums[c].sphereVisible(worldC, worldR))
                                    item.cascadeMask |= (1u << c);
                            }
                        } else {
                            // No bounds (e.g. legacy/imported mesh): never cull.
                            item.cascadeMask = (1u << ShadowConfig::kCascades) - 1u;
                        }
                    }
                    out.push_back(std::move(item));
                };

                if (mr->usesParts()) {
                    for (int p = 0; p < mr->partCount; ++p) {
                        Material *mat =
                            mr->parts[p].material ? mr->parts[p].material : mr->material;
                        const bool asHair = mat ? mat->isTransparentHair() : mr->isHair;
                        pushItem(mr->parts[p].mesh, mr->parts[p].mesh, mat, p, asHair);
                    }
                } else {
                    Mesh *drawMesh = mr->meshForDistance(dist);
                    Mesh *drawMeshShadow = mr->meshForDistance(distMain);
                    pushItem(drawMesh, drawMeshShadow, mr->material, -1, mr->effectiveHair());
                }
            }
        },
        kChunk);
    entityJob->wait();
    jobs->endFrame();  // frame jobs are arena-allocated; endFrame recycles them

    for (auto &chunk : chunks) {
        for (auto &item : chunk) {
            if (item.hair)
                frame.drawList.hair.push_back(std::move(item));
            else
                frame.drawList.opaque.push_back(std::move(item));
        }
    }
    std::stable_sort(frame.drawList.hair.begin(), frame.drawList.hair.end(),
                     [](const RenderItem3D &a, const RenderItem3D &b) {
                         return a.distSq > b.distSq;
                     });
    frame.drawList.hasAny = !frame.drawList.opaque.empty() || !frame.drawList.hair.empty();
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
    const bool frustumCull = rc->isEnabled("frustumCull");

    const float aspect =
        (gfx.getHeight() > 0) ? float(gfx.getWidth()) / float(gfx.getHeight()) : 1.f;

    // Job-ified frame data prep: ECS traversal, model matrices, LOD selection,
    // optional frustum culling, light packing and hair sorting run on the
    // JobSystem workers and land in the double-buffered draw list. The rest of
    // this function only consumes the prebuilt result.
    FrameState3D &frame = g_frameStates[g_frameIndex & 1];
    ++g_frameIndex;
    frame.aspect = aspect;
    prepareFrame3D(frame, rc, defaultCam, doShadow, frustumCull);

    // ---- shadow pass (consumes the prebuilt caster list) ----
    if ((frame.shadowCaster || frame.haveExtraShadowCasters) && defaultCam &&
        (frame.hadRenderables || frame.haveExtraShadowCasters)) {
        auto cd = defaultCam->data();
        for (int c = 0; c < ShadowConfig::kCascades; ++c) {
            eve::debug::rtPassBegin("ShadowPass");
            gfx.beginShadowPass(c);
            auto drawShadowItem = [&](const RenderItem3D &item) {
                if (!item.shadowCastable) return;
                if (item.material && !item.material->getCastShadow()) return;
                if (!item.meshShadow) return;
                if ((item.cascadeMask & (1u << c)) == 0) return;
                eve::debug::rtBind("mesh", "shadowCaster");
                eve::debug::rtDraw("drawMeshShadow", "cascade");
                gfx.drawMeshShadow(item.meshShadow, frame.shadowUpload.ubo.lightVP[c] * item.model);
            };
            for (const auto &item : frame.drawList.opaque) drawShadowItem(item);
            for (const auto &item : frame.drawList.hair) drawShadowItem(item);
            // Extra shadow casters (billboard/card geometry not in the ECS).
            for (const auto &drawer : g_shadowDrawers)
                drawer(gfx, frame.shadowUpload.ubo.lightVP[c], *cd);
            gfx.endShadowPass();
            eve::debug::rtPassEnd("ShadowPass");
        }
    }
    gfx.setMesh3DShadows(frame.shadowUpload);

    if (!frame.useClustered) {
        ClusteredLightingUpload off{};
        off.active = false;
        gfx.setMesh3DClusteredLighting(off);
        gfx.setMesh3DLighting(frame.lighting);
    }

    // G-buffer fill (sampleable depth/normal) — before the forward swapchain pass.
    if (doGBuffer && defaultCam &&
        (frame.hadRenderables || !g_gbufferDrawers.empty())) {
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
        auto drawGBufferItem = [&](const RenderItem3D &item) {
            if (item.culledMain) return;
            // X-ray targets are skipped so their pixels record the occluder depth
            // behind them; the X-ray shader samples that to detect occlusion.
            if (item.mr->xrayHighlight) return;
            if (item.hair) return;
            if (!item.meshShadow) return;
            Material *mat = item.material;
            Texture *alb = mat ? mat->getAlbedoTexture() : item.mr->texture;
            float tr = mat ? mat->getTintR() : item.mr->r;
            float tg = mat ? mat->getTintG() : item.mr->g;
            float tb = mat ? mat->getTintB() : item.mr->b;
            eve::debug::rtDraw("drawMeshGBuffer", "gbuffer");
            gfx.drawMeshGBuffer(item.meshShadow, viewProj * item.model, item.model, cd->nearZ,
                                cd->farZ, alb, tr, tg, tb);
        };
        for (const auto &item : frame.drawList.opaque) drawGBufferItem(item);
        for (const auto &item : frame.drawList.hair) drawGBufferItem(item);
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
        } else if (frame.useClustered && !shader) {
            glm::vec4 ambient(cd->ambientR, cd->ambientG, cd->ambientB, 0.f);
            auto upload = buildClusteredLighting(frame.clusteredPoints, frame.clusteredDirs, viewM,
                                                 cd->nearZ, cd->farZ, gfx.getWidth(),
                                                 gfx.getHeight(), fovRad, ambient);
            gfx.setMesh3DClusteredLighting(upload);
            gfx.setMesh3DLighting(packLights3D(frame.packed, cd));
        } else {
            ClusteredLightingUpload off{};
            off.active = false;
            gfx.setMesh3DClusteredLighting(off);
            gfx.setMesh3DLighting(packLights3D(frame.packed, cd));
        }
    };

    auto drawMeshWithMaterial = [&](const RenderItem3D &item) {
        Mesh *drawMesh = item.meshForward;
        Material *mat = item.material;
        if (!drawMesh) return;
        Renderable3D::MeshRenderer *mr = item.mr;
        Camera3D *camEnt = item.camera;
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

        const glm::mat4 model = item.model;
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
        for (const auto &item : frame.drawList.opaque) {
            if (item.culled || !item.camera) continue;
            drawMeshWithMaterial(item);
        }
    }
    if (doHair) {
        for (const auto &item : frame.drawList.hair) {
            if (item.culled || !item.camera) continue;
            drawMeshWithMaterial(item);
        }
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
