#include "common/Exception.h"
#include "graphics/webgpu/Graphics.h"

namespace eve::graphics::webgpu {
wgpu::Buffer Graphics::uploadSkinPalette(Mesh* mesh) {
    const bool     skinned = mesh && mesh->hasGpuSkinning() && mesh->getSkinPaletteCount() > 0;
    const uint64_t bytes   = skinned ? mesh->skinPalette().size() * sizeof(float) : sizeof(glm::mat4);
    wgpu::Limits   limits{};
    if (device.GetLimits(&limits) != wgpu::Status::Success || bytes > limits.maxStorageBufferBindingSize)
        throw Exception("Skin palette exceeds device maxStorageBufferBindingSize");
    auto&        arena = currentUboArena();
    const size_t slot  = arena.paletteIndex++;
    if (arena.palettes.size() <= slot) arena.palettes.resize(slot + 1);
    auto& buffer = arena.palettes[slot];
    if (!buffer || buffer.GetSize() < bytes) {
        wgpu::BufferDescriptor desc{};
        desc.size  = bytes;
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        buffer     = device.CreateBuffer(&desc);
    }
    const glm::mat4 identity(1.f);
    queue.WriteBuffer(buffer, 0, skinned ? mesh->skinPalette().data() : &identity[0][0], bytes);
    return buffer;
}
}  // namespace eve::graphics::webgpu
