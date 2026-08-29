#include "graphics/webgpu/Canvas.h"
#include "graphics/TextureSampler.h"
#include "graphics/webgpu/Graphics.h"

#include "common/Exception.h"
#include "image/ImageData.h"

#include <cstring>

namespace eve::graphics::webgpu {

OffscreenCanvas::OffscreenCanvas(Graphics *gfx, int width, int height)
    : gfx(gfx), width(width), height(height) {
    if (!gfx || !gfx->getDevice() || width <= 0 || height <= 0)
        throw Exception("OffscreenCanvas: invalid size or uninitialized device");

    auto &device = gfx->getDevice();
    WGPUTextureDescriptor cd{};
    cd.label = sv("eve_canvas_color");
    cd.dimension = WGPUTextureDimension_2D;
    cd.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    cd.sampleCount = 1;
    cd.format = WGPUTextureFormat_RGBA8Unorm;
    cd.mipLevelCount = 1;
    cd.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
               WGPUTextureUsage_CopySrc;
    color = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&cd));
    colorView = color.CreateView();

    WGPUTextureDescriptor dd{};
    dd.label = sv("eve_canvas_depth");
    dd.dimension = WGPUTextureDimension_2D;
    dd.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    dd.sampleCount = 1;
    dd.format = WGPUTextureFormat_Depth32Float;
    dd.mipLevelCount = 1;
    dd.usage = WGPUTextureUsage_RenderAttachment;
    depth = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&dd));
    depthView = depth.CreateView();

    colorGpu.texture = color;
    colorGpu.view = colorView;
    colorGpu.width = width;
    colorGpu.height = height;
    colorGpu.samplerState = TextureSampler::linearMipmap();
    WGPUSamplerDescriptor sd{};
    sd.label = sv("eve_canvas_sampler");
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.maxAnisotropy = 1;
    colorGpu.sampler = device.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&sd));

    colorTex.gpuHandle = &colorGpu;
    colorTex.width = width;
    colorTex.height = height;
    colorTex.mipmapCount = 1;
}

OffscreenCanvas::~OffscreenCanvas() = default;

void OffscreenCanvas::clear(std::optional<Color> color, std::optional<int> /*stencil*/,
                            std::optional<double> /*depth*/) {
    clearRequested = true;
    clearColor = color ? *color : Color(0.f, 0.f, 0.f, 1.f);
}

Color OffscreenCanvas::getPixel(int x, int y) {
    return gfx->getPixelImpl(this, x, y);
}

image::ImageData *OffscreenCanvas::newImageData() {
    return gfx->newImageDataImpl(this);
}

}  // namespace eve::graphics::webgpu
