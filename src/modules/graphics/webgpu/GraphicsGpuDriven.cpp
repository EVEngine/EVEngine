#include "graphics/webgpu/Graphics.h"

#include "graphics/Material.h"

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

}  // namespace eve::graphics::webgpu
