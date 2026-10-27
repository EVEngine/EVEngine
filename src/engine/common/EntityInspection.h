#pragma once
#include "common/FrameCapture.h"

namespace eve {
/** @brief Generation-qualified Renderable3D request; geometry diagnostics, world axes, isolated entity. */
struct EntityViewRequest {
    uint32_t entityId = 0, generation = 0, cellSize = 512;
    /** @brief Optional owned per-part palettes for an independently sampled pose. Empty uses the live pose.
     * Nonempty requires one palette per mesh part and GPU skinning on every part. Consumed during freeze;
     * snapshots omit these input arrays. Never written into the source Mesh. */
    std::vector<std::vector<float>> skinPalettes;
};
/** @brief Owning per-panel metadata; matrices are column-major RH/ZO/Y-down. */
struct EntityViewCamera {
    std::array<float, 16> view{}, projection{};
    uint32_t              x = 0, y = 0, width = 0, height = 0;
};
/** @brief Atomic four-view CPU atlas with metadata from the same owner-thread freeze operation. */
struct EntityViewSnapshot {
    FrameCaptureSnapshot            capture;
    EntityViewRequest               request;
    std::array<EntityViewCamera, 4> cameras;
    std::array<float, 6>            bounds{};
    float                           worldUnitsPerPixel = 0.f;
};
/**
 * @brief Optional native geometry-inspection provider, independent of graphics types.
 * @thread Engine owner thread only; no scripts or callbacks are invoked.
 * @lifetime Query on each call. Provider owns GPU work; returned CPU storage is immutable and lease-owned.
 */
class EVENGINE_API_FOUNDATION_INLINE IEntityInspection {
public:
    static constexpr const char* capabilityName = "IEntityInspection";
    /** @brief Withdraw registration before destroying the provider. */
    virtual ~IEntityInspection() = default;
    /**
     * @brief Queue a bounded geometry-only capture; never wait for GPU completion.
     * @param request Owning target identity and panel dimensions; resolved and frozen on this owner-thread call.
     * @return Opaque token or explicit unsupported/budget diagnostic.
     * @cost Four extra raster views plus readback per request; reuse the resulting snapshot.
     */
    [[nodiscard]] virtual Result<std::string> request(EntityViewRequest request) = 0;
    /** @brief Poll token without waiting; returns owning metadata and CPU lease only when ready. */
    [[nodiscard]] virtual Result<EntityViewSnapshot> snapshot(std::string_view token) = 0;
    /** @brief Cancel/release; submitted resources survive until their fence completes. */
    [[nodiscard]] virtual Result<FrameCapturePhase> release(std::string_view token) = 0;
};
}  // namespace eve
