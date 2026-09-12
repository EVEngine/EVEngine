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

void Renderable3D::clearMeshLod() {
    auto mr      = meshRenderer();
    mr->lodCount = 0;
    for (int i = 0; i < MeshRenderer::kMaxLodLevels; ++i) mr->lodMeshes[i] = nullptr;
}

int Renderable3D::getMeshLodCount() { return meshRenderer()->lodCount; }

int Renderable3D::getMeshLodLevelAtDistance(float distance) { return meshRenderer()->lodLevelForDistance(distance); }


}  // namespace eve::graphics
