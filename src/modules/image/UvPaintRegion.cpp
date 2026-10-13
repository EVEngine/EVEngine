#include "image/UvPaintRegion.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "image/ImageData.h"
#include "image/UvPaintSession.h"

// UvPaintRegion.cpp
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

// UvPaintSession.cpp
namespace eve::image {
// namespace

UvPaintSession::UvPaintSession()  = default;
UvPaintSession::~UvPaintSession() = default;

Result<void> UvPaintSession::initializeResult(const ImageData& image) {
    if (image.getFormat() != "RGBA8")
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "UV painting requires RGBA8 pixels",
                                                       "image", {}, "image.uvPaintSession"));
    original_ = std::make_unique<ImageData>(image);
    current_  = std::make_unique<ImageData>(image);
    undo_.clear();
    ++revision_;
    return Result<void>::success();
}

Result<UvPaintReceipt> UvPaintSession::paintCircleResult(float u, float v, float radiusPixels, float r, float g,
                                                         float b, float a, bool wrapU, bool wrapV) {
    if (!current_)
        return Result<UvPaintReceipt>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                 "UV paint session is not initialized", "session", {},
                                                                 "image.uvPaintSession"));
    auto candidate = std::make_unique<ImageData>(*current_);
    auto painted   = candidate->paintCircleUv(u, v, radiusPixels, ImageData::Colorf{r, g, b, a}, wrapU, wrapV);
    if (!painted.ok()) return painted;
    undo_.push_back(std::move(current_));
    if (undo_.size() > 32u) undo_.erase(undo_.begin());
    current_ = std::move(candidate);
    ++revision_;
    return painted;
}

Result<void> UvPaintSession::undoResult() {
    if (!current_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "UV paint session is not initialized", "session", {},
                                                       "image.uvPaintSession"));
    if (undo_.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "UV paint undo history is empty", "undo", {},
                                                       "image.uvPaintSession"));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    ++revision_;
    return Result<void>::success();
}

Result<void> UvPaintSession::restoreResult() {
    if (!original_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "UV paint session is not initialized", "session", {},
                                                       "image.uvPaintSession"));
    current_ = std::make_unique<ImageData>(*original_);
    undo_.clear();
    ++revision_;
    return Result<void>::success();
}

Result<void> UvPaintSession::bakeResult() {
    if (!current_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "UV paint session is not initialized", "session", {},
                                                       "image.uvPaintSession"));
    original_ = std::make_unique<ImageData>(*current_);
    undo_.clear();
    ++revision_;
    return Result<void>::success();
}

Result<std::unique_ptr<ImageData>> UvPaintSession::currentImageResult() const {
    if (!current_)
        return Result<std::unique_ptr<ImageData>>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                             "UV paint session is not initialized",
                                                                             "session", {}, "image.uvPaintSession"));
    return Result<std::unique_ptr<ImageData>>::success(std::make_unique<ImageData>(*current_));
}

Result<void> UvPaintSession::copyCurrentToResult(ImageData& destination) const {
    if (!current_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "UV paint session is not initialized", "session", {},
                                                       "image.uvPaintSession"));
    ImageData replacement(*current_);
    destination.adopt(replacement);
    return Result<void>::success();
}

}  // namespace eve::image
