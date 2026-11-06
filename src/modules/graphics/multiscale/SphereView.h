#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include "common/Export.h"
#include "common/Result.h"

namespace eve {
struct GpuResidentBufferView;
}
namespace eve::spatial {
struct CellPosition;
}
namespace eve::graphics {
class Graphics;
}
namespace eve::graphics::multiscale {
struct AngularView;
/** @brief Opaque unlit sphere proxy; RGB is linear and radius uses physical mantissa/exponent units. */
struct SphereAppearance {
    double               radiusMantissa = 1;
    int                  radiusExponent = 0;
    std::array<float, 3> color{1, 1, 1};
};
/**
 * @brief Immutable owning reference-render frame, independent of source owners and graphics resources.
 * @ownership Owns all packed sphere values; no input references survive preparation.
 * @thread Const frames may be read concurrently; moves/destruction require exclusivity. No callbacks.
 */
class EVENGINE_API_WORLD SphereFrame {
public:
    /** @brief Release packed values. */
    ~SphereFrame();
    /** @brief Transfer immutable storage. */
    SphereFrame(SphereFrame&&) noexcept;
    /** @brief Replace storage by ownership transfer. */
    SphereFrame& operator=(SphereFrame&&) noexcept;
    SphereFrame(const SphereFrame&)            = delete;
    SphereFrame& operator=(const SphereFrame&) = delete;
    /**
     * @brief Validate and pack a complete frame before any GPU or presentation state is changed.
     * @param centers Borrowed same-lattice positions, parallel to appearances; not retained.
     * @param appearances Borrowed opaque sphere parameters; not retained.
     * @param camera Borrowed position in the same lattice; not retained.
     * @param view Borrowed explicit perspective basis; not retained.
     * @param maxSpheres Input admission budget, including offscreen candidates.
     * @return Owning frame or structured range/precision/budget failure; no partial publication.
     * @thread May use bounded internal workers for large batches. Caller keeps borrowed inputs immutable until return;
     * workers only read inputs and write disjoint private slots. No callbacks or graphics calls.
     * @cost O(N) validation, O(N) temporary visibility bytes and O(N) packet capacity per rebuild;
     * bounded worker count is amortized only for large batches. Reuse unchanged frames.
     * @remarks Reference path rejects radius/distance below 1e-5 and camera-on-surface uncertainty.
     * This is opaque geometry, not subpixel flux, arbitrary meshes or certified pixel-level accuracy.
     */
    [[nodiscard]] static Result<SphereFrame> build(std::span<const spatial::CellPosition> centers,
                                                   std::span<const SphereAppearance>      appearances,
                                                   const spatial::CellPosition& camera, const AngularView& view,
                                                   std::size_t maxSpheres);

private:
    friend class SphereView;
    friend class SphereBatch;
    struct Impl;
    explicit SphereFrame(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/**
 * @brief Dedicated opaque sphere reference view with shared logarithmic ray-distance depth.
 * @ownership Owns resource leases; Graphics owns live handles. Weak provider lifetime revokes access.
 * @thread Graphics-owner thread only, non-reentrant, no callbacks or concurrent provider destruction.
 * @remarks render owns the whole begin3DFrame/present sequence; draw leaves presentation to the host.
 * Call either only at a fresh frame boundary;
 * do not interleave legacy draws, shadows, transparency or postprocessing using a different depth convention.
 */
class EVENGINE_API_WORLD SphereView {
public:
    /** @brief Release resources if their provider is live; provider-first destruction is safe. */
    ~SphereView();
    SphereView(const SphereView&)            = delete;
    SphereView& operator=(const SphereView&) = delete;
    /**
     * @brief Allocate a shader and fixed full-screen quad for the explicit reference view.
     * @param graphics Borrowed live provider; its weak lifetime guards retained resource handles.
     * @return Owning view or Unsupported for an unavailable backend; no default renderer changes.
     * @cost Shader/pipeline creation and one fixed mesh upload per view lifetime; amortize across frames.
     */
    [[nodiscard]] static Result<std::unique_ptr<SphereView>> build(Graphics& graphics);
    /**
     * @brief Render and present one already validated owning frame in this dedicated view.
     * @param frame Borrowed only until return; does not retain frame or scene pointers.
     * @return Success, stale-provider or moved-frame failure before drawing; backend errors remain failures.
     * @cost O(visible N) draw submissions and O(visible N * viewport pixels) fragment work per frame;
     * this reference path is not the million-object performance implementation.
     */
    [[nodiscard]] Result<void> render(const SphereFrame& frame);
    /**
     * @brief Begin and draw a dedicated sphere pass without presenting; the host must present afterward.
     * @param frame Borrowed until return; no frame or scene pointers are retained.
     * @return Success or structured failure. Invalid frames/providers fail before beginning the pass.
     * @remarks Intended for the normal engine render callback, whose loop owns presentation.
     * Backend failure may leave a partial pass; the host must finish/recover it. Never mix legacy depth draws.
     * @cost O(visible N) submissions and O(visible N * viewport pixels) fragment work per frame.
     */
    [[nodiscard]] Result<void> draw(const SphereFrame& frame);

private:
    friend class SphereBatch;
    SphereView();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
/**
 * @brief Uploaded sphere view with one instanced draw submission and explicit prepared-frame updates.
 * @ownership Owns GPU resource leases; a weak Graphics lifetime revokes all access.
 * @thread Graphics-owner thread only, non-reentrant; no callbacks or concurrent provider destruction.
 * @remarks Dedicated logarithmic depth pass, with the same admitted geometry as SphereFrame.
 * Existing SphereView remains the per-sphere reference renderer. This type explicitly pays upload
 * and program construction during preparation; it does not promise a frame-time bound.
 */
class EVENGINE_API_WORLD SphereBatch {
public:
    /** @brief Release resources when the provider remains live; provider-first destruction is safe. */
    ~SphereBatch();
    SphereBatch(const SphereBatch&)            = delete;
    SphereBatch& operator=(const SphereBatch&) = delete;
    /**
     * @brief Prepare an owning GPU snapshot outside a render pass; failures publish no batch.
     * @param graphics Borrowed provider; retained access is guarded by its weak lifetime.
     * @param frame Borrowed validated CPU snapshot, not retained after this call.
     * @return Owning batch or structured backend, capacity, allocation or stale-frame failure.
     * @remarks Requires Vulkan and the existing SPIR-V resource validation provider. Missing capability
     * is an explicit failure. Up to 1,000,000 visible spheres; no candidates are silently dropped.
     * @cost O(visible N) packing, validation and immutable upload plus shader/pipeline construction;
     * amortize across draws of the unchanged snapshot. At least 128 bytes per visible sphere before mirrors.
     */
    [[nodiscard]] static Result<std::unique_ptr<SphereBatch>> prepare(Graphics& graphics, const SphereFrame& frame);
    /**
     * @brief Publish an already prepared CPU frame into this batch's existing capacity.
     * @param frame Borrowed immutable CPU preparation result, not retained after return.
     * @return Success or structured failure. Moved frames, stale providers and excess visible counts reject
     * before GPU writes. Device failure after submission requires host recovery, without a rollback promise.
     * @remarks Capacity is max(1, initial visible count). Outside submission only, graphics-owner thread,
     * non-reentrant, no callbacks. Pipeline, matrix buffer and image dimensions remain unchanged.
     * @cost Linear in reserved batch capacity for packing/upload, plus provider shader lookup and GPU
     * synchronization per prepared frame; no pipeline creation. Shrinking draw count does not shrink the upload.
     */
    [[nodiscard]] Result<void> update(const SphereFrame& frame);
    /**
     * @brief Publish already packed RGBA32F sphere packets from a same-device resident buffer.
     * @param packets Borrowed full-capacity image payload; its producer must finish before this call.
     * @param count Number of active visible packets, at most the prepared batch capacity.
     * @return Success or structured shape/provider/ownership failure without changing the draw count.
     * @ownership The producer retains the buffer; no native handle is retained after return.
     * @thread Graphics owner thread only, outside submission, non-reentrant; no callbacks.
     * @cost O(reserved capacity) device-local transfer plus graphics synchronization, with no CPU packet
     * packing, readback, shader compilation or matrix upload. Reuse the prepared batch across frames.
     */
    [[nodiscard]] Result<void> updateResidentPackets(const GpuResidentBufferView& packets, std::uint32_t count);
    /**
     * @brief Begin and draw the prepared dedicated pass; the host owns presentation.
     * @return Success, stale-provider failure before submission, or backend failure requiring host recovery.
     * @remarks Call only at a fresh frame boundary; do not mix legacy depth, shadows or transparency.
     * @cost One instanced submission for a nonempty snapshot; O(sum of projected rectangle areas) GPU work.
     * No per-sphere CPU traversal, resource upload or shader compilation occurs here.
     */
    [[nodiscard]] Result<void> draw();

private:
    SphereBatch();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::graphics::multiscale
