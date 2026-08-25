#include "graphics/webgpu/Graphics.h"

#include "graphics/Material.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_access.hpp>

namespace eve::graphics::webgpu {

uint32_t Graphics::gpuDrivenMeshRecord(Mesh *mesh) {
    if (!mesh || !mesh->gpuHandle) return kInvalidGpuDrivenSlot;
    auto found = gpuDrivenMeshIds_.find(mesh);
    if (found != gpuDrivenMeshIds_.end()) return found->second;
    const uint32_t id = static_cast<uint32_t>(gpuDrivenMeshes_.size());
    gpuDrivenMeshes_.push_back(mesh);
    gpuDrivenMeshIds_.emplace(mesh, id);
    return id;
}

uint32_t Graphics::gpuDrivenMaterialRecord(Material *material) {
    if (!gpuDrivenMaterialUsable(material)) return kInvalidGpuDrivenSlot;
    auto found = gpuDrivenMaterialIds_.find(material);
    if (found != gpuDrivenMaterialIds_.end()) return found->second;
    const uint32_t id = static_cast<uint32_t>(gpuDrivenMaterials_.size());
    gpuDrivenMaterials_.push_back(material);
    gpuDrivenMaterialIds_.emplace(material, id);
    return id;
}

bool Graphics::gpuDrivenMaterialUsable(Material *material) {
    if (!material || material->surfaceMode() != SurfaceMode::Opaque ||
        material->effectiveShader() != nullptr || material->isTransparentHair())
        return false;
    const std::string model = material->getShadingModel();
    return model == "pbr" && material->getReceiveLight();
}

bool Graphics::gpuDrivenSubmitOpaque(const GpuInstance *instances, uint32_t instanceCount) {
    if (!gpuDrivenEnabled_ || !instances || instanceCount == 0) return false;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        const GpuInstance &instance = instances[i];
        if (instance.meshId >= gpuDrivenMeshes_.size() ||
            instance.materialId >= gpuDrivenMaterials_.size())
            return false;
        Mesh *mesh = gpuDrivenMeshes_[instance.meshId];
        Material *material = gpuDrivenMaterials_[instance.materialId];
        if (!mesh || !material || !gpuDrivenMaterialUsable(material)) return false;
        material->bind(*this);
        drawMeshShader(mesh, instance.model, material->getAlbedoTexture(),
                       Color(material->getTintR(), material->getTintG(), material->getTintB(),
                             material->getTintA()),
                       nullptr);
    }
    return true;
}

bool Graphics::gpuDrivenCullBegin(const GpuInstance *instances, uint32_t instanceCount) {
    if (!gpuDrivenEnabled_ || !instances || instanceCount == 0) return false;
    gpuDrivenPending_.assign(instances, instances + instanceCount);
    gpuDrivenVisible_.clear();
    return true;
}

void Graphics::gpuDrivenCullEmit(const glm::mat4 &viewProj, const glm::vec3 &eye, float fovYDeg,
                                 float nearZ, float farZ) {
    (void)eye;
    (void)fovYDeg;
    (void)nearZ;
    (void)farZ;
    glm::vec4 planes[6] = {
        glm::row(viewProj, 3) + glm::row(viewProj, 0),
        glm::row(viewProj, 3) - glm::row(viewProj, 0),
        glm::row(viewProj, 3) + glm::row(viewProj, 1),
        glm::row(viewProj, 3) - glm::row(viewProj, 1),
        glm::row(viewProj, 2),
        glm::row(viewProj, 3) - glm::row(viewProj, 2),
    };
    for (glm::vec4 &plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1e-6f) plane /= length;
    }
    for (const GpuInstance &instance : gpuDrivenPending_) {
        if (instance.meshId >= gpuDrivenMeshes_.size()) continue;
        Mesh *mesh = gpuDrivenMeshes_[instance.meshId];
        if (!mesh || !mesh->hasBounds()) {
            gpuDrivenVisible_.push_back(instance);
            continue;
        }
        const glm::vec3 center = glm::vec3(
            instance.model * glm::vec4(mesh->boundsCx, mesh->boundsCy, mesh->boundsCz, 1.f));
        const float sx = glm::length(glm::vec3(instance.model[0]));
        const float sy = glm::length(glm::vec3(instance.model[1]));
        const float sz = glm::length(glm::vec3(instance.model[2]));
        const float radius = mesh->boundsRadius * std::max({sx, sy, sz});
        bool visible = true;
        for (const glm::vec4 &plane : planes) {
            if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
                visible = false;
                break;
            }
        }
        if (visible) gpuDrivenVisible_.push_back(instance);
    }
}

void Graphics::gpuDrivenDrawOpaque() {
    if (!gpuDrivenVisible_.empty())
        gpuDrivenSubmitOpaque(gpuDrivenVisible_.data(),
                              static_cast<uint32_t>(gpuDrivenVisible_.size()));
    gpuDrivenPending_.clear();
}

}  // namespace eve::graphics::webgpu
