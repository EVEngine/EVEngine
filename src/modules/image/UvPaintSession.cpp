#include "image/UvPaintSession.h"

#include "image/ImageData.h"

namespace eve::image {
namespace {

Result<void> failure(DiagnosticCode code, const char* message, const char* path) {
    return Result<void>::failure(Diagnostic::error(code, message, path, {}, "image.uvPaintSession"));
}

}  // namespace

UvPaintSession::UvPaintSession()  = default;
UvPaintSession::~UvPaintSession() = default;

Result<void> UvPaintSession::initializeResult(const ImageData& image) {
    if (image.getFormat() != "RGBA8")
        return failure(DiagnosticCode::Unsupported, "UV painting requires RGBA8 pixels", "image");
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
        return failure(DiagnosticCode::PreconditionViolation, "UV paint session is not initialized", "session");
    if (undo_.empty()) return failure(DiagnosticCode::PreconditionViolation, "UV paint undo history is empty", "undo");
    current_ = std::move(undo_.back());
    undo_.pop_back();
    ++revision_;
    return Result<void>::success();
}

Result<void> UvPaintSession::restoreResult() {
    if (!original_)
        return failure(DiagnosticCode::PreconditionViolation, "UV paint session is not initialized", "session");
    current_ = std::make_unique<ImageData>(*original_);
    undo_.clear();
    ++revision_;
    return Result<void>::success();
}

Result<void> UvPaintSession::bakeResult() {
    if (!current_)
        return failure(DiagnosticCode::PreconditionViolation, "UV paint session is not initialized", "session");
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
        return failure(DiagnosticCode::PreconditionViolation, "UV paint session is not initialized", "session");
    ImageData replacement(*current_);
    destination.adopt(replacement);
    return Result<void>::success();
}

}  // namespace eve::image
