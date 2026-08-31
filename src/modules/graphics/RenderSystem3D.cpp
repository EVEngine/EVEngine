#include "graphics/RenderSystem3D.h"
#include "graphics/DepthPyramid.h"
#include "common/RenderTrace.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/ClipSpace.h"
#include "graphics/ClusteredLight.h"
#include "graphics/Graphics.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Outline.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shadow.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace eve::graphics {

namespace {

std::vector<RenderSystem3D::GBufferExtraDrawer> g_gbufferDrawers;
std::vector<RenderSystem3D::ShadowExtraDrawer> g_shadowDrawers;
std::vector<RenderSystem3D::DecalExtraDrawer> g_decalDrawers;
struct CaptureExtraDrawerEntry {
    uint64_t token = 0;
    uint32_t mask = 0;
    RenderSystem3D::CaptureExtraDrawer drawer;
};
std::vector<CaptureExtraDrawerEntry> g_captureDrawers;
uint64_t g_nextCaptureDrawerToken = 1;

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

void Camera3D::setOrthographic(float height) {
    data()->orthographic = true;
    data()->orthoHeight = std::max(0.001f, height);
}

void Camera3D::setPerspective() { data()->orthographic = false; }

void Camera3D::setClipPlanes(float nearZ, float farZ) {
    data()->nearZ = std::max(0.001f, nearZ);
    data()->farZ = std::max(data()->nearZ + 0.001f, farZ);
}

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

void Camera3D::setExposure(float ev) { data()->exposureEV = std::clamp(ev, -16.f, 16.f); }

float Camera3D::getExposure() { return data()->exposureEV; }

void Camera3D::setAutoExposure(bool enabled, float minEV, float maxEV) {
    auto d = data();
    d->autoExposure = enabled;
    d->autoExposureMinEV = std::clamp(std::min(minEV, maxEV), -16.f, 16.f);
    d->autoExposureMaxEV = std::clamp(std::max(minEV, maxEV), -16.f, 16.f);
}

bool Camera3D::isAutoExposure() { return data()->autoExposure; }

void Camera3D::setBloom(float intensity, float threshold) {
    auto d = data();
    d->bloomIntensity = std::clamp(intensity, 0.f, 8.f);
    d->bloomThreshold = std::clamp(threshold, 0.f, 16.f);
}

float Camera3D::getBloomIntensity() { return data()->bloomIntensity; }
float Camera3D::getBloomThreshold() { return data()->bloomThreshold; }

void Camera3D::setEnvProbe(float centerX, float centerY, float centerZ, float extentX,
                           float extentY, float extentZ) {
    data()->envProbeCenter = glm::vec3(centerX, centerY, centerZ);
    data()->envProbeExtent = glm::max(glm::vec3(extentX, extentY, extentZ), glm::vec3(0.f));
}

void Camera3D::clearEnvProbe() { data()->envProbeExtent = glm::vec3(0.f); }

bool Camera3D::hasEnvProbe() {
    const glm::vec3 e = data()->envProbeExtent;
    return e.x > 0.f && e.y > 0.f && e.z > 0.f;
}
float Camera3D::getEnvProbeCenterX() { return data()->envProbeCenter.x; }
float Camera3D::getEnvProbeCenterY() { return data()->envProbeCenter.y; }
float Camera3D::getEnvProbeCenterZ() { return data()->envProbeCenter.z; }
float Camera3D::getEnvProbeExtentX() { return data()->envProbeExtent.x; }
float Camera3D::getEnvProbeExtentY() { return data()->envProbeExtent.y; }
float Camera3D::getEnvProbeExtentZ() { return data()->envProbeExtent.z; }

void Camera3D::setReflectionProbe(int slot, Texture *cubemap, float centerX, float centerY,
                                  float centerZ, float extentX, float extentY, float extentZ,
                                  float intensity, float blendDistance, int priority) {
    if (slot < 0 || slot >= Data::kMaxReflectionProbes) return;
    auto &probe = data()->reflectionProbes[static_cast<size_t>(slot)];
    probe.cubemap = cubemap;
    probe.center = glm::vec3(centerX, centerY, centerZ);
    probe.extent = glm::max(glm::vec3(extentX, extentY, extentZ), glm::vec3(0.f));
    probe.intensity = std::max(intensity, 0.f);
    probe.blendDistance = std::max(blendDistance, 0.f);
    probe.priority = priority;
    probe.enabled = cubemap && glm::all(glm::greaterThan(probe.extent, glm::vec3(0.f))) &&
                    probe.intensity > 0.f;
}

void Camera3D::clearReflectionProbe(int slot) {
    if (slot < 0 || slot >= Data::kMaxReflectionProbes) return;
    data()->reflectionProbes[static_cast<size_t>(slot)] = {};
}

int Camera3D::getReflectionProbeCount() {
    int count = 0;
    for (const auto &probe : data()->reflectionProbes)
        if (probe.enabled) ++count;
    return count;
}

static ReflectionProbeUpload selectReflectionProbes(const Camera3D::Data &camera,
                                                     const glm::vec3 &samplePosition) {
    struct Candidate {
        int slot = 0;
        int priority = 0;
        float distance2 = 0.f;
    };
    std::array<Candidate, Camera3D::Data::kMaxReflectionProbes> candidates{};
    int candidateCount = 0;
    for (int slot = 0; slot < Camera3D::Data::kMaxReflectionProbes; ++slot) {
        const auto &probe = camera.reflectionProbes[static_cast<size_t>(slot)];
        if (!probe.enabled || !probe.cubemap) continue;
        const glm::vec3 outside = glm::max(glm::abs(samplePosition - probe.center) - probe.extent,
                                           glm::vec3(0.f));
        candidates[static_cast<size_t>(candidateCount++)] =
            Candidate{slot, probe.priority, glm::dot(outside, outside)};
    }
    std::stable_sort(candidates.begin(), candidates.begin() + candidateCount,
                     [](const Candidate &a, const Candidate &b) {
                         if (a.priority != b.priority) return a.priority > b.priority;
                         if (a.distance2 != b.distance2) return a.distance2 < b.distance2;
                         return a.slot < b.slot;
                     });

    ReflectionProbeUpload upload;
    upload.count = std::min(candidateCount, ReflectionProbeUpload::kMaxProbes);
    for (int i = 0; i < upload.count; ++i) {
        const auto &source = camera.reflectionProbes[static_cast<size_t>(candidates[i].slot)];
        auto &target = upload.probes[i];
        target.cubemap = source.cubemap;
        target.center = source.center;
        target.extent = source.extent;
        target.intensity = source.intensity;
        target.blendDistance = source.blendDistance;
        target.priority = source.priority;
    }
    return upload;
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
    const glm::mat4 projM = cameraProjectionVulkanRH_ZO(
        d->orthographic, fovRad, d->orthoHeight, aspect, d->nearZ, d->farZ);
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
    if (d->orthographic) {
        d->screenRayOx = nearPt.x;
        d->screenRayOy = nearPt.y;
        d->screenRayOz = nearPt.z;
    }
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

Mesh *Renderable3D::getMesh() { return meshRenderer()->mesh; }

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

void Renderable3D::setPartSortPriority(int index, int priority) {
    auto mr = meshRenderer();
    if (index < 0 || index >= MeshRenderer::kMaxParts) return;
    mr->parts[index].sortPriority    = priority;
    mr->parts[index].hasSortPriority = true;
}

void Renderable3D::clearPartSortPriority(int index) {
    auto mr = meshRenderer();
    if (index < 0 || index >= MeshRenderer::kMaxParts) return;
    mr->parts[index].sortPriority    = 0;
    mr->parts[index].hasSortPriority = false;
}

int Renderable3D::getPartSortPriority(int index) {
    auto mr = meshRenderer();
    if (index < 0 || index >= mr->partCount) return 0;
    const auto& part = mr->parts[index];
    if (part.hasSortPriority) return part.sortPriority;
    return part.material ? part.material->getSortPriority() : 0;
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

void Renderable3D::setReflectionCaptureMask(int mask) {
    meshRenderer()->reflectionCaptureMask = static_cast<uint32_t>(mask);
}

int Renderable3D::getReflectionCaptureMask() {
    return static_cast<int>(meshRenderer()->reflectionCaptureMask);
}

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

uint64_t RenderSystem3D::addCaptureExtraDrawer(uint32_t reflectionCaptureMask,
                                               CaptureExtraDrawer drawer) {
    if (!drawer || reflectionCaptureMask == 0u) return 0;
    const uint64_t token = g_nextCaptureDrawerToken++;
    g_captureDrawers.push_back(
        CaptureExtraDrawerEntry{token, reflectionCaptureMask, std::move(drawer)});
    return token;
}

void RenderSystem3D::removeCaptureExtraDrawer(uint64_t token) {
    if (token == 0) return;
    g_captureDrawers.erase(
        std::remove_if(g_captureDrawers.begin(), g_captureDrawers.end(),
                       [token](const CaptureExtraDrawerEntry &entry) {
                           return entry.token == token;
                       }),
        g_captureDrawers.end());
}

void RenderSystem3D::addShadowExtraDrawer(ShadowExtraDrawer drawer) {
    if (!drawer) return;
    g_shadowDrawers.push_back(std::move(drawer));
}

void RenderSystem3D::addDecalExtraDrawer(DecalExtraDrawer drawer) {
    if (!drawer) return;
    g_decalDrawers.push_back(std::move(drawer));
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
    FrustumPlanes   f;
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    auto            norm = [](glm::vec4 &v) {
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

/**
 * @brief Per-camera constants resolved once per frame: view/proj matrices,
 * frustum for culling, lighting pack, and (when clustered) the clustered
 * light table for this camera. Multi-camera scenes pay once per camera.
 */
struct CameraView {
    Camera3D::Data         *data = nullptr;
    glm::vec3               eye{0.f};
    glm::mat4               view{1.f};
    glm::mat4               proj{1.f};
    glm::mat4               viewProj{1.f};
    glm::mat4               cullViewProj{1.f};
    FrustumPlanes           frustum;
    float                   fovRad = 1.f;
    Lighting3DPack          lighting{};
    ClusteredLightingUpload clustered{};
    bool                    clusteredValid = false;
    /** @brief Whether the clustered SSBOs were uploaded for this camera this frame. */
    bool clusteredUploaded = false;
};

CameraView buildCameraView(Camera3D::Data *cd, const std::vector<PackedLight3D> &packed, bool useClustered,
                           float aspect, const std::vector<ClusteredLightGpu> &clusteredPoints,
                           const std::vector<ClusteredLightGpu> &clusteredDirs, Graphics &gfx,
                           const glm::vec2 &jitterNdc) {
    CameraView cv;
    cv.data = cd;
    cv.eye  = glm::vec3(cd->eyeX, cd->eyeY, cd->eyeZ);
    const glm::vec3 look(cd->targetX, cd->targetY, cd->targetZ);
    const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
    cv.view     = glm::lookAtRH(cv.eye, look, up);
    cv.fovRad   = cd->fovYDeg * 0.017453292519943295f;
    cv.proj     = cameraProjectionVulkanRH_ZO(cd->orthographic, cv.fovRad,
                                              cd->orthoHeight, aspect,
                                              cd->nearZ, cd->farZ);
    cv.cullViewProj = cv.proj * cv.view;
    cv.frustum  = extractFrustum(cv.cullViewProj);
    cv.proj[2][0] += jitterNdc.x;
    cv.proj[2][1] += jitterNdc.y;
    cv.viewProj = cv.proj * cv.view;
    cv.lighting = packLights3D(packed, cd);
    if (useClustered && !cd->orthographic) {
        cv.clustered      = buildClusteredLighting(clusteredPoints, clusteredDirs, cv.view, cd->nearZ, cd->farZ,
                                                   gfx.getWidth(), gfx.getHeight(), cv.fovRad,
                                                   glm::vec4(cd->ambientR, cd->ambientG, cd->ambientB, 0.f));
        cv.clusteredValid = true;
    }
    return cv;
}

/**
 * @brief One collected draw unit (mesh + material + transform). The ECS is
 * traversed exactly once per frame; shadow / G-buffer / forward passes replay
 * this list instead of re-walking the whole scene five times.
 */
struct CulledItem {
    Renderable3D::MeshRenderer *mr       = nullptr;
    Mesh                       *mesh     = nullptr;
    Material                   *material = nullptr;
    Shader                     *shader   = nullptr;  // effective mesh shader (material or legacy)
    glm::mat4                   model{1.f};
    glm::vec3                   worldC{0.f};          // world-space bounding-sphere center
    glm::vec2                   temporalMotion{0.f};  // object-only previous-UV correction
    float                       worldR        = 0.f;  // world-space bounding-sphere radius (0 = unknown → unculled)
    float                       distSq        = 0.f;
    float                       projectedDepth = 0.f;
    SurfaceMode                 surfaceMode = SurfaceMode::Opaque;
    int                         sortPriority = 0;
    int                         camIdx        = 0;
    uint32_t                    cascadeMask   = 0;      // bit c set when the caster may contribute to cascade c
    bool                        inView        = false;  // inside the item camera's frustum
    bool                        inDefaultView = false;  // inside the default camera's frustum (G-buffer)
    bool                        hair          = false;
    bool                        xray          = false;
};

}  // namespace

void RenderSystem3D::render(Graphics &gfx) {
    eve::debug::RenderPassScope pass3d("RenderSystem3D");
    Camera3D                   *defaultCam = findDefaultCamera3D();

    RenderControl *rc = gfx.getRenderControl();
    rc->ensureCompiled();
    const bool doShadow = rc->hasPass("shadow");
    const bool doGBuffer = rc->hasPass("gbuffer");
    const bool doDecal = rc->hasPass("decal");
    const bool doForward = rc->hasPass("forward");
    const bool doHair = rc->hasPass("hair");
    const bool allowClustered = rc->isEnabled("clustered");

    std::vector<PackedLight3D> packed;
    collectLights3D(packed, size_t(ClusteredLightConfig::kMaxLights));
    promoteDirectional(packed);
    Light3D::Data *shadowCaster = doShadow ? findShadowCasterDir(packed) : nullptr;
    prioritizeShadowCaster(packed, shadowCaster);
    const bool haveExtraShadowCasters = doShadow && !g_shadowDrawers.empty();

    const float aspect = (gfx.getHeight() > 0) ? float(gfx.getWidth()) / float(gfx.getHeight()) : 1.f;

    ShadowUpload shadowUpload{};
    shadowUpload.active = false;
    // Per-cascade light frustums for caster culling (sphere vs frustum).
    FrustumPlanes cascadeFrustums[ShadowConfig::kCascades];
    if ((shadowCaster || haveExtraShadowCasters) && defaultCam) {
        auto      cd  = defaultCam->data();
        glm::vec3 dir = shadowCaster ? glm::vec3(shadowCaster->dx, shadowCaster->dy, shadowCaster->dz) : gLightDir;
        if (glm::length(dir) < 1e-6f)
            dir = glm::vec3(0.f, 1.f, 0.f);
        else
            dir = glm::normalize(dir);
        const float shadowBias     = shadowCaster ? shadowCaster->shadowBias : 0.003f;
        const float shadowStrength = shadowCaster ? shadowCaster->shadowStrength : 1.f;
        const float fovRad         = cd->fovYDeg * 0.017453292519943295f;
        shadowUpload               = buildDirectionalCSM(
            dir, glm::vec3(cd->eyeX, cd->eyeY, cd->eyeZ), glm::vec3(cd->targetX, cd->targetY, cd->targetZ),
            glm::vec3(cd->upX, cd->upY, cd->upZ), fovRad, aspect, cd->nearZ, cd->farZ, shadowBias, shadowStrength);
        for (int c = 0; c < ShadowConfig::kCascades; ++c)
            cascadeFrustums[c] = extractFrustum(shadowUpload.ubo.lightVP[c]);
    }
    gfx.setMesh3DShadows(shadowUpload);

    const bool useClustered = allowClustered && packed.size() > size_t(Lighting3DPack::kMaxLights);
    // Light split / directional promotion is camera-independent: compute once,
    // then each camera view bakes its own clustered table from these lists.
    std::vector<ClusteredLightGpu> clusteredPoints;
    std::vector<ClusteredLightGpu> clusteredDirs;
    if (useClustered) {
        splitLights(packed, clusteredPoints, clusteredDirs);
        if (clusteredDirs.empty()) {
            ClusteredLightGpu d{};
            d.posRadius = glm::vec4(gLightDir, 0.f);
            d.color     = glm::vec4(gLightColor, 1.f);
            clusteredDirs.push_back(d);
        }
        if (shadowCaster && !clusteredDirs.empty()) {
            glm::vec3 dir(shadowCaster->dx, shadowCaster->dy, shadowCaster->dz);
            if (glm::length(dir) < 1e-6f)
                dir = glm::vec3(0.f, 1.f, 0.f);
            else
                dir = glm::normalize(dir);
            for (size_t i = 0; i < clusteredDirs.size(); ++i) {
                if (glm::length(glm::vec3(clusteredDirs[i].posRadius) - dir) < 1e-3f) {
                    if (i != 0) std::swap(clusteredDirs[0], clusteredDirs[i]);
                    break;
                }
            }
        }
    }

    const bool haveManager  = ecs::current()->getManager<Renderable3D>() != nullptr;
    const bool shadowActive = doShadow && shadowUpload.active;

    // Per-camera constants (matrices, frustum, lighting, clustered table).
    // The default camera is slot 0 so the G-buffer pass reuses its view.
    std::vector<CameraView> cams;
    cams.reserve(2);
    glm::vec2 temporalJitter(0.f);
    AntiAliasing *temporalAA = nullptr;
    if (defaultCam && rc->isEnabled("taa")) {
        auto cd = defaultCam->data();
        temporalAA = gfx.pipelineAntiAliasing();
        temporalAA->setTemporalCamera(glm::vec3(cd->eyeX, cd->eyeY, cd->eyeZ),
                                      glm::vec3(cd->targetX, cd->targetY, cd->targetZ),
                                      cd->fovYDeg);
        temporalJitter = temporalAA->prepareTemporalJitter(gfx.getWidth(), gfx.getHeight());
    }
    if (defaultCam) {
        cams.push_back(buildCameraView(defaultCam->data().operator->(), packed, useClustered, aspect, clusteredPoints,
                                       clusteredDirs, gfx, temporalJitter));
        if (temporalAA) {
            auto cd = defaultCam->data();
            temporalAA->setTemporalViewProjection(cams.front().viewProj, cd->nearZ, cd->farZ);
        }
    }
    auto findOrAddCam = [&](Camera3D *camEnt) -> int {
        Camera3D::Data *d = camEnt->data().operator->();
        for (size_t i = 0; i < cams.size(); ++i) {
            if (cams[i].data == d) return int(i);
        }
        cams.push_back(buildCameraView(d, packed, useClustered, aspect, clusteredPoints,
                                       clusteredDirs, gfx, temporalJitter));
        return int(cams.size()) - 1;
    };

    // Single ECS traversal: one model matrix, one LOD pick, one set of
    // sphere-vs-frustum tests per part. Passes below only replay the list.
    std::vector<CulledItem> items;
    items.reserve(64);
    if (haveManager) {
        auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [xf, mr] = *it;
            if (!mr->visible) continue;
            Camera3D *camEnt = mr->camera ? mr->camera : defaultCam;
            if (!camEnt) continue;
            const int         camIdx   = findOrAddCam(camEnt);
            const CameraView &cv       = cams[size_t(camIdx)];
            const float       dx       = xf->x - cv.eye.x;
            const float       dy       = xf->y - cv.eye.y;
            const float       dz       = xf->z - cv.eye.z;
            const float       distSq   = dx * dx + dy * dy + dz * dz;
            const float       dist     = std::sqrt(distSq);
            const glm::mat4   model    = modelFromTransform(*xf);
            const float       maxScale = std::max(std::abs(xf->sx), std::max(std::abs(xf->sy), std::abs(xf->sz)));
            const bool        shadowOk = mr->effectiveCastShadow();

            auto pushPart = [&](Mesh *drawMesh, Material *mat, bool asHair, bool castsShadow,
                                const ModelPart* modelPart) {
                if (!drawMesh) return;
                CulledItem item;
                item.mr       = mr;
                item.mesh     = drawMesh;
                item.material = mat;
                item.shader   = mat ? mat->effectiveShader() : mr->shader;
                item.model    = model;
                if (temporalAA)
                    item.temporalMotion = temporalAA->prepareTemporalObjectMotion(mr, model);
                item.distSq   = distSq;
                const glm::vec4 viewCenter = cv.view * glm::vec4(xf->x, xf->y, xf->z, 1.f);
                item.projectedDepth = -viewCenter.z;
                item.camIdx   = camIdx;
                item.hair     = asHair;
                item.surfaceMode = mat ? mat->surfaceMode()
                                       : (asHair ? SurfaceMode::Transparent
                                                 : SurfaceMode::Opaque);
                item.sortPriority = modelPart && modelPart->hasSortPriority
                                        ? modelPart->sortPriority
                                        : (mat ? mat->getSortPriority() : 0);
                item.xray     = mr->xrayHighlight;
                if (drawMesh->hasBounds()) {
                    const glm::vec4 c4 =
                        model * glm::vec4(drawMesh->boundsCx, drawMesh->boundsCy, drawMesh->boundsCz, 1.f);
                    item.worldC        = glm::vec3(c4);
                    item.worldR        = drawMesh->boundsRadius * maxScale;
                    item.inView        = cv.frustum.sphereVisible(item.worldC, item.worldR);
                    item.inDefaultView = defaultCam ? cams[0].frustum.sphereVisible(item.worldC, item.worldR) : true;
                    if (shadowActive && castsShadow) {
                        for (int c = 0; c < ShadowConfig::kCascades; ++c) {
                            if (cascadeFrustums[c].sphereVisible(item.worldC, item.worldR))
                                item.cascadeMask |= (1u << c);
                        }
                    }
                } else {
                    // No bounds (e.g. legacy/imported mesh): never cull.
                    item.inView        = true;
                    item.inDefaultView = true;
                    if (shadowActive && castsShadow) item.cascadeMask = (1u << ShadowConfig::kCascades) - 1u;
                }
                items.push_back(item);
            };

            if (mr->usesParts()) {
                for (int p = 0; p < mr->partCount; ++p) {
                    Material  *mat    = mr->parts[p].material ? mr->parts[p].material : mr->material;
                    const bool asHair = mat ? mat->isTransparentHair() : mr->isHair;
                    const bool casts  = shadowOk && !(mat && !mat->getCastShadow());
                    pushPart(mr->parts[p].mesh, mat, asHair, casts, &mr->parts[p]);
                }
            } else {
                Mesh *drawMesh = mr->meshForDistance(dist);
                pushPart(drawMesh, mr->material, mr->effectiveHair(), shadowOk, nullptr);
            }
        }
    }

    // CSM shadow passes — replay the collected casters, culled per cascade.
    if (shadowActive && (haveManager || haveExtraShadowCasters)) {
        auto cd = defaultCam->data();
        for (int c = 0; c < ShadowConfig::kCascades; ++c) {
            eve::debug::rtPassBegin("ShadowPass");
            gfx.beginShadowPass(c);
            for (const auto &item : items) {
                if ((item.cascadeMask & (1u << c)) == 0) continue;
                eve::debug::rtBind("mesh", "shadowCaster");
                eve::debug::rtDraw("drawMeshShadow", "cascade");
                Texture *shadowAlbedo =
                    item.material ? item.material->getAlbedoTexture() : item.mr->texture;
                if (item.surfaceMode == SurfaceMode::Masked)
                    gfx.drawMeshShadowAlpha(item.mesh, shadowUpload.ubo.lightVP[c] * item.model,
                                            shadowAlbedo);
                else if (item.surfaceMode != SurfaceMode::Transparent)
                    gfx.drawMeshShadow(item.mesh, shadowUpload.ubo.lightVP[c] * item.model);
            }
            // Extra shadow casters (billboard/card geometry not in the ECS).
            for (const auto &drawer : g_shadowDrawers) drawer(gfx, shadowUpload.ubo.lightVP[c], *cd);
            gfx.endShadowPass();
            eve::debug::rtPassEnd("ShadowPass");
        }
    }

    // G-buffer fill (sampleable depth/normal) — before the forward swapchain pass.
    if (doGBuffer && defaultCam && (haveManager || !g_gbufferDrawers.empty())) {
        eve::debug::rtPassBegin("GBufferPass");
        const CameraView &cv = cams[0];  // default camera is slot 0
        const int         gw = std::max(1, gfx.getPixelWidth() > 0 ? gfx.getPixelWidth() : gfx.getWidth());
        const int         gh = std::max(1, gfx.getPixelHeight() > 0 ? gfx.getPixelHeight() : gfx.getHeight());
        gfx.beginGBufferPass(gw, gh);
        for (const auto &item : items) {
            // X-ray targets are skipped so their pixels record the occluder depth
            // behind them; the X-ray shader samples that to detect occlusion.
            if (item.xray) continue;
            if (item.surfaceMode == SurfaceMode::Transparent) continue;
            if (!item.inDefaultView) continue;
            Texture    *alb = item.material ? item.material->getAlbedoTexture() : item.mr->texture;
            const float tr  = item.material ? item.material->getTintR() : item.mr->r;
            const float tg  = item.material ? item.material->getTintG() : item.mr->g;
            const float tb  = item.material ? item.material->getTintB() : item.mr->b;
            const float roughness =
                item.material ? item.material->getRoughness() : item.mr->roughness;
            const float metallic =
                item.material ? item.material->getMetallic() : item.mr->metallic;
            eve::debug::rtDraw("drawMeshGBuffer", "gbuffer");
            if (item.surfaceMode == SurfaceMode::Masked)
                gfx.drawMeshGBufferAlpha(item.mesh, cv.viewProj * item.model, item.model,
                                         cv.data->nearZ, cv.data->farZ, alb, tr, tg, tb,
                                         item.temporalMotion.x, item.temporalMotion.y, roughness,
                                         metallic);
            else
                gfx.drawMeshGBuffer(item.mesh, cv.viewProj * item.model, item.model,
                                    cv.data->nearZ, cv.data->farZ, alb, tr, tg, tb,
                                    item.temporalMotion.x, item.temporalMotion.y, roughness,
                                    metallic);
        }
        // Extra G-buffer contributors (billboard/card geometry not in the ECS).
        for (const auto &drawer : g_gbufferDrawers) drawer(gfx, *cv.data, cv.viewProj, aspect);
        gfx.endGBufferPass();
        eve::debug::rtPassEnd("GBufferPass");
    } else if (!doGBuffer) {
        rc->getGBuffer()->clear();
    }

    // Screen-space decal layer: box-projected decals read the G-buffer
    // depth/normal and write albedo/normal/params targets sampled by
    // mesh3d.frag before lighting. Skipped when no decals are registered or
    // the backend cannot run the pass (WebGPU).
    if (doDecal && defaultCam && !g_decalDrawers.empty() && gfx.supportsDecal()) {
        eve::debug::rtPassBegin("DecalPass");
        const CameraView &cv = cams[0];
        const int dw = std::max(1, gfx.getPixelWidth() > 0 ? gfx.getPixelWidth() : gfx.getWidth());
        const int dh = std::max(1, gfx.getPixelHeight() > 0 ? gfx.getPixelHeight() : gfx.getHeight());
        gfx.beginDecalPass(dw, dh);
        gfx.setDecalCamera(cv.viewProj, cv.data->nearZ, cv.data->farZ);
        for (const auto &drawer : g_decalDrawers) drawer(gfx, *cv.data, cv.viewProj, aspect);
        gfx.endDecalPass();
        eve::debug::rtPassEnd("DecalPass");
    }

    if (!doForward && !doHair) return;

    // GPU-driven intent must be set BEFORE begin3DFrame: when the stage-2 cull
    // chain is live, begin3DFrame defers opening the scene color pass so the
    // compute section can be recorded before the opaque draws.
    const bool gpuDrivenWanted =
        rc->isEnabled("gpuDriven") && gfx.supportsGpuDriven3D();
    gfx.gpuDrivenSetEnabled(gpuDrivenWanted);

    if (defaultCam) {
        auto cd = defaultCam->data();
        gfx.setSceneExposure(std::exp2(cd->exposureEV));
        gfx.setSceneAutoExposure(cd->autoExposure, cd->autoExposureMinEV,
                                 cd->autoExposureMaxEV);
        gfx.setSceneBloom(cd->bloomIntensity, cd->bloomThreshold);
    }
    gfx.begin3DFrame();
    if (!gfx.had3DThisFrame()) return;

    if (!haveManager) return;

    // Replay the items collected above: opaque first, hair back-to-front.
    std::vector<const CulledItem *> opaque;
    std::vector<const CulledItem *> transparentItems;
    opaque.reserve(items.size());
    transparentItems.reserve(items.size() / 4);
    for (const auto &item : items) {
        if (!item.inView) continue;
        (item.surfaceMode == SurfaceMode::Transparent ? transparentItems : opaque).push_back(&item);
    }
    // Opaque: group by (camera, shader, material, mesh) so the backend sees
    // long runs of identical pipeline/descriptor state instead of thrashing
    // between materials. Hair stays sorted back-to-front by distance below.
    std::stable_sort(opaque.begin(), opaque.end(), [](const CulledItem *a, const CulledItem *b) {
        if (a->camIdx != b->camIdx) return a->camIdx < b->camIdx;
        if (a->shader != b->shader) return a->shader < b->shader;
        if (a->material != b->material) return a->material < b->material;
        return a->mesh < b->mesh;
    });
    std::stable_sort(transparentItems.begin(), transparentItems.end(),
                     [](const CulledItem *a, const CulledItem *b) {
                         if (a->camIdx != b->camIdx) return a->camIdx < b->camIdx;
                         if (a->sortPriority != b->sortPriority)
                             return a->sortPriority < b->sortPriority;
                         return a->projectedDepth > b->projectedDepth;
                     });

    auto bindLegacyMaterial = [&](Renderable3D::MeshRenderer *mr) {
        gfx.setMesh3DSurface(mr->isHair ? SurfaceMode::Transparent : SurfaceMode::Opaque,
                             BlendMode::Alpha, false, mr->isHair, 0.5f);
        gfx.setMesh3DMaterial(mr->metallic, mr->roughness);
        gfx.setMesh3DTexCellBomb(mr->texBombScale, mr->texBombStrength, mr->texBombRot);
        gfx.setMesh3DNormalTexture(mr->normalTexture);
        gfx.setMesh3DHeightTexture(mr->heightTexture);
        gfx.setMesh3DParallax(mr->parallaxScale, mr->parallaxMinLayers, mr->parallaxMaxLayers);
        gfx.setMesh3DShadowReceive(mr->receiveShadow);
    };

    // Per-camera / per-lighting state. The clustered SSBO table is uploaded at
    // most once per camera; afterwards only the cheap active flag toggles.
    int  curCam       = -1;
    bool curLit       = false;
    bool curClustered = false;

    auto drawMeshWithMaterial = [&](const CulledItem &item, CameraView &cv) {
        auto     *mr       = item.mr;
        Mesh     *drawMesh = item.mesh;
        Material *mat      = item.material;
        if (!drawMesh) return;

        Texture *albedo = mr->texture;
        Color    tint(mr->r, mr->g, mr->b, mr->a);
        Shader  *shader = mr->shader;
        if (mat) {
            mat->bind(gfx);
            albedo = mat->getAlbedoTexture();
            tint   = Color(mat->getTintR(), mat->getTintG(), mat->getTintB(), mat->getTintA());
            shader = mat->effectiveShader();
        } else {
            bindLegacyMaterial(mr);
        }

        const bool lit       = mat ? mat->getReceiveLight() : mr->receiveLight;
        const bool clustered = lit && useClustered && !shader;
        if (item.camIdx != curCam || lit != curLit || clustered != curClustered) {
            if (item.camIdx != curCam) {
                gfx.setMesh3DViewProj(cv.viewProj);
                gfx.setMesh3DView(cv.view);
                gfx.setMesh3DClip(cv.data->nearZ, cv.data->farZ);
                gfx.setMesh3DCameraPos(cv.eye);
                gfx.setMesh3DEnv(cv.data->envMap, cv.data->envIntensity);
                gfx.setMesh3DEnvProbe(cv.data->envProbeCenter, cv.data->envProbeExtent);
                curCam = item.camIdx;
            }
            if (clustered && cv.clusteredValid) {
                if (!cv.clusteredUploaded) {
                    gfx.setMesh3DClusteredLighting(cv.clustered);
                    cv.clusteredUploaded = true;
                } else {
                    gfx.setMesh3DClusteredActive(true);
                }
            } else {
                gfx.setMesh3DClusteredActive(false);
            }
            if (lit) {
                gfx.setMesh3DLighting(cv.lighting);
            } else {
                Lighting3DPack none{};
                none.count   = 0;
                none.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
                gfx.setMesh3DLighting(none);
            }
            curLit       = lit;
            curClustered = clustered;
        }

        // Local probes are spatial data. Select at the renderable bounds center
        // so adjacent volumes do not leak the camera's nearest probe into every
        // object in the frame.
        gfx.setMesh3DReflectionProbes(selectReflectionProbes(*cv.data, item.worldC));

        const glm::mat4 &model = item.model;
        if (albedo) eve::debug::rtBind("texture", "albedo");
        if (shader) eve::debug::rtBind("shader", item.hair ? "hair" : "mesh");
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
        bool gpuDrivenUsed = false;
        if (gpuDrivenWanted && !useClustered && defaultCam && !opaque.empty()) {
            bool eligible = true;
            for (const CulledItem *it : opaque) {
                if (it->camIdx != 0 || !it->material || it->mr->camera != nullptr ||
                    it->material->effectiveShader() != nullptr ||
                    !gfx.gpuDrivenMaterialUsable(it->material)) {
                    eligible = false;
                    break;
                }
            }
            if (eligible) {
                const CameraView     &cv = cams[0];  // default camera is slot 0
                const Camera3D::Data *cd = cv.data;
                const glm::vec3       eye = cv.eye;
                gfx.setMesh3DViewProj(cv.viewProj);
                gfx.setMesh3DView(cv.view);
                gfx.setMesh3DClip(cd->nearZ, cd->farZ);
                gfx.setMesh3DCameraPos(cv.eye);
                gfx.setMesh3DEnv(cd->envMap, cd->envIntensity);
                gfx.setMesh3DEnvProbe(cd->envProbeCenter, cd->envProbeExtent);
                gfx.setMesh3DReflectionProbes(ReflectionProbeUpload{});
                ClusteredLightingUpload off{};
                off.active = false;
                gfx.setMesh3DClusteredLighting(off);
                gfx.setMesh3DLighting(cv.lighting);

                std::vector<eve::graphics::GpuInstance> instances;
                instances.reserve(opaque.size());
                bool recordsOk = true;
                bool vgAny     = false;
                const bool resolveWanted = gfx.gpuDrivenResolveWanted();
                for (const CulledItem *it : opaque) {
                    const ReflectionProbeUpload probes =
                        selectReflectionProbes(*cd, it->worldC);
                    // Stage 3 VG: meshes with a virtual-geometry asset are culled /
                    // drawn through the cluster path, not the instance chain.
                    const uint32_t vgAsset =
                        resolveWanted ? gfx.gpuDrivenVgAssetId(it->mesh)
                                      : eve::graphics::kInvalidGpuDrivenSlot;
                    if (vgAsset != eve::graphics::kInvalidGpuDrivenSlot && probes.count == 0) {
                        const uint32_t matId = gfx.gpuDrivenMaterialRecord(it->material);
                        if (matId == eve::graphics::kInvalidGpuDrivenSlot) {
                            recordsOk = false;
                            break;
                        }
                        vgAny |= gfx.gpuDrivenVgSetInstance(vgAsset, it->model, matId);
                        continue;
                    }
                    eve::graphics::GpuInstance inst{};
                    inst.model      = it->model;
                    inst.meshId     = gfx.gpuDrivenMeshRecord(it->mesh);
                    inst.materialId = gfx.gpuDrivenMaterialRecord(it->material);
                    inst.reflectionProbeSlots.z = uint32_t(probes.count);
                    for (int probeIndex = 0; probeIndex < probes.count; ++probeIndex) {
                        const auto &probe = probes.probes[probeIndex];
                        const uint32_t slot = gfx.gpuDrivenReflectionProbeSlot(probe.cubemap);
                        if (slot == eve::graphics::kInvalidGpuDrivenSlot) {
                            recordsOk = false;
                            break;
                        }
                        inst.reflectionProbeSlots[probeIndex] = slot;
                        inst.reflectionProbeCenter[probeIndex] =
                            glm::vec4(probe.center, probe.intensity);
                        inst.reflectionProbeExtent[probeIndex] =
                            glm::vec4(probe.extent, probe.blendDistance);
                    }
                    if (!recordsOk) break;
                    if (inst.meshId == eve::graphics::kInvalidGpuDrivenSlot ||
                        inst.materialId == eve::graphics::kInvalidGpuDrivenSlot) {
                        recordsOk = false;
                        break;
                    }
                    instances.push_back(inst);
                }
                if (recordsOk) {
                    // Stage 2: GPU frustum/HZB cull + GPU-written indirect commands.
                    if (gfx.gpuDrivenCullEnabled() && !instances.empty() &&
                        gfx.gpuDrivenCullBegin(instances.data(), uint32_t(instances.size()))) {
                        gfx.gpuDrivenCullEmit(cv.cullViewProj, eye, cd->fovYDeg, cd->nearZ,
                                              cd->farZ);
                        if (gfx.gpuDrivenResolveWanted()) {
                            // Stage 3: opaque goes to the GBuffer vis pass, the
                            // scene color pass runs the fullscreen resolve.
                            gfx.gpuDrivenRecordVisPass();
                            gfx.gpuDrivenOpenScenePass();
                            gfx.gpuDrivenResolve();
                        } else {
                            gfx.gpuDrivenOpenScenePass();
                            gfx.gpuDrivenDrawOpaque();
                        }
                        gpuDrivenUsed = true;
                    } else if (gfx.gpuDrivenCullEnabled() && instances.empty() && vgAny) {
                        // VG-only frame: no instance chain; the vis pass runs the
                        // VG cluster cull + draws + resolve.
                        gfx.gpuDrivenVgComputeSection(cv.cullViewProj, eye, cd->fovYDeg, cd->nearZ,
                                                      cd->farZ);
                        gfx.gpuDrivenRecordVisPass();
                        gfx.gpuDrivenOpenScenePass();
                        gfx.gpuDrivenResolve();
                        gpuDrivenUsed = true;
                    } else {
                        // Deferred scene pass must be open before stage-1 recording.
                        gfx.gpuDrivenOpenScenePass();
                        if (gfx.gpuDrivenSubmitOpaque(instances.data(),
                                                      uint32_t(instances.size())))
                            gpuDrivenUsed = true;
                    }
                }
            }
        }
        if (!gpuDrivenUsed) {
            if (gfx.gpuDrivenScenePassPending()) gfx.gpuDrivenOpenScenePass();
            for (const CulledItem *item : opaque)
                drawMeshWithMaterial(*item, cams[size_t(item->camIdx)]);
        }
    }
    if (doForward || doHair) {
        // Generic transparent surfaces belong to the forward pass; the legacy
        // hair pass remains independently switchable for hair materials.
        if (gfx.gpuDrivenScenePassPending()) gfx.gpuDrivenOpenScenePass();
        for (const CulledItem *item : transparentItems) {
            if ((item->hair && doHair) || (!item->hair && doForward))
                drawMeshWithMaterial(*item, cams[size_t(item->camIdx)]);
        }
    }

    const bool doAO = rc->isEnabled("ao");
    const bool doRTGI = rc->isEnabled("rtgi") || rc->isEnabled("reflectionChain");
    const bool doSSR = rc->isEnabled("ssr") || rc->isEnabled("reflectionChain");
    bool aoApplied = false;
    auto applyAO = [&]() {
        if (!doAO || !gfx.supportsGBufferPost() || !defaultCam || !gfx.had3DThisFrame()) return;
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
            // WebGPU cannot bind a depth-aspect texture to the generic 2D
            // post-process layout. Its G-buffer already carries equivalent
            // linear depth in a filterable RGBA target.
            Texture *depth = gfx.getBackendName() == "webgpu" ? gb->getDepthTexture()
                                                               : gb->getHwDepthTexture();
            if (depth)
                ao->applyFromGBuffer(&gfx, depth, gb->getNormalTexture());
            // Fullscreen SSGI from lit scene color reprints nearby props
            // (curtains, planters) onto the floor as multiple swimming ghosts.
            // Mesh shaders still add hemispheric sky/ground + wrap fill.
        }
        aoApplied = true;
    };

    const bool doReflectionLighting = doRTGI || doSSR;
    if (doReflectionLighting && defaultCam && gfx.had3DThisFrame()) {
        GBuffer *gb = rc->getGBuffer();
        if (gb && gb->isValid()) {
            Texture *sceneColor = gfx.getSceneColorTexture();
            Texture *depth = gfx.getBackendName() == "webgpu" ? gb->getDepthTexture()
                                                               : gb->getHwDepthTexture();
            if (sceneColor && depth) {
                DepthPyramid *depthPyramid = gfx.pipelineDepthPyramid();
                const std::string reflectionQuality = rc->getReflectionQuality();
                const int depthBudget = reflectionQuality == "low" ? 4 :
                                        reflectionQuality == "medium" ? 6 : 8;
                Texture *depthAtlas = depthPyramid->build(depth, depthBudget);
                const int depthLevels = depthPyramid->getLevelCount();
                auto cd = defaultCam->data();
                const float aspectSafe = aspect > 1e-4f ? aspect : 1.f;
                const float dw =
                    gfx.getCanvas() ? float(gfx.getCanvas()->getWidth()) : float(gfx.getWidth());
                const float dh =
                    gfx.getCanvas() ? float(gfx.getCanvas()->getHeight()) : float(gfx.getHeight());
                auto bindCam = [&](auto *fx) {
                    fx->setCamera(cd->eyeX, cd->eyeY, cd->eyeZ, cd->targetX, cd->targetY,
                                  cd->targetZ, cd->upX, cd->upY, cd->upZ, cd->fovYDeg, aspectSafe,
                                  cd->nearZ, cd->farZ);
                    if (!cams.empty()) fx->setInvViewProj(glm::inverse(cams.front().viewProj));
                };

                Texture *rtgiTexture = nullptr;
                Texture *ssrTexture = nullptr;

                if (doRTGI) {
                    GlobalIllumination *gi = gfx.pipelineGlobalIllumination();
                    if (gi->getQuality() != rc->getReflectionQuality())
                        gi->setQuality(rc->getReflectionQuality());
                    gi->setWorldNormalTexture(gb->getNormalTexture());
                    gi->setAlbedoTexture(gb->getAlbedoTexture());
                    gi->setTemporalMotionTexture(gb->getDepthTexture());
                    gi->setDepthPyramid(depthAtlas, depthLevels);
                    bindCam(gi);
                    Canvas *rtgiCanvas = gi->getWorkingCanvas();
                    if (rtgiCanvas) {
                        gi->applyFromSceneTo(&gfx, sceneColor, depth, rtgiCanvas);
                        rtgiTexture = gi->getWorkingTexture();
                    }
                }

                if (doSSR) {
                    ScreenSpaceReflection *ssr = gfx.pipelineScreenSpaceReflection();
                    if (ssr->getQuality() != rc->getReflectionQuality())
                        ssr->setQuality(rc->getReflectionQuality());
                    ssr->setEnabled(true);
                    ssr->setTemporalMotionTexture(gb->getDepthTexture());
                    ssr->setDepthPyramid(depthAtlas, depthLevels);
                    bindCam(ssr);
                    Canvas *reflCanvas = ssr->getReflectionCanvas();
                    if (reflCanvas) {
                        ssr->applyFromSceneTo(&gfx, sceneColor, depth, gb->getNormalTexture(),
                                              gb->getAlbedoTexture(), reflCanvas);
                        ssrTexture = ssr->getReflectionTexture();
                    }
                }

                // All offscreen reflection work is complete. Queue AO and the
                // reflection overlays only now, so a later setCanvas() cannot
                // force an early swapchain present with pending overlays.
                applyAO();
                if (rtgiTexture || ssrTexture) {
                    if (gfx.getBackendName() == "vulkan") {
                        if (rtgiTexture)
                            gfx.drawTexturedRectShaderUV(
                                rtgiTexture, nullptr, 0.f, 0.f, dw, dh, 0.f, 0.f, 1.f, 1.f,
                                Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Additive);
                        if (ssrTexture)
                            gfx.drawTexturedRectShaderUV(
                                ssrTexture, nullptr, 0.f, 0.f, dw, dh, 0.f, 0.f, 1.f, 1.f,
                                Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Additive);
                    } else if (Canvas *composite =
                                   gfx.pipelineReflectionComposite(int(dw), int(dh))) {
                        // Register before setCanvas(): switching away from the scene target
                        // closes its pass and queues the final resolve immediately.
                        gfx.setFinalSceneTexture(composite->getTexture());
                        Canvas *previous = gfx.getCanvas();
                        gfx.setCanvas(composite);
                        gfx.drawTexturedRectShaderUV(
                            sceneColor, nullptr, 0.f, 0.f, dw, dh, 0.f, 0.f, 1.f, 1.f,
                            Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Opaque);
                        if (rtgiTexture)
                            gfx.drawTexturedRectShaderUV(
                                rtgiTexture, nullptr, 0.f, 0.f, dw, dh, 0.f, 0.f, 1.f, 1.f,
                                Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Additive);
                        if (ssrTexture)
                            gfx.drawTexturedRectShaderUV(
                                ssrTexture, nullptr, 0.f, 0.f, dw, dh, 0.f, 0.f, 1.f, 1.f,
                                Color(1.f, 1.f, 1.f, 1.f), false,
                                BlendMode::Premultiplied);
                        gfx.setCanvas(previous);
                    }
                }
            }
        }
    }
    if (!aoApplied) applyAO();

    const bool doOutline = rc->isEnabled("outline");
    if (doOutline && defaultCam && gfx.had3DThisFrame()) {
        GBuffer *gb = rc->getGBuffer();
        if (gb && gb->isValid()) {
            Outline *outline = gfx.pipelineOutline();
            auto cd = defaultCam->data();
            outline->setClip(cd->nearZ, cd->farZ);
            Texture *depth = gfx.getBackendName() == "webgpu" ? gb->getDepthTexture()
                                                               : gb->getHwDepthTexture();
            if (depth)
                outline->apply(&gfx, depth, gb->getNormalTexture());
        }
    }
}

void RenderSystem3D::renderToCanvas(Graphics &gfx, Canvas *target, Camera3D *camera,
                                    uint32_t reflectionCaptureMask, float lodDistanceScale,
                                    bool includeTransparent, bool useClusteredLighting,
                                    Texture *skyFaceTexture, Mesh *skyQuad,
                                    float skyFaceTextureScale) {
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
    const glm::mat4 projM = cameraProjectionVulkanRH_ZO(
        cd->orthographic, fovRad, cd->orthoHeight, aspect, cd->nearZ, cd->farZ);
    gfx.setMesh3DViewProj(projM * viewM);
    gfx.setMesh3DView(viewM);
    gfx.setMesh3DClip(cd->nearZ, cd->farZ);
    gfx.setMesh3DCameraPos(eye);
    gfx.setMesh3DEnv(cd->envMap, cd->envIntensity);
    gfx.setMesh3DEnvProbe(cd->envProbeCenter, cd->envProbeExtent);
    gfx.setMesh3DReflectionProbes(selectReflectionProbes(*cd, eye));
    gfx.setSceneExposure(std::exp2(cd->exposureEV));
    gfx.setSceneAutoExposure(cd->autoExposure, cd->autoExposureMinEV,
                             cd->autoExposureMaxEV);
    gfx.setSceneBloom(cd->bloomIntensity, cd->bloomThreshold);

    // Lighting: real lights (if any) + camera ambient; shadows/clustered off.
    std::vector<PackedLight3D> packed;
    collectLights3D(packed, size_t(ClusteredLightConfig::kMaxLights));
    promoteDirectional(packed);
    bool clusteredCapture = false;
    if (useClusteredLighting && packed.size() > size_t(Lighting3DPack::kMaxLights)) {
        std::vector<ClusteredLightGpu> clusteredPoints;
        std::vector<ClusteredLightGpu> clusteredDirections;
        splitLights(packed, clusteredPoints, clusteredDirections);
        if (clusteredDirections.empty()) {
            ClusteredLightGpu direction{};
            direction.posRadius = glm::vec4(gLightDir, 0.f);
            direction.color = glm::vec4(gLightColor, 1.f);
            clusteredDirections.push_back(direction);
        }
        ClusteredLightingUpload clustered = buildClusteredLighting(
            clusteredPoints, clusteredDirections, viewM, cd->nearZ, cd->farZ,
            target->getWidth(), target->getHeight(),
            cd->fovYDeg * 0.017453292519943295f,
            glm::vec4(cd->ambientR, cd->ambientG, cd->ambientB, 0.f));
        clusteredCapture = clustered.active;
        gfx.setMesh3DClusteredLighting(clustered);
    } else {
        ClusteredLightingUpload noClustered{};
        noClustered.active = false;
        gfx.setMesh3DClusteredLighting(noClustered);
    }
    gfx.setMesh3DLighting(packLights3D(packed, cd.operator->()));
    ShadowUpload noShadow{};
    noShadow.active = false;
    gfx.setMesh3DShadows(noShadow);

    if (skyFaceTexture && skyQuad) {
        const float distance = std::max(cd->farZ * 0.999f, cd->nearZ * 2.f);
        glm::mat4 skyModel = glm::inverse(viewM);
        skyModel = glm::translate(skyModel, glm::vec3(0.f, 0.f, -distance));
        skyModel = glm::scale(skyModel, glm::vec3(distance * aspect * 1.01f,
                                                   distance * 1.01f, 1.f));
        Lighting3DPack unlit{};
        unlit.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
        gfx.setMesh3DClusteredActive(false);
        gfx.setMesh3DLighting(unlit);
        gfx.setMesh3DEnv(nullptr, 0.f);
        gfx.setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, false, 0.5f);
        gfx.setMesh3DMaterial(0.f, 1.f);
        gfx.setMesh3DShadowReceive(false);
        const float skyScale = std::max(skyFaceTextureScale, 0.f);
        gfx.drawMeshShader(skyQuad, skyModel, skyFaceTexture,
                           Color(skyScale, skyScale, skyScale, 1.f), nullptr);
        gfx.setMesh3DEnv(cd->envMap, cd->envIntensity);
        gfx.setMesh3DClusteredActive(clusteredCapture);
        gfx.setMesh3DLighting(packLights3D(packed, cd.operator->()));
    }

    const glm::mat4 captureViewProj = projM * viewM;
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        const glm::mat4 captureView = glm::lookAtRH(
            eye, glm::vec3(cd->targetX, cd->targetY, cd->targetZ),
            glm::vec3(cd->upX, cd->upY, cd->upZ));
        const glm::mat4 captureProjection = perspectiveVulkanRH_ZO(
            cd->fovYDeg * 0.017453292519943295f, aspect, cd->nearZ, cd->farZ);
        const FrustumPlanes captureFrustum = extractFrustum(captureProjection * captureView);
        struct CanvasItem {
            Renderable3D::MeshRenderer *mr = nullptr;
            Mesh *mesh = nullptr;
            Material *material = nullptr;
            glm::mat4 model{1.f};
            float distance2 = 0.f;
            SurfaceMode surface = SurfaceMode::Opaque;
            int sortPriority = 0;
        };
        std::vector<CanvasItem> canvasItems;
        auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [xf, mr] = *it;
            if (!mr->visible || (mr->reflectionCaptureMask & reflectionCaptureMask) == 0u)
                continue;
            const glm::mat4 model = modelFromTransform(*xf);
            const float maxScale =
                std::max(std::abs(xf->sx), std::max(std::abs(xf->sy), std::abs(xf->sz)));
            const float dx = xf->x - eye.x;
            const float dy = xf->y - eye.y;
            const float dz = xf->z - eye.z;
            const float distance2 = dx * dx + dy * dy + dz * dz;
            auto append = [&](Mesh *mesh, Material *material, bool hair) {
                if (!mesh) return;
                if (mesh->hasBounds()) {
                    const glm::vec4 worldCenter =
                        model * glm::vec4(mesh->boundsCx, mesh->boundsCy, mesh->boundsCz, 1.f);
                    if (!captureFrustum.sphereVisible(glm::vec3(worldCenter),
                                                      mesh->boundsRadius * maxScale))
                        return;
                }
                const SurfaceMode surface = material ? material->surfaceMode()
                                                     : (hair ? SurfaceMode::Transparent
                                                             : SurfaceMode::Opaque);
                if (surface == SurfaceMode::Transparent && !includeTransparent) return;
                const int sortPriority = material ? material->getSortPriority() : 0;
                canvasItems.push_back(
                    CanvasItem{mr, mesh, material, model, distance2, surface, sortPriority});
            };
            if (mr->usesParts()) {
                for (int part = 0; part < mr->partCount; ++part) {
                    Material *material =
                        mr->parts[part].material ? mr->parts[part].material : mr->material;
                    append(mr->parts[part].mesh, material,
                           material ? material->isTransparentHair() : mr->isHair);
                }
            } else {
                const float distance = std::sqrt(distance2) * std::max(lodDistanceScale, 0.01f);
                append(mr->meshForDistance(distance), mr->material,
                       mr->material ? mr->material->isTransparentHair() : mr->isHair);
            }
        }

        std::stable_sort(canvasItems.begin(), canvasItems.end(),
                         [](const CanvasItem &a, const CanvasItem &b) {
                             const bool transparentA = a.surface == SurfaceMode::Transparent;
                             const bool transparentB = b.surface == SurfaceMode::Transparent;
                             if (transparentA != transparentB) return !transparentA;
                             if (transparentA) {
                                 if (a.sortPriority != b.sortPriority)
                                     return a.sortPriority < b.sortPriority;
                                 if (a.distance2 != b.distance2) return a.distance2 > b.distance2;
                             }
                             if (a.material != b.material) return a.material < b.material;
                             return a.mesh < b.mesh;
                         });

        bool lightingEnabled = true;
        for (const CanvasItem &item : canvasItems) {
            Renderable3D::MeshRenderer *mr = item.mr;
            Texture *albedo = mr->texture;
            Color tint(mr->r, mr->g, mr->b, mr->a);
            Shader *shader = mr->shader;
            const bool lit = item.material ? item.material->getReceiveLight() : mr->receiveLight;
            if (lit != lightingEnabled) {
                if (lit) {
                    gfx.setMesh3DClusteredActive(clusteredCapture);
                    gfx.setMesh3DLighting(packLights3D(packed, cd.operator->()));
                } else {
                    gfx.setMesh3DClusteredActive(false);
                    Lighting3DPack unlit{};
                    unlit.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
                    gfx.setMesh3DLighting(unlit);
                }
                lightingEnabled = lit;
            }
            if (item.material) {
                item.material->bind(gfx);
                albedo = item.material->getAlbedoTexture();
                tint = Color(item.material->getTintR(), item.material->getTintG(),
                             item.material->getTintB(), item.material->getTintA());
                shader = item.material->effectiveShader();
            } else {
                gfx.setMesh3DSurface(item.surface, BlendMode::Alpha, false, mr->isHair, 0.5f);
                gfx.setMesh3DMaterial(mr->metallic, mr->roughness);
                gfx.setMesh3DTexCellBomb(mr->texBombScale, mr->texBombStrength, mr->texBombRot);
                gfx.setMesh3DNormalTexture(mr->normalTexture);
                gfx.setMesh3DHeightTexture(mr->heightTexture);
                gfx.setMesh3DParallax(mr->parallaxScale, mr->parallaxMinLayers,
                                      mr->parallaxMaxLayers);
            }
            gfx.setMesh3DShadowReceive(false);
            eve::debug::rtDraw("drawMeshShader", shader ? "custom" : "default");
            gfx.drawMeshShader(item.mesh, item.model, albedo, tint, shader);
        }
    }

    // Copy the list so a contributor may safely unregister itself while drawing.
    const auto captureDrawers = g_captureDrawers;
    for (const CaptureExtraDrawerEntry &entry : captureDrawers) {
        if ((entry.mask & reflectionCaptureMask) == 0u || !entry.drawer) continue;
        entry.drawer(gfx, *cd, captureViewProj, aspect, reflectionCaptureMask);
    }

    gfx.end3DFrameToCanvas();
}

} // namespace eve::graphics
