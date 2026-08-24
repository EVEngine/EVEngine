// Vulkan backend implementation — 2D drawing, textures and batching.
//
// Re-split from the merged dev single-TU Graphics.cpp (pure move;
// dev changes preserved). Shared helpers live in GraphicsInternal.h.

#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/Canvas.h"
#include "graphics/Light.h"
#include "graphics/AntiAliasing.h"
#include "graphics/RenderControl.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <memory>


#include <assimp/mesh.h>
#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/vector3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "graphics/shaders/textured_vert_spv.inc"
#include "graphics/shaders/textured_frag_spv.inc"
#include "graphics/shaders/mesh3d_vert_spv.inc"
#include "graphics/shaders/mesh3d_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {

namespace {

void transitionSampledColorForTransfer(vk::CommandBuffer cb, vkb::ColorTarget &image,
                                       vk::ImageLayout newLayout) {
    vk::ImageMemoryBarrier barrier{};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.oldLayout = image.currentLayout();
    barrier.newLayout = newLayout;
    barrier.image = image.image();
    barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    vk::PipelineStageFlags srcStages;
    vk::PipelineStageFlags dstStages;
    if (newLayout == vk::ImageLayout::eTransferSrcOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        srcStages = vk::PipelineStageFlagBits::eVertexShader |
                    vk::PipelineStageFlagBits::eFragmentShader |
                    vk::PipelineStageFlagBits::eComputeShader;
        dstStages = vk::PipelineStageFlagBits::eTransfer;
    } else {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        srcStages = vk::PipelineStageFlagBits::eTransfer;
        dstStages = vk::PipelineStageFlagBits::eVertexShader |
                    vk::PipelineStageFlagBits::eFragmentShader |
                    vk::PipelineStageFlagBits::eComputeShader;
    }
    cb.pipelineBarrier(srcStages, dstStages, {}, 0, nullptr, 0, nullptr, 1, &barrier);
    image.setCurrentLayout(newLayout);
}

}  // namespace

// Shader helper functions. They used to live in GraphicsInternal.h (which is
// an anonymous-namespace header copied into every backend TU); only this file
// uses them, so they are defined here to avoid the per-TU duplication and the
// header's dependency on the filesystem module.
namespace {

std::vector<uint32_t> loadSpirvBytes(const void *data, size_t size) {
    if (!data || size < 4 || (size % 4) != 0)
        throw Exception("SPIR-V: invalid size %zu", size);
    const auto *words = static_cast<const uint32_t *>(data);
    if (words[0] != 0x07230203)
        throw Exception("SPIR-V: bad magic (expected 0x07230203)");
    return std::vector<uint32_t>(words, words + size / 4);
}

std::vector<uint32_t> readSpirvFile(const std::string &path) {
    auto *fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> fd(fs->read(path));
    if (!fd) throw Exception("newShaderFromSpvFile: failed to read '%s'", path.c_str());
    return loadSpirvBytes(fd->getData(), fd->getSize());
}

std::vector<uint32_t> compileGlslWithGlslc(const std::string &source, const char *stage) {
    if (source.empty()) throw Exception("newShader: empty %s GLSL", stage);
#if defined(_WIN32)
    (void)source;
    (void)stage;
    throw Exception("newShader: GLSL compile via glslc is not supported on Windows; "
                    "use newShaderFromSpv / newShaderFromSpvFile");
#else
    char inPath[] = "/tmp/eve_shader_XXXXXX";
    int fd = mkstemp(inPath);
    if (fd < 0) throw Exception("newShader: mkstemp failed");
    std::string outPath = std::string(inPath) + ".spv";
    {
        ssize_t n = write(fd, source.data(), source.size());
        close(fd);
        if (n < 0 || size_t(n) != source.size()) {
            unlink(inPath);
            throw Exception("newShader: failed to write temp GLSL");
        }
    }

    std::string cmd = std::string("glslc -fshader-stage=") + stage + " \"" + inPath + "\" -o \"" +
                      outPath + "\" 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    std::string err;
    if (pipe) {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) err += buf;
        int status = pclose(pipe);
        unlink(inPath);
        if (status != 0) {
            unlink(outPath.c_str());
            throw Exception("newShader: glslc failed for %s:\n%s", stage, err.c_str());
        }
    } else {
        unlink(inPath);
        throw Exception("newShader: glslc not available (popen failed)");
    }

    FILE *f = fopen(outPath.c_str(), "rb");
    if (!f) {
        unlink(outPath.c_str());
        throw Exception("newShader: failed to open compiled SPIR-V");
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(static_cast<size_t>(sz > 0 ? sz : 0));
    if (sz > 0 && fread(bytes.data(), 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz)) {
        fclose(f);
        unlink(outPath.c_str());
        throw Exception("newShader: failed to read compiled SPIR-V");
    }
    fclose(f);
    unlink(outPath.c_str());
    return loadSpirvBytes(bytes.data(), bytes.size());
#endif
}

}  // namespace

void Graphics::ensurePresentCaptureHook() {
    // Screen readback is recorded inline into the present command buffer (see
    // flushToSwapchain / abortOpen3DFrame), so the present model must never
    // get a post-submit hook: drawFrame() waits for this frame's fence
    // whenever the hook is set, which serializes every frame and erases the
    // multi-frame overlap we rely on for async rendering.
    presentModel.after_render_before_present = nullptr;
}

void Graphics::ensureReadbackSlots() {
    if (pixelWidth <= 0 || pixelHeight <= 0) return;
    const size_t bytes = size_t(pixelWidth) * size_t(pixelHeight) * 4;
    const size_t want = std::max<size_t>(2, frameSlotCount());
    if (!screenReadbackSlots.empty() && screenReadbackSlots.size() >= want &&
        screenReadbackBytes == bytes)
        return;
    // Recreating the staging ring must never race in-flight copies. Callers
    // run on the render thread, and a size change implies the swapchain was
    // rebuilt under waitIdle (rebuildSwapchainIfNeeded / recreate path).
    for (auto &slot : screenReadbackSlots) {
        if (slot.mapped) {
            device->unmapMemory(slot.staging.memory);
            slot.mapped = nullptr;
        }
        slot.staging.release();
    }
    screenReadbackSlots.clear();
    screenReadbackSlots.resize(want);
    for (auto &slot : screenReadbackSlots) {
        slot.staging = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eTransferDst,
                                          vk::DeviceSize(bytes),
                                          vk::MemoryPropertyFlagBits::eHostVisible |
                                              vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    screenReadbackBytes = bytes;
    readbackReady = false;
    readbackCpuSynced = false;
    readbackWriteSlot = 0;
}

bool Graphics::recordSwapchainReadback(vk::CommandBuffer cb) {
    if (!screenReadbackEnabled || pixelWidth <= 0 || pixelHeight <= 0) return false;
    if (!presentModel.has_acquired_image) return false;
    const vk::Format fmt = swapchain.image_format;
    const bool bgra = (fmt == vk::Format::eB8G8R8A8Unorm || fmt == vk::Format::eB8G8R8A8Srgb);
    const bool rgba = (fmt == vk::Format::eR8G8B8A8Unorm || fmt == vk::Format::eR8G8B8A8Srgb);
    if (!bgra && !rgba) return false;
    readbackBgra = bgra;

    const uint32_t imageIndex = presentModel.acquired_image_index;
    auto &images = swapchain.get_images();
    if (imageIndex >= images.size()) return false;

    ensureReadbackSlots();
    if (screenReadbackSlots.empty()) return false;

    const size_t slot = size_t(presentRecording.slot().index) % screenReadbackSlots.size();
    readbackWriteSlot = slot;
    const vk::Image image = images[imageIndex];
    // Recorded at the end of the present command buffer: the swapchain render
    // pass has already transitioned the image back to PresentSrcKHR, and the
    // copy runs in the same submit+present — no extra queue submit, no
    // waitIdle, no per-frame buffer allocation.
    vk::ImageMemoryBarrier toTransfer{};
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.oldLayout = vk::ImageLayout::ePresentSrcKHR;
    toTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    toTransfer.image = image;
    toTransfer.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    // The render pass has already transitioned the image to PresentSrcKHR, but
    // there is no fence between it and this copy — the barrier must order the
    // render-pass color writes against the transfer explicitly.
    toTransfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                       vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0, nullptr, 1,
                       &toTransfer);

    vk::BufferImageCopy region{};
    region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.imageExtent = vk::Extent3D{uint32_t(pixelWidth), uint32_t(pixelHeight), 1};
    cb.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal,
                         screenReadbackSlots[slot].staging.buffer, region);

    vk::ImageMemoryBarrier toPresent = toTransfer;
    toPresent.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
    toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
    toPresent.srcAccessMask = vk::AccessFlagBits::eTransferRead;
    toPresent.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                       vk::PipelineStageFlagBits::eBottomOfPipe, {}, 0, nullptr, 0, nullptr, 1,
                       &toPresent);
    return true;
}

void Graphics::syncReadbackCpu() {
    if (!readbackReady || screenReadbackSlots.empty()) return;
    if (readbackCpuSynced && !lastFrameRgba.empty()) return;
    if (readbackWriteSlot >= screenReadbackSlots.size()) return;

    const size_t bytes = screenReadbackBytes;
    if (bytes == 0) return;

    // The newest copy lives in the present submission of this frame slot; wait
    // only that slot's fence instead of a device-wide waitIdle.
    presentModel.waitForFrameSlot(readbackWriteSlot);

    auto &slot = screenReadbackSlots[readbackWriteSlot];
    if (!slot.mapped)
        slot.mapped = device->mapMemory(slot.staging.memory, 0, vk::DeviceSize(bytes));

    lastFrameRgba.resize(bytes);
    if (readbackBgra) {
        // BGRA -> RGBA byte swap, two pixels per 64-bit word (masked swap).
        const size_t words = bytes / 8;
        const uint64_t *src64 = static_cast<const uint64_t *>(slot.mapped);
        uint64_t *dst64 = reinterpret_cast<uint64_t *>(lastFrameRgba.data());
        for (size_t i = 0; i < words; ++i) {
            const uint64_t v = src64[i];
            dst64[i] = (v & 0xFF00FF00FF00FF00ull) |
                       ((v & 0x000000FF000000FFull) << 16) |
                       ((v >> 16) & 0x000000FF000000FFull);
        }
        for (size_t i = words * 8; i < bytes; i += 4) {
            const uint32_t v = *reinterpret_cast<const uint32_t *>(
                static_cast<const uint8_t *>(slot.mapped) + i);
            const uint32_t out = (v & 0xFF00FF00u) | ((v & 0xFFu) << 16) | ((v >> 16) & 0xFFu);
            std::memcpy(lastFrameRgba.data() + i, &out, 4);
        }
    } else {
        std::memcpy(lastFrameRgba.data(), slot.mapped, bytes);
    }
    readbackCpuSynced = true;
}

void Graphics::destroyReadbackResources() {
    for (auto &slot : screenReadbackSlots) {
        if (slot.mapped) {
            device->unmapMemory(slot.staging.memory);
            slot.mapped = nullptr;
        }
        slot.staging.release();
    }
    screenReadbackSlots.clear();
    screenReadbackBytes = 0;
    readbackReady = false;
    readbackCpuSynced = false;
    hasPresentedFrame = false;
}

image::ImageData *Graphics::newImageData() {
    syncReadbackCpu();
    if (!hasPresentedFrame || lastFrameRgba.empty())
        throw Exception("Graphics::newImageData: no presented frame");
    auto *img = new image::ImageData(pixelWidth, pixelHeight, "RGBA8");
    std::memcpy(img->getData(), lastFrameRgba.data(), lastFrameRgba.size());
    return img;
}

Color Graphics::getPixel(int x, int y) {
    syncReadbackCpu();
    if (!hasPresentedFrame || lastFrameRgba.empty())
        throw Exception("Graphics::getPixel: no presented frame");
    if (x < 0 || y < 0 || x >= width || y >= height)
        throw Exception("Graphics::getPixel: out of bounds (%d,%d)", x, y);

    const int pxX = (width > 0) ? int((int64_t(x) * pixelWidth) / width) : x;
    const int pxY = (height > 0) ? int((int64_t(y) * pixelHeight) / height) : y;
    const int cx = std::min(std::max(pxX, 0), pixelWidth - 1);
    const int cy = std::min(std::max(pxY, 0), pixelHeight - 1);
    const size_t i = (size_t(cy) * size_t(pixelWidth) + size_t(cx)) * 4;
    float r = lastFrameRgba[i + 0] / 255.f;
    float g = lastFrameRgba[i + 1] / 255.f;
    float b = lastFrameRgba[i + 2] / 255.f;
    float a = lastFrameRgba[i + 3] / 255.f;
    // If surface ended up sRGB, convert encoded bytes back to linear Color space.
    const vk::Format fmt = swapchain.image_format;
    if (fmt == vk::Format::eB8G8R8A8Srgb || fmt == vk::Format::eR8G8B8A8Srgb) {
        auto toLinear = [](float u) {
            return (u <= 0.04045f) ? (u / 12.92f) : std::pow((u + 0.055f) / 1.055f, 2.4f);
        };
        r = toLinear(r);
        g = toLinear(g);
        b = toLinear(b);
    }
    return Color(r, g, b, a);
}

image::ImageData *Graphics::renderEntityIdMask(
    const std::vector<eve::graphics::Graphics::EntityIdDraw> &draws, const glm::mat4 &viewProj,
    int maskW, int maskH) {
    if (!initialized || maskW <= 0 || maskH <= 0) return nullptr;
    // G-buffer pipeline / render pass are created lazily by createGBufferResources,
    // so that must run before the availability check.
    createGBufferResources(maskW, maskH);
    if (!gbufferPipeline || !gbufferRenderPass) return nullptr;
    auto *slot = currentGBufferSlot();
    if (!slot || !slot->framebuffer || !whiteTexture) return nullptr;

    // Render each mesh with a flat idColor into the G-buffer albedo attachment
    // (location 2) by reusing the G-buffer pipeline (its fragment shader writes
    // outAlbedo = albedo * tint, so passing a white texture + idColor tint gives
    // a per-pixel flat entity-ID color).
    std::vector<GBufferDraw> idDraws;
    idDraws.reserve(draws.size());
    auto u8 = [](float x) -> uint32_t {
        return uint32_t(std::lround(std::clamp(x, 0.f, 1.f) * 255.f));
    };
    for (const auto &d : draws) {
        if (!d.mesh || !d.mesh->gpuHandle) continue;
        GBufferDraw gd{};
        gd.mesh = d.mesh;
        gd.albedo = whiteTexture;
        gd.push.mvp = viewProj * d.model;
        gd.push.modelR0 = glm::vec4(d.model[0][0], d.model[1][0], d.model[2][0], d.model[3][0]);
        gd.push.modelR1 = glm::vec4(d.model[0][1], d.model[1][1], d.model[2][1], d.model[3][1]);
        gd.push.modelR2 = glm::vec4(d.model[0][2], d.model[1][2], d.model[2][2], d.model[3][2]);
        const uint32_t packed = u8(d.idColor.r) | (u8(d.idColor.g) << 8) |
                                (u8(d.idColor.b) << 16) | (u8(d.idColor.a) << 24);
        gd.push.clip = glm::vec4(0.1f, 100.f, glm::uintBitsToFloat(packed), 0.f);
        idDraws.push_back(gd);
    }
    if (idDraws.empty()) return nullptr;

    const uint32_t w = uint32_t(maskW);
    const uint32_t h = uint32_t(maskH);
    const vk::DeviceSize byteSize = vk::DeviceSize(w) * vk::DeviceSize(h) * 4;
    vkb::GenericBuffer staging(device, vk::BufferUsageFlagBits::eTransferDst, byteSize,
                               vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent);

    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                std::array<vk::ClearValue, 4> clears{};
                                clears[0].color = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
                                clears[1].color = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
                                clears[2].color = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
                                clears[3].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
                                vk::RenderPassBeginInfo rpBegin{};
                                rpBegin.renderPass = gbufferRenderPass;
                                rpBegin.framebuffer = slot->framebuffer;
                                rpBegin.renderArea = vk::Rect2D{{0, 0}, {w, h}};
                                rpBegin.clearValueCount = uint32_t(clears.size());
                                rpBegin.pClearValues = clears.data();
                                slot->normal.beginColorAttachment();
                                slot->depthColor.beginColorAttachment();
                                slot->albedo.beginColorAttachment();
                                slot->depth.beginDepthAttachment();
                                cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
                                setViewportAndScissor(cb, w, h);
                                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gbufferPipeline);
                                for (const auto &d : idDraws) {
                                    auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
                                    if (!gpuMesh) continue;
                                    if (whiteTexture && whiteTexture->gpuHandle && texSetLayout) {
                                        auto *gpuTex = static_cast<GpuTexture *>(whiteTexture->gpuHandle);
                                        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                              gbufferPipelineLayout, 0, 1,
                                                              gpuTex->descriptorSet.ptr(), 0, nullptr);
                                    }
                                    cb.pushConstants(gbufferPipelineLayout,
                                                     vk::ShaderStageFlagBits::eVertex |
                                                         vk::ShaderStageFlagBits::eFragment,
                                                     0, sizeof(GBufferPush), &d.push);
                                    drawIndexedMesh(cb, *gpuMesh);
                                }
                                cb.endRenderPass();
                                slot->normal.endSampledLayout();
                                slot->depthColor.endSampledLayout();
                                slot->albedo.endSampledLayout();
                                slot->depth.endSampledLayout();

                                // Copy the albedo attachment (location 2 = ID colors) to CPU.
                                slot->albedo.setLayout(cb, vk::ImageLayout::eTransferSrcOptimal);
                                vk::BufferImageCopy region{};
                                region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                                region.imageExtent = vk::Extent3D{w, h, 1};
                                cb.copyImageToBuffer(slot->albedo.image(),
                                                     vk::ImageLayout::eTransferSrcOptimal, staging.buffer,
                                                     region);
                                slot->albedo.setLayout(cb, vk::ImageLayout::eShaderReadOnlyOptimal);
                            });

    auto *img = new image::ImageData(int(w), int(h), "RGBA8");
    void *mapped = device->mapMemory(staging.memory, 0, byteSize);
    std::memcpy(img->getData(), mapped, size_t(byteSize));
    device->unmapMemory(staging.memory);
    staging.release();

    // 让 RenderControl 的 GBuffer 也指向该槽位（镜像 endGBufferPass），这样
    // 上层可通过 getDepthTexture()/getNormalTexture() 读取本次离屏 ID 渲染
    // 生成的深度/法线（供 capture_render_frame 的 depth/normal 复用）。
    if (RenderControl *rc = getRenderControl()) {
        rc->getGBuffer()->setTargets(int(w), int(h), &slot->depthColorTex, &slot->normalTex,
                                     &slot->albedoTex, &slot->depthTex);
    }
    return img;
}

image::ImageData *Graphics::readGBufferToImageData(const std::string &attachment) {
    if (!initialized) return nullptr;
    auto *slot = currentGBufferSlot();
    if (!slot) return nullptr;
    vkb::ColorTarget *src = nullptr;
    if (attachment == "depth")
        src = &slot->depthColor;  // RGBA8 linear depth
    else if (attachment == "normal")
        src = &slot->normal;
    else if (attachment == "albedo")
        src = &slot->albedo;
    else
        return nullptr;

    const uint32_t w = uint32_t(gbufferWidth);
    const uint32_t h = uint32_t(gbufferHeight);
    if (w == 0 || h == 0) return nullptr;

    const vk::DeviceSize byteSize = vk::DeviceSize(w) * vk::DeviceSize(h) * 4;
    if (gbufferPending && !gbufferPassDraws.empty()) recordDeferredFrameGraph();
    vkb::GenericBuffer staging(device, vk::BufferUsageFlagBits::eTransferDst, byteSize,
                               vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent);
    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                transitionSampledColorForTransfer(
                                    cb, *src, vk::ImageLayout::eTransferSrcOptimal);
                                vk::BufferImageCopy region{};
                                region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                                region.imageExtent = vk::Extent3D{w, h, 1};
                                cb.copyImageToBuffer(src->image(), vk::ImageLayout::eTransferSrcOptimal,
                                                     staging.buffer, region);
                                transitionSampledColorForTransfer(
                                    cb, *src, vk::ImageLayout::eShaderReadOnlyOptimal);
                            });

    auto *img = new image::ImageData(int(w), int(h), "RGBA8");
    void *mapped = device->mapMemory(staging.memory, 0, byteSize);
    std::memcpy(img->getData(), mapped, size_t(byteSize));
    device->unmapMemory(staging.memory);
    staging.release();
    return img;
}

image::ImageData *Graphics::readDecalLayerToImageData(const std::string &attachment) {
    if (!initialized) return nullptr;
    auto *slot = currentDecalSlot();
    auto *gslot = currentGBufferSlot();
    if (!slot || !gslot || !slot->framebuffer || !decalPipeline || !decalRenderPass ||
        !gbufferPipeline || !gbufferRenderPass || !gslot->framebuffer)
        return nullptr;
    vkb::ColorTarget *src = nullptr;
    if (attachment == "normal")
        src = &slot->normal;
    else if (attachment == "params")
        src = &slot->params;
    else if (attachment == "albedo")
        src = &slot->albedo;
    else
        return nullptr;

    const uint32_t w = uint32_t(decalWidth);
    const uint32_t h = uint32_t(decalHeight);
    if (w == 0 || h == 0) return nullptr;
    if (gbufferPending && !gbufferPassDraws.empty()) recordDeferredFrameGraph();

    const vk::DeviceSize byteSize = vk::DeviceSize(w) * vk::DeviceSize(h) * 4;
    vkb::GenericBuffer staging(device, vk::BufferUsageFlagBits::eTransferDst, byteSize,
                               vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent);
    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                // The G-buffer was submitted through the production FrameGraph
                                // above. Queue order makes it visible to this immediate pass.
                                recordDecalPassInto(cb, *slot, *gslot);
                                transitionSampledColorForTransfer(
                                    cb, *src, vk::ImageLayout::eTransferSrcOptimal);
                                vk::BufferImageCopy region{};
                                region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0,
                                                           1};
                                region.imageExtent = vk::Extent3D{w, h, 1};
                                cb.copyImageToBuffer(src->image(),
                                                     vk::ImageLayout::eTransferSrcOptimal,
                                                     staging.buffer, region);
                                transitionSampledColorForTransfer(
                                    cb, *src, vk::ImageLayout::eShaderReadOnlyOptimal);
                            });

    auto *img = new image::ImageData(int(w), int(h), "RGBA8");
    void *mapped = device->mapMemory(staging.memory, 0, byteSize);
    std::memcpy(img->getData(), mapped, size_t(byteSize));
    device->unmapMemory(staging.memory);
    staging.release();
    return img;
}

Canvas *Graphics::newCanvas(int w, int h) {
    ASSERT(initialized);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    if (!initialized) throw Exception("newCanvas: graphics not initialized");
    if (w <= 0 || h <= 0) throw Exception("newCanvas: invalid size");
    ensureOffscreenPipelines();
    auto c = std::make_unique<OffscreenCanvas>(this, w, h);
    Canvas *raw = c.get();
    ownedCanvases.push_back(std::move(c));
    return raw;
}

void Graphics::setCanvas(Canvas *canvas) {
    Canvas *next = canvas;
    if (next == static_cast<Canvas *>(this)) next = nullptr;
    if (next == activeCanvas) return;
    bool hasSolid = false;
    for (const auto &sb : solidBatches)
        if (!sb.batch.empty()) hasSolid = true;
    if (hasSolid || !texturedBatches.empty()) flushBatch();
    activeCanvas = next;
}

bool Graphics::isCanvasActive() const {
    return activeCanvas != nullptr;
}

Canvas *Graphics::getCanvas() const {
    return activeCanvas ? activeCanvas : const_cast<Graphics *>(this);
}

void Graphics::setViewportSize(int newW, int newH, int newPw, int newPh) {
    bool changed = (newW != width) || (newH != height) || (newPw != pixelWidth) || (newPh != pixelHeight);
    width = newW;
    height = newH;
    pixelWidth = newPw;
    pixelHeight = newPh;
    if (initialized && changed) swapchainDirty = true;
}

void Graphics::clear2DBatches() {
    solidBatches.clear();
    texturedBatches.clear();
    litBatches.clear();
    overlaySpans.clear();
    engine3DSpans.clear();
    pendingSceneResolve.reset();
    pendingUiResolve.reset();
    sceneColorComposited = false;
}

void Graphics::noteSolidOverlay() {
    if (solidBatches.empty()) return;
    const uint32_t idx = uint32_t(solidBatches.size() - 1);
    const uint32_t n = uint32_t(solidBatches.back().batch.vertices().size());
    auto &spans = recordingEngine3D_ ? engine3DSpans : overlaySpans;
    if (!spans.empty() && spans.back().kind == OverlayKind::Solid &&
        spans.back().index == idx) {
        spans.back().vertCount = n - spans.back().vertBegin;
        return;
    }
    const uint32_t begin = n >= 6u ? n - 6u : 0u;
    spans.push_back({OverlayKind::Solid, idx, begin, n - begin});
}

void Graphics::noteTexturedOverlay(Texture *tex) {
    if (tex && tex == getSceneColorTexture()) sceneColorComposited = true;
    auto &spans = recordingEngine3D_ ? engine3DSpans : overlaySpans;
    const uint32_t idx = texturedBatches.empty() ? 0u : uint32_t(texturedBatches.size() - 1);
    if (!spans.empty() && spans.back().kind == OverlayKind::Textured && spans.back().index == idx)
        return;
    spans.push_back({OverlayKind::Textured, idx, 0, 0});
}

void Graphics::noteLitOverlay() {
    auto &spans = recordingEngine3D_ ? engine3DSpans : overlaySpans;
    const uint32_t idx = litBatches.empty() ? 0u : uint32_t(litBatches.size() - 1);
    if (!spans.empty() && spans.back().kind == OverlayKind::Lit && spans.back().index == idx)
        return;
    spans.push_back({OverlayKind::Lit, idx, 0, 0});
}

void Graphics::clear(std::optional<Color> color, std::optional<int>, std::optional<double>) {
    // Keep 3D framebuffer contents when composing 2D on top of an open 3D pass.
    if (frameHad3D && activeCanvas == nullptr) return;
    clearColor = color.value_or(backgroundColor);
    hasPendingClear = true;
    clear2DBatches();
    if (auto *oc = dynamic_cast<OffscreenCanvas *>(activeCanvas)) {
        oc->clear(clearColor, std::nullopt, std::nullopt);
    }
}

void Graphics::drawSolidRect(float x, float y, float w, float h, const Color &color,
                             BlendMode blend) {
    auto it = std::find_if(solidBatches.begin(), solidBatches.end(),
                           [&](const SolidBatch &sb) { return sb.blend == blend; });
    if (it == solidBatches.end()) {
        solidBatches.push_back(SolidBatch{blend, Batcher{}});
        it = solidBatches.end() - 1;
    }
    it->batch.addRect(x, y, w, h, color);
    noteSolidOverlay();
}

void Graphics::drawSolidRectRotated(float cx, float cy, float w, float h, float degrees,
                                    const Color &color, BlendMode blend) {
    auto it = std::find_if(solidBatches.begin(), solidBatches.end(),
                           [&](const SolidBatch &sb) { return sb.blend == blend; });
    if (it == solidBatches.end()) {
        solidBatches.push_back(SolidBatch{blend, Batcher{}});
        it = solidBatches.end() - 1;
    }
    it->batch.addRectRotated(cx, cy, w, h, degrees, color);
    noteSolidOverlay();
}


float Graphics::getMaxAnisotropy() const { return maxSamplerAnisotropy; }

vk::Sampler Graphics::createVkSampler(const TextureSampler &sampler, uint32_t mipLevels) const {
    auto wrapMode = [](bool repeat) {
        return repeat ? vk::SamplerAddressMode::eRepeat : vk::SamplerAddressMode::eClampToEdge;
    };
    auto toFilter = [](FilterMode m) {
        return m == FilterMode::Nearest ? vk::Filter::eNearest : vk::Filter::eLinear;
    };
    auto toMip = [](MipmapMode m) {
        return m == MipmapMode::Nearest ? vk::SamplerMipmapMode::eNearest
                                        : vk::SamplerMipmapMode::eLinear;
    };

    const bool useMips = sampler.mipmap != MipmapMode::Disabled && mipLevels > 1;
    float maxLod = useMips ? std::min(sampler.maxLod, float(mipLevels - 1)) : 0.f;
    if (maxLod < sampler.minLod) maxLod = sampler.minLod;

    float aniso = 1.f;
    bool enableAniso = false;
    if (sampler.maxAnisotropy > 1.f && maxSamplerAnisotropy > 1.f) {
        enableAniso = true;
        aniso = std::min(sampler.maxAnisotropy, maxSamplerAnisotropy);
    }

    vkb::SamplerBuilder sb;
    return sb.magFilter(toFilter(sampler.mag))
        .minFilter(toFilter(sampler.min))
        .mipmapMode(useMips ? toMip(sampler.mipmap) : vk::SamplerMipmapMode::eNearest)
        .addressModeU(wrapMode(sampler.repeatU))
        .addressModeV(wrapMode(sampler.repeatV))
        .addressModeW(wrapMode(sampler.repeatW))
        .mipLodBias(sampler.lodBias)
        .anisotropyEnable(enableAniso ? VK_TRUE : VK_FALSE)
        .maxAnisotropy(aniso)
        .minLod(useMips ? sampler.minLod : 0.f)
        .maxLod(maxLod)
        .build(device);
}


void Graphics::drawTexturedRect(Texture *texture, float x, float y, float w, float h, const Color &color) {
    drawTexturedRectUV(texture, x, y, w, h, 0.f, 0.f, 1.f, 1.f, color);
}

void Graphics::drawTexturedRectShader(Texture *texture, Shader *shader, float x, float y, float w,
                                      float h, const Color &color) {
    drawTexturedRectShaderUV(texture, shader, x, y, w, h, 0.f, 0.f, 1.f, 1.f, color);
}

void Graphics::drawTexturedRectUV(Texture *texture, float x, float y, float w, float h, float u0,
                                  float v0, float u1, float v1, const Color &color) {
    drawTexturedRectShaderUV(texture, currentShader, x, y, w, h, u0, v0, u1, v1, color);
}

void Graphics::drawTexturedRectShaderUV(Texture *texture, Shader *shader, float x, float y, float w,
                                        float h, float u0, float v0, float u1, float v1,
                                        const Color &color, bool rotatedUV, BlendMode blend) {
    if (!texture) {
        drawSolidRect(x, y, w, h, color, blend);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture ||
        texturedBatches.back().depth != nullptr ||
        texturedBatches.back().shader != shader ||
        texturedBatches.back().blend != blend) {
        texturedBatches.push_back(TexturedBatch{texture, nullptr, shader, blend, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1, rotatedUV);
    noteTexturedOverlay(texture);
}

void Graphics::drawTexturedRectShaderUVRotated(Texture *texture, Shader *shader, float cx, float cy,
                                               float w, float h, float degrees, float u0, float v0,
                                               float u1, float v1, const Color &color,
                                               bool rotatedUV, BlendMode blend) {
    if (!texture) {
        drawSolidRect(cx - w * 0.5f, cy - h * 0.5f, w, h, color, blend);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture ||
        texturedBatches.back().depth != nullptr ||
        texturedBatches.back().shader != shader ||
        texturedBatches.back().blend != blend) {
        texturedBatches.push_back(TexturedBatch{texture, nullptr, shader, blend, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRectRotated(cx, cy, w, h, degrees, color, u0, v0, u1, v1,
                                                        rotatedUV);
    noteTexturedOverlay(texture);
}

void Graphics::drawTexturedRectShaderDepth(Texture *color, Texture *depth, Shader *shader, float x,
                                           float y, float w, float h, const Color &tint) {
    if (!color) {
        drawSolidRect(x, y, w, h, tint);
        return;
    }
    if (!depth) {
        drawTexturedRectShader(color, shader, x, y, w, h, tint);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != color ||
        texturedBatches.back().depth != depth || texturedBatches.back().shader != shader) {
        texturedBatches.push_back(
            TexturedBatch{color, depth, shader, BlendMode::Alpha, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, tint, 0.f, 0.f, 1.f, 1.f);
    noteTexturedOverlay(color);
}

void Graphics::setLighting2D(const Lighting2DUBO &ubo) { lighting2dFrame = ubo; }

void Graphics::ensureFlatNormalTexture() {
    if (flatNormalTexture) return;
    const uint8_t px[4] = {128, 128, 255, 255};  // flat normal pointing +Z
    flatNormalTexture = newTexture(1, 1, px);
}

void Graphics::drawTexturedRectLitUV(Texture *albedo, Texture *normal, float x, float y, float w,
                                     float h, float u0, float v0, float u1, float v1,
                                     const Color &color) {
    if (!albedo) {
        drawSolidRect(x, y, w, h, color);
        return;
    }
    ensureFlatNormalTexture();
    if (!normal) normal = flatNormalTexture;
    if (litBatches.empty() || litBatches.back().albedo != albedo ||
        litBatches.back().normal != normal) {
        litBatches.push_back(LitBatch{albedo, normal, Batcher{}});
    }
    litBatches.back().batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1);
    noteLitOverlay();
}

vkb::BoundSet Graphics::lit2dSetFor(GpuTexture *albedo, GpuTexture *normal, bool offscreen) {
    ASSERT(albedo != nullptr);
    ASSERT(normal != nullptr);
    auto &sets = offscreen ? offscreenLit2dSets : currentLit2dSets();
    vkb::GenericBuffer &ubo = offscreen ? offscreenLighting2dUbo : currentLighting2dUbo();
    LitSetKey key{albedo, normal};
    auto it = sets.find(key);
    if (it != sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &lit2dSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(albedo->sampler, albedo->image.imageView()))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(normal->sampler, normal->image.imageView()))
        .beginBuffers(2, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(ubo.buffer, 0, sizeof(Lighting2DUBO))
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    sets.emplace(key, bound);
    return bound;
}

vkb::BoundSet Graphics::post2SetFor(GpuTexture *color, GpuTexture *depth) {
    if (!color || !color->sampler) return {};
    vk::ImageView colorView = color->imageView();
    if (!colorView) return {};
    if (!depth || !depth->sampler || !depth->imageView()) depth = color;
    LitSetKey key{color, depth};
    auto it = post2Sets.find(key);
    if (it != post2Sets.end()) return it->second;

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);

    vkb::UnboundSet unbound{sets[0]};
    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(color->sampler, colorView))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(depth->sampler, depth->imageView()))
        .update(device.instance);
    vkb::BoundSet bound = std::move(unbound).publish();
    post2Sets.emplace(key, bound);
    return bound;
}

void Graphics::drawLitBatches(vk::CommandBuffer cb, int viewW, int viewH, vk::Pipeline litPipeline,
                              std::vector<LitBatch> &batches,
                              std::vector<vkb::HostVertexBuffer> &texBufs, size_t &texBufIndex,
                              bool offscreen) {
    if (!litPipeline || batches.empty() || !lit2dPipelineLayout) return;
    lighting2dFrame.meta.y = float(viewW);
    lighting2dFrame.meta.z = float(viewH);
    vkb::GenericBuffer &ubo = offscreen ? offscreenLighting2dUbo : currentLighting2dUbo();
    ubo.updateLocal(frameToken(), &lighting2dFrame, sizeof(Lighting2DUBO));

    for (auto &lb : batches) {
        if (lb.batch.empty() || !lb.albedo || !lb.albedo->gpuHandle) continue;
        ensureFlatNormalTexture();
        Texture *ntex = lb.normal ? lb.normal : flatNormalTexture;
        if (!ntex || !ntex->gpuHandle) continue;
        auto *albedoGpu = static_cast<GpuTexture *>(lb.albedo->gpuHandle);
        auto *normalGpu = static_cast<GpuTexture *>(ntex->gpuHandle);

        Batcher ndc = lb.batch;
        ndc.toNDC(viewW, viewH);
        std::vector<TexturedVertex> gpuVerts;
        gpuVerts.reserve(ndc.vertices().size());
        for (const auto &v : ndc.vertices())
            gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});

        if (texBufIndex >= texBufs.size()) texBufs.emplace_back();
        vkb::HostVertexBuffer &vb = texBufs[texBufIndex++];
        vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);

        vk::DescriptorSet set = lit2dSetFor(albedoGpu, normalGpu, offscreen);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, litPipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, lit2dPipelineLayout, 0, 1, &set, 0,
                              nullptr);
        vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, vb, &offset);
        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
    }
}


Shader *Graphics::newShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                   const std::vector<uint32_t> &fragSpv) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newShaderFromSpv: graphics not initialized");
    if (fragSpv.empty()) throw Exception("newShaderFromSpv: empty fragment SPIR-V");

    std::vector<uint32_t> vert = vertSpv;
    if (vert.empty())
        vert.assign(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    if (vert[0] != 0x07230203 || fragSpv[0] != 0x07230203)
        throw Exception("newShaderFromSpv: SPIR-V magic mismatch");

    auto gpu = std::make_unique<GpuShader>();
    gpu->pipelineLayout = shaderPipelineLayout;
    gpu->swapchainPipeline =
        createTexturedStylePipeline(vert, fragSpv, renderpass, shaderPipelineLayout);
    if (offscreenRenderPass) {
        gpu->offscreenPipeline =
            createTexturedStylePipeline(vert, fragSpv, offscreenRenderPass, shaderPipelineLayout);
    }

    auto sh = std::make_unique<Shader>();
    sh->setSpirv(std::move(vert), fragSpv);
    sh->gpuHandle = gpu.get();

    Shader *raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

Shader *Graphics::newShaderFromSpvFile(const std::string &vertPath, const std::string &fragPath) {
    if (fragPath.empty()) throw Exception("newShaderFromSpvFile: empty fragPath");
    std::vector<uint32_t> vert;
    if (!vertPath.empty()) vert = readSpirvFile(vertPath);
    auto frag = readSpirvFile(fragPath);
    return newShaderFromSpv(vert, frag);
}

Shader *Graphics::newShader(const std::string &vertGlsl, const std::string &fragGlsl) {
    if (fragGlsl.empty()) throw Exception("newShader: empty fragment GLSL");
    std::vector<uint32_t> vert;
    if (!vertGlsl.empty()) vert = compileGlslWithGlslc(vertGlsl, "vert");
    auto frag = compileGlslWithGlslc(fragGlsl, "frag");
    return newShaderFromSpv(vert, frag);
}

Shader *Graphics::newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                       const std::vector<uint32_t> &fragSpv) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshShaderFromSpv: graphics not initialized");
    if (fragSpv.empty()) throw Exception("newMeshShaderFromSpv: empty fragment SPIR-V");
    if (!mesh3dShaderPipelineLayout)
        throw Exception("newMeshShaderFromSpv: mesh3d pipeline layout missing");

    std::vector<uint32_t> vert = vertSpv;
    if (vert.empty())
        vert.assign(mesh3d_vert_spv, mesh3d_vert_spv + mesh3d_vert_spv_count);
    if (vert[0] != 0x07230203 || fragSpv[0] != 0x07230203)
        throw Exception("newMeshShaderFromSpv: SPIR-V magic mismatch");

    auto gpu = std::make_unique<GpuShader>();
    gpu->isMesh3D = true;
    gpu->pipelineLayout = mesh3dShaderPipelineLayout;
    gpu->mesh3dPipeline = createMesh3DStylePipeline(vert, fragSpv, mesh3dShaderPipelineLayout,
                                                    activeScenePass(), activeSceneSamples());
    // Built here, not lazily in drawMeshShader: vkCreateGraphicsPipelines
    // during an open render pass crashes software ICDs (Lavapipe).
    gpu->mesh3dXrayPipeline = createMesh3DXrayPipeline(vert, fragSpv, mesh3dShaderPipelineLayout,
                                                       activeScenePass(), activeSceneSamples());

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eMesh3D);
    sh->setSpirv(std::move(vert), fragSpv);
    sh->gpuHandle = gpu.get();
    gpu->owner = sh.get();

    Shader *raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

Shader *Graphics::newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                       const std::vector<uint32_t> &fragSpv) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newHairShaderFromSpv: graphics not initialized");
    if (fragSpv.empty()) throw Exception("newHairShaderFromSpv: empty fragment SPIR-V");
    if (!mesh3dShaderPipelineLayout)
        throw Exception("newHairShaderFromSpv: mesh3d pipeline layout missing");

    std::vector<uint32_t> vert = vertSpv;
    if (vert.empty())
        vert.assign(mesh3d_hair_vert_spv, mesh3d_hair_vert_spv + mesh3d_hair_vert_spv_count);
    if (vert[0] != 0x07230203 || fragSpv[0] != 0x07230203)
        throw Exception("newHairShaderFromSpv: SPIR-V magic mismatch");

    auto gpu = std::make_unique<GpuShader>();
    gpu->isMesh3D = true;
    gpu->isHair3D = true;
    gpu->pipelineLayout = mesh3dShaderPipelineLayout;
    gpu->mesh3dPipeline = createMesh3DHairPipeline(vert, fragSpv, mesh3dShaderPipelineLayout,
                                                   activeScenePass(), activeSceneSamples());

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eMesh3D);
    sh->setSpirv(std::move(vert), fragSpv);
    sh->gpuHandle = gpu.get();
    gpu->owner = sh.get();

    Shader *raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

bool Graphics::releaseShader(Shader *shader) {
    if (!shader || !shader->gpuHandle) return false;

    auto *gpu = static_cast<GpuShader *>(shader->gpuHandle);
    auto gpuIt = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const std::unique_ptr<GpuShader> &g) {
                                  return g.get() == gpu;
                              });
    if (gpuIt == ownedGpuShaders.end()) return false;

    auto shIt = std::find_if(ownedShaders.begin(), ownedShaders.end(),
                             [&](const std::unique_ptr<Shader> &s) {
                                 return s.get() == shader;
                             });
    if (shIt == ownedShaders.end()) return false;

    // Mirror ~Graphics: pipelines are raw handles that must be destroyed here;
    // pipelineLayout is shared and must not be destroyed per-shader.
    waitForSharedGpuResources();
    if (gpu->swapchainPipeline) device->destroyPipeline(gpu->swapchainPipeline);
    if (gpu->offscreenPipeline) device->destroyPipeline(gpu->offscreenPipeline);
    if (gpu->mesh3dPipeline) device->destroyPipeline(gpu->mesh3dPipeline);
    if (gpu->mesh3dXrayPipeline) device->destroyPipeline(gpu->mesh3dXrayPipeline);
    shader->gpuHandle = nullptr;
    ownedGpuShaders.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)shIt->release();
    ownedShaders.erase(shIt);
    return true;
}

Shader *Graphics::newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) {
    if (fragGlsl.empty()) throw Exception("newMeshShader: empty fragment GLSL");
    std::vector<uint32_t> vert;
    if (!vertGlsl.empty()) vert = compileGlslWithGlslc(vertGlsl, "vert");
    auto frag = compileGlslWithGlslc(fragGlsl, "frag");
    return newMeshShaderFromSpv(vert, frag);
}

Shader *Graphics::newMeshShaderFromWgsl(const std::string &, const std::string &) {
    throw Exception("newMeshShaderFromWgsl: WGSL mesh shaders are only supported on the "
                    "WebGPU backend; use newMeshShaderFromSpv on Vulkan.");
}

Shader *Graphics::newShaderFromWgsl(const std::string &, const std::string &) {
    throw Exception("newShaderFromWgsl: WGSL shaders are only supported on the WebGPU "
                    "backend; use newShaderFromSpv on Vulkan.");
}

void Graphics::flushBatch() {
    if (!initialized) return;
    if (isCanvasActive()) {
        auto *oc = dynamic_cast<OffscreenCanvas *>(activeCanvas);
        if (!oc) throw Exception("flushBatch: active canvas is not an OffscreenCanvas");
        flushToOffscreen(oc);
    } else {
        flushToSwapchain();
    }
}

void Graphics::flushToOffscreen(OffscreenCanvas *canvas) {
    auto solid = std::move(solidBatches);
    auto textured = std::move(texturedBatches);
    auto lit = std::move(litBatches);
    auto spans = std::move(overlaySpans);
    clear2DBatches();

    const Color cc = canvas->pendingClearColor();
    const bool needClear = canvas->takePendingClear();
    bool hasSolid = false;
    for (const auto &sb : solid)
        if (!sb.batch.empty()) hasSolid = true;
    if (!hasSolid && textured.empty() && lit.empty() && !needClear) return;

    // Offscreen color is a single shared image. An in-flight swapchain frame
    // may still be sampling it (draw canvas to screen last frame), so drain
    // those frames before transitioning it back to a color attachment.
    waitForSharedGpuResources();

    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                canvas->colorImage().setLayout(cb, vk::ImageLayout::eColorAttachmentOptimal);

                                vk::ClearValue cv{
                                    vk::ClearColorValue(std::array<float, 4>{cc.r, cc.g, cc.b, cc.a})};
                                vk::RenderPassBeginInfo rpBegin{};
                                rpBegin.renderPass = offscreenRenderPass;
                                rpBegin.framebuffer = canvas->framebuffer();
                                rpBegin.renderArea.extent =
                                    vk::Extent2D{uint32_t(canvas->getWidth()), uint32_t(canvas->getHeight())};
                                rpBegin.clearValueCount = 1;
                                rpBegin.pClearValues = &cv;
                                canvas->colorImage().beginColorAttachment();
                                cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

                                setViewportAndScissor(cb, uint32_t(canvas->getWidth()),
                                                      uint32_t(canvas->getHeight()));

                                std::vector<vkb::HostVertexBuffer> &solidBufs =
                                    offscreenBuffers.solidBufs;
                                std::vector<vkb::HostVertexBuffer> &texBufs = offscreenBuffers.texBufs;
                                size_t texBufIndex = 0;
                                const int vw = canvas->getWidth();
                                const int vh = canvas->getHeight();

                                auto offscreenTexPipe = [&](BlendMode mode) -> vk::Pipeline {
                                    switch (mode) {
                                        case BlendMode::Additive:
                                            return offscreenAdditiveTexPipeline;
                                        case BlendMode::Premultiplied:
                                            return offscreenPremultipliedTexPipeline;
                                        case BlendMode::Multiply:
                                            return offscreenMultiplyTexPipeline;
                                        case BlendMode::Opaque:
                                            return offscreenOpaqueTexPipeline;
                                        case BlendMode::Alpha:
                                        default:
                                            return offscreenTexPipeline;
                                    }
                                };
                                auto offscreenSolidPipe = [&](BlendMode mode) -> vk::Pipeline {
                                    switch (mode) {
                                        case BlendMode::Additive:
                                            return offscreenAdditiveSolidPipeline;
                                        case BlendMode::Premultiplied:
                                            return offscreenPremultipliedSolidPipeline;
                                        case BlendMode::Multiply:
                                            return offscreenMultiplySolidPipeline;
                                        case BlendMode::Alpha:
                                            return offscreenSolidAlphaPipeline;
                                        case BlendMode::Opaque:
                                        default:
                                            return offscreenSolidPipeline;
                                    }
                                };

                                auto drawOffscreenTextured = [&](TexturedBatch &tb) {
                                    if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) return;
                                    auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
                                    vk::DescriptorSet texSet = gpu->descriptorSet;
                                    if (tb.depth && tb.depth->gpuHandle) {
                                        auto *depthGpu = static_cast<GpuTexture *>(tb.depth->gpuHandle);
                                        if (vk::DescriptorSet combo = post2SetFor(gpu, depthGpu))
                                            texSet = combo;
                                    }
                                    Batcher ndc = tb.batch;
                                    ndc.toNDC(vw, vh);
                                    std::vector<TexturedVertex> gpuVerts;
                                    gpuVerts.reserve(ndc.vertices().size());
                                    for (const auto &v : ndc.vertices())
                                        gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});

                                    if (texBufIndex >= texBufs.size()) texBufs.emplace_back();
                                    vkb::HostVertexBuffer &vb = texBufs[texBufIndex++];
                                    vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);

                                    if (tb.shader && tb.shader->gpuHandle) {
                                        ensureShaderOffscreenPipeline(tb.shader);
                                        auto *gs = static_cast<GpuShader *>(tb.shader->gpuHandle);
                                        if (!gs->offscreenPipeline) return;
                                        cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                        gs->offscreenPipeline);
                                        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                              shaderPipelineLayout, 0, 1,
                                                              &texSet, 0, nullptr);
                                        cb.pushConstants(shaderPipelineLayout,
                                                         vk::ShaderStageFlagBits::eVertex |
                                                             vk::ShaderStageFlagBits::eFragment,
                                                         0, Shader::kPushConstantBytes,
                                                         tb.shader->pushConstantData());
                                    } else {
                                        vk::Pipeline pipe = offscreenTexPipe(tb.blend);
                                        if (!pipe) return;
                                        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
                                        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                              texPipelineLayout, 0, 1,
                                                              &texSet, 0, nullptr);
                                    }
                                    vk::DeviceSize offset = 0;
                                    cb.bindVertexBuffers(0, 1, vb, &offset);
                                    cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                };

                                std::vector<bool> solidUploaded(solid.size(), false);
                                auto uploadSolid = [&](size_t idx) {
                                    if (idx >= solid.size() || solidUploaded[idx] ||
                                        solid[idx].batch.empty())
                                        return;
                                    vk::Pipeline pipe = offscreenSolidPipe(solid[idx].blend);
                                    if (!pipe) return;
                                    Batcher ndc = solid[idx].batch;
                                    ndc.toNDC(vw, vh);
                                    std::vector<ColorVertex> gpuVerts;
                                    gpuVerts.reserve(ndc.vertices().size());
                                    for (const auto &v : ndc.vertices())
                                        gpuVerts.push_back(ColorVertex{v.pos, v.color});
                                    if (solidBufs.size() <= idx) solidBufs.resize(idx + 1);
                                    solidBufs[idx].allocate<ColorVertex>(frameToken(), device,
                                                                         gpuVerts);
                                    solidUploaded[idx] = true;
                                };

                                auto drawSolidSpan = [&](uint32_t batchIndex, uint32_t begin,
                                                         uint32_t count) {
                                    if (batchIndex >= solid.size() || count == 0 ||
                                        solid[batchIndex].batch.empty())
                                        return;
                                    vk::Pipeline pipe = offscreenSolidPipe(solid[batchIndex].blend);
                                    if (!pipe) return;
                                    uploadSolid(batchIndex);
                                    vk::DeviceSize offset = 0;
                                    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
                                    cb.bindVertexBuffers(0, 1, solidBufs[batchIndex], &offset);
                                    cb.draw(count, 1, begin, 0);
                                };

                                if (!spans.empty()) {
                                    for (const auto &sp : spans) {
                                        if (sp.kind == OverlayKind::Solid)
                                            drawSolidSpan(sp.index, sp.vertBegin, sp.vertCount);
                                        else if (sp.kind == OverlayKind::Textured &&
                                                 sp.index < textured.size())
                                            drawOffscreenTextured(textured[sp.index]);
                                        else if (sp.kind == OverlayKind::Lit && offscreenLitPipeline &&
                                                 sp.index < lit.size()) {
                                            std::vector<LitBatch> one;
                                            one.push_back(std::move(lit[sp.index]));
                                            drawLitBatches(cb, vw, vh, offscreenLitPipeline, one,
                                                           texBufs, texBufIndex, true);
                                        }
                                    }
                                } else {
                                    for (size_t i = 0; i < solid.size(); ++i) {
                                        if (!solid[i].batch.empty())
                                            drawSolidSpan(uint32_t(i), 0,
                                                          uint32_t(solid[i].batch.vertices().size()));
                                    }
                                    for (auto &tb : textured) drawOffscreenTextured(tb);
                                    if (offscreenLitPipeline)
                                        drawLitBatches(cb, vw, vh, offscreenLitPipeline, lit, texBufs,
                                                       texBufIndex, true);
                                }

                                cb.endRenderPass();
                                canvas->colorImage().endSampledLayout();
                            });
}

void Graphics::abortOpen3DFrame() {
    const bool hadScene = sceneColorPassOpen;
    const bool had3D = swapchainPassOpen;
    try {
        if (hadScene) endSceneColorRenderPass();
    } catch (...) {
        sceneColorPassOpen = false;
    }
    try {
        if (had3D) {
            // Scene-pass path never opened the swapchain pass; open a dummy
            // one so the acquired command buffer can be submitted.
            if (hadScene) beginSwapchainColorPass();
            presentRecording = swapchainPass.endRenderPass();
            swapchainPass = {};
            const bool captured = screenReadbackEnabled
                                      ? recordSwapchainReadback(presentRecording.commandBuffer())
                                      : false;
            presentRecording.end().submitAndPresent();
            presentRecording = {};
            if (captured) {
                hasPresentedFrame = true;
                readbackReady = true;
                readbackCpuSynced = false;
            }
        }
    } catch (...) {
        swapchainPass = {};
        presentRecording = {};
    }
    swapchainPassOpen = false;
    sceneColorPassOpen = false;
    frameHad3D = false;
    hasPendingClear = false;
    flushingSwapchain_ = false;
    clear2DBatches();
}

void Graphics::flushToSwapchain() {
    if (flushingSwapchain_) return;
    flushingSwapchain_ = true;
    bool completed = false;
    struct FlushGuard {
        Graphics *g;
        bool *completed;
        ~FlushGuard() {
            g->flushingSwapchain_ = false;
            if (!*completed) g->abortOpen3DFrame();
        }
    } guard{this, &completed};

    const bool continue3D = swapchainPassOpen;
    const bool hadScenePass = sceneColorPassOpen;

    if (hadScenePass) {
        endSceneColorRenderPass();
        queueSceneColorResolve();
    }

    if (!continue3D) {
        // 2D-only path: acquire the present CB and record deferred passes now,
        // so the UI overlay's dedicated MSAA pass can be recorded before the
        // swapchain pass begins.
        if (!beginPresentCommandBuffer()) {
            dropPendingOffscreenPasses();
            hasPendingClear = false;
            completed = true;
            return;
        }
        recordDeferredFrameGraph();
    }

    // Render the UI overlay (ImGui) into its own MSAA pass, resolved and
    // composited as the top-most fullscreen quad. Skipped only on the rare 3D
    // fallback path where the swapchain pass is already open from begin3DFrame.
    if (presentOverlayFn_ && !(continue3D && !hadScenePass)) {
        renderUiOverlayPass();
    }

    if (hadScenePass || !continue3D) {
        beginSwapchainColorPass();
        swapchainPassOpen = true;
    }

    auto solid = std::move(solidBatches);
    auto textured = std::move(texturedBatches);
    auto lit = std::move(litBatches);
    auto spans = std::move(overlaySpans);
    auto engineSpans = std::move(engine3DSpans);
    auto sceneResolve = std::move(pendingSceneResolve);
    auto uiResolve = std::move(pendingUiResolve);
    const bool autoScene = hadScenePass && !sceneColorComposited;
    Texture *sceneTex = getSceneColorTexture();
    clear2DBatches();

    auto &cb = currentPresentCb();
    setViewportAndScissor(cb, swapchain.extent.width, swapchain.extent.height);

    // Persistent per-frame-slot buffers (see currentFrame2DBuffers). Safe to
    // overwrite: acquireForFrame() already waited this slot's fence.
    auto &frameBufs = currentFrame2DBuffers();
    std::vector<vkb::HostVertexBuffer> &solidBufs = frameBufs.solidBufs;
    std::vector<vkb::HostVertexBuffer> &texBufs = frameBufs.texBufs;
    size_t texBufIndex = 0;

    auto swapchainTexPipe = [&](BlendMode mode) -> vk::Pipeline {
        switch (mode) {
            case BlendMode::Additive:
                return additiveTexPipeline;
            case BlendMode::Premultiplied:
                return premultipliedTexPipeline;
            case BlendMode::Multiply:
                return multiplyTexPipeline;
            case BlendMode::Opaque:
                return opaqueTexPipeline;
            case BlendMode::Alpha:
            default:
                return texPipeline;
        }
    };
    auto swapchainSolidPipe = [&](BlendMode mode) -> vk::Pipeline {
        switch (mode) {
            case BlendMode::Additive:
                return additiveSolidPipeline;
            case BlendMode::Premultiplied:
                return premultipliedSolidPipeline;
            case BlendMode::Multiply:
                return multiplySolidPipeline;
            case BlendMode::Alpha:
                return solidAlphaPipeline;
            case BlendMode::Opaque:
            default:
                return pipeline;
        }
    };

    auto drawTextured = [&](TexturedBatch &tb) {
        if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) return;
        auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
        vk::DescriptorSet texSet = gpu->descriptorSet;
        if (tb.depth && tb.depth->gpuHandle) {
            auto *depthGpu = static_cast<GpuTexture *>(tb.depth->gpuHandle);
            if (vk::DescriptorSet combo = post2SetFor(gpu, depthGpu)) texSet = combo;
        }
        Batcher ndc = tb.batch;
        ndc.toNDC(width, height);
        std::vector<TexturedVertex> gpuVerts;
        gpuVerts.reserve(ndc.vertices().size());
        for (const auto &v : ndc.vertices())
            gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});

        if (texBufIndex >= texBufs.size()) texBufs.emplace_back();
        vkb::HostVertexBuffer &vb = texBufs[texBufIndex++];
        vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);

        if (tb.shader && tb.shader->gpuHandle) {
            auto *gs = static_cast<GpuShader *>(tb.shader->gpuHandle);
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gs->swapchainPipeline);
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shaderPipelineLayout, 0, 1,
                                  &texSet, 0, nullptr);
            cb.pushConstants(shaderPipelineLayout,
                             vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                             Shader::kPushConstantBytes, tb.shader->pushConstantData());
        } else {
            vk::Pipeline pipe = swapchainTexPipe(tb.blend);
            if (!pipe) return;
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1,
                                  &texSet, 0, nullptr);
        }
        vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, vb, &offset);
        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
    };

    std::vector<bool> solidUploaded(solid.size(), false);
    auto uploadSolid = [&](size_t idx) {
        if (idx >= solid.size() || solidUploaded[idx] || solid[idx].batch.empty()) return;
        vk::Pipeline pipe = swapchainSolidPipe(solid[idx].blend);
        if (!pipe) return;
        Batcher ndc = solid[idx].batch;
        ndc.toNDC(width, height);
        std::vector<ColorVertex> gpuVerts;
        gpuVerts.reserve(ndc.vertices().size());
        for (const auto &v : ndc.vertices())
            gpuVerts.push_back(ColorVertex{v.pos, v.color});
        if (solidBufs.size() <= idx) solidBufs.resize(idx + 1);
        solidBufs[idx].allocate<ColorVertex>(frameToken(), device, gpuVerts);
        solidUploaded[idx] = true;
    };

    auto drawSolidSpan = [&](uint32_t batchIndex, uint32_t begin, uint32_t count) {
        if (batchIndex >= solid.size() || count == 0 || solid[batchIndex].batch.empty()) return;
        vk::Pipeline pipe = swapchainSolidPipe(solid[batchIndex].blend);
        if (!pipe) return;
        uploadSolid(batchIndex);
        vk::DeviceSize offset = 0;
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
        cb.bindVertexBuffers(0, 1, solidBufs[batchIndex], &offset);
        cb.draw(count, 1, begin, 0);
    };

    auto replaySpans = [&](const std::vector<OverlaySpan> &list) {
        for (const auto &sp : list) {
            if (sp.kind == OverlayKind::Solid && sp.index < solid.size() && sp.vertCount > 0) {
                drawSolidSpan(sp.index, sp.vertBegin, sp.vertCount);
            } else if (sp.kind == OverlayKind::Textured && texPipeline &&
                       sp.index < textured.size()) {
                drawTextured(textured[sp.index]);
            } else if (sp.kind == OverlayKind::Lit && lit2dPipeline && sp.index < lit.size()) {
                std::vector<LitBatch> one;
                one.push_back(std::move(lit[sp.index]));
                drawLitBatches(cb, width, height, lit2dPipeline, one, texBufs, texBufIndex, false);
            }
        }
    };

    bool engineDrawn = false;
    auto drawEngine3D = [&]() {
        if (engineDrawn) return;
        engineDrawn = true;
        replaySpans(engineSpans);
    };

    // Default: blit 3D fullscreen under script 2D. Scripts that call
    // drawScene3D / drawTexturedRect(getSceneColorTexture()) own the order.
    if (autoScene && sceneResolve) {
        drawTextured(*sceneResolve);
        drawEngine3D();
    }

    if (spans.empty()) {
        for (size_t i = 0; i < solid.size(); ++i) {
            if (!solid[i].batch.empty())
                drawSolidSpan(uint32_t(i), 0, uint32_t(solid[i].batch.vertices().size()));
        }
        if (texPipeline) {
            for (auto &tb : textured) drawTextured(tb);
        }
        if (lit2dPipeline) drawLitBatches(cb, width, height, lit2dPipeline, lit, texBufs, texBufIndex,
                                          false);
    } else {
        for (const auto &sp : spans) {
            if (sp.kind == OverlayKind::Solid && sp.index < solid.size() && sp.vertCount > 0) {
                drawSolidSpan(sp.index, sp.vertBegin, sp.vertCount);
            } else if (sp.kind == OverlayKind::Textured && texPipeline &&
                       sp.index < textured.size()) {
                drawTextured(textured[sp.index]);
                if (textured[sp.index].texture == sceneTex) drawEngine3D();
            } else if (sp.kind == OverlayKind::Lit && lit2dPipeline && sp.index < lit.size()) {
                std::vector<LitBatch> one;
                one.push_back(std::move(lit[sp.index]));
                drawLitBatches(cb, width, height, lit2dPipeline, one, texBufs, texBufIndex, false);
            }
        }
    }

    if (uiResolve) drawTextured(*uiResolve);

    // Invalidate prior readback so a failed present cannot reuse a stale frame.
    if (screenReadbackEnabled) hasPresentedFrame = false;

    // Fallback path only: the swapchain pass was already open when this frame
    // began (3D MSAA scene pass unavailable), so draw the overlay directly.
    if (presentOverlayFn_ && continue3D && !hadScenePass) {
        VkCommandBuffer raw = static_cast<VkCommandBuffer>(cb);
        presentOverlayFn_(presentOverlayUser_, raw);
    }

    presentRecording = swapchainPass.endRenderPass();
    swapchainPass = {};
    const bool captured =
        screenReadbackEnabled ? recordSwapchainReadback(presentRecording.commandBuffer()) : false;
    presentRecording.end().submitAndPresent();
    presentRecording = {};
    if (captured) {
        hasPresentedFrame = true;
        readbackReady = true;
        readbackCpuSynced = false;
    }
    hasPendingClear = false;
    swapchainPassOpen = false;
    frameHad3D = false;
    completed = true;
}


}  // namespace eve::graphics::vulkan
