#include "graphics/vulkan/Graphics.h"
#include "graphics/DisplayOutputEncoding.h"

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

}  // namespace eve::graphics::vulkan
