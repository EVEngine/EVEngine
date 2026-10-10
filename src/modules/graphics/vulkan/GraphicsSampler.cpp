#include <algorithm>
#include <iterator>
#include "graphics/Texture.h"
#include "graphics/vulkan/Graphics.h"

namespace eve::graphics::vulkan {
void Graphics::setTextureSampler(Texture *texture, const TextureSampler &sampler) {
    if (!texture || texture->sampler == sampler) return;
    if (std::none_of(ownedTextures.begin(), ownedTextures.end(),
                     [texture](const auto &owned) { return owned.get() == texture; }))
        return;
    texture->sampler       = sampler;
    samplerChangesPending_ = true;
}

void Graphics::applyPendingResourceChanges() {
    if (!samplerChangesPending_) return;
    bool changed = false;
    for (auto &texture : ownedTextures) {
        if (!texture->gpuHandle) continue;
        auto *gpu = static_cast<GpuTexture *>(texture->gpuHandle);
        if (gpu->samplerState == texture->sampler) continue;
        auto found = std::find_if(cachedSamplers_.begin(), cachedSamplers_.end(), [&](const CachedSampler &entry) {
            return entry.mipLevels == gpu->mipLevels && entry.state == texture->sampler;
        });
        if (found == cachedSamplers_.end()) {
            cachedSamplers_.push_back(
                {texture->sampler, gpu->mipLevels,
                 vk::UniqueSampler(createVkSampler(texture->sampler, gpu->mipLevels), device.instance)});
            found = std::prev(cachedSamplers_.end());
        }
        if (!changed) waitForSharedGpuResources();
        if (gpu->sampler && !gpu->sharedSampler) device->destroySampler(gpu->sampler);
        gpu->sampler       = *found->sampler;
        gpu->sharedSampler = true;
        if (!gpu->isCube) writeCombinedImageDescriptor(gpu);
        gpu->samplerState = texture->sampler;
        changed           = true;
    }
    samplerChangesPending_ = false;
    if (changed) invalidateTextureBindings();
}
}  // namespace eve::graphics::vulkan
