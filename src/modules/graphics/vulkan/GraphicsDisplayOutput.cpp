#include "graphics/vulkan/Graphics.h"
#include "graphics/DisplayOutputEncoding.h"
#include "graphics/vulkan/GraphicsInternal.h"

#include <cmath>
#include <optional>
#include <vector>

namespace eve::graphics::vulkan {
namespace {

Graphics::DisplayOutputSupport probeSupport(vk::PhysicalDevice phys, vk::SurfaceKHR surface) {
    Graphics::DisplayOutputSupport support{};
    if (!phys || !surface) return support;
    const auto formats = phys.getSurfaceFormatsKHR(surface);
    for (const auto &fmt : formats) {
        if (fmt.colorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT &&
            (fmt.format == vk::Format::eA2B10G10R10UnormPack32 ||
             fmt.format == vk::Format::eA2R10G10B10UnormPack32))
            support.hdr10 = true;
        if (fmt.colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT &&
            (fmt.format == vk::Format::eR16G16B16A16Sfloat ||
             fmt.format == vk::Format::eB10G11R11UfloatPack32))
            support.scRgb = true;
    }
    return support;
}

vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &available,
                                         Graphics::DisplayOutputMode mode,
                                         Graphics::DisplayColorSpace &activeOut) {
    auto has = [&](vk::Format format, vk::ColorSpaceKHR space) {
        for (const auto &fmt : available)
            if (fmt.format == format && fmt.colorSpace == space) return true;
        return false;
    };
    auto pickHdr10 = [&]() -> std::optional<vk::SurfaceFormatKHR> {
        const vk::Format formats[] = {vk::Format::eA2B10G10R10UnormPack32,
                                      vk::Format::eA2R10G10B10UnormPack32};
        for (vk::Format format : formats) {
            if (has(format, vk::ColorSpaceKHR::eHdr10St2084EXT))
                return vk::SurfaceFormatKHR{format, vk::ColorSpaceKHR::eHdr10St2084EXT};
        }
        return std::nullopt;
    };
    auto pickScRgb = [&]() -> std::optional<vk::SurfaceFormatKHR> {
        const vk::Format formats[] = {vk::Format::eR16G16B16A16Sfloat,
                                      vk::Format::eB10G11R11UfloatPack32};
        for (vk::Format format : formats) {
            if (has(format, vk::ColorSpaceKHR::eExtendedSrgbLinearEXT))
                return vk::SurfaceFormatKHR{format, vk::ColorSpaceKHR::eExtendedSrgbLinearEXT};
        }
        return std::nullopt;
    };

    std::optional<vk::SurfaceFormatKHR> chosen;
    activeOut = Graphics::DisplayColorSpace::Sdr;
    switch (mode) {
        case Graphics::DisplayOutputMode::Hdr10:
            chosen = pickHdr10();
            break;
        case Graphics::DisplayOutputMode::ScRgb:
            chosen = pickScRgb();
            break;
        case Graphics::DisplayOutputMode::Auto:
            chosen = pickHdr10();
            if (!chosen) chosen = pickScRgb();
            break;
        case Graphics::DisplayOutputMode::Sdr:
        default:
            break;
    }
    if (chosen) {
        activeOut = chosen->colorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT
                        ? Graphics::DisplayColorSpace::Hdr10
                        : Graphics::DisplayColorSpace::ScRgb;
        return *chosen;
    }

    const vk::SurfaceFormatKHR sdrPrefs[] = {
        {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
        {vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
        {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
        {vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
    };
    for (const auto &pref : sdrPrefs) {
        if (has(pref.format, pref.colorSpace)) return pref;
    }
    if (!available.empty()) return available.front();
    return sdrPrefs[0];
}

}  // namespace

Result<void> Graphics::setDisplayOutputMode(DisplayOutputMode mode) {
    if (mode != DisplayOutputMode::Sdr && mode != DisplayOutputMode::Auto &&
        mode != DisplayOutputMode::Hdr10 && mode != DisplayOutputMode::ScRgb)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Unknown display output mode",
                                                       "graphics.presentation"));
    if (mode == displayOutputMode_) return Result<void>::success();
    displayOutputMode_ = mode;
    markSwapchainDirty();
    return Result<void>::success();
}

Result<void> Graphics::setDisplayHdrCalibration(float paperWhiteNits, float peakNits) {
    if (!std::isfinite(paperWhiteNits) || !std::isfinite(peakNits))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Display HDR calibration must be finite",
                                                       "graphics.presentation"));
    display::clampCalibration(paperWhiteNits, peakNits);
    displayPaperWhiteNits_ = paperWhiteNits;
    displayPeakNits_ = peakNits;
    return Result<void>::success();
}

Result<Graphics::DisplayOutputSupport> Graphics::queryDisplayOutputSupport() const {
    if (!initialized || !surface)
        return Result<DisplayOutputSupport>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "Display output support requires an initialized window surface",
            "graphics.presentation"));
    return Result<DisplayOutputSupport>::success(
        probeSupport(device.physical_device, surface));
}

void Graphics::applyPreferredSwapchainFormats(vkb::SwapchainBuilder &builder) {
    DisplayColorSpace active = DisplayColorSpace::Sdr;
    std::vector<vk::SurfaceFormatKHR> available;
    if (surface)
        available = device.physical_device->getSurfaceFormatsKHR(surface);
    selectedSurfaceFormat_ = chooseSurfaceFormat(available, displayOutputMode_, active);
    activeDisplayColorSpace_ = active;
    builder.set_desired_format(selectedSurfaceFormat_);
    builder.add_fallback_format({vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear});
    builder.add_fallback_format({vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear});
    builder.add_fallback_format({vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear});
    builder.add_fallback_format({vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear});
}

void Graphics::destroyPresentComposeResources() {
    if (device.instance) device->waitIdle();
    for (auto &slot : presentComposeSlots) {
        slot.colorTex.gpuHandle = nullptr;
        if (slot.framebuffer) {
            device->destroyFramebuffer(slot.framebuffer);
            slot.framebuffer = vk::Framebuffer{};
        }
        destroySampler(device, slot.colorGpu.sampler);
    }
    presentComposeSlots.clear();
    presentComposeWidth = 0;
    presentComposeHeight = 0;
}

void Graphics::ensurePresentComposeResources(int composeW, int composeH) {
    if (composeW <= 0 || composeH <= 0) return;
    ensureHdrOffscreenPipelines();
    if (!texSetLayout || !descriptorPool || !hdrOffscreenRenderPass) return;
    if (!presentComposeSlots.empty() && presentComposeWidth == composeW &&
        presentComposeHeight == composeH)
        return;
    destroyPresentComposeResources();
    presentComposeWidth = composeW;
    presentComposeHeight = composeH;
    const uint32_t w = uint32_t(composeW);
    const uint32_t h = uint32_t(composeH);
    const vk::Format colorFmt = vk::Format::eR16G16B16A16Sfloat;
    presentComposeSlots.resize(kAsyncResourceCopies);
    for (auto &slot : presentComposeSlots) {
        slot.color = device.createColorTarget(w, h, colorFmt);
        slot.framebuffer =
            hdrOffscreenRenderPass.createFramebuffer(device, w, h, {slot.color.asAttachment()});
        vkb::SamplerBuilder sb;
        slot.colorGpu.sampler = sb.magFilter(vk::Filter::eLinear)
                                    .minFilter(vk::Filter::eLinear)
                                    .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                                    .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                                    .build(device);
        auto sets = vkb::DescriptorSetBuilder()
                        .layout(texSetLayout)
                        .build(device.instance, descriptorPool);
        slot.colorGpu.descriptorSet = vkb::BoundSet{sets[0]};
        slot.colorGpu.width = composeW;
        slot.colorGpu.height = composeH;
        slot.colorGpu.viewOverride = slot.color.imageView();
        writeCombinedImageDescriptor(&slot.colorGpu);
        slot.colorTex.width = composeW;
        slot.colorTex.height = composeH;
        slot.colorTex.pixelWidth = composeW;
        slot.colorTex.pixelHeight = composeH;
        slot.colorTex.gpuHandle = &slot.colorGpu;
    }
}

Graphics::PresentComposeSlot *Graphics::currentPresentComposeSlot() {
    if (presentComposeSlots.empty()) return nullptr;
    return &presentComposeSlots[currentFrameSlot() % presentComposeSlots.size()];
}

bool Graphics::beginPresentComposePass() {
    ensurePresentComposeResources(int(swapchain.extent.width), int(swapchain.extent.height));
    auto *slot = currentPresentComposeSlot();
    if (!slot || !hdrOffscreenRenderPass || !slot->framebuffer) return false;
    auto &cb = currentPresentCb();
    vk::ClearValue clear{};
    clear.color = vk::ClearColorValue(
        std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, clearColor.a});
    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = hdrOffscreenRenderPass;
    rpBegin.framebuffer = slot->framebuffer;
    rpBegin.renderArea = vk::Rect2D{{0, 0}, {uint32_t(presentComposeWidth), uint32_t(presentComposeHeight)}};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clear;
    slot->color.beginColorAttachment();
    cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    presentComposeActive_ = true;
    return true;
}

void Graphics::endPresentComposePass() {
    if (!presentComposeActive_) return;
    auto &cb = currentPresentCb();
    cb.endRenderPass();
    if (auto *slot = currentPresentComposeSlot()) slot->color.endSampledLayout();
    presentComposeActive_ = false;
}

void Graphics::encodePresentComposeToSwapchain(vk::CommandBuffer cb) {
    auto *slot = currentPresentComposeSlot();
    if (!slot || !slot->colorTex.gpuHandle || !sceneTonemapPipeline) return;
    auto *gpu = static_cast<GpuTexture *>(slot->colorTex.gpuHandle);
    const display::ActiveColorSpace space =
        activeDisplayColorSpace_ == DisplayColorSpace::Hdr10
            ? display::ActiveColorSpace::Hdr10
            : display::ActiveColorSpace::ScRgb;
    const Color tint = display::sceneResolveTint(false, space, false, displayPaperWhiteNits_,
                                                 displayPeakNits_);
    Batcher batch;
    batch.addTexturedRect(0.f, 0.f, float(width), float(height), tint, 0.f, 0.f, 1.f, 1.f);
    batch.toNDC(width, height);
    std::vector<TexturedVertex> gpuVerts;
    gpuVerts.reserve(batch.vertices().size());
    for (const auto &v : batch.vertices())
        gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});
    auto &frameBufs = currentFrame2DBuffers();
    if (frameBufs.texBufs.empty()) frameBufs.texBufs.emplace_back();
    // Append a dedicated vertex buffer so the encode blit does not overwrite
    // buffers still referenced by the just-recorded compose draws.
    frameBufs.texBufs.emplace_back();
    vkb::HostVertexBuffer &vb = frameBufs.texBufs.back();
    vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, sceneTonemapPipeline);
    vk::DescriptorSet texSet = gpu->descriptorSet;
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1, &texSet, 0,
                          nullptr);
    vk::DeviceSize offset = 0;
    cb.bindVertexBuffers(0, 1, vb, &offset);
    cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
}

}  // namespace eve::graphics::vulkan
