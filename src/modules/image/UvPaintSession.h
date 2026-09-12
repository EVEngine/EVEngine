#pragma once

#include "common/Result.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace eve::image {

class ImageData;
struct UvPaintReceipt;

/**
 * @brief Transactional, renderer-independent UV texture painting session.
 *
 * The session owns original/current pixels and bounded undo snapshots. It is synchronous and
 * thread-affine, retains no model, renderer, physics hit, or caller-owned image pointer.
 */
class UvPaintSession {
public:
    UvPaintSession();
    ~UvPaintSession();
    UvPaintSession(const UvPaintSession&)            = delete;
    UvPaintSession& operator=(const UvPaintSession&) = delete;

    /** @brief Initialize with an owning RGBA8 copy and clear history. */
    [[nodiscard]] Result<void> initializeResult(const ImageData& image);
    /** @brief Paint one circular UV brush as a single undoable transaction. */
    [[nodiscard]] Result<UvPaintReceipt> paintCircleResult(float u, float v, float radiusPixels, float r, float g,
                                                           float b, float a, bool wrapU = false, bool wrapV = false);
    /** @brief Restore the previous committed paint transaction. */
    [[nodiscard]] Result<void> undoResult();
    /** @brief Restore the initial/baked pixels and clear history. */
    [[nodiscard]] Result<void> restoreResult();
    /** @brief Make current pixels the new restore point and clear history. */
    [[nodiscard]] Result<void> bakeResult();
    /** @brief Return a caller-owned current pixel snapshot. */
    [[nodiscard]] Result<std::unique_ptr<ImageData>> currentImageResult() const;
    /** @brief Atomically copy current pixels into caller-owned RGBA8 storage. */
    [[nodiscard]] Result<void> copyCurrentToResult(ImageData& destination) const;
    /** @brief Return monotonic session revision. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Return available undo transaction count. */
    [[nodiscard]] int undoCount() const noexcept { return static_cast<int>(undo_.size()); }
    /** @brief Report whether pixels have been initialized. */
    [[nodiscard]] bool isInitialized() const noexcept { return current_ != nullptr; }

private:
    std::unique_ptr<ImageData>              original_;
    std::unique_ptr<ImageData>              current_;
    std::vector<std::unique_ptr<ImageData>> undo_;
    std::uint64_t                           revision_ = 0;
};

}  // namespace eve::image
