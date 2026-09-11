#include "image/UvPaintRegion.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::image {

Result<void> UvPaintRegion::prepareResult(int width, int height, float centerU, float centerV, float brushRadiusU,
                                          float brushRadiusV, float red, float green, float blue, float alpha,
                                          bool flipV) {
    auto prepared = prepareUvPaintRegionResult(width, height, centerU, centerV, brushRadiusU, brushRadiusV, red, green,
                                               blue, alpha, flipV);
    if (!prepared.ok()) return Result<void>::failure(prepared.status());
    *this = std::move(prepared).takeValue();
    return Result<void>::success();
}

Result<UvPaintRegion> prepareUvPaintRegionResult(int targetWidth, int targetHeight, float u, float v, float radiusU,
                                                 float radiusV, float r, float g, float b, float a, bool flipV) {
    const auto invalid = [](const char* message, const char* path) {
        return Result<UvPaintRegion>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, path, {}, "image.uvPaintRegion"));
    };
    if (targetWidth <= 0 || targetHeight <= 0) return invalid("paint target dimensions must be positive", "targetSize");
    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(radiusU) || !std::isfinite(radiusV) || radiusU < 0.f ||
        radiusV < 0.f)
        return invalid("UV center and radii must be finite; radii must be non-negative", "brush");
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a) || r < 0.f || r > 1.f ||
        g < 0.f || g > 1.f || b < 0.f || b > 1.f || a < 0.f || a > 1.f)
        return invalid("paint color channels must be finite values in [0,1]", "color");

    UvPaintRegion region;
    region.targetWidth  = targetWidth;
    region.targetHeight = targetHeight;
    region.u            = std::clamp(u, 0.f, 1.f);
    region.v            = std::clamp(v, 0.f, 1.f);
    region.radiusU      = radiusU;
    region.radiusV      = radiusV;
    region.r            = r;
    region.g            = g;
    region.b            = b;
    region.a            = a;

    const float pixelU  = region.u * float(targetWidth - 1);
    const float mappedV = flipV ? 1.f - region.v : region.v;
    const float pixelV  = mappedV * float(targetHeight - 1);
    region.centerX      = static_cast<int>(std::lround(pixelU));
    region.centerY      = static_cast<int>(std::lround(pixelV));
    region.minX         = std::clamp(static_cast<int>(std::floor(pixelU - radiusU * targetWidth)), 0, targetWidth - 1);
    region.maxX         = std::clamp(static_cast<int>(std::ceil(pixelU + radiusU * targetWidth)), 0, targetWidth - 1);
    region.minY = std::clamp(static_cast<int>(std::floor(pixelV - radiusV * targetHeight)), 0, targetHeight - 1);
    region.maxY = std::clamp(static_cast<int>(std::ceil(pixelV + radiusV * targetHeight)), 0, targetHeight - 1);
    return Result<UvPaintRegion>::success(region);
}

}  // namespace eve::image
