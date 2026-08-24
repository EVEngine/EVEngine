// Vulkan backend implementation — texture creation, upload and reload.
//
// Split out of Graphics2D.cpp (pure move; shared helpers live in
// GraphicsInternal.h). Keep the include list tight: the embedded shader
// .inc arrays are unused here and would trip -Wunused-const-variable under
// strict-warning CI builds.

#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

#include "common/Exception.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace eve::graphics::vulkan {

void Graphics::writeCombinedImageDescriptor(GpuTexture *gpu) {
    if (!gpu || !gpu->descriptorSet || !gpu->sampler) return;
    vk::ImageView view = gpu->imageView();
    if (!view) return;
    vkb::UnboundSet unbound = vkb::UnboundSet::reopenAfterIdle(gpu->descriptorSet);
    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpu->sampler, view))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpu->sampler, view))
        .update(device.instance);
    gpu->descriptorSet = std::move(unbound).publish();
}

Texture *Graphics::newTexture(int w, int h, const uint8_t *rgba, bool repeatU, bool repeatV) {
    TextureCreateInfo info;
    info.sampler.repeatU = repeatU;
    info.sampler.repeatV = repeatV;
    return newTexture(w, h, rgba, info);
}

Texture *Graphics::newTexture(int w, int h, const uint8_t *rgba, const TextureCreateInfo &rawInfo) {
    ASSERT(initialized);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    ASSERT(rgba != nullptr);
    if (!initialized) throw Exception("newTexture: graphics not initialized");
    if (w <= 0 || h <= 0 || !rgba) throw Exception("newTexture: invalid args");

    TextureCreateInfo info = normalizeTextureInfo(rawInfo);
    const uint32_t mipLevels =
        info.generateMipmaps ? uint32_t(mipmapCountForSize(w, h)) : 1u;

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = w;
    gpu->height = h;
    gpu->isCube = false;
    gpu->mipLevels = mipLevels;
    gpu->samplerState = info.sampler;
    gpu->image = vkb::TextureImage2D(device, uint32_t(w), uint32_t(h), mipLevels);

    std::vector<uint8_t> bytes =
        (mipLevels > 1) ? buildMipChain2D(rgba, uint32_t(w), uint32_t(h), mipLevels)
                        : std::vector<uint8_t>(rgba, rgba + size_t(w) * size_t(h) * 4);
    gpu->image.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);

    gpu->descriptorSet = vkb::BoundSet{sets[0]};
    writeCombinedImageDescriptor(gpu.get());
    registerBindlessTexture2D(gpu.get());

    auto tex = std::make_unique<Texture>();
    tex->width = w;
    tex->height = h;
    tex->pixelWidth = w;
    tex->pixelHeight = h;
    tex->mipmapCount = int(mipLevels);
    tex->sampler = info.sampler;
    tex->gpuHandle = gpu.get();

    Texture *raw = tex.get();
    ownedTextures.push_back(std::move(tex));
    ownedGpuTextures.push_back(std::move(gpu));
    return raw;
}

Texture *Graphics::newCubemap(int faceSize, const uint8_t *rgbaFaces) {
    // IBL shaders use textureLod(roughness * 5); generate mips by default.
    return newCubemap(faceSize, rgbaFaces, TextureCreateInfo::withMipmaps(false));
}

Texture *Graphics::newCubemap(int faceSize, const uint8_t *rgbaFaces,
                              const TextureCreateInfo &rawInfo) {
    ASSERT(initialized);
    ASSERT_GT(faceSize, 0);
    ASSERT(rgbaFaces != nullptr);
    if (!initialized) throw Exception("newCubemap: graphics not initialized");
    if (faceSize <= 0 || !rgbaFaces) throw Exception("newCubemap: invalid args");

    TextureCreateInfo info = normalizeTextureInfo(rawInfo);
    info.sampler.repeatU = false;
    info.sampler.repeatV = false;
    info.sampler.repeatW = false;
    const uint32_t mipLevels =
        info.generateMipmaps ? uint32_t(mipmapCountForSize(faceSize, faceSize)) : 1u;

    const size_t faceBytes = size_t(faceSize) * size_t(faceSize) * 4u;
    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = faceSize;
    gpu->height = faceSize;
    gpu->isCube = true;
    gpu->mipLevels = mipLevels;
    gpu->samplerState = info.sampler;
    gpu->cubeImage = vkb::TextureImageCube(device, device.physical_device.memory_properties,
                                           uint32_t(faceSize), uint32_t(faceSize), mipLevels);

    std::vector<uint8_t> bytes =
        (mipLevels > 1) ? buildMipChainCube(rgbaFaces, uint32_t(faceSize), mipLevels)
                        : std::vector<uint8_t>(rgbaFaces, rgbaFaces + faceBytes * 6u);
    gpu->cubeImage.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);
    // Cubemap sampled via mesh3d descriptor sets — no 2D texSetLayout binding required here.
    registerBindlessTextureCube(gpu.get());

    auto tex = std::make_unique<Texture>();
    tex->width = faceSize;
    tex->height = faceSize;
    tex->pixelWidth = faceSize;
    tex->pixelHeight = faceSize;
    tex->layers = 6;
    tex->mipmapCount = int(mipLevels);
    tex->sampler = info.sampler;
    tex->gpuHandle = gpu.get();

    Texture *raw = tex.get();
    ownedTextures.push_back(std::move(tex));
    ownedGpuTextures.push_back(std::move(gpu));
    return raw;
}

Texture *Graphics::newTexture(image::ImageData *data) {
    ASSERT(data != nullptr);
    if (!data) throw Exception("newTexture: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw Exception("newTexture: only RGBA8 ImageData supported for now");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()));
}

Texture *Graphics::newTexture(image::ImageData *data, const TextureCreateInfo &info) {
    ASSERT(data != nullptr);
    if (!data) throw Exception("newTexture: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw Exception("newTexture: only RGBA8 ImageData supported for now");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()), info);
}


void Graphics::setTextureSampler(Texture *texture, const TextureSampler &sampler) {
    if (!texture || !texture->gpuHandle || !initialized) return;
    for (auto &owned : ownedGpuTextures) {
        if (owned.get() != texture->gpuHandle) continue;
        // In-flight frames may still be sampling the old sampler / descriptor.
        waitForSharedGpuResources();
        if (owned->sampler) device->destroySampler(owned->sampler);
        owned->samplerState = sampler;
        owned->sampler = createVkSampler(sampler, owned->mipLevels);
        texture->sampler = sampler;
        if (!owned->isCube) writeCombinedImageDescriptor(owned.get());
        invalidateTextureBindings();
        return;
    }
}

bool Graphics::releaseTexture(Texture *texture) {
    if (!texture || !texture->gpuHandle) return false;
    // Renderer-owned fallback textures must never be released by callers.
    if (texture == whiteTexture || texture == flatNormalTexture ||
        texture == flatNormalTexture3D || texture == defaultEnvCubemap)
        return false;

    auto *gpu = static_cast<GpuTexture *>(texture->gpuHandle);
    auto gpuIt = std::find_if(ownedGpuTextures.begin(), ownedGpuTextures.end(),
                              [&](const std::unique_ptr<GpuTexture> &g) {
                                  return g.get() == gpu;
                              });
    if (gpuIt == ownedGpuTextures.end()) return false;

    auto texIt = std::find_if(ownedTextures.begin(), ownedTextures.end(),
                              [&](const std::unique_ptr<Texture> &t) {
                                  return t.get() == texture;
                              });
    if (texIt == ownedTextures.end()) return false;

    // Path-cached textures must leave the hot-reload cache once released.
    for (auto it = texturesByPath.begin(); it != texturesByPath.end();) {
        if (it->second == texture)
            it = texturesByPath.erase(it);
        else
            ++it;
    }

    // In-flight frames may still sample the image / sampler; drain first.
    waitForSharedGpuResources();
    unregisterBindlessTexture(gpu);
    if ((*gpuIt)->sampler) device->destroySampler((*gpuIt)->sampler);
    texture->gpuHandle = nullptr;
    ownedGpuTextures.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)texIt->release();
    ownedTextures.erase(texIt);
    return true;
}

bool Graphics::replaceTexturePixels(Texture *tex, image::ImageData *data) {
    if (!tex || !data) return false;
    if (data->getFormat() != "RGBA8") return false;
    return replaceTexturePixelsRGBA(tex, data->getWidth(), data->getHeight(),
                                    static_cast<const uint8_t *>(data->getData()));
}

bool Graphics::updateTexture(Texture *tex, int width, int height, const uint8_t *rgba) {
    if (!tex || width <= 0 || height <= 0 || !rgba) return false;
    if (width != tex->width || height != tex->height) return false;
    return replaceTexturePixelsRGBA(tex, width, height, rgba);
}

bool Graphics::replaceTexturePixelsRGBA(Texture *tex, int w, int h, const uint8_t *rgba) {
    if (!tex || !rgba || w <= 0 || h <= 0 || !initialized) return false;

    TextureCreateInfo info;
    info.sampler = tex->sampler;
    info.generateMipmaps = tex->mipmapCount > 1;
    info = normalizeTextureInfo(info);
    const uint32_t mipLevels =
        info.generateMipmaps ? uint32_t(mipmapCountForSize(w, h)) : 1u;

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = w;
    gpu->height = h;
    gpu->isCube = false;
    gpu->mipLevels = mipLevels;
    gpu->samplerState = info.sampler;
    gpu->image = vkb::TextureImage2D(device, uint32_t(w), uint32_t(h), mipLevels);
    std::vector<uint8_t> bytes =
        (mipLevels > 1) ? buildMipChain2D(rgba, uint32_t(w), uint32_t(h), mipLevels)
                        : std::vector<uint8_t>(rgba, rgba + size_t(w) * size_t(h) * 4);
    gpu->image.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);

    gpu->descriptorSet = vkb::BoundSet{sets[0]};
    writeCombinedImageDescriptor(gpu.get());
    registerBindlessTexture2D(gpu.get());

    void *oldHandle = tex->gpuHandle;
    for (auto &owned : ownedGpuTextures) {
        if (owned.get() != oldHandle) continue;
        // Destroying the old image/sampler while an in-flight frame still
        // samples it is a typical TDR. Drain first, then drop cached sets.
        waitForSharedGpuResources();
        unregisterBindlessTexture(static_cast<GpuTexture *>(oldHandle));
        if (owned->sampler) device->destroySampler(owned->sampler);
        owned = std::move(gpu);
        tex->gpuHandle = owned.get();
        tex->width = w;
        tex->height = h;
        tex->pixelWidth = w;
        tex->pixelHeight = h;
        tex->mipmapCount = int(mipLevels);
        tex->sampler = info.sampler;
        registerBindlessTexture2D(owned.get());
        invalidateTextureBindings();
        return true;
    }

    // Texture not in owned list — attach as new ownership.
    tex->gpuHandle = gpu.get();
    registerBindlessTexture2D(static_cast<GpuTexture *>(tex->gpuHandle));
    tex->width = w;
    tex->height = h;
    tex->pixelWidth = w;
    tex->pixelHeight = h;
    tex->mipmapCount = int(mipLevels);
    tex->sampler = info.sampler;
    ownedGpuTextures.push_back(std::move(gpu));
    return true;
}

Texture *Graphics::newTextureFromFile(const std::string &filename) {
    ASSERT(!filename.empty());
    if (filename.empty()) throw Exception("newTextureFromFile: empty filename");

    const std::string key = normalizeTexPath(filename);
    auto *imgMod = image::Image::create();
    eve::ref<image::ImageData> data(imgMod->newImageDataFromFile(filename));

    auto it = texturesByPath.find(key);
    if (it != texturesByPath.end() && it->second) {
        if (!replaceTexturePixels(it->second, data.get()))
            throw Exception("newTextureFromFile: reload failed '%s'", filename.c_str());
        return it->second;
    }

    Texture *tex = newTexture(data.get());
    texturesByPath[key] = tex;
    return tex;
}

bool Graphics::reloadTextureFromFile(const std::string &filename) {
    if (filename.empty()) return false;
    const std::string key = normalizeTexPath(filename);
    auto it = texturesByPath.find(key);
    if (it == texturesByPath.end() || !it->second) return false;

    image::ImageData *data = nullptr;
    try {
        auto *imgMod = image::Image::create();
        eve::ref<image::ImageData> cached(imgMod->newImageDataFromFile(filename));
        data = cached.get();
    } catch (...) {
        return false;
    }
    if (!data) return false;
    return replaceTexturePixels(it->second, data);
}

}  // namespace eve::graphics::vulkan

