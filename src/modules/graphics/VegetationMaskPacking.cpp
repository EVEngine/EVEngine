#include "graphics/VegetationMaskPacking.h"
#include <cmath>
#include <new>
#include "graphics/VegetationField.h"
namespace eve::graphics {
Result<VegetationMask> packVegetationOrm(const VegetationMask& source, float occlusion, float smoothness) {
    auto           unit       = [](float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; };
    const uint64_t pixelCount = uint64_t(source.width) * source.height;
    if (!unit(occlusion) || !unit(smoothness) || !source.width || !source.height || pixelCount > 16ull * 1024 * 1024 ||
        source.pixels.size() != pixelCount)
        return Result<VegetationMask>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                 "invalid vegetation mask dimensions or factors", {},
                                                                 {}, "graphics.vegetation.mask"));
    for (const auto& pixel : source.pixels)
        for (unsigned c = 0; c < 4; ++c)
            if (!unit(pixel[c]))
                return Result<VegetationMask>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "vegetation source mask must contain finite linear unit values",
                    {}, {}, "graphics.vegetation.mask"));
    try {
        VegetationMask result;
        result.width  = source.width;
        result.height = source.height;
        result.pixels.reserve(source.pixels.size());
        for (const auto& pixel : source.pixels)
            result.pixels.emplace_back(std::lerp(1.f, pixel.g, occlusion), 1.f - pixel.a * smoothness, 0.f, pixel.b);
        return Result<VegetationMask>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<VegetationMask>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "vegetation mask allocation failed", {}, {}, "graphics.vegetation.mask"));
    }
}
}  // namespace eve::graphics
