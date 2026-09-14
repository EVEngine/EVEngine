#include <algorithm>
#include <cmath>
#include "common/Exception.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"

namespace eve::graphics {
void Renderable3D::setPosition(float x, float y, float z) {
    auto t = transform();
    t->x   = x;
    t->y   = y;
    t->z   = z;
}

void Renderable3D::setRotation(float yaw, float pitch, float roll) {
    auto t   = transform();
    t->yaw   = yaw;
    t->pitch = pitch;
    t->roll  = roll;
}

void Renderable3D::setYaw(float yaw) { transform()->yaw = yaw; }

float Renderable3D::getYaw() { return transform()->yaw; }

void Renderable3D::setScale(float sx, float sy, float sz) {
    auto t = transform();
    t->sx  = sx;
    t->sy  = sy;
    t->sz  = sz;
}

void Renderable3D::setMesh(Mesh* mesh) { meshRenderer()->mesh = mesh; }

Mesh* Renderable3D::getMesh() { return meshRenderer()->mesh; }

void Renderable3D::setTexture(Texture* texture) { meshRenderer()->texture = texture; }

void Renderable3D::setNormalTexture(Texture* texture) { meshRenderer()->normalTexture = texture; }

void Renderable3D::setHeightTexture(Texture* texture) { meshRenderer()->heightTexture = texture; }

void Renderable3D::setShader(Shader* shader) { meshRenderer()->shader = shader; }

void Renderable3D::setMaterial(Material* material) { meshRenderer()->material = material; }

Material* Renderable3D::getMaterial() { return meshRenderer()->material; }

void Renderable3D::setXRayShader(Shader* shader) { meshRenderer()->xrayShader = shader; }

Shader* Renderable3D::getXRayShader() { return meshRenderer()->xrayShader; }

void Renderable3D::setXRayHighlight(bool on) { meshRenderer()->xrayHighlight = on; }

bool Renderable3D::getXRayHighlight() { return meshRenderer()->xrayHighlight; }

void Renderable3D::setPart(int index, const std::string& name, Mesh* mesh, Material* material) {
    auto mr = meshRenderer();
    if (index < 0 || index >= MeshRenderer::kMaxParts) return;
    mr->parts[index].name     = name;
    mr->parts[index].mesh     = mesh;
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
    auto mr       = meshRenderer();
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

Mesh* Renderable3D::getPartMesh(int index) {
    auto mr = meshRenderer();
    if (index < 0 || index >= mr->partCount) return nullptr;
    return mr->parts[index].mesh;
}

Material* Renderable3D::getPartMaterial(int index) {
    auto mr = meshRenderer();
    if (index < 0 || index >= mr->partCount) return nullptr;
    return mr->parts[index].material;
}

void Renderable3D::setHair(bool hair) { meshRenderer()->isHair = hair; }

bool Renderable3D::getHair() { return meshRenderer()->isHair; }

void Renderable3D::setTint(float r, float g, float b, float a) {
    auto mr = meshRenderer();
    mr->r   = r;
    mr->g   = g;
    mr->b   = b;
    mr->a   = a;
}

void Renderable3D::setMetallic(float metallic) { meshRenderer()->metallic = metallic; }

void Renderable3D::setRoughness(float roughness) { meshRenderer()->roughness = roughness; }

void Renderable3D::setTexCellBomb(float cellScale, float strength, float rotAmount) {
    auto mr             = meshRenderer();
    mr->texBombScale    = cellScale > 1e-3f ? cellScale : 1e-3f;
    mr->texBombStrength = strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength);
    mr->texBombRot      = rotAmount < 0.f ? 0.f : (rotAmount > 1.f ? 1.f : rotAmount);
}

float Renderable3D::getTexCellBombScale() { return meshRenderer()->texBombScale; }

float Renderable3D::getTexCellBombStrength() { return meshRenderer()->texBombStrength; }

float Renderable3D::getTexCellBombRotation() { return meshRenderer()->texBombRot; }

void Renderable3D::setParallax(float scale, float minLayers, float maxLayers) {
    auto mr           = meshRenderer();
    mr->parallaxScale = scale < 0.f ? 0.f : (scale > 0.25f ? 0.25f : scale);
    float minL        = minLayers < 1.f ? 1.f : minLayers;
    float maxL        = maxLayers < minL ? minL : maxLayers;
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

int Renderable3D::getReflectionCaptureMask() { return static_cast<int>(meshRenderer()->reflectionCaptureMask); }

void Renderable3D::setReceiveLight(bool receive) { meshRenderer()->receiveLight = receive; }

void Renderable3D::setCastShadow(bool cast) { meshRenderer()->castShadow = cast; }

void Renderable3D::setReceiveShadow(bool receive) { meshRenderer()->receiveShadow = receive; }

void Renderable3D::setCastOcclusion(bool cast) { meshRenderer()->castOcclusion = cast; }

bool Renderable3D::getCastOcclusion() { return meshRenderer()->castOcclusion; }

void Renderable3D::setCamera(Camera3D* camera) { meshRenderer()->camera = camera; }

void Renderable3D::setMeshLod(int index, Mesh* mesh, float switchDistance) {
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

void Renderable3D::MeshRenderer::lodBlendForDistance(float distance, int& primary, int& secondary,
                                                      float& secondaryWeight) const {
    primary = lodLevelForDistance(distance);
    secondary = -1;
    secondaryWeight = 0.f;
    if (lodFadeMode == 0 || lodCount <= 0) return;
    if (lodAnimateCrossFading && lodAnimatedCurrent != -2) {
        primary = lodAnimatedPrevious == -2 ? lodAnimatedCurrent : lodAnimatedPrevious;
        secondary = lodAnimatedCurrent;
        secondaryWeight = std::clamp(lodAnimatedProgress, 0.f, 1.f);
        if (primary == secondary || secondaryWeight >= 1.f) {
            primary = secondary;
            secondary = -1;
            secondaryWeight = 0.f;
        }
        return;
    }
    for (int level = 0; level < lodCount; ++level) {
        const float boundary = level + 1 < lodCount ? lodDistances[level] : lodCullDistance;
        if (!(boundary > 0.f)) continue;
        const float start = level == 0 ? 0.f : lodDistances[level - 1];
        const float width = (boundary - start) * std::clamp(lodFadeWidths[level], 0.f, 1.f);
        if (width > 0.f && distance >= boundary - width && distance < boundary) {
            primary = level;
            secondary = level + 1 < lodCount ? level + 1 : -1;
            secondaryWeight = std::clamp((distance - (boundary - width)) / width, 0.f, 1.f);
            return;
        }
    }
}

void Renderable3D::setMeshLodCullDistance(float distance) {
    meshRenderer()->lodCullDistance = std::isfinite(distance) && distance > 0.f ? distance : 0.f;
}

void Renderable3D::clearMeshLod() {
    auto mr      = meshRenderer();
    mr->lodCount = 0;
    for (int i = 0; i < MeshRenderer::kMaxLodLevels; ++i) mr->lodMeshes[i] = nullptr;
}

int Renderable3D::getMeshLodCount() { return meshRenderer()->lodCount; }

int Renderable3D::getMeshLodLevelAtDistance(float distance) { return meshRenderer()->lodLevelForDistance(distance); }


Result<void> Renderable3D::setMeshLodRendererState(int index, int skinQuality, int shadowCastingMode,
                                                   bool receiveShadows, int motionVectorMode,
                                                   bool skinnedMotionVectors, int lightProbeUsage,
                                                   int reflectionProbeUsage) {
    const bool validSkin = skinQuality == 0 || skinQuality == 1 || skinQuality == 2 || skinQuality == 4;
    const bool validLight = lightProbeUsage == 0 || lightProbeUsage == 1 || lightProbeUsage == 2 ||
                            lightProbeUsage == 4;
    if (index < 0 || index >= MeshRenderer::kMaxLodLevels || !validSkin || shadowCastingMode < 0 ||
        shadowCastingMode > 3 || motionVectorMode < 0 || motionVectorMode > 2 || !validLight ||
        reflectionProbeUsage < 0 || reflectionProbeUsage > 2)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "mesh LOD renderer state is invalid", "graphics.renderable3d.lod"));
    auto& state = meshRenderer()->lodRendererStates[index];
    state.configured = true;
    state.skinQuality = skinQuality;
    state.shadowCastingMode = shadowCastingMode;
    state.receiveShadows = receiveShadows;
    state.motionVectorMode = motionVectorMode;
    state.skinnedMotionVectors = skinnedMotionVectors;
    state.lightProbeUsage = lightProbeUsage;
    state.reflectionProbeUsage = reflectionProbeUsage;
    return Result<void>::success();
}

Result<void> Renderable3D::setMeshLodFadeWidth(int index, float width) {
    if (index < 0 || index >= MeshRenderer::kMaxLodLevels || !std::isfinite(width) || width < 0.f || width > 1.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "mesh LOD fade width is invalid", "graphics.renderable3d.lod"));
    meshRenderer()->lodFadeWidths[index] = width;
    return Result<void>::success();
}

Result<void> Renderable3D::setMeshLodFadePolicy(int mode, bool animate, float duration) {
    if (mode < 0 || mode > 2 || !std::isfinite(duration) || duration <= 0.f || (animate && mode == 0))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "mesh LOD fade policy is invalid", "graphics.renderable3d.lod"));
    auto mr = meshRenderer();
    mr->lodFadeMode = mode;mr->lodAnimateCrossFading = animate;mr->lodCrossFadeDuration = duration;
    mr->lodAnimatedCurrent = -2;mr->lodAnimatedPrevious = -2;mr->lodAnimatedProgress = 1.f;
    return Result<void>::success();
}

int Renderable3D::getMeshLodRendererState(int index, int field) {
    auto mr = meshRenderer();
    if (index < 0 || index >= mr->lodCount || field < 0 || field > 6) return -1;
    const auto& state = mr->lodRendererStates[index];
    switch (field) {
        case 0: return state.skinQuality;
        case 1: return state.shadowCastingMode;
        case 2: return state.receiveShadows ? 1 : 0;
        case 3: return state.motionVectorMode;
        case 4: return state.skinnedMotionVectors ? 1 : 0;
        case 5: return state.lightProbeUsage;
        case 6: return state.reflectionProbeUsage;
        default: return -1;
    }
}

Result<void> Renderable3D::advanceMeshLodTransition(float distance, float dt) {
    if (!std::isfinite(distance) || distance < 0.f || !std::isfinite(dt) || dt < 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "mesh LOD transition inputs are invalid", "graphics.renderable3d.lod"));
    auto mr = meshRenderer();const int target = mr->lodLevelForDistance(distance);
    if (mr->lodAnimatedCurrent == -2) {mr->lodAnimatedCurrent=target;mr->lodAnimatedProgress=1.f;return Result<void>::success();}
    if (target != mr->lodAnimatedCurrent) {
        mr->lodAnimatedPrevious=mr->lodAnimatedCurrent;mr->lodAnimatedCurrent=target;mr->lodAnimatedProgress=0.f;
    }
    if (mr->lodAnimatedPrevious != -2 && mr->lodAnimatedPrevious != mr->lodAnimatedCurrent) {
        mr->lodAnimatedProgress=std::min(1.f,mr->lodAnimatedProgress+dt/mr->lodCrossFadeDuration);
        if (mr->lodAnimatedProgress>=1.f)mr->lodAnimatedPrevious=-2;
    }
    return Result<void>::success();
}

int Renderable3D::getMeshLodSecondaryLevelAtDistance(float distance) {
    int primary=-1,secondary=-1;float weight=0.f;meshRenderer()->lodBlendForDistance(distance,primary,secondary,weight);
    return secondary;
}

float Renderable3D::getMeshLodSecondaryWeightAtDistance(float distance) {
    int primary=-1,secondary=-1;float weight=0.f;meshRenderer()->lodBlendForDistance(distance,primary,secondary,weight);
    return weight;
}

float Renderable3D::getMeshLodCullDistance() { return meshRenderer()->lodCullDistance; }

void Renderable3D::setLayer(int layer) {
    EV_PARAM_CHECK(layer >= 0, "layer must be in [0,31]");
    EV_PARAM_CHECK(layer < 32, "layer must be in [0,31]");
    meshRenderer()->layer = layer;
}

int Renderable3D::getLayer() { return meshRenderer()->layer; }

Result<void> Renderable3D::setCustomLightProbe(float r, float g, float b) {
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || r < 0.f || g < 0.f || b < 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "custom light probe irradiance must be finite and nonnegative", "graphics.renderable3d.lightProbe"));
    meshRenderer()->customLightProbe = true;
    meshRenderer()->customLightProbeSh = {};
    constexpr float kY00 = 0.2820947918f;
    meshRenderer()->customLightProbeSh[0] = glm::vec4(r / kY00, g / kY00, b / kY00, 0.f);
    return Result<void>::success();
}

Result<void> Renderable3D::setCustomLightProbeCoefficient(int coefficient, float r, float g, float b) {
    if (coefficient < 0 || coefficient >= 9 || !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "custom light probe coefficient requires index 0..8 and finite RGB", "graphics.renderable3d.lightProbe"));
    meshRenderer()->customLightProbe = true;
    meshRenderer()->customLightProbeSh[static_cast<size_t>(coefficient)] = glm::vec4(r, g, b, 0.f);
    return Result<void>::success();
}

void Renderable3D::clearCustomLightProbe() {
    meshRenderer()->customLightProbe = false;
    meshRenderer()->customLightProbeSh = {};
}

}  // namespace eve::graphics
