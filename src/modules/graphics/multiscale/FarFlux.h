#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "graphics/multiscale/AngularView.h"
#include "spatial/multiscale/CellPosition.h"

namespace eve::graphics {
class Graphics;
}
namespace eve::graphics::multiscale {
/** @brief A point emitter with positive radiant power and independent linear RGB weights. */
struct FarEmitter {
    spatial::CellPosition position;
    double                powerMantissa = 1;
    int                   powerExponent = 0;
    std::array<double, 3> rgbWeights{1, 1, 1};
};

/** @brief Explicit finite output and projection-error budgets for a point-flux snapshot. */
struct FarFluxPolicy {
    std::size_t width = 1920, height = 1080;
    std::size_t maxPixels = 4194304, maxEmitters = 1000000;
    double      maxPositionErrorCells = 1e6;
};

/**
 * @brief Owning linear radiant flux image for distant point emitters, before exposure or occlusion.
 * @ownership Owns every pixel value; no input, camera, provider or callback pointer survives build.
 * @thread Reentrant build; const reads may run concurrently, moves/destruction require exclusivity.
 * @remarks A point contributes its full inverse-square flux to at most four pixels when its
 * conservatively projected direction straddles a pixel boundary. An interval spanning more than
 * two rows or columns is rejected instead of silently misplacing flux. This is a point-flux
 * representation, not a PSF, physical star surface, occlusion or final display transform.
 */
class EVENGINE_API_WORLD FarFluxFrame {
public:
    /** @brief Release owned pixel storage. */
    ~FarFluxFrame();
    /** @brief Transfer an owning flux image. */
    FarFluxFrame(FarFluxFrame&&) noexcept;
    /** @brief Replace pixel storage by ownership transfer. */
    FarFluxFrame& operator=(FarFluxFrame&&) noexcept;
    FarFluxFrame(const FarFluxFrame&) = delete;
    FarFluxFrame& operator=(const FarFluxFrame&) = delete;
    /** @brief Build a complete linear flux image without a fixed distance cutoff.
     * @param emitters Borrowed immutable point sources in the same lattice as camera; not retained.
     * @param camera Borrowed canonical camera position; not retained.
     * @param view Borrowed orthonormal perspective basis; not retained.
     * @param policy Pixel/source budgets and maximum lattice projection error.
     * @return Owning frame or structured range, ambiguity or allocation failure; no partial output.
     * @cost O(P+N) time and O(P) storage for P admitted pixels and N emitters; each source touches at most four pixels.
     */
    [[nodiscard]] static Result<FarFluxFrame> build(std::span<const FarEmitter> emitters,
                                                    const spatial::CellPosition& camera, const AngularView& view,
                                                    FarFluxPolicy policy);
    /** @brief Return the output width in pixels. */
    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    /** @brief Return the output height in pixels. */
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    /** @brief Borrow row-major RGB flux until move or destruction; no exposure is applied. */
    [[nodiscard]] std::span<const std::array<double, 3>> pixels() const noexcept { return pixels_; }

private:
    FarFluxFrame();
    std::size_t                        width_ = 0, height_ = 0;
    std::vector<std::array<double, 3>> pixels_;
};

/**
 * @brief Dedicated Vulkan display of an owning point-flux image in an active multiscale 3D pass.
 * @ownership Owns GPU resources; Graphics owns live handles and a weak provider lifetime revokes use.
 * @thread Graphics-owner thread only, non-reentrant; no callbacks or concurrent provider destruction.
 * @remarks Draw after opaque multiscale geometry and before presentation. Stars use far depth with
 * depth testing and additive color, so nearer opaque geometry can occlude them. This is a point
 * display with exponential exposure, not a PSF, HDR compositor or legacy camera replacement.
 */
class EVENGINE_API_WORLD FarFluxView {
public:
    /** @brief Release resources while their Graphics provider remains live. */
    ~FarFluxView();
    FarFluxView(const FarFluxView&) = delete;
    FarFluxView& operator=(const FarFluxView&) = delete;
    /** @brief Prepare an owning image upload; frame pixels are borrowed only during this call.
     * @param graphics Live Vulkan provider; no retained raw access after its lifetime expires.
     * @param frame Complete flux image matching the provider's current pixel dimensions.
     * @param exposure Positive finite multiplier used by the display transform.
     * @return Owning display or structured range/provider failure without partial publication.
     * @cost O(P) float conversion/upload plus one pipeline and fixed quad creation; amortize across draws.
     */
    [[nodiscard]] static Result<std::unique_ptr<FarFluxView>> prepare(Graphics& graphics, const FarFluxFrame& frame,
                                                                      float exposure);
    /** @brief Replace the resident flux image while keeping its pipeline and display settings.
     * @param frame Complete new flux image with the original surface dimensions; not retained.
     * @return Success or validation/provider failure; no partial publication before GPU submission.
     * @cost O(P) host conversion, owned staging copy and synchronous GPU upload; amortizes pipeline creation.
     */
    [[nodiscard]] Result<void> update(const FarFluxFrame& frame);
    /** @brief Change display exposure without reprojecting or reuploading the flux image.
     * @param exposure Positive finite display multiplier.
     * @return Success or invalid exposure; existing setting remains on failure.
     * @cost Constant CPU state change; next draw updates a small push constant.
     */
    [[nodiscard]] Result<void> setExposure(float exposure);
    /** @brief Draw into the active 3D frame without beginning or presenting it.
     * @return Success or stale/resize/backend failure; backend submission failure needs host recovery.
     * @cost One instanced full-screen quad, O(P) fragment work, no per-emitter draw calls.
     */
    [[nodiscard]] Result<void> draw();

private:
    FarFluxView();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::graphics::multiscale
