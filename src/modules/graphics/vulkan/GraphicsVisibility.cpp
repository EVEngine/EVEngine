#include <algorithm>
#include <cmath>
#include <cstring>
#include "graphics/Mesh.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
Result<eve::graphics::Graphics::VisibilityPixels> Graphics::captureVisibilityPixels(
    const std::vector<eve::graphics::Graphics::EntityIdDraw> &draws, const glm::mat4 &viewProj, int maskW,
    int maskH) try {
    const auto reject = [](DiagnosticCode code, const char *message) {
        return Result<VisibilityPixels>::failure(Diagnostic::error(code, message));
    };
    if (!initialized || maskW <= 0 || maskH <= 0 || maskW > 2048 || maskH > 2048)
        return reject(DiagnosticCode::InvalidArgument, "Visibility viewport must be 1..2048");
    // Initialize shared immutable pipelines at the live viewport, never resize live targets for a query.
    if (!gbufferPipeline) createGBufferResources(getPixelWidth(), getPixelHeight());
    if (!gbufferPipeline || !gbufferRenderPass || !whiteTexture)
        return reject(DiagnosticCode::Unsupported, "Visibility pipeline unavailable");
    struct Scratch {
        vkb::ColorTarget  normal, depthColor, albedo, pbrParams, emissive;
        vkb::GenericImage depth;
        vk::Framebuffer   framebuffer{};
    } scratch;
    struct Cleanup {
        Graphics &graphics;
        Scratch  &slot;
        ~Cleanup() {
            if (slot.framebuffer) graphics.device.instance.destroyFramebuffer(slot.framebuffer);
        }
    } cleanup{*this, scratch};
    const auto format  = pickGBufferColorFormat(device);
    scratch.normal     = device.createColorTarget(maskW, maskH, format);
    scratch.depthColor = device.createColorTarget(maskW, maskH, format);
    scratch.albedo     = device.createColorTarget(maskW, maskH, format);
    scratch.pbrParams  = device.createColorTarget(maskW, maskH, format);
    scratch.emissive   = device.createColorTarget(maskW, maskH, format);
    vk::ImageCreateInfo depthInfo{};
    depthInfo.imageType   = vk::ImageType::e2D;
    depthInfo.format      = vk::Format::eD32Sfloat;
    depthInfo.extent      = vk::Extent3D{uint32_t(maskW), uint32_t(maskH), 1};
    depthInfo.mipLevels   = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples     = vk::SampleCountFlagBits::e1;
    depthInfo.tiling      = vk::ImageTiling::eOptimal;
    depthInfo.usage       = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled |
                      vk::ImageUsageFlagBits::eTransferSrc;
    scratch.depth =
        vkb::GenericImage(device, depthInfo, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eDepth, false);
    scratch.framebuffer = gbufferRenderPass.createFramebuffer(
        device, maskW, maskH,
        {scratch.normal.asAttachment(), scratch.depthColor.asAttachment(), scratch.albedo.asAttachment(),
         scratch.pbrParams.asAttachment(), scratch.emissive.asAttachment(), scratch.depth.asAttachment()});
    auto *slot = &scratch;

    // Render each mesh with a flat idColor into the G-buffer albedo attachment
    // (location 2) by reusing the G-buffer pipeline (its fragment shader writes
    // outAlbedo = albedo * tint, so passing a white texture + idColor tint gives
    // a per-pixel flat entity-ID color).
    std::vector<GBufferDraw> idDraws;
    idDraws.reserve(draws.size());
    auto u8 = [](float x) -> uint32_t { return uint32_t(std::lround(std::clamp(x, 0.f, 1.f) * 255.f)); };
    for (const auto &d : draws) {
        if (!d.mesh || !describeMesh(d.mesh))
            return reject(DiagnosticCode::StaleHandle, "Visibility mesh does not belong to this backend");
        GBufferDraw gd{};
        gd.mesh               = d.mesh;
        gd.albedo             = whiteTexture;
        gd.push.mvp           = viewProj * d.model;
        gd.push.modelR0       = glm::vec4(d.model[0][0], d.model[1][0], d.model[2][0], d.model[3][0]);
        gd.push.modelR1       = glm::vec4(d.model[0][1], d.model[1][1], d.model[2][1], d.model[3][1]);
        gd.push.modelR2       = glm::vec4(d.model[0][2], d.model[1][2], d.model[2][2], d.model[3][2]);
        const uint32_t packed = u8(d.idColor.r) | (u8(d.idColor.g) << 8) | (u8(d.idColor.b) << 16);
        gd.push.clip          = glm::vec4(0.1f, 100.f, glm::uintBitsToFloat(packed), 0.f);
        idDraws.push_back(gd);
    }


    const uint32_t       w         = uint32_t(maskW);
    const uint32_t       h         = uint32_t(maskH);
    const vk::DeviceSize planeSize = vk::DeviceSize(w) * vk::DeviceSize(h) * 4;
    const vk::DeviceSize byteSize  = planeSize * 2;
    vkb::GenericBuffer   staging(device, vk::BufferUsageFlagBits::eTransferDst, byteSize,
                                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    vkb::executeImmediately(
        device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics), [&](vk::CommandBuffer cb) {
            std::array<vk::ClearValue, 6> clears{};
            clears[0].color        = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
            clears[1].color        = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
            clears[2].color        = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
            clears[5].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass      = gbufferRenderPass;
            rpBegin.framebuffer     = slot->framebuffer;
            rpBegin.renderArea      = vk::Rect2D{{0, 0}, {w, h}};
            rpBegin.clearValueCount = uint32_t(clears.size());
            rpBegin.pClearValues    = clears.data();
            slot->normal.beginColorAttachment();
            slot->depthColor.beginColorAttachment();
            slot->albedo.beginColorAttachment();
            slot->pbrParams.beginColorAttachment();
            slot->emissive.beginColorAttachment();
            slot->depth.beginDepthAttachment();
            cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            setViewportAndScissor(cb, w, h);
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gbufferPipeline);
            for (const auto &d : idDraws) {
                auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
                if (!gpuMesh) continue;
                if (whiteTexture && whiteTexture->gpuHandle && texSetLayout) {
                    auto *gpuTex = static_cast<GpuTexture *>(whiteTexture->gpuHandle);
                    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, gbufferPipelineLayout, 0, 1,
                                          gpuTex->descriptorSet.ptr(), 0, nullptr);
                }
                cb.pushConstants(gbufferPipelineLayout,
                                 vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                                 sizeof(GBufferPush), &d.push);
                drawIndexedMesh(cb, *gpuMesh);
            }
            cb.endRenderPass();
            slot->normal.endSampledLayout();
            slot->depthColor.endSampledLayout();
            slot->albedo.endSampledLayout();
            slot->pbrParams.endSampledLayout();
            slot->emissive.endSampledLayout();
            slot->depth.endSampledLayout();

            // Copy the albedo attachment (location 2 = ID colors) to CPU.
            slot->albedo.setLayout(cb, vk::ImageLayout::eTransferSrcOptimal);
            vk::BufferImageCopy region{};
            region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            region.imageExtent      = vk::Extent3D{w, h, 1};
            cb.copyImageToBuffer(slot->albedo.image(), vk::ImageLayout::eTransferSrcOptimal, staging.buffer, region);
            slot->albedo.setLayout(cb, vk::ImageLayout::eShaderReadOnlyOptimal);
            slot->depth.setLayout(cb, vk::ImageLayout::eTransferSrcOptimal, vk::ImageAspectFlagBits::eDepth);
            region.bufferOffset                = planeSize;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
            cb.copyImageToBuffer(slot->depth.image(), vk::ImageLayout::eTransferSrcOptimal, staging.buffer, region);
            slot->depth.setLayout(cb, vk::ImageLayout::eDepthStencilReadOnlyOptimal, vk::ImageAspectFlagBits::eDepth);
        });

    VisibilityPixels output;
    output.width  = maskW;
    output.height = maskH;
    output.ids.resize(size_t(w) * h);
    output.depth.resize(output.ids.size());
    const auto *mapped = static_cast<const std::uint8_t *>(staging.map());
    // G-buffer RGB6 tint packing is lossless for the bounded 18-bit ID space.
    auto channel = [](std::uint8_t value) { return std::uint32_t(std::lround(value * (63.f / 255.f))); };
    for (size_t i = 0; i < output.ids.size(); ++i)
        output.ids[i] = channel(mapped[i * 4]) | (channel(mapped[i * 4 + 1]) << 6) | (channel(mapped[i * 4 + 2]) << 12);
    std::memcpy(output.depth.data(), mapped + planeSize, size_t(planeSize));
    staging.unmap();
    staging.release();
    return Result<VisibilityPixels>::success(std::move(output));
} catch (const std::exception &error) {
    return Result<VisibilityPixels>::failure(
        Diagnostic::error(DiagnosticCode::Failed, std::string("Visibility GPU capture failed: ") + error.what()));
}

}  // namespace eve::graphics::vulkan
