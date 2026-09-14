#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
// Per-frame, per-draw storage: queued draws own snapshots until the frame fence.
// A slot can grow only before that slot has been referenced by this frame.
size_t Graphics::uploadSkinPalette(Mesh* mesh, Mesh3dFrameSlots& fslots) {
    const bool   skinned = mesh && mesh->hasGpuSkinning() && mesh->getSkinPaletteCount() > 0;
    const size_t slot    = skinned ? fslots.drawIndex + 1 : 0;
    const size_t bytes   = skinned ? mesh->skinPalette().size() * sizeof(float) : sizeof(glm::mat4);
    if (bytes > device.physical_device.properties.limits.maxStorageBufferRange)
        throw Exception("Skin palette exceeds device maxStorageBufferRange");
    if (fslots.palettes.size() <= slot) fslots.palettes.resize(slot + 1);
    auto& buffer = fslots.palettes[slot];
    if (!buffer.buffer || buffer.capacity < bytes) {
        buffer.allocate(frameToken(), device, vk::BufferUsageFlagBits::eStorageBuffer, bytes, kHostVisibleCoherent);
        // The fence protects earlier frames. Other slots used this frame retain
        // their buffers and published descriptor sets unchanged.
        fslots.sets.clear();
        fslots.skinSets.clear();
    }
    const glm::mat4 identity(1.f);
    updateRingLocal(buffer, 0, skinned ? mesh->skinPalette().data() : &identity[0][0], bytes);
    fslots.activePalette = slot;
    return slot;
}

vk::DescriptorSet Graphics::skinPassSetFor(GpuTexture* albedo, Mesh3dFrameSlots& fslots) {
    const auto key = std::make_pair(fslots.activePalette, albedo);
    auto       it  = fslots.skinSets.find(key);
    if (it != fslots.skinSets.end()) return it->second;
    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool     = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &skinPassSetLayout;
    vkb::UnboundSet           unbound{device->allocateDescriptorSets(alloc).front()};
    vkb::DescriptorSetUpdater updater(2, 1, 0);
    updater.beginDescriptorSet(unbound)
        .beginBuffers(0, 0, vk::DescriptorType::eUniformBufferDynamic)
        .buffer(fslots.uboRing.buffer, 0, sizeof(SkinPassUBO))
        .beginBuffers(2, 0, vk::DescriptorType::eStorageBuffer)
        .buffer(fslots.palettes[fslots.activePalette].buffer, 0, fslots.palettes[fslots.activePalette].capacity)
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(albedo->sampler, albedo->imageView()))
        .update(device.instance);
    vkb::BoundSet bound      = std::move(unbound).publish();
    auto [inserted, ignored] = fslots.skinSets.emplace(key, bound);
    return inserted->second;
}

bool Graphics::prepareSkinPass(Mesh* mesh, Texture* albedo, const glm::mat4& mvp, const glm::mat4& model,
                               const glm::vec4& clip, vk::DescriptorSet& set, uint32_t& uboOffset) {
    if (!mesh || !mesh->hasGpuSkinning()) return false;
    auto& fslots = currentMesh3dFrameSlots();
    // A 3D frame can inherit an already-open present command buffer (for
    // example, when script-side 2D clear work precedes render3D).  In that
    // path begin3DFrame may not have initialized this slot yet.  Allocate the
    // empty slot lazily before the first skinned draw; no recorded command can
    // reference it while capacity is zero.
    if (!fslots.uboRing.buffer || fslots.capacity == 0) ensureMesh3dRing(fslots);
    if (fslots.drawIndex >= fslots.capacity) {
        std::fprintf(stderr, "[vulkan] skin pass UBO ring exhausted (%zu draws); draw skipped\n", fslots.capacity);
        return false;
    }
    SkinPassUBO ubo;
    ubo.mvp        = mvp;
    ubo.model      = model;
    ubo.clip       = clip;
    ubo.skinInfo.x = static_cast<float>(mesh->getSkinPaletteCount());
    uploadSkinPalette(mesh, fslots);
    const size_t slot = fslots.drawIndex++;
    ensureMesh3dStrides();
    uboOffset = uint32_t(slot) * mesh3dUboStride;
    updateRingLocal(fslots.uboRing, uboOffset, &ubo, sizeof(ubo));
    Texture* texture = albedo ? albedo : whiteTexture;
    if (!texture || !texture->gpuHandle) return false;
    set = skinPassSetFor(static_cast<GpuTexture*>(texture->gpuHandle), fslots);
    return bool(set);
}


}  // namespace eve::graphics::vulkan
