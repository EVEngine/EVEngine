#include "common/FrameCapture.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Batcher.h"
#include "graphics/ClusteredLight.h"
#include "graphics/CubemapPrefilter.h"
#include "graphics/GBuffer.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/ReflectionProbeUpload.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/shaders/ReflectionProbeWgsl.h"
#include "graphics/webgpu/BindGroupLayoutBuilder.h"
#include "graphics/webgpu/Canvas.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/GraphicsPipelineSupport.h"
#include "graphics/webgpu/InitFlow.h"
#include "graphics/webgpu/PipelineBuilder.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include "common/Exception.h"
#include "common/Resource.h"
#include "common/config.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/gtc/constants.hpp>

namespace eve::graphics::webgpu {
using namespace detail;

wgpu::Sampler Graphics::makeSampler(const TextureSampler& s, uint32_t mipLevels) const {
    WGPUSamplerDescriptor d{};
    d.label        = sv("eve_sampler");
    d.addressModeU = s.repeatU ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    d.addressModeV = s.repeatV ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    d.addressModeW = s.repeatW ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    d.magFilter    = s.mag == FilterMode::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    d.minFilter    = s.min == FilterMode::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    if (s.mipmap == MipmapMode::Disabled || mipLevels <= 1) {
        d.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        d.lodMinClamp  = 0.f;
        d.lodMaxClamp  = 0.f;
    } else {
        d.mipmapFilter = s.mipmap == MipmapMode::Nearest ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
        d.lodMinClamp  = s.minLod;
        d.lodMaxClamp  = std::min(std::max(s.maxLod, 0.f), float(mipLevels - 1));
        if (d.lodMaxClamp < d.lodMinClamp) d.lodMaxClamp = d.lodMinClamp;
    }
    float aniso     = s.maxAnisotropy > 1.f ? s.maxAnisotropy : 1.f;
    d.maxAnisotropy = static_cast<uint16_t>(std::min(std::max(aniso, 1.f), std::max(maxSamplerAnisotropy, 1.f)));
    return device.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&d));
}

// ---------------------------------------------------------------------------
// Texture creation
// ---------------------------------------------------------------------------

Texture* Graphics::newTexture(int width, int height, const uint8_t* rgba, bool repeatU, bool repeatV) {
    TextureCreateInfo info;
    info.sampler.repeatU = repeatU;
    info.sampler.repeatV = repeatV;
    return newTexture(width, height, rgba, info);
}

Texture* Graphics::newTexture(image::ImageData* data) {
    if (!data) throw Exception("newTexture: null ImageData");
    TextureCreateInfo info;
    return newTexture(data->getWidth(), data->getHeight(), static_cast<const uint8_t*>(data->getData()), info);
}

Texture* Graphics::newTexture(image::ImageData* data, const TextureCreateInfo& info) {
    if (!data) throw Exception("newTexture: null ImageData");
    if (data->getFormat() != "RGBA8") throw Exception("newTexture: only RGBA8 supported");
    return newTexture(data->getWidth(), data->getHeight(), static_cast<const uint8_t*>(data->getData()), info);
}

ResultRef<Texture> Graphics::newSharedTexture(image::ImageData* data, const std::string& contentKey) {
    if (!data || contentKey.empty() || data->getFormat() != "RGBA8")
        return ResultRef<Texture>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                             "RGBA8 image and nonempty content key are required", {},
                                                             {}, "graphics.texture.shared"));
    if (auto found = sharedTexturesByContent.find(contentKey); found != sharedTexturesByContent.end())
        return ResultRef<Texture>::success(std::ref(*found->second));
    try {
        Texture* texture = newTexture(data);
        if (!texture) throw Exception("shared texture upload produced no texture");
        sharedTexturesByContent.emplace(contentKey, texture);
        return ResultRef<Texture>::success(std::ref(*texture));
    } catch (const std::exception& error) {
        return ResultRef<Texture>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), {}, {}, "graphics.texture.shared"));
    }
}

Texture* Graphics::newTexture(int width, int height, const uint8_t* rgba, const TextureCreateInfo& rawInfo) {
    if (width <= 0 || height <= 0) throw Exception("newTexture: invalid size %dx%d", width, height);

    TextureCreateInfo info = rawInfo;
    if (info.generateMipmaps && info.sampler.mipmap == MipmapMode::Disabled) info.sampler.mipmap = MipmapMode::Linear;
    if (info.sampler.maxAnisotropy < 1.f) info.sampler.maxAnisotropy = 1.f;

    auto gpu          = std::make_unique<GpuTexture>();
    gpu->width        = width;
    gpu->height       = height;
    gpu->samplerState = info.sampler;
    gpu->mipLevels    = info.generateMipmaps ? uint32_t(mipmapCountForSize(width, height)) : 1u;

    WGPUTextureDescriptor td{};
    td.label                   = sv("eve_tex2d");
    td.dimension               = WGPUTextureDimension_2D;
    td.size.width              = static_cast<uint32_t>(width);
    td.size.height             = static_cast<uint32_t>(height);
    td.size.depthOrArrayLayers = 1;
    td.sampleCount             = 1;
    td.format                  = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount           = gpu->mipLevels;
    td.usage     = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment;
    gpu->texture = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));

    uploadTexturePixelsMips(gpu.get(), rgba, width, height);

    WGPUTextureViewDescriptor vd{};
    vd.format          = WGPUTextureFormat_RGBA8Unorm;
    vd.dimension       = WGPUTextureViewDimension_2D;
    vd.baseMipLevel    = 0;
    vd.mipLevelCount   = gpu->mipLevels;
    vd.baseArrayLayer  = 0;
    vd.arrayLayerCount = 1;
    gpu->view          = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
    gpu->sampler       = makeSampler(info.sampler, gpu->mipLevels);

    auto* tex        = new Texture();
    tex->width       = width;
    tex->height      = height;
    tex->mipmapCount = int(gpu->mipLevels);
    tex->sampler     = info.sampler;
    tex->gpuHandle   = gpu.get();

    ownedGpuTextures.push_back(std::move(gpu));
    ownedTextures.push_back(std::unique_ptr<Texture>(tex));
    return tex;
}

void Graphics::uploadTexturePixels(GpuTexture* gt, const uint8_t* rgba, int w, int h, const TextureCreateInfo& info) {
    (void)info;
    uploadTexturePixelsMips(gt, rgba, w, h);
}

void Graphics::uploadTexturePixelsMips(GpuTexture* gt, const uint8_t* rgba, int w, int h) {
    if (!rgba) return;
    std::vector<uint8_t>      current;
    WGPUTexelCopyBufferLayout layout{};
    layout.offset       = 0;
    layout.bytesPerRow  = static_cast<uint32_t>(w * 4);
    layout.rowsPerImage = static_cast<uint32_t>(h);
    WGPUExtent3D extent{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};

    for (uint32_t m = 0; m < gt->mipLevels; ++m) {
        WGPUTexelCopyTextureInfo dst{};
        dst.texture  = gt->texture.Get();
        dst.mipLevel = m;
        dst.aspect   = WGPUTextureAspect_All;
        queue.WriteTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&dst), rgba,
                           static_cast<uint64_t>(w) * h * 4,
                           reinterpret_cast<const wgpu::TexelCopyBufferLayout*>(&layout),
                           reinterpret_cast<const wgpu::Extent3D*>(&extent));
        if (m + 1 < gt->mipLevels) {
            // Box-filter downsample into the CPU buffer for the next level.
            const int            nextW = std::max(w / 2, 1);
            const int            nextH = std::max(h / 2, 1);
            std::vector<uint8_t> next(size_t(nextW) * size_t(nextH) * 4u);
            for (int y = 0; y < nextH; ++y) {
                const int y0 = std::min(y * 2, h - 1);
                const int y1 = std::min(y0 + 1, h - 1);
                for (int x = 0; x < nextW; ++x) {
                    const int x0 = std::min(x * 2, w - 1);
                    const int x1 = std::min(x0 + 1, w - 1);
                    for (int c = 0; c < 4; ++c) {
                        uint32_t acc = 0;
                        acc += rgba[(size_t(y0) * w + x0) * 4u + c];
                        acc += rgba[(size_t(y0) * w + x1) * 4u + c];
                        acc += rgba[(size_t(y1) * w + x0) * 4u + c];
                        acc += rgba[(size_t(y1) * w + x1) * 4u + c];
                        next[(size_t(y) * nextW + x) * 4u + c] = uint8_t((acc + 2u) / 4u);
                    }
                }
            }
            current             = std::move(next);
            w                   = nextW;
            h                   = nextH;
            rgba                = current.data();
            layout.bytesPerRow  = static_cast<uint32_t>(w * 4);
            layout.rowsPerImage = static_cast<uint32_t>(h);
            extent              = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        }
    }
}

Texture* Graphics::newCubemap(int faceSize, const uint8_t* rgbaFaces) {
    return newCubemap(faceSize, rgbaFaces, TextureCreateInfo::withMipmaps(false));
}

Texture* Graphics::newCubemap(int faceSize, const uint8_t* rgbaFaces, const TextureCreateInfo& rawInfo) {
    if (faceSize <= 0 || !rgbaFaces) throw Exception("newCubemap: invalid size or null data");

    TextureCreateInfo info = rawInfo;
    if (info.generateMipmaps && info.sampler.mipmap == MipmapMode::Disabled) info.sampler.mipmap = MipmapMode::Linear;
    if (info.sampler.maxAnisotropy < 1.f) info.sampler.maxAnisotropy = 1.f;

    auto gpu          = std::make_unique<GpuTexture>();
    gpu->width        = faceSize;
    gpu->height       = faceSize;
    gpu->samplerState = info.sampler;
    gpu->isCube       = true;
    gpu->mipLevels    = info.generateMipmaps ? uint32_t(mipmapCountForSize(faceSize, faceSize)) : 1u;

    WGPUTextureDescriptor td{};
    td.label                   = sv("eve_cubemap");
    td.dimension               = WGPUTextureDimension_2D;
    td.size.width              = static_cast<uint32_t>(faceSize);
    td.size.height             = static_cast<uint32_t>(faceSize);
    td.size.depthOrArrayLayers = 6;
    td.sampleCount             = 1;
    td.format                  = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount           = gpu->mipLevels;
    td.usage                   = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    gpu->texture               = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));

    const std::vector<uint8_t> mipChain  = buildGgxCubemapMipChain(rgbaFaces, uint32_t(faceSize), gpu->mipLevels);
    size_t                     mipOffset = 0;
    int                        mipW      = faceSize;
    int                        mipH      = faceSize;
    for (uint32_t mip = 0; mip < gpu->mipLevels; ++mip) {
        const size_t mipFaceBytes = size_t(mipW) * mipH * 4u;
        for (int f = 0; f < 6; ++f) {
            const uint8_t*            pixels = mipChain.data() + mipOffset + size_t(f) * mipFaceBytes;
            WGPUTexelCopyBufferLayout layout{};
            layout.offset       = 0;
            layout.bytesPerRow  = static_cast<uint32_t>(mipW * 4);
            layout.rowsPerImage = static_cast<uint32_t>(mipH);
            WGPUExtent3D             extent{static_cast<uint32_t>(mipW), static_cast<uint32_t>(mipH), 1};
            WGPUTexelCopyTextureInfo dst{};
            dst.texture  = gpu->texture.Get();
            dst.mipLevel = mip;
            dst.aspect   = WGPUTextureAspect_All;
            dst.origin   = {0, 0, static_cast<uint32_t>(f)};
            queue.WriteTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&dst), pixels,
                               static_cast<uint64_t>(mipW) * mipH * 4,
                               reinterpret_cast<const wgpu::TexelCopyBufferLayout*>(&layout),
                               reinterpret_cast<const wgpu::Extent3D*>(&extent));
        }
        mipOffset += mipFaceBytes * 6u;
        mipW = std::max(mipW / 2, 1);
        mipH = std::max(mipH / 2, 1);
    }

    WGPUTextureViewDescriptor vd{};
    vd.format          = WGPUTextureFormat_RGBA8Unorm;
    vd.dimension       = WGPUTextureViewDimension_Cube;
    vd.baseMipLevel    = 0;
    vd.mipLevelCount   = gpu->mipLevels;
    vd.baseArrayLayer  = 0;
    vd.arrayLayerCount = 6;
    gpu->view          = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
    gpu->sampler       = makeSampler(info.sampler, gpu->mipLevels);

    auto* tex        = new Texture();
    tex->width       = faceSize;
    tex->height      = faceSize;
    tex->pixelWidth  = faceSize;
    tex->pixelHeight = faceSize;
    tex->layers      = 6;
    tex->mipmapCount = int(gpu->mipLevels);
    tex->sampler     = info.sampler;
    tex->gpuHandle   = gpu.get();

    ownedGpuTextures.push_back(std::move(gpu));
    ownedTextures.push_back(std::unique_ptr<Texture>(tex));
    return tex;
}

Texture* Graphics::newHDRCubemap(int faceSize) {
    if (faceSize <= 0) throw Exception("newHDRCubemap: invalid face size");
    auto gpu                 = std::make_unique<GpuTexture>();
    gpu->width               = faceSize;
    gpu->height              = faceSize;
    gpu->isCube              = true;
    gpu->isHDR               = true;
    gpu->mipLevels           = uint32_t(mipmapCountForSize(faceSize, faceSize));
    gpu->samplerState        = TextureSampler::linear();
    gpu->samplerState.mipmap = MipmapMode::Linear;

    WGPUTextureDescriptor td{};
    td.label         = sv("eve_hdr_reflection_probe_staging");
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {static_cast<uint32_t>(faceSize), static_cast<uint32_t>(faceSize), 6};
    td.sampleCount   = 1;
    td.format        = WGPUTextureFormat_RGBA16Float;
    td.mipLevelCount = gpu->mipLevels;
    td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc |
               WGPUTextureUsage_RenderAttachment;
    gpu->texture = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));

    WGPUTextureViewDescriptor vd{};
    vd.format          = WGPUTextureFormat_RGBA16Float;
    vd.dimension       = WGPUTextureViewDimension_Cube;
    vd.baseMipLevel    = 0;
    vd.mipLevelCount   = gpu->mipLevels;
    vd.baseArrayLayer  = 0;
    vd.arrayLayerCount = 6;
    gpu->view          = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
    gpu->sampler       = makeSampler(gpu->samplerState, gpu->mipLevels);

    auto* texture        = new Texture();
    texture->width       = faceSize;
    texture->height      = faceSize;
    texture->mipmapCount = int(gpu->mipLevels);
    texture->sampler     = gpu->samplerState;
    texture->gpuHandle   = gpu.get();
    ownedGpuTextures.push_back(std::move(gpu));
    ownedTextures.emplace_back(texture);
    return texture;
}

bool Graphics::copyHDRCanvasToCubemapFace(Canvas* source, Texture* cubemap, int face) {
    auto* canvas = dynamic_cast<OffscreenCanvas*>(source);
    if (!canvas || !canvas->hdr || !cubemap || !cubemap->gpuHandle || face < 0 || face >= 6) return false;
    auto* target = static_cast<GpuTexture*>(cubemap->gpuHandle);
    if (!target->isCube || !target->isHDR || target->width != canvas->width || target->height != canvas->height)
        return false;

    wgpu::CommandEncoder     encoder = device.CreateCommandEncoder();
    WGPUTexelCopyTextureInfo src{};
    src.texture  = canvas->color.Get();
    src.mipLevel = 0;
    src.aspect   = WGPUTextureAspect_All;
    src.origin   = {0, 0, 0};
    WGPUTexelCopyTextureInfo dst{};
    dst.texture  = target->texture.Get();
    dst.mipLevel = 0;
    dst.aspect   = WGPUTextureAspect_All;
    dst.origin   = {0, 0, static_cast<uint32_t>(face)};
    WGPUExtent3D extent{static_cast<uint32_t>(canvas->width), static_cast<uint32_t>(canvas->height), 1};
    encoder.CopyTextureToTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&src),
                                 reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&dst),
                                 reinterpret_cast<const wgpu::Extent3D*>(&extent));
    wgpu::CommandBuffer command = encoder.Finish();
    queue.Submit(1, &command);
    return true;
}

bool Graphics::copyHDRCanvasesToCubemap(Canvas* const* sources, int faceCount, Texture* cubemap) {
    if (!sources || faceCount < 1 || faceCount > 6 || !cubemap || !cubemap->gpuHandle) return false;
    auto* target = static_cast<GpuTexture*>(cubemap->gpuHandle);
    if (!target->isCube || !target->isHDR) return false;

    std::array<OffscreenCanvas*, 6> canvases{};
    for (int face = 0; face < faceCount; ++face) {
        auto* canvas = dynamic_cast<OffscreenCanvas*>(sources[face]);
        if (!canvas || !canvas->hdr || target->width != canvas->width || target->height != canvas->height) return false;
        canvases[static_cast<size_t>(face)] = canvas;
    }

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    for (int face = 0; face < faceCount; ++face) {
        const auto*              canvas = canvases[static_cast<size_t>(face)];
        WGPUTexelCopyTextureInfo src{};
        src.texture  = canvas->color.Get();
        src.mipLevel = 0;
        src.aspect   = WGPUTextureAspect_All;
        src.origin   = {0, 0, 0};
        WGPUTexelCopyTextureInfo dst{};
        dst.texture  = target->texture.Get();
        dst.mipLevel = 0;
        dst.aspect   = WGPUTextureAspect_All;
        dst.origin   = {0, 0, static_cast<uint32_t>(face)};
        WGPUExtent3D extent{static_cast<uint32_t>(canvas->width), static_cast<uint32_t>(canvas->height), 1};
        encoder.CopyTextureToTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&src),
                                     reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&dst),
                                     reinterpret_cast<const wgpu::Extent3D*>(&extent));
    }
    wgpu::CommandBuffer command = encoder.Finish();
    queue.Submit(1, &command);
    return true;
}

bool Graphics::filterHDRReflectionCubemap(Texture* cubemap, int sampleCount) {
    if (!cubemap || !cubemap->gpuHandle) return false;
    auto* target = static_cast<GpuTexture*>(cubemap->gpuHandle);
    if (!target->isCube || !target->isHDR || target->mipLevels < 2) return false;
    sampleCount = std::clamp(sampleCount, 8, 512);
    wgpuInstanceProcessEvents(instance.Get());
    OffscreenTimestampReadback* timestampSlot = nullptr;
    if (offscreenTimestampSupported && offscreenTimestampQuerySet && offscreenTimestampResolveBuffer) {
        for (size_t candidate = 0; candidate < offscreenTimestampReadbacks.size(); ++candidate) {
            const size_t index = (offscreenTimestampReadbackCursor + candidate) % offscreenTimestampReadbacks.size();
            if (!offscreenTimestampReadbacks[index].pending) {
                timestampSlot                    = &offscreenTimestampReadbacks[index];
                offscreenTimestampReadbackCursor = (index + 1) % offscreenTimestampReadbacks.size();
                break;
            }
        }
    }

    if (!reflectionProbeFilterPipeline) {
        WGPUBindGroupLayoutEntry entries[3]{};
        entries[0].binding                 = 0;
        entries[0].visibility              = WGPUShaderStage_Fragment;
        entries[0].texture.sampleType      = WGPUTextureSampleType_Float;
        entries[0].texture.viewDimension   = WGPUTextureViewDimension_Cube;
        entries[1].binding                 = 1;
        entries[1].visibility              = WGPUShaderStage_Fragment;
        entries[1].sampler.type            = WGPUSamplerBindingType_Filtering;
        entries[2].binding                 = 2;
        entries[2].visibility              = WGPUShaderStage_Fragment;
        entries[2].buffer.type             = WGPUBufferBindingType_Uniform;
        entries[2].buffer.hasDynamicOffset = true;
        entries[2].buffer.minBindingSize   = 16;
        WGPUBindGroupLayoutDescriptor bgl{};
        bgl.label      = sv("eve_reflection_probe_filter_bgl");
        bgl.entryCount = 3;
        bgl.entries    = entries;
        reflectionProbeFilterSetLayout =
            device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&bgl));
        WGPUBindGroupLayout          rawLayout = reflectionProbeFilterSetLayout.Get();
        WGPUPipelineLayoutDescriptor pld{};
        pld.label                = sv("eve_reflection_probe_filter_layout");
        pld.bindGroupLayoutCount = 1;
        pld.bindGroupLayouts     = &rawLayout;
        reflectionProbeFilterPipelineLayout =
            device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&pld));

        wgpu::ShaderModule   module = makeWgslModule(device, shaders::kReflectionProbeFilterWgsl);
        WGPUColorTargetState colorTarget{};
        colorTarget.format    = WGPUTextureFormat_RGBA16Float;
        colorTarget.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module      = module.Get();
        fragment.entryPoint  = sv("fs_main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;
        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label               = sv("eve_reflection_probe_filter");
        pipeline.layout              = reflectionProbeFilterPipelineLayout.Get();
        pipeline.vertex.module       = module.Get();
        pipeline.vertex.entryPoint   = sv("vs_main");
        pipeline.fragment            = &fragment;
        pipeline.primitive.topology  = WGPUPrimitiveTopology_TriangleList;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode  = WGPUCullMode_None;
        pipeline.multisample.count   = 1;
        pipeline.multisample.mask    = 0xFFFFFFFFu;
        reflectionProbeFilterPipeline =
            device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pipeline));
    }

    const uint32_t       passCount = (target->mipLevels - 1u) * 6u;
    WGPUBufferDescriptor bufferDesc{};
    bufferDesc.label          = sv("eve_reflection_probe_filter_params");
    bufferDesc.size           = uint64_t(passCount) * 256u;
    bufferDesc.usage          = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    wgpu::Buffer paramsBuffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bufferDesc));
    WGPUTextureViewDescriptor sourceViewDesc{};
    sourceViewDesc.format          = WGPUTextureFormat_RGBA16Float;
    sourceViewDesc.dimension       = WGPUTextureViewDimension_Cube;
    sourceViewDesc.baseMipLevel    = 0;
    sourceViewDesc.mipLevelCount   = 1;
    sourceViewDesc.baseArrayLayer  = 0;
    sourceViewDesc.arrayLayerCount = 6;
    wgpu::TextureView sourceView =
        target->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&sourceViewDesc));
    WGPUBindGroupEntry groupEntries[3]{};
    groupEntries[0].binding     = 0;
    groupEntries[0].textureView = sourceView.Get();
    groupEntries[1].binding     = 1;
    groupEntries[1].sampler     = target->sampler.Get();
    groupEntries[2].binding     = 2;
    groupEntries[2].buffer      = paramsBuffer.Get();
    groupEntries[2].size        = 16;
    WGPUBindGroupDescriptor groupDesc{};
    groupDesc.label       = sv("eve_reflection_probe_filter_group");
    groupDesc.layout      = reflectionProbeFilterSetLayout.Get();
    groupDesc.entryCount  = 3;
    groupDesc.entries     = groupEntries;
    wgpu::BindGroup group = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&groupDesc));

    wgpu::CommandEncoder encoder   = device.CreateCommandEncoder();
    uint32_t             passIndex = 0;
    for (uint32_t mip = 1; mip < target->mipLevels; ++mip) {
        const bool  diffuse   = mip + 1u == target->mipLevels;
        const float roughness = diffuse ? 1.f : float(mip) / float(target->mipLevels - 1u);
        for (uint32_t face = 0; face < 6; ++face, ++passIndex) {
            const float params[4] = {float(face), roughness, diffuse ? 1.f : 0.f, float(sampleCount)};
            queue.WriteBuffer(paramsBuffer, uint64_t(passIndex) * 256u, params, sizeof(params));
            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format          = WGPUTextureFormat_RGBA16Float;
            viewDesc.dimension       = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel    = mip;
            viewDesc.mipLevelCount   = 1;
            viewDesc.baseArrayLayer  = face;
            viewDesc.arrayLayerCount = 1;
            wgpu::TextureView faceView =
                target->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&viewDesc));
            WGPURenderPassColorAttachment attachment{};
            attachment.view    = faceView.Get();
            attachment.loadOp  = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments     = &attachment;
            WGPUPassTimestampWrites timestampWrites{};
            if (timestampSlot) {
                timestampWrites.querySet                  = offscreenTimestampQuerySet.Get();
                timestampWrites.beginningOfPassWriteIndex = passIndex == 0 ? 0 : UINT32_MAX;
                timestampWrites.endOfPassWriteIndex       = passIndex + 1u == passCount ? 1 : UINT32_MAX;
                passDesc.timestampWrites                  = &timestampWrites;
            }
            wgpu::RenderPassEncoder pass =
                encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&passDesc));
            pass.SetPipeline(reflectionProbeFilterPipeline);
            const uint32_t offset = passIndex * 256u;
            pass.SetBindGroup(0, group, 1, &offset);
            pass.Draw(3);
            pass.End();
        }
    }
    if (timestampSlot) {
        encoder.ResolveQuerySet(offscreenTimestampQuerySet, 0, 2, offscreenTimestampResolveBuffer, 0);
        encoder.CopyBufferToBuffer(offscreenTimestampResolveBuffer, 0, timestampSlot->buffer, 0, sizeof(uint64_t) * 2);
    }
    wgpu::CommandBuffer command = encoder.Finish();
    queue.Submit(1, &command);
    if (timestampSlot) {
        timestampSlot->pending = true;
        WGPUBufferMapCallbackInfo callback{};
        callback.mode     = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
            auto* slot = static_cast<OffscreenTimestampReadback*>(userdata1);
            if (status == WGPUMapAsyncStatus_Success && slot && slot->owner) {
                const auto* ticks =
                    static_cast<const uint64_t*>(slot->buffer.GetConstMappedRange(0, sizeof(uint64_t) * 2));
                if (ticks && ticks[1] >= ticks[0]) {
                    slot->owner->completedOffscreenTimestampMs.fetch_add(float(ticks[1] - ticks[0]) * 1.0e-6f);
                }
                slot->buffer.Unmap();
            }
            if (slot) slot->pending = false;
        };
        callback.userdata1 = timestampSlot;
        wgpuBufferMapAsync(timestampSlot->buffer.Get(), WGPUMapMode_Read, 0, sizeof(uint64_t) * 2, callback);
    }
    return true;
}


float Graphics::getMaxAnisotropy() const { return maxSamplerAnisotropy; }


bool Graphics::releaseTexture(Texture* texture) {
    if (!texture) return false;
    dropDeferredFileTexture(texture);
    if (!texture->gpuHandle) {
        auto texIt = std::find_if(ownedTextures.begin(), ownedTextures.end(),
                                  [&](const std::unique_ptr<Texture>& t) { return t.get() == texture; });
        if (texIt == ownedTextures.end()) return false;
        for (auto it = texturesByPath.begin(); it != texturesByPath.end();) {
            if (it->second == texture)
                it = texturesByPath.erase(it);
            else
                ++it;
        }
        for (auto it = sharedTexturesByContent.begin(); it != sharedTexturesByContent.end();) {
            if (it->second == texture)
                it = sharedTexturesByContent.erase(it);
            else
                ++it;
        }
        (void)texIt->release();
        ownedTextures.erase(texIt);
        return true;
    }
    // Renderer-owned fallback textures must never be released by callers.
    if (texture->gpuHandle == whiteTexture || texture->gpuHandle == flatNormalTexture ||
        texture->gpuHandle == flatNormalTexture3D || texture->gpuHandle == defaultEnvCubemap)
        return false;

    auto* gpu   = static_cast<GpuTexture*>(texture->gpuHandle);
    auto  gpuIt = std::find_if(ownedGpuTextures.begin(), ownedGpuTextures.end(),
                               [&](const std::unique_ptr<GpuTexture>& g) { return g.get() == gpu; });
    if (gpuIt == ownedGpuTextures.end()) return false;

    auto texIt = std::find_if(ownedTextures.begin(), ownedTextures.end(),
                              [&](const std::unique_ptr<Texture>& t) { return t.get() == texture; });
    if (texIt == ownedTextures.end()) return false;

    // Path-cached textures must leave the hot-reload cache once released.
    for (auto it = texturesByPath.begin(); it != texturesByPath.end();) {
        if (it->second == texture)
            it = texturesByPath.erase(it);
        else
            ++it;
    }
    for (auto it = sharedTexturesByContent.begin(); it != sharedTexturesByContent.end();) {
        if (it->second == texture)
            it = sharedTexturesByContent.erase(it);
        else
            ++it;
    }

    texture->gpuHandle = nullptr;
    ownedGpuTextures.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)texIt->release();
    ownedTextures.erase(texIt);
    return true;
}

bool Graphics::updateTexture(Texture* texture, int width, int height, const uint8_t* rgba) {
    if (!texture || !rgba || width <= 0 || height <= 0) return false;
    const auto ownedTexture =
        std::find_if(ownedTextures.begin(), ownedTextures.end(),
                     [texture](const std::unique_ptr<Texture>& candidate) { return candidate.get() == texture; });
    if (ownedTexture == ownedTextures.end()) return false;
    auto* gpu = gpuForTexture(texture);
    if (!gpu || width != texture->width || height != texture->height) return false;
    const auto owned =
        std::find_if(ownedGpuTextures.begin(), ownedGpuTextures.end(),
                     [gpu](const std::unique_ptr<GpuTexture>& candidate) { return candidate.get() == gpu; });
    if (owned == ownedGpuTextures.end() || gpu->isCube) return false;
    uploadTexturePixelsMips(gpu, rgba, width, height);
    return true;
}

eve::Result<void> Graphics::updateTextureRegion(Texture* texture, int x, int y, int width, int height,
                                                std::span<const std::uint8_t> rgba, std::size_t bytesPerRow) {
    const TextureRegionUpload upload{x, y, width, height, rgba, bytesPerRow};
    return updateTextureRegions(texture, std::span<const TextureRegionUpload>(&upload, 1));
}

eve::Result<void> Graphics::updateTextureRegions(Texture* texture, std::span<const TextureRegionUpload> regions) {
    const auto ownedTexture =
        std::find_if(ownedTextures.begin(), ownedTextures.end(),
                     [texture](const std::unique_ptr<Texture>& candidate) { return candidate.get() == texture; });
    if (ownedTexture == ownedTextures.end())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "texture is not an owned 2D texture",
                                                                 "graphics.updateTextureRegions.texture"));
    auto*      gpu = gpuForTexture(texture);
    const auto owned =
        std::find_if(ownedGpuTextures.begin(), ownedGpuTextures.end(),
                     [gpu](const std::unique_ptr<GpuTexture>& candidate) { return candidate.get() == gpu; });
    if (!gpu || owned == ownedGpuTextures.end() || gpu->isCube)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "texture is not an owned 2D texture",
                                                                 "graphics.updateTextureRegions.texture"));
    if (texture->mipmapCount != 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                                 "partial updates require a single-mip texture",
                                                                 "graphics.updateTextureRegions.mipmaps"));

    for (const TextureRegionUpload& region : regions) {
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            region.x > texture->width - region.width || region.y > texture->height - region.height)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "invalid texture region",
                                                                     "graphics.updateTextureRegions.region"));
        const std::size_t packedRow = std::size_t(region.width) * 4U;
        const std::size_t stride    = region.bytesPerRow == 0 ? packedRow : region.bytesPerRow;
        if (stride < packedRow || stride > std::numeric_limits<std::uint32_t>::max())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "texture row stride is out of range",
                                                                     "graphics.updateTextureRegions.bytesPerRow"));
        if (std::size_t(region.height - 1) > (std::numeric_limits<std::size_t>::max() - packedRow) / stride)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "texture region byte count overflows",
                                                                     "graphics.updateTextureRegions.bytesPerRow"));
        const std::size_t requiredBytes = stride * std::size_t(region.height - 1) + packedRow;
        if (region.rgba.size() < requiredBytes)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "source bytes do not cover the texture region",
                                                                     "graphics.updateTextureRegions.bytes"));
    }

    for (const TextureRegionUpload& region : regions) {
        const std::size_t        packedRow     = std::size_t(region.width) * 4U;
        const std::size_t        stride        = region.bytesPerRow == 0 ? packedRow : region.bytesPerRow;
        const std::size_t        requiredBytes = stride * std::size_t(region.height - 1) + packedRow;
        WGPUTexelCopyTextureInfo dst{};
        dst.texture  = gpu->texture.Get();
        dst.mipLevel = 0;
        dst.origin   = {std::uint32_t(region.x), std::uint32_t(region.y), 0};
        dst.aspect   = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.offset       = 0;
        layout.bytesPerRow  = std::uint32_t(stride);
        layout.rowsPerImage = std::uint32_t(region.height);
        WGPUExtent3D extent{std::uint32_t(region.width), std::uint32_t(region.height), 1};
        queue.WriteTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&dst), region.rgba.data(), requiredBytes,
                           reinterpret_cast<const wgpu::TexelCopyBufferLayout*>(&layout),
                           reinterpret_cast<const wgpu::Extent3D*>(&extent));
    }
    return eve::Result<void>::success();
}

GpuTexture* Graphics::gpuForTexture(Texture* t) const { return t ? static_cast<GpuTexture*>(t->gpuHandle) : nullptr; }

GpuTexture* Graphics::gpuForTextureOrWhite(Texture* t) const {
    GpuTexture* g = gpuForTexture(t);
    return g ? g : whiteTexture;
}

wgpu::BindGroup Graphics::makeTex2DBindGroup(GpuTexture* color, GpuTexture* depth, GpuTexture* motion,
                                             GpuTexture* extra, GpuTexture* specular) {
    GpuTexture*        c = color ? color : whiteTexture;
    GpuTexture*        d = depth ? depth : c;
    GpuTexture*        m = motion ? motion : c;
    GpuTexture*        e = extra ? extra : c;
    GpuTexture*        s = specular ? specular : c;
    WGPUBindGroupEntry entries[11]{};
    entries[0].binding     = 0;
    entries[0].textureView = c->view.Get();
    entries[1].binding     = 1;
    entries[1].textureView = d->view.Get();
    entries[2].binding     = 2;
    entries[2].sampler     = c->sampler.Get();
    entries[3].binding     = 3;
    entries[3].sampler     = d->sampler.Get();
    entries[4].binding     = 4;
    entries[4].buffer      = currentUboArena().buffer.Get();  // replaced per-draw via dynamic offset
    entries[4].size        = std::max<uint32_t>(Shader::kPushConstantBytes, uint32_t(sizeof(Lighting2DUBO)));
    entries[5].binding     = 5;
    entries[5].textureView = m->view.Get();
    entries[6].binding     = 6;
    entries[6].sampler     = m->sampler.Get();
    entries[7].binding     = 7;
    entries[7].textureView = e->view.Get();
    entries[8].binding     = 8;
    entries[8].sampler     = e->sampler.Get();
    entries[9].binding     = 9;
    entries[9].textureView = s->view.Get();
    entries[10].binding    = 10;
    entries[10].sampler    = s->sampler.Get();
    WGPUBindGroupDescriptor desc{};
    desc.label      = sv("eve_tex2d_group");
    desc.layout     = tex2DSetLayout.Get();
    desc.entryCount = 11;
    desc.entries    = entries;
    return device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
}

wgpu::BindGroup Graphics::makeMeshBindGroup(GpuTexture* albedo, GpuTexture* normal, GpuTexture* env, GpuTexture* height,
                                            GpuTexture* depth, GpuTexture* sceneColor, uint32_t frameUboOffset,
                                            uint32_t shadowUboOffset, uint32_t pushUboOffset,
                                            const wgpu::Buffer& skinBuffer) {
    GpuTexture* a          = albedo ? albedo : whiteTexture;
    GpuTexture* n          = normal ? normal : flatNormalTexture;
    GpuTexture* e          = env ? env : defaultEnvCubemap;
    auto        localProbe = [&](int index) -> GpuTexture* {
        if (index < mesh3dReflectionProbes.count) {
            GpuTexture* gpu = gpuForTexture(mesh3dReflectionProbes.probes[index].cubemap);
            if (gpu && gpu->isCube) return gpu;
        }
        return defaultEnvCubemap;
    };
    GpuTexture*       probe0 = localProbe(0);
    GpuTexture*       probe1 = localProbe(1);
    GpuTexture*       h      = height ? height : flatHeightTexture3D;
    GpuTexture*       d      = depth ? depth : flatDepthTexture3D;
    GpuTexture*       c      = sceneColor ? sceneColor : whiteTexture;
    GpuTexture*       shadow = shadowDepthArray ? shadowDepthArray : defaultShadowTex;
    wgpu::TextureView aoView_ =
        aoReady ? aoView[(aoWriteIndex + 1) % 2] : (whiteTexture ? whiteTexture->view : wgpu::TextureView());
    wgpu::TextureView decalAlbedoView = gpuForTextureOrWhite(decalFlatAlbedo)->view;
    wgpu::TextureView decalNormalView = gpuForTextureOrWhite(decalFlatNormal)->view;
    wgpu::TextureView decalParamsView = gpuForTextureOrWhite(decalFlatParams)->view;
    if (decalReady && lastDecalSlot < decalSlots.size()) {
        decalAlbedoView = decalSlots[lastDecalSlot].albedoView;
        decalNormalView = decalSlots[lastDecalSlot].normalView;
        decalParamsView = decalSlots[lastDecalSlot].paramsView;
    }

    MeshBindGroupKey key{reinterpret_cast<uintptr_t>(currentUboArena().buffer.Get()),
                         reinterpret_cast<uintptr_t>(a->view.Get()),
                         reinterpret_cast<uintptr_t>(n->view.Get()),
                         reinterpret_cast<uintptr_t>(e->view.Get()),
                         reinterpret_cast<uintptr_t>(h->view.Get()),
                         reinterpret_cast<uintptr_t>(d->view.Get()),
                         reinterpret_cast<uintptr_t>(shadow->view.Get()),
                         reinterpret_cast<uintptr_t>(shadow->sampler.Get()),
                         reinterpret_cast<uintptr_t>(aoView_.Get()),
                         reinterpret_cast<uintptr_t>(decalAlbedoView.Get()),
                         reinterpret_cast<uintptr_t>(decalNormalView.Get()),
                         reinterpret_cast<uintptr_t>(decalParamsView.Get()),
                         reinterpret_cast<uintptr_t>(probe0->view.Get()),
                         reinterpret_cast<uintptr_t>(probe1->view.Get()),
                         reinterpret_cast<uintptr_t>(c->view.Get()),
                         reinterpret_cast<uintptr_t>(skinBuffer.Get())};
    auto             cached = meshBindGroupCache_.find(key);
    if (cached != meshBindGroupCache_.end()) return cached->second;
    if (meshBindGroupCache_.size() >= kMaxMeshBindGroupCache) meshBindGroupCache_.clear();

    WGPUBindGroupEntry entries[21]{};
    entries[0].binding     = 0;
    entries[0].buffer      = currentUboArena().buffer.Get();
    entries[0].size        = sizeof(Mesh3DUBO);
    entries[1].binding     = 1;
    entries[1].textureView = a->view.Get();
    entries[2].binding     = 2;
    entries[2].textureView = n->view.Get();
    entries[3].binding     = 3;
    entries[3].textureView = e->view.Get();
    entries[4].binding     = 4;
    entries[4].buffer      = currentUboArena().buffer.Get();
    entries[4].size        = sizeof(ShadowUBO);
    entries[5].binding     = 5;
    entries[5].textureView = shadow->view.Get();
    entries[6].binding     = 6;
    entries[6].textureView = h->view.Get();
    entries[7].binding     = 7;
    // The mesh shader currently has one material sampler binding. Follow the
    // albedo texture's public sampler state instead of silently substituting
    // the backend-global linear sampler (Vulkan also samples albedo with the
    // sampler owned by that texture).
    entries[7].sampler      = a->sampler.Get();
    entries[8].binding      = 8;
    entries[8].sampler      = shadow->sampler.Get();
    entries[9].binding      = 9;
    entries[9].textureView  = d->view.Get();
    entries[10].binding     = 10;
    entries[10].textureView = aoView_.Get();
    entries[11].binding     = 11;
    entries[11].sampler     = mainSampler.Get();
    entries[12].binding     = 12;
    entries[12].textureView = decalAlbedoView.Get();
    entries[13].binding     = 13;
    entries[13].textureView = decalNormalView.Get();
    entries[14].binding     = 14;
    entries[14].textureView = decalParamsView.Get();
    entries[15].binding     = 15;
    entries[15].buffer      = currentUboArena().buffer.Get();
    entries[15].size        = Shader::kPushConstantBytes;
    entries[16].binding     = 16;
    entries[16].textureView = probe0->view.Get();
    entries[17].binding     = 17;
    entries[17].textureView = probe1->view.Get();
    entries[18].binding     = 18;
    entries[18].textureView = c->view.Get();
    entries[19].binding     = 19;
    entries[19].sampler     = c->sampler.Get();

    entries[20].binding = 21;
    entries[20].buffer  = skinBuffer.Get();
    entries[20].size    = skinBuffer.GetSize();
    (void)frameUboOffset;
    (void)shadowUboOffset;
    (void)pushUboOffset;
    WGPUBindGroupDescriptor desc{};
    desc.label         = sv("eve_mesh_group");
    desc.layout        = mesh3dSetLayout.Get();
    desc.entryCount    = 21;
    desc.entries       = entries;
    wgpu::BindGroup bg = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
    meshBindGroupCache_.emplace(key, bg);
    return bg;
}

wgpu::BindGroup Graphics::makeMesh3DClusteredBindGroup(GpuTexture* albedo, GpuTexture* normal, GpuTexture* env,
                                                       GpuTexture* height, GpuTexture* depth, wgpu::TextureView aoView,
                                                       uint32_t frameUboOffset, uint32_t shadowUboOffset) {
    GpuTexture* a          = albedo ? albedo : whiteTexture;
    GpuTexture* n          = normal ? normal : flatNormalTexture3D;
    GpuTexture* e          = env ? env : defaultEnvCubemap;
    auto        localProbe = [&](int index) -> GpuTexture* {
        if (index < mesh3dReflectionProbes.count) {
            GpuTexture* gpu = gpuForTexture(mesh3dReflectionProbes.probes[index].cubemap);
            if (gpu && gpu->isCube) return gpu;
        }
        return defaultEnvCubemap;
    };
    GpuTexture*       probe0          = localProbe(0);
    GpuTexture*       probe1          = localProbe(1);
    GpuTexture*       h               = height ? height : flatHeightTexture3D;
    GpuTexture*       d               = depth ? depth : flatDepthTexture3D;
    GpuTexture*       shadow          = shadowDepthArray ? shadowDepthArray : defaultShadowTex;
    ClusteredStorage& st              = clusteredStorage[currentFrameSlot()];
    wgpu::TextureView decalAlbedoView = gpuForTextureOrWhite(decalFlatAlbedo)->view;
    wgpu::TextureView decalNormalView = gpuForTextureOrWhite(decalFlatNormal)->view;
    wgpu::TextureView decalParamsView = gpuForTextureOrWhite(decalFlatParams)->view;
    if (decalReady && lastDecalSlot < decalSlots.size()) {
        decalAlbedoView = decalSlots[lastDecalSlot].albedoView;
        decalNormalView = decalSlots[lastDecalSlot].normalView;
        decalParamsView = decalSlots[lastDecalSlot].paramsView;
    }

    WGPUBindGroupEntry entries[20]{};
    entries[0].binding      = 0;
    entries[0].buffer       = currentUboArena().buffer.Get();
    entries[0].size         = sizeof(Mesh3DClusteredUBO);
    entries[1].binding      = 1;
    entries[1].textureView  = a->view.Get();
    entries[2].binding      = 2;
    entries[2].textureView  = n->view.Get();
    entries[3].binding      = 3;
    entries[3].textureView  = e->view.Get();
    entries[4].binding      = 4;
    entries[4].buffer       = currentUboArena().buffer.Get();
    entries[4].size         = sizeof(ShadowUBO);
    entries[5].binding      = 5;
    entries[5].textureView  = shadow->view.Get();
    entries[6].binding      = 6;
    entries[6].textureView  = h->view.Get();
    entries[7].binding      = 7;
    entries[7].sampler      = a->sampler.Get();
    entries[8].binding      = 8;
    entries[8].sampler      = shadow->sampler.Get();
    entries[9].binding      = 9;
    entries[9].textureView  = d->view.Get();
    entries[10].binding     = 10;
    entries[10].buffer      = st.lights.Get();
    entries[10].size        = st.lightsCap;
    entries[11].binding     = 11;
    entries[11].buffer      = st.table.Get();
    entries[11].size        = st.tableCap;
    entries[12].binding     = 12;
    entries[12].buffer      = st.indices.Get();
    entries[12].size        = st.indicesCap;
    entries[13].binding     = 13;
    entries[13].textureView = aoView.Get();
    entries[14].binding     = 14;
    entries[14].sampler     = mainSampler.Get();
    entries[15].binding     = 15;
    entries[15].textureView = decalAlbedoView.Get();
    entries[16].binding     = 16;
    entries[16].textureView = decalNormalView.Get();
    entries[17].binding     = 17;
    entries[17].textureView = decalParamsView.Get();
    entries[18].binding     = 18;
    entries[18].textureView = probe0->view.Get();
    entries[19].binding     = 19;
    entries[19].textureView = probe1->view.Get();

    (void)frameUboOffset;
    (void)shadowUboOffset;
    WGPUBindGroupDescriptor desc{};
    desc.label      = sv("eve_mesh3d_clustered_group");
    desc.layout     = mesh3dClusteredSetLayout.Get();
    desc.entryCount = 20;
    desc.entries    = entries;
    return device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
}

// ---------------------------------------------------------------------------
// Mesh creation
// ---------------------------------------------------------------------------


}  // namespace eve::graphics::webgpu
