#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>
#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "graphics/webgpu/Canvas.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/PipelineBuilder.h"
#include "image/ImageData.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif
namespace eve::graphics::webgpu {
void Graphics::recordPresentedReadback(wgpu::CommandEncoder &encoder, const wgpu::Texture &surfaceTexture) {
    if (!surfaceCanCopySrc) throw Exception("WebGPU: surface does not support screen readback");
    const int w = static_cast<int>(surfaceTexture.GetWidth());
    const int h = static_cast<int>(surfaceTexture.GetHeight());
    if (!presentedReadback || presentedReadbackW != w || presentedReadbackH != h ||
        presentedReadbackFormat != surfaceFormat) {
        WGPUTextureDescriptor desc{};
        desc.label              = sv("eve_presented_readback");
        desc.dimension          = WGPUTextureDimension_2D;
        desc.size               = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        desc.format             = surfaceFormat;
        desc.mipLevelCount      = 1;
        desc.sampleCount        = 1;
        desc.usage              = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
        presentedReadback       = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor *>(&desc));
        presentedReadbackW      = w;
        presentedReadbackH      = h;
        presentedReadbackFormat = surfaceFormat;
    }
    WGPUTexelCopyTextureInfo from{}, to{};
    from.texture = surfaceTexture.Get();
    from.aspect  = WGPUTextureAspect_All;
    to.texture   = presentedReadback.Get();
    to.aspect    = WGPUTextureAspect_All;
    WGPUExtent3D extent{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    encoder.CopyTextureToTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo *>(&from),
                                 reinterpret_cast<const wgpu::TexelCopyTextureInfo *>(&to),
                                 reinterpret_cast<const wgpu::Extent3D *>(&extent));
}

Color Graphics::getPixel(int x, int y) {
    if (activeCanvas) return getPixelImpl(static_cast<OffscreenCanvas *>(activeCanvas), x, y);
    if ((screenReadbackEnabled && presentedReadback) || !sceneColorSlots.empty()) {
        return getPixelImpl(nullptr, x, y);
    }
    return clearColor;
}

image::ImageData *Graphics::newImageData() {
    if (activeCanvas) return newImageDataImpl(static_cast<OffscreenCanvas *>(activeCanvas));
    if ((screenReadbackEnabled && presentedReadback) || !sceneColorSlots.empty()) return newImageDataImpl(nullptr);
    return new image::ImageData(1, 1, "RGBA8");
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

namespace {

bool copyTextureToCpu(wgpu::Instance &instance, wgpu::Device &device, wgpu::Queue &queue, wgpu::Texture src, int width,
                      int height, std::vector<uint8_t> &outRgba, int bytesPerPixel = 4) {
    if (!src) return false;
    uint64_t bytesPerRow = static_cast<uint64_t>(width) * bytesPerPixel;
    bytesPerRow          = (bytesPerRow + 255) / 256 * 256;  // copy alignment 256
    uint64_t size        = bytesPerRow * height;

    WGPUBufferDescriptor bd{};
    bd.label            = sv("eve_readback");
    bd.size             = size;
    bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.mappedAtCreation = false;
    wgpu::Buffer dst    = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));

    wgpu::CommandEncoder     enc = device.CreateCommandEncoder();
    WGPUTexelCopyTextureInfo from{};
    from.texture  = src.Get();
    from.mipLevel = 0;
    from.aspect   = WGPUTextureAspect_All;
    from.origin   = {0, 0, 0};
    WGPUTexelCopyBufferInfo to{};
    to.buffer              = dst.Get();
    to.layout.offset       = 0;
    to.layout.bytesPerRow  = static_cast<uint32_t>(bytesPerRow);
    to.layout.rowsPerImage = static_cast<uint32_t>(height);
    WGPUExtent3D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    enc.CopyTextureToBuffer(reinterpret_cast<const wgpu::TexelCopyTextureInfo *>(&from),
                            reinterpret_cast<const wgpu::TexelCopyBufferInfo *>(&to),
                            reinterpret_cast<const wgpu::Extent3D *>(&extent));
    wgpu::CommandBuffer cmd = enc.Finish();
    queue.Submit(1, &cmd);

    struct MapState {
        bool               done    = false;
        bool               success = false;
        WGPUMapAsyncStatus status  = WGPUMapAsyncStatus_Force32;
    } map;
    WGPUBufferMapCallbackInfo cbInfo{};
#if defined(__EMSCRIPTEN__)
    cbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
#else
    cbInfo.mode = WGPUCallbackMode_WaitAnyOnly;
#endif
    cbInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView /*message*/, void *userdata1, void * /*userdata2*/) {
        auto *state    = static_cast<MapState *>(userdata1);
        state->status  = status;
        state->success = status == WGPUMapAsyncStatus_Success;
        state->done    = true;
    };
    cbInfo.userdata1  = &map;
    WGPUFuture future = wgpuBufferMapAsync(dst.Get(), WGPUMapMode_Read, 0, size, cbInfo);

#if defined(__EMSCRIPTEN__)
    int guard = 0;
    while (!map.done && guard < 2000) {
        emscripten_sleep(0);
        wgpuInstanceProcessEvents(instance.Get());
        ++guard;
    }
#else
    WGPUFutureWaitInfo waitInfo{};
    waitInfo.future = future;
    (void)wgpuInstanceWaitAny(instance.Get(), 1, &waitInfo, UINT64_MAX);
#endif
    if (!map.success) {
        std::fprintf(stderr, "[webgpu] texture readback map failed: status=%d done=%d\n", int(map.status),
                     map.done ? 1 : 0);
        return false;
    }

    const uint8_t *data = static_cast<const uint8_t *>(dst.GetConstMappedRange(0, size));
    if (!data) return false;
    const size_t tightRowBytes = static_cast<size_t>(width) * bytesPerPixel;
    outRgba.resize(tightRowBytes * height);
    for (int y = 0; y < height; ++y)
        std::memcpy(outRgba.data() + size_t(y) * tightRowBytes, data + size_t(y) * bytesPerRow, tightRowBytes);
    dst.Unmap();
    return true;
}

float decodeHalf(uint16_t value) {
    const uint32_t sign     = uint32_t(value & 0x8000u) << 16u;
    uint32_t       exponent = (value >> 10u) & 0x1fu;
    uint32_t       mantissa = value & 0x03ffu;
    uint32_t       bits     = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1u;
                --exponent;
            }
            mantissa &= 0x03ffu;
            bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
        }
    } else if (exponent == 31u) {
        bits = sign | 0x7f800000u | (mantissa << 13u);
    } else {
        bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
    }
    float result = 0.f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

float hdrToDisplay(float value) {
    const float linear = std::max(value, 0.f);
    const float mapped =
        std::clamp((linear * (2.51f * linear + 0.03f)) / (linear * (2.43f * linear + 0.59f) + 0.14f), 0.f, 1.f);
    return mapped <= 0.0031308f ? mapped * 12.92f : 1.055f * std::pow(mapped, 1.f / 2.4f) - 0.055f;
}

Color hdrPixelToColor(const uint8_t *pixel) {
    uint16_t channels[4]{};
    std::memcpy(channels, pixel, sizeof(channels));
    return Color(hdrToDisplay(decodeHalf(channels[0])), hdrToDisplay(decodeHalf(channels[1])),
                 hdrToDisplay(decodeHalf(channels[2])), std::clamp(decodeHalf(channels[3]), 0.f, 1.f));
}

}  // namespace

Color Graphics::getPixelImpl(OffscreenCanvas *canvas, int x, int y) {
    const bool presented = !canvas && screenReadbackEnabled && presentedReadback;
    const bool bgra      = presented && (presentedReadbackFormat == WGPUTextureFormat_BGRA8Unorm ||
                                    presentedReadbackFormat == WGPUTextureFormat_BGRA8UnormSrgb);
    int w = presented ? presentedReadbackW : canvas ? canvas->getWidth() : (sceneColorWidth > 0 ? sceneColorWidth : 1);
    int h = presented ? presentedReadbackH
            : canvas  ? canvas->getHeight()
                      : (sceneColorHeight > 0 ? sceneColorHeight : 1);
    if (x < 0 || y < 0 || x >= w || y >= h) return Color(0.f, 0.f, 0.f, 0.f);
    std::vector<uint8_t> rgba;
    wgpu::Texture        src = presented ? presentedReadback
                               : canvas  ? canvas->color
                                         : (sceneColorSlots.empty() ? nullptr : sceneColorSlots[lastPresentSlot].color);
    const bool hdrScene      = !presented && canvas == nullptr && sceneColorFormat == WGPUTextureFormat_RGBA16Float;
    if (!src || !copyTextureToCpu(instance, device, queue, src, w, h, rgba, hdrScene ? 8 : 4)) return clearColor;
    if (hdrScene) return hdrPixelToColor(rgba.data() + (size_t(y) * w + x) * 8);
    const uint8_t *p = rgba.data() + (size_t(y) * w + x) * 4;
    return Color(p[bgra ? 2 : 0] / 255.f, p[1] / 255.f, p[bgra ? 0 : 2] / 255.f, p[3] / 255.f);
}

image::ImageData *Graphics::newImageDataImpl(OffscreenCanvas *canvas) {
    const bool presented = !canvas && screenReadbackEnabled && presentedReadback;
    const bool bgra      = presented && (presentedReadbackFormat == WGPUTextureFormat_BGRA8Unorm ||
                                    presentedReadbackFormat == WGPUTextureFormat_BGRA8UnormSrgb);
    int w = presented ? presentedReadbackW : canvas ? canvas->getWidth() : (sceneColorWidth > 0 ? sceneColorWidth : 1);
    int h = presented ? presentedReadbackH
            : canvas  ? canvas->getHeight()
                      : (sceneColorHeight > 0 ? sceneColorHeight : 1);
    auto                *img = new image::ImageData(w, h, "RGBA8");
    std::vector<uint8_t> rgba;
    wgpu::Texture        src = presented ? presentedReadback
                               : canvas  ? canvas->color
                                         : (sceneColorSlots.empty() ? nullptr : sceneColorSlots[lastPresentSlot].color);
    const bool hdrScene      = !presented && canvas == nullptr && sceneColorFormat == WGPUTextureFormat_RGBA16Float;
    if (src && copyTextureToCpu(instance, device, queue, src, w, h, rgba, hdrScene ? 8 : 4)) {
        if (!hdrScene) {
            if (bgra)
                for (size_t pixel = 0; pixel < size_t(w) * h; ++pixel) std::swap(rgba[pixel * 4], rgba[pixel * 4 + 2]);
            std::memcpy(img->getData(), rgba.data(), rgba.size());
        } else {
            auto *dst = static_cast<uint8_t *>(img->getData());
            for (size_t pixel = 0; pixel < size_t(w) * h; ++pixel) {
                const Color color  = hdrPixelToColor(rgba.data() + pixel * 8);
                dst[pixel * 4 + 0] = uint8_t(std::clamp(color.r * 255.f, 0.f, 255.f));
                dst[pixel * 4 + 1] = uint8_t(std::clamp(color.g * 255.f, 0.f, 255.f));
                dst[pixel * 4 + 2] = uint8_t(std::clamp(color.b * 255.f, 0.f, 255.f));
                dst[pixel * 4 + 3] = uint8_t(std::clamp(color.a * 255.f, 0.f, 255.f));
            }
        }
    }
    return img;
}

image::ImageData *Graphics::newHDRImageDataImpl(OffscreenCanvas *canvas) {
    if (!canvas || !canvas->hdr) return nullptr;
    const int            w = canvas->getWidth();
    const int            h = canvas->getHeight();
    std::vector<uint8_t> rgba16f;
    if (!copyTextureToCpu(instance, device, queue, canvas->color, w, h, rgba16f, 8)) return nullptr;
    auto *img = new image::ImageData(w, h, "RGBA16F");
    std::memcpy(img->getData(), rgba16f.data(), rgba16f.size());
    return img;
}

image::ImageData *Graphics::readGBufferToImageData(const std::string &attachment) {
    submitPendingDeferredPasses();
    if (!device || gbufferSlots.empty() || gbufferWidth <= 0 || gbufferHeight <= 0) return nullptr;
    GbufferSlot  &slot = gbufferSlots[std::min<size_t>(lastGbufferSlot, gbufferSlots.size() - 1)];
    wgpu::Texture src;
    if (attachment == "depth")
        src = slot.depthColor;
    else if (attachment == "normal")
        src = slot.normal;
    else if (attachment == "albedo")
        src = slot.albedo;
    else
        return nullptr;

    std::vector<uint8_t> rgba;
    if (!copyTextureToCpu(instance, device, queue, src, gbufferWidth, gbufferHeight, rgba)) return nullptr;
    auto *image = new image::ImageData(gbufferWidth, gbufferHeight, "RGBA8");
    std::memcpy(image->getData(), rgba.data(), rgba.size());
    return image;
}

image::ImageData *Graphics::readDecalLayerToImageData(const std::string &attachment) {
    submitPendingDeferredPasses();
    if (!device || decalSlots.empty() || decalWidth <= 0 || decalHeight <= 0) return nullptr;
    DecalSlot    &slot = decalSlots[std::min<size_t>(lastDecalSlot, decalSlots.size() - 1)];
    wgpu::Texture src;
    if (attachment == "albedo")
        src = slot.albedo;
    else if (attachment == "normal")
        src = slot.normal;
    else if (attachment == "params")
        src = slot.params;
    else
        return nullptr;
    std::vector<uint8_t> rgba;
    if (!copyTextureToCpu(instance, device, queue, src, decalWidth, decalHeight, rgba)) return nullptr;
    auto *image = new image::ImageData(decalWidth, decalHeight, "RGBA8");
    std::memcpy(image->getData(), rgba.data(), rgba.size());
    return image;
}

// ---------------------------------------------------------------------------
// Async frame readback (browser)
// ---------------------------------------------------------------------------

namespace {

bool encodeRgbaToPng(const std::string &path, int width, int height, const std::vector<uint8_t> &rgba) {
    try {
        image::ImageData img(width, height, "RGBA8");
        std::memcpy(img.getData(), rgba.data(), rgba.size());
        std::unique_ptr<eve::filesystem::FileData> png(
            img.encode(medialoader::FormatHandler::ENCODED_PNG, path.c_str(), false));
        if (!png) return false;
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
        return out.good();
    } catch (...) {
        return false;
    }
}

}  // namespace

bool Graphics::beginFrameReadback(const std::string &path) {
    // A finished readback can be replaced; only refuse while one is in flight.
    if ((pendingReadback_ && !pendingReadback_->done) || !device) return false;
    const bool    presented = screenReadbackEnabled && presentedReadback;
    wgpu::Texture src       = presented ? presentedReadback : lastReadbackTex;
    int           w         = presented ? presentedReadbackW : lastReadbackW;
    int           h         = presented ? presentedReadbackH : lastReadbackH;
    if (!src || w <= 0 || h <= 0) {
        // Fall back to the scene color slot before anything was rendered.
        if (sceneColorSlots.empty()) return false;
        const uint32_t slot = lastPresentSlot;
        if (slot >= sceneColorSlots.size()) return false;
        src = sceneColorSlots[slot].color;
        w   = sceneColorWidth;
        h   = sceneColorHeight;
        if (!src || w <= 0 || h <= 0) return false;
    }

    const bool hdr = src.GetFormat() == wgpu::TextureFormat::RGBA16Float;
    const bool bgra =
        src.GetFormat() == wgpu::TextureFormat::BGRA8Unorm || src.GetFormat() == wgpu::TextureFormat::BGRA8UnormSrgb;
    uint64_t bytesPerRow = static_cast<uint64_t>(w * (hdr ? 8 : 4));
    bytesPerRow          = (bytesPerRow + 255) / 256 * 256;
    const uint64_t size  = bytesPerRow * static_cast<uint64_t>(h);

    WGPUBufferDescriptor bd{};
    bd.label            = sv("eve_readback");
    bd.size             = size;
    bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.mappedAtCreation = false;
    wgpu::Buffer dst    = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
    if (!dst) return false;

    // Copy the resolved scene color into the readback buffer, then map
    // asynchronously (two-phase: the pump waits for the map callback on the
    // browser event loop without ASYNCIFY sleeps).
    wgpu::CommandEncoder     enc = device.CreateCommandEncoder();
    WGPUTexelCopyTextureInfo from{};
    from.texture  = src.Get();
    from.mipLevel = 0;
    from.aspect   = WGPUTextureAspect_All;
    from.origin   = {0, 0, 0};
    WGPUTexelCopyBufferInfo to{};
    to.buffer              = dst.Get();
    to.layout.offset       = 0;
    to.layout.bytesPerRow  = static_cast<uint32_t>(bytesPerRow);
    to.layout.rowsPerImage = static_cast<uint32_t>(h);
    WGPUExtent3D extent{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    enc.CopyTextureToBuffer(reinterpret_cast<const wgpu::TexelCopyTextureInfo *>(&from),
                            reinterpret_cast<const wgpu::TexelCopyBufferInfo *>(&to),
                            reinterpret_cast<const wgpu::Extent3D *>(&extent));
    wgpu::CommandBuffer cmd = enc.Finish();
    queue.Submit(1, &cmd);

    auto pr         = std::make_unique<PendingReadback>();
    pr->path        = path;
    pr->width       = w;
    pr->height      = h;
    pr->bytesPerRow = bytesPerRow;
    pr->dst         = dst;
    pr->hdr         = hdr;
    pr->bgra        = bgra;

    WGPUBufferMapCallbackInfo cbInfo{};
    cbInfo.mode     = WGPUCallbackMode_AllowProcessEvents;
    cbInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView /*message*/, void *userdata1, void * /*userdata2*/) {
        auto *p   = static_cast<PendingReadback *>(userdata1);
        p->mapped = (status == WGPUMapAsyncStatus_Success);
    };
    cbInfo.userdata1 = pr.get();
    wgpuBufferMapAsync(dst.Get(), WGPUMapMode_Read, 0, size, cbInfo);

    pendingReadback_ = std::move(pr);
    return true;
}

int Graphics::frameReadbackStatus() const {
    if (!pendingReadback_) return 0;
    if (!pendingReadback_->done) return 1;
    return pendingReadback_->ok ? 2 : 3;
}

void Graphics::pumpReadback() {
    if (!pendingReadback_ || pendingReadback_->done) return;
    auto &pr = *pendingReadback_;
    if (!pr.mapped) {
        // The map callback is delivered on the browser event loop between
        // frames (emdawnwebgpu callUserCallback), so no ASYNCIFY sleep is
        // needed here �?this runs from present() on the main loop.
#if defined(__EMSCRIPTEN__)
        wgpuInstanceProcessEvents(instance.Get());
#endif
        return;
    }
    const uint8_t *data =
        static_cast<const uint8_t *>(pr.dst.GetConstMappedRange(0, pr.bytesPerRow * static_cast<uint64_t>(pr.height)));
    std::vector<uint8_t> rgba(static_cast<size_t>(pr.width) * pr.height * 4);
    if (data) {
        for (int y = 0; y < pr.height; ++y) {
            auto       *dst = rgba.data() + size_t(y) * pr.width * 4;
            const auto *row = data + size_t(y) * pr.bytesPerRow;
            for (int x = 0; x < pr.width; ++x) {
                if (pr.hdr) {
                    const auto color = hdrPixelToColor(row + size_t(x) * 8);
                    dst[x * 4]       = uint8_t(std::clamp(color.r * 255.f, 0.f, 255.f));
                    dst[x * 4 + 1]   = uint8_t(std::clamp(color.g * 255.f, 0.f, 255.f));
                    dst[x * 4 + 2]   = uint8_t(std::clamp(color.b * 255.f, 0.f, 255.f));
                    dst[x * 4 + 3]   = uint8_t(std::clamp(color.a * 255.f, 0.f, 255.f));
                } else {
                    dst[x * 4]     = row[x * 4 + (pr.bgra ? 2 : 0)];
                    dst[x * 4 + 1] = row[x * 4 + 1];
                    dst[x * 4 + 2] = row[x * 4 + (pr.bgra ? 0 : 2)];
                    dst[x * 4 + 3] = row[x * 4 + 3];
                }
            }
        }
    }
    pr.dst.Unmap();
    pr.ok   = data && encodeRgbaToPng(pr.path, pr.width, pr.height, rgba);
    pr.done = true;
}


}  // namespace eve::graphics::webgpu
