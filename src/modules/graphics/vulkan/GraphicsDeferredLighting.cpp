#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>
#include "common/Exception.h"
#include "graphics/ClusteredLight.h"
#include "graphics/DisplayOutputEncoding.h"
#include "graphics/Shadow.h"
#include "graphics/shaders/deferred_lighting_frag_spv.inc"
#include "graphics/shaders/deferred_lighting_vert_spv.inc"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

// GraphicsDeferredLighting.cpp
// Hybrid clustered deferred lighting (Phase C): fullscreen pass that samples
// the Phase B GBuffer and evaluates core PBR with ClusteredLight + CSM.


namespace eve::graphics::vulkan {
namespace {

std::vector<uint32_t> embeddedSpirv(const uint32_t* words, size_t count) {
    return std::vector<uint32_t>(words, words + count);
}

}  // namespace

void Graphics::destroyDeferredLightingResources() {
    destroyPipeline(device, deferredLightingPipeline);
    destroyPipelineLayout(device, deferredLightingPipelineLayout);
    deferredLightingSetLayout = vk::DescriptorSetLayout{};
    deferredLightingSetLayoutUnique.reset();
    deferredLightingFrames.clear();
    if (deferredLightingSampler) {
        device->destroySampler(deferredLightingSampler);
        deferredLightingSampler = nullptr;
    }
}

void Graphics::createDeferredLightingPipeline() {
    if (deferredLightingPipeline || !sceneColorRenderPass) return;

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    deferredLightingSetLayoutUnique =
        layoutBuilder.buffer(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(4, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(5, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(6, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(7, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(8, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(9, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(10, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .image(11, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    deferredLightingSetLayout      = *deferredLightingSetLayoutUnique;
    deferredLightingPipelineLayout = createPipelineLayout(device, deferredLightingSetLayout);

    auto             vert       = embeddedSpirv(deferred_lighting_vert_spv, deferred_lighting_vert_spv_count);
    auto             frag       = embeddedSpirv(deferred_lighting_frag_spv, deferred_lighting_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    deferredLightingPipeline    = device.createPipeline()
                                   .useClassicPipeline(vertModule, fragModule)
                                   .setPipelineLayout(deferredLightingPipelineLayout)
                                   .setDynamicStatesViewportScissor()
                                   .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                                  vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise)
                                   .setMultisampler(false, sceneColorSamples)
                                   // Always pass; fragment writes gl_FragDepth from GBuffer hwDepth so
                                   // transparent Forward+ can depth-test against deferred opaques.
                                   .setDepthStencil(true, true, vk::CompareOp::eAlways)
                                   .setColorAttachmentCount(1)
                                   .build(sceneColorRenderPass);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);

    if (!deferredLightingSampler) {
        vkb::SamplerBuilder sb;
        deferredLightingSampler = sb.nearestClamp().build(device);
    }
}

void Graphics::drawDeferredLighting() {
    if (!sceneColorPassOpen || !sceneColorRenderPass) return;
    auto* gslot = currentGBufferSlot();
    if (!gslot || !gslot->normalGpu.sampler) return;
    createDeferredLightingPipeline();
    if (!deferredLightingPipeline || !descriptorPool) return;
    auto& frame = currentSlot(deferredLightingFrames, frameSlotCount(), currentFrameSlot());
    frame.ubo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(DeferredLightingUBO),
                       kHostVisibleCoherent);
    if (!frame.set) {
        vk::DescriptorSetAllocateInfo alloc{};
        alloc.descriptorPool     = descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts        = &deferredLightingSetLayout;
        frame.set                = device->allocateDescriptorSets(alloc).front();
    }
    const auto deferredLightingSet = frame.set;

    createShadowResources();
    vk::ImageView shadowView = currentShadowArrayView();
    if (!shadowView || !shadowSampler) return;

    // Ensure clustered storage exists even with an empty/inactive upload so
    // the SSBO bindings stay valid. Hybrid always samples the cluster tables.
    if (!mesh3dClustered.active) {
        std::vector<ClusteredLightGpu> dirs;
        if (glm::length(glm::vec3(mesh3dFrameUbo.lightDir)) > 1e-4f) {
            ClusteredLightGpu d{};
            d.posRadius = glm::vec4(glm::normalize(glm::vec3(mesh3dFrameUbo.lightDir)), 0.f);
            d.color     = glm::vec4(glm::vec3(mesh3dFrameUbo.lightColor), 1.f);
            dirs.push_back(d);
        }
        ClusteredLightingUpload empty = buildClusteredLighting(
            {}, dirs, mesh3dFrameUbo.view, mesh3dFrameUbo.clipInfo.x, mesh3dFrameUbo.clipInfo.y,
            std::max(1, sceneColorWidth), std::max(1, sceneColorHeight), /*fovYRad*/ 1.f, mesh3dFrameUbo.ambient);
        empty.active = true;
        setMesh3DClusteredLighting(empty);
    } else {
        uploadClusteredLighting(mesh3dClustered);
    }
    auto& storage = currentClusteredStorage();
    if (!storage.lightsBuf.buffer || !storage.tableBuf.buffer || !storage.indicesBuf.buffer) return;

    DeferredLightingUBO ubo{};
    ubo.invViewProj = glm::inverse(mesh3dFrameUbo.mvp);
    ubo.view        = mesh3dFrameUbo.view;
    ubo.lightDir    = mesh3dFrameUbo.lightDir;
    // Mesh3DUBO packs lightCount in lightDir.w; deferred_lighting.frag uses w as
    // the primary-enable flag. Re-encode enable from the direction vector.
    ubo.lightDir.w = glm::length(glm::vec3(mesh3dFrameUbo.lightDir)) > 1e-4f ? 1.f : 0.f;
    ubo.lightColor = mesh3dFrameUbo.lightColor;
    ubo.cameraPos  = glm::vec4(glm::vec3(mesh3dFrameUbo.cameraPos), 0.f);
    ubo.ambient    = mesh3dFrameUbo.ambient;
    if (mesh3dClustered.active) {
        ubo.gridInfo   = mesh3dClustered.gridInfo;
        ubo.clipInfo   = mesh3dClustered.clipInfo;
        ubo.ambient    = mesh3dClustered.ambient;
        ubo.lightDir   = mesh3dClustered.primaryDir;
        ubo.lightColor = glm::vec4(glm::vec3(mesh3dClustered.primaryColor), 0.f);
        ubo.view       = mesh3dClustered.view;
    }
    ubo.clipInfo.z = float(std::max(1, sceneColorWidth));
    ubo.clipInfo.w = float(std::max(1, sceneColorHeight));
    frame.ubo.updateLocal(frameToken(), &ubo, sizeof(ubo));

    // Shadow UBO: stage the latest CSM upload into the clustered shadow ring.
    ensureMesh3dStrides();
    auto& cfslots = currentMesh3dClusteredFrameSlots();
    ensureMesh3dClusteredRing(cfslots);
    if (cfslots.drawIndex >= cfslots.capacity)
        throw Exception("drawDeferredLighting: clustered shadow UBO ring exhausted (%zu draws)", cfslots.capacity);
    // Reserve a slot just as Forward+ draws do: their per-object receive-shadow
    // upload must not overwrite this fullscreen pass's shadow state before submit.
    const size_t   shadowSlot   = cfslots.drawIndex++;
    const uint32_t shadowOffset = uint32_t(shadowSlot) * shadowUboStride;
    updateRingLocal(cfslots.shadowRing, shadowOffset, &mesh3dShadows.ubo, sizeof(ShadowUBO));

    auto writeImage = [&](uint32_t binding, vk::ImageView view, vk::Sampler sampler,
                          vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal) {
        vk::DescriptorImageInfo img{};
        img.sampler     = sampler;
        img.imageView   = view;
        img.imageLayout = layout;
        vk::WriteDescriptorSet w{};
        w.dstSet          = deferredLightingSet;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        w.pImageInfo      = &img;
        device->updateDescriptorSets(w, {});
    };
    auto writeBuffer = [&](uint32_t binding, vk::Buffer buffer, vk::DeviceSize size, vk::DescriptorType type) {
        vk::DescriptorBufferInfo buf{};
        buf.buffer = buffer;
        buf.offset = 0;
        buf.range  = size;
        vk::WriteDescriptorSet w{};
        w.dstSet          = deferredLightingSet;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = type;
        w.pBufferInfo     = &buf;
        device->updateDescriptorSets(w, {});
    };

    writeBuffer(0, frame.ubo.buffer, sizeof(DeferredLightingUBO), vk::DescriptorType::eUniformBuffer);
    writeImage(1, gslot->normal.imageView(), deferredLightingSampler);
    writeImage(2, gslot->depthColor.imageView(), deferredLightingSampler);
    writeImage(3, gslot->albedo.imageView(), deferredLightingSampler);
    writeImage(4, gslot->pbrParams.imageView(), deferredLightingSampler);
    writeImage(5, gslot->emissive.imageView(), deferredLightingSampler);
    writeImage(6, gslot->depth.imageView(), deferredLightingSampler);
    writeBuffer(7, storage.lightsBuf.buffer, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    writeBuffer(8, storage.tableBuf.buffer, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    writeBuffer(9, storage.indicesBuf.buffer, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    {
        vk::DescriptorBufferInfo buf{};
        buf.buffer = cfslots.shadowRing.buffer;
        buf.offset = shadowOffset;
        buf.range  = sizeof(ShadowUBO);
        vk::WriteDescriptorSet w{};
        w.dstSet          = deferredLightingSet;
        w.dstBinding      = 10;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eUniformBuffer;
        w.pBufferInfo     = &buf;
        device->updateDescriptorSets(w, {});
    }
    writeImage(11, shadowView, vk::Sampler(shadowSampler), currentShadowMap().image.currentLayout());

    auto& cb = currentPresentCb();
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, deferredLightingPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, deferredLightingPipelineLayout, 0, 1, &deferredLightingSet,
                          0, nullptr);
    setViewportAndScissor(cb, uint32_t(sceneColorWidth), uint32_t(sceneColorHeight));
    cb.draw(3, 1, 0, 0);
}

}  // namespace eve::graphics::vulkan

// GraphicsDisplayOutput.cpp
namespace eve::graphics::vulkan {
namespace {

Graphics::DisplayOutputSupport probeSupport(vk::PhysicalDevice phys, vk::SurfaceKHR surface) {
    Graphics::DisplayOutputSupport support{};
    if (!phys || !surface) return support;
    const auto formats = phys.getSurfaceFormatsKHR(surface);
    for (const auto& fmt : formats) {
        if (fmt.colorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT &&
            (fmt.format == vk::Format::eA2B10G10R10UnormPack32 || fmt.format == vk::Format::eA2R10G10B10UnormPack32))
            support.hdr10 = true;
        if (fmt.colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT &&
            (fmt.format == vk::Format::eR16G16B16A16Sfloat || fmt.format == vk::Format::eB10G11R11UfloatPack32))
            support.scRgb = true;
    }
    return support;
}

vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& available,
                                         Graphics::DisplayOutputMode mode, Graphics::DisplayColorSpace& activeOut) {
    auto has = [&](vk::Format format, vk::ColorSpaceKHR space) {
        for (const auto& fmt : available)
            if (fmt.format == format && fmt.colorSpace == space) return true;
        return false;
    };
    auto pickHdr10 = [&]() -> std::optional<vk::SurfaceFormatKHR> {
        const vk::Format formats[] = {vk::Format::eA2B10G10R10UnormPack32, vk::Format::eA2R10G10B10UnormPack32};
        for (vk::Format format : formats) {
            if (has(format, vk::ColorSpaceKHR::eHdr10St2084EXT))
                return vk::SurfaceFormatKHR{format, vk::ColorSpaceKHR::eHdr10St2084EXT};
        }
        return std::nullopt;
    };
    auto pickScRgb = [&]() -> std::optional<vk::SurfaceFormatKHR> {
        const vk::Format formats[] = {vk::Format::eR16G16B16A16Sfloat, vk::Format::eB10G11R11UfloatPack32};
        for (vk::Format format : formats) {
            if (has(format, vk::ColorSpaceKHR::eExtendedSrgbLinearEXT))
                return vk::SurfaceFormatKHR{format, vk::ColorSpaceKHR::eExtendedSrgbLinearEXT};
        }
        return std::nullopt;
    };

    std::optional<vk::SurfaceFormatKHR> chosen;
    activeOut = Graphics::DisplayColorSpace::Sdr;
    switch (mode) {
        case Graphics::DisplayOutputMode::Hdr10: chosen = pickHdr10(); break;
        case Graphics::DisplayOutputMode::ScRgb: chosen = pickScRgb(); break;
        case Graphics::DisplayOutputMode::Auto:
            chosen = pickHdr10();
            if (!chosen) chosen = pickScRgb();
            break;
        case Graphics::DisplayOutputMode::Sdr:
        default: break;
    }
    if (chosen) {
        activeOut = chosen->colorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT ? Graphics::DisplayColorSpace::Hdr10
                                                                             : Graphics::DisplayColorSpace::ScRgb;
        return *chosen;
    }

    const vk::SurfaceFormatKHR sdrPrefs[] = {
        {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
        {vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
        {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
        {vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
    };
    for (const auto& pref : sdrPrefs) {
        if (has(pref.format, pref.colorSpace)) return pref;
    }
    if (!available.empty()) return available.front();
    return sdrPrefs[0];
}

}  // namespace

Result<void> Graphics::setDisplayOutputMode(DisplayOutputMode mode) {
    if (mode != DisplayOutputMode::Sdr && mode != DisplayOutputMode::Auto && mode != DisplayOutputMode::Hdr10 &&
        mode != DisplayOutputMode::ScRgb)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown display output mode", "graphics.presentation"));
    if (mode == displayOutputMode_) return Result<void>::success();
    displayOutputMode_ = mode;
    markSwapchainDirty();
    return Result<void>::success();
}

Result<void> Graphics::setDisplayHdrCalibration(float paperWhiteNits, float peakNits) {
    if (!std::isfinite(paperWhiteNits) || !std::isfinite(peakNits))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Display HDR calibration must be finite", "graphics.presentation"));
    display::clampCalibration(paperWhiteNits, peakNits);
    displayPaperWhiteNits_ = paperWhiteNits;
    displayPeakNits_       = peakNits;
    return Result<void>::success();
}

Result<Graphics::DisplayOutputSupport> Graphics::queryDisplayOutputSupport() const {
    if (!initialized || !surface)
        return Result<DisplayOutputSupport>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Display output support requires an initialized window surface",
                              "graphics.presentation"));
    return Result<DisplayOutputSupport>::success(probeSupport(device.physical_device, surface));
}

void Graphics::applyPreferredSwapchainFormats(vkb::SwapchainBuilder& builder) {
    DisplayColorSpace                 active = DisplayColorSpace::Sdr;
    std::vector<vk::SurfaceFormatKHR> available;
    if (surface) available = device.physical_device->getSurfaceFormatsKHR(surface);
    selectedSurfaceFormat_   = chooseSurfaceFormat(available, displayOutputMode_, active);
    activeDisplayColorSpace_ = active;
    builder.set_desired_format(selectedSurfaceFormat_);
    builder.add_fallback_format({vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear});
    builder.add_fallback_format({vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear});
    builder.add_fallback_format({vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear});
    builder.add_fallback_format({vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear});
}

void Graphics::destroyPresentComposeResources() {
    if (device.instance) device->waitIdle();
    for (auto& slot : presentComposeSlots) {
        slot.colorTex.gpuHandle = nullptr;
        if (slot.framebuffer) {
            device->destroyFramebuffer(slot.framebuffer);
            slot.framebuffer = vk::Framebuffer{};
        }
        destroySampler(device, slot.colorGpu.sampler);
    }
    presentComposeSlots.clear();
    presentComposeWidth  = 0;
    presentComposeHeight = 0;
}

void Graphics::ensurePresentComposeResources(int composeW, int composeH) {
    if (composeW <= 0 || composeH <= 0) return;
    ensureHdrOffscreenPipelines();
    if (!texSetLayout || !descriptorPool || !hdrOffscreenRenderPass) return;
    if (!presentComposeSlots.empty() && presentComposeWidth == composeW && presentComposeHeight == composeH) return;
    destroyPresentComposeResources();
    presentComposeWidth       = composeW;
    presentComposeHeight      = composeH;
    const uint32_t   w        = uint32_t(composeW);
    const uint32_t   h        = uint32_t(composeH);
    const vk::Format colorFmt = vk::Format::eR16G16B16A16Sfloat;
    presentComposeSlots.resize(kAsyncResourceCopies);
    for (auto& slot : presentComposeSlots) {
        slot.color       = device.createColorTarget(w, h, colorFmt);
        slot.framebuffer = hdrOffscreenRenderPass.createFramebuffer(device, w, h, {slot.color.asAttachment()});
        vkb::SamplerBuilder sb;
        slot.colorGpu.sampler = sb.magFilter(vk::Filter::eLinear)
                                    .minFilter(vk::Filter::eLinear)
                                    .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                                    .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                                    .build(device);
        auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);
        slot.colorGpu.descriptorSet = vkb::BoundSet{sets[0]};
        slot.colorGpu.width         = composeW;
        slot.colorGpu.height        = composeH;
        slot.colorGpu.viewOverride  = slot.color.imageView();
        writeCombinedImageDescriptor(&slot.colorGpu);
        slot.colorTex.width       = composeW;
        slot.colorTex.height      = composeH;
        slot.colorTex.pixelWidth  = composeW;
        slot.colorTex.pixelHeight = composeH;
        slot.colorTex.gpuHandle   = &slot.colorGpu;
    }
}

Graphics::PresentComposeSlot* Graphics::currentPresentComposeSlot() {
    if (presentComposeSlots.empty()) return nullptr;
    return &presentComposeSlots[currentFrameSlot() % presentComposeSlots.size()];
}

bool Graphics::beginPresentComposePass() {
    ensurePresentComposeResources(int(swapchain.extent.width), int(swapchain.extent.height));
    auto* slot = currentPresentComposeSlot();
    if (!slot || !hdrOffscreenRenderPass || !slot->framebuffer) return false;
    auto&          cb = currentPresentCb();
    vk::ClearValue clear{};
    clear.color = vk::ClearColorValue(std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, clearColor.a});
    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass      = hdrOffscreenRenderPass;
    rpBegin.framebuffer     = slot->framebuffer;
    rpBegin.renderArea      = vk::Rect2D{{0, 0}, {uint32_t(presentComposeWidth), uint32_t(presentComposeHeight)}};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues    = &clear;
    slot->color.beginColorAttachment();
    cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    presentComposeActive_ = true;
    return true;
}

void Graphics::endPresentComposePass() {
    if (!presentComposeActive_) return;
    auto& cb = currentPresentCb();
    cb.endRenderPass();
    if (auto* slot = currentPresentComposeSlot()) slot->color.endSampledLayout();
    presentComposeActive_ = false;
}

void Graphics::encodePresentComposeToSwapchain(vk::CommandBuffer cb) {
    auto* slot = currentPresentComposeSlot();
    if (!slot || !slot->colorTex.gpuHandle || !sceneTonemapPipeline) return;
    auto*                           gpu   = static_cast<GpuTexture*>(slot->colorTex.gpuHandle);
    const display::ActiveColorSpace space = activeDisplayColorSpace_ == DisplayColorSpace::Hdr10
                                                ? display::ActiveColorSpace::Hdr10
                                                : display::ActiveColorSpace::ScRgb;
    const Color tint = display::sceneResolveTint(false, space, false, displayPaperWhiteNits_, displayPeakNits_);
    Batcher     batch;
    batch.addTexturedRect(0.f, 0.f, float(width), float(height), tint, 0.f, 0.f, 1.f, 1.f);
    batch.toNDC(width, height);
    std::vector<TexturedVertex> gpuVerts;
    gpuVerts.reserve(batch.vertices().size());
    for (const auto& v : batch.vertices()) gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});
    auto& frameBufs = currentFrame2DBuffers();
    if (frameBufs.texBufs.empty()) frameBufs.texBufs.emplace_back();
    // Append a dedicated vertex buffer so the encode blit does not overwrite
    // buffers still referenced by the just-recorded compose draws.
    frameBufs.texBufs.emplace_back();
    vkb::HostVertexBuffer& vb = frameBufs.texBufs.back();
    vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, sceneTonemapPipeline);
    vk::DescriptorSet texSet = gpu->descriptorSet;
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1, &texSet, 0, nullptr);
    vk::DeviceSize offset = 0;
    cb.bindVertexBuffers(0, 1, vb, &offset);
    cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
}

}  // namespace eve::graphics::vulkan

// GraphicsMeshPool.cpp
namespace eve::graphics::vulkan {
void Graphics::ensureGpuVertexPool() {
    if (gpuVertexPool_.positions.buffer) return;
    constexpr uint32_t kInitialVertices = 256u << 10;  // 256k verts (~11 MB total)
    constexpr uint32_t kInitialIndices  = 768u << 10;  // 768k u32 indices (3 MB)
    const auto         hostMem          = kHostVisibleCoherent;
    gpuVertexPool_.positions            = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                                             kInitialVertices * sizeof(glm::vec4), hostMem);
    gpuVertexPool_.normals              = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                                             kInitialVertices * sizeof(glm::vec4), hostMem);
    gpuVertexPool_.uvs                  = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                                             kInitialVertices * sizeof(glm::vec2), hostMem);
    gpuVertexPool_.indices              = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                                             kInitialIndices * sizeof(uint32_t), hostMem);
    gpuVertexPool_.vertexCount          = 0;
    gpuVertexPool_.indexCount           = 0;
}

void Graphics::growGpuVertexPool(uint32_t needVertices, uint32_t needIndices) {
    // Pool growth reallocates the buffers; a pending frame may still be reading
    // them, so drain the GPU first (rare path: first-time mesh registration).
    device->waitIdle();
    const uint32_t oldVerts = gpuVertexPool_.vertexCount;
    const uint32_t oldInds  = gpuVertexPool_.indexCount;
    const auto     hostMem  = kHostVisibleCoherent;
    auto           copyInto = [&](vkb::GenericBuffer& dst, vkb::GenericBuffer& src, vk::DeviceSize oldBytes,
                        vk::DeviceSize newBytes) {
        vkb::GenericBuffer grown(device, vk::BufferUsageFlagBits::eStorageBuffer, newBytes, hostMem);
        if (oldBytes > 0) {
            void* srcMap = src.map();
            void* dstMap = grown.map();
            std::memcpy(dstMap, srcMap, oldBytes);
            grown.unmap();
            src.unmap();
        }
        src.release();
        dst = std::move(grown);
    };
    // Leave room after a large first insertion. Exact-fit growth made the next
    // tiny mesh copy the entire large allocation immediately afterwards.
    const auto capacity = [](uint32_t needed) {
        return uint32_t(std::min(uint64_t(needed) * 2u, uint64_t(UINT32_MAX)));
    };
    const uint32_t newVerts =
        std::max(capacity(needVertices), uint32_t(gpuVertexPool_.positions.size / sizeof(glm::vec4)));
    const uint32_t newInds = std::max(capacity(needIndices), uint32_t(gpuVertexPool_.indices.size / sizeof(uint32_t)));
    copyInto(gpuVertexPool_.positions, gpuVertexPool_.positions, vk::DeviceSize(oldVerts) * sizeof(glm::vec4),
             vk::DeviceSize(newVerts) * sizeof(glm::vec4));
    copyInto(gpuVertexPool_.normals, gpuVertexPool_.normals, vk::DeviceSize(oldVerts) * sizeof(glm::vec4),
             vk::DeviceSize(newVerts) * sizeof(glm::vec4));
    copyInto(gpuVertexPool_.uvs, gpuVertexPool_.uvs, vk::DeviceSize(oldVerts) * sizeof(glm::vec2),
             vk::DeviceSize(newVerts) * sizeof(glm::vec2));
    copyInto(gpuVertexPool_.indices, gpuVertexPool_.indices, vk::DeviceSize(oldInds) * sizeof(uint32_t),
             vk::DeviceSize(newInds) * sizeof(uint32_t));
    bindGpuVertexPoolBindless();
}

void Graphics::bindGpuVertexPoolBindless() {
    if (bindlessSets_.empty() || !gpuVertexPool_.positions.buffer) return;
    auto bufWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize size) {
        vk::DescriptorBufferInfo info{buffer, 0, size};
        vk::WriteDescriptorSet   w{};
        w.dstSet          = set;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eStorageBuffer;
        w.pBufferInfo     = &info;
        device->updateDescriptorSets(1, &w, 0, nullptr);
    };
    const uint32_t cap = [&]() {
        const vk::DeviceSize bytes = gpuVertexPool_.positions.size;
        return uint32_t(bytes / sizeof(glm::vec4));
    }();
    const uint32_t indCap = uint32_t(gpuVertexPool_.indices.size / sizeof(uint32_t));
    for (vk::DescriptorSet set : bindlessSets_) {
        bufWrite(set, 18, gpuVertexPool_.positions.buffer, vk::DeviceSize(cap) * sizeof(glm::vec4));
        bufWrite(set, 19, gpuVertexPool_.normals.buffer, vk::DeviceSize(cap) * sizeof(glm::vec4));
        bufWrite(set, 20, gpuVertexPool_.uvs.buffer, vk::DeviceSize(cap) * sizeof(glm::vec2));
        bufWrite(set, 21, gpuVertexPool_.indices.buffer, vk::DeviceSize(indCap) * sizeof(uint32_t));
    }
}

void Graphics::appendGpuMeshToPool(GpuMesh& gpu, const std::vector<MeshVertex>* vertices,
                                   const std::vector<uint32_t>* indices) {
    if (!gpu.vertices.buffer || !gpu.indices.buffer) return;
    ensureGpuVertexPool();
    const uint32_t nVerts = gpu.record.vertexCount;
    const uint32_t nInds  = gpu.record.indexCount;
    if (nVerts == 0 || nInds == 0) return;
    const uint32_t newVerts = gpuVertexPool_.vertexCount + nVerts;
    const uint32_t newInds  = gpuVertexPool_.indexCount + nInds;
    const uint32_t capVerts = uint32_t(gpuVertexPool_.positions.size / sizeof(glm::vec4));
    const uint32_t capInds  = uint32_t(gpuVertexPool_.indices.size / sizeof(uint32_t));
    if (newVerts > capVerts || newInds > capInds) {
        growGpuVertexPool(newVerts, newInds);
    }

    const void* vMap = vertices ? vertices->data() : gpu.vertices.map();
    const void* iMap = indices ? indices->data() : gpu.indices.map();
    if (!vMap || !iMap) return;
    auto* verts  = static_cast<const MeshVertex*>(vMap);
    auto* posDst = static_cast<glm::vec4*>(gpuVertexPool_.positions.map());
    auto* nrmDst = static_cast<glm::vec4*>(gpuVertexPool_.normals.map());
    auto* uvDst  = static_cast<glm::vec2*>(gpuVertexPool_.uvs.map());
    auto* idxDst = static_cast<uint32_t*>(gpuVertexPool_.indices.map());
    if (!posDst || !nrmDst || !uvDst || !idxDst) {
        if (!vertices) gpu.vertices.unmap();
        if (!indices) gpu.indices.unmap();
        return;
    }
    posDst += gpuVertexPool_.vertexCount;
    nrmDst += gpuVertexPool_.vertexCount;
    uvDst += gpuVertexPool_.vertexCount;
    idxDst += gpuVertexPool_.indexCount;
    for (uint32_t i = 0; i < nVerts; ++i) {
        posDst[i] = glm::vec4(verts[i].pos, 0.f);
        nrmDst[i] = glm::vec4(verts[i].normal, 0.f);
        uvDst[i]  = verts[i].uv;
    }
    if (!indices && gpu.indexType == vk::IndexType::eUint16) {
        const auto* src16 = static_cast<const uint16_t*>(iMap);
        for (uint32_t i = 0; i < nInds; ++i) idxDst[i] = uint32_t(src16[i]);
    } else {
        const auto* src32 = static_cast<const uint32_t*>(iMap);
        for (uint32_t i = 0; i < nInds; ++i) idxDst[i] = src32[i];
    }
    gpuVertexPool_.positions.unmap();
    gpuVertexPool_.normals.unmap();
    gpuVertexPool_.uvs.unmap();
    gpuVertexPool_.indices.unmap();
    if (!vertices) gpu.vertices.unmap();
    if (!indices) gpu.indices.unmap();

    // Pool offsets are vertex/index counts, resolved by the vis shaders.
    gpu.record.vertexOffset    = gpuVertexPool_.vertexCount;
    gpu.record.indexOffset     = gpuVertexPool_.indexCount;
    gpu.record.firstIndex      = 0;  // vis pass draws non-indexed from the pool
    gpu.record.vertexBase      = 0;
    gpuVertexPool_.vertexCount = newVerts;
    gpuVertexPool_.indexCount  = newInds;
}

uint32_t Graphics::registerMeshRecord(GpuMesh* gpu, const std::vector<MeshVertex>* vertices,
                                      const std::vector<uint32_t>* indices) {
    if (!gpu) return kInvalidBindlessSlot;
    if (gpu->gpuRecordIndex != kInvalidBindlessSlot) return gpu->gpuRecordIndex;
    if (meshTableRecords_.size() >= meshTableCapacity_) return kInvalidBindlessSlot;
    // dev's mesh factories do not populate GpuMeshRecord; build it on first
    // registration from the host-visible buffers (bounds + ranges).
    if (gpu->record.vertexCount == 0 && gpu->vertices.buffer) {
        const uint32_t vertexCount = uint32_t(gpu->vertices.size / sizeof(MeshVertex));
        gpu->record.vertexCount    = vertexCount;
        gpu->record.indexCount     = gpu->indexCount;
        gpu->record.indexType      = gpu->indexType == vk::IndexType::eUint16 ? 0u : 1u;
        const void* map            = vertices ? vertices->data() : gpu->vertices.map();
        if (map && vertexCount > 0) {
            const auto* verts = static_cast<const MeshVertex*>(map);
            glm::vec3   minv(1e30f), maxv(-1e30f);
            for (uint32_t i = 0; i < vertexCount; ++i) {
                minv = glm::min(minv, verts[i].pos);
                maxv = glm::max(maxv, verts[i].pos);
            }
            const glm::vec3 center = (minv + maxv) * 0.5f;
            float           radius = 0.f;
            for (uint32_t i = 0; i < vertexCount; ++i) radius = std::max(radius, glm::length(verts[i].pos - center));
            gpu->record.boundsCenterRadius = glm::vec4(center, radius);
        }
        if (map && !vertices) gpu->vertices.unmap();
    }
    // Stage 3: lazily pool the mesh's vertices/indices so the vis resolve can
    // fetch attributes by (pool offset + triangle + barycentric).
    appendGpuMeshToPool(*gpu, vertices, indices);
    const uint32_t idx = uint32_t(meshTableRecords_.size());
    meshTableRecords_.push_back(gpu->record);
    meshRecordOwners_.push_back(gpu);
    gpu->gpuRecordIndex = idx;
    syncMeshTable();
    return idx;
}


}  // namespace eve::graphics::vulkan

// GraphicsMeshStylePipeline.cpp
namespace eve::graphics::vulkan {
vk::Pipeline Graphics::createMesh3DStylePipeline(const std::vector<uint32_t>& vert, const std::vector<uint32_t>& frag,
                                                 vk::PipelineLayout layout, const vkb::BuiltRenderPass& rp,
                                                 vk::SampleCountFlagBits samples, BlendMode blend, bool depthWrite,
                                                 bool doubleSided, const MeshShaderRasterState& raster) {
    vk::ShaderModuleCreateInfo vertexInfo{};
    vertexInfo.codeSize                   = vert.size() * sizeof(uint32_t);
    vertexInfo.pCode                      = vert.data();
    auto                       vertModule = device->createShaderModuleUnique(vertexInfo);
    vk::ShaderModuleCreateInfo fragmentInfo{};
    fragmentInfo.codeSize   = frag.size() * sizeof(uint32_t);
    fragmentInfo.pCode      = frag.data();
    auto         fragModule = device->createShaderModuleUnique(fragmentInfo);
    const auto   cull       = doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
    const auto   compare    = raster.depthCompare == MeshDepthCompare::Always      ? vk::CompareOp::eAlways
                              : raster.depthCompare == MeshDepthCompare::LessEqual ? vk::CompareOp::eLessOrEqual
                                                                                   : vk::CompareOp::eLess;
    vk::Pipeline pipe{};
    if (blend != BlendMode::Opaque || raster.colorWriteMask != 15) {
        std::vector<vk::PipelineColorBlendAttachmentState> attachments(1, makeBlendAttachment(blend));
        attachments[0].blendEnable    = blend != BlendMode::Opaque;
        attachments[0].colorWriteMask = vk::ColorComponentFlags(raster.colorWriteMask);
        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.attachmentCount = 1;
        cbs.pAttachments    = attachments.data();
        pipe =
            device.createPipeline()
                .useClassicPipeline(vertModule.get(), fragModule.get())
                .setPipelineLayout(layout)
                .setVertexInputState(
                    vkb::VertexInputStateBuilder().addInputBinding<MeshVertex>().addAttributeDescription<MeshVertex>())
                .setDynamicStatesViewportScissor()
                .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, cull, vk::FrontFace::eCounterClockwise)
                .setMultisampler(false, samples)
                .setDepthBias(raster.depthBiasConstant, raster.depthBiasSlope)
                .setDepthStencil(true, depthWrite, compare)
                .setColorBlending(cbs)
                .build(rp);
    } else {
        pipe =
            device.createPipeline()
                .useClassicPipeline(vertModule.get(), fragModule.get())
                .setPipelineLayout(layout)
                .setVertexInputState(
                    vkb::VertexInputStateBuilder().addInputBinding<MeshVertex>().addAttributeDescription<MeshVertex>())
                .setDynamicStatesViewportScissor()
                .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, cull, vk::FrontFace::eCounterClockwise)
                .setMultisampler(false, samples)
                .setDepthBias(raster.depthBiasConstant, raster.depthBiasSlope)
                .setDepthStencil(true, depthWrite, compare)
                .setColorAttachmentCount(1)
                .build(rp);
    }
    return pipe;
}

}  // namespace eve::graphics::vulkan
