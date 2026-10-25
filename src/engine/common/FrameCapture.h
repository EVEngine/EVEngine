#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve {

/** @brief Stable logical render attachments; Depth and Velocity share storage. */
enum class FrameAttachment : uint8_t {
    Final,
    Depth,
    HwDepth,
    Normal,
    Albedo,
    PbrParams,
    Emissive,
    Velocity,
    Shadow0,
    Shadow1,
    Shadow2
};
/** @brief Capture progress; Ready is published only after all selected copies complete. */
enum class FrameCapturePhase : uint8_t {
    Queued,
    Recording,
    ReadbackPending,
    Ready,
    Failed,
    Cancelled,
    Expired,
    Released
};
/** @brief CPU storage encoding; no GPU object escapes the provider. */
enum class FramePixelFormat : uint8_t { Rgba8, Bgra8, Depth32Float };
/** @brief Stable protocol spelling of an attachment. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view frameAttachmentName(FrameAttachment attachment);
/** @brief Validate a protocol attachment name without inventing a resource. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<FrameAttachment> parseFrameAttachment(std::string_view name);
/** @brief Stable protocol spelling of a progress state. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view frameCapturePhaseName(FrameCapturePhase phase);

/** @brief Owning request; timeout uses monotonic wall time, never gameplay time. */
struct FrameCaptureRequest {
    std::vector<FrameAttachment> attachments;
    std::chrono::milliseconds    timeout{5000};
    bool                         reportUnavailable = false;
    std::string                  requestKey;
};

/** @brief Immutable CPU attachment with producer-provided decoding metadata. */
struct FrameCaptureBuffer {
    FrameAttachment                             attachment = FrameAttachment::Final;
    FramePixelFormat                            format     = FramePixelFormat::Rgba8;
    uint32_t                                    width = 0, height = 0;
    uint64_t                                    producerFrameId = 0;
    std::string                                 unavailableReason;
    std::string                                 encoding;
    std::string                                 colorSpace;
    std::array<float, 16>                       producerMatrix{};
    std::shared_ptr<const std::vector<uint8_t>> bytes;
};

/** @brief Owning snapshot metadata; byte storage is immutable and lease-owned. */
struct FrameCaptureSnapshot {
    std::string                     token;
    FrameCapturePhase               phase   = FrameCapturePhase::Queued;
    uint64_t                        frameId = 0, generation = 0;
    uint64_t                        targetGeneration = 0;
    std::string                     backend;
    std::string                     reason;
    std::vector<FrameCaptureBuffer> buffers;
    std::array<float, 16>           inverseProjection{};
    std::array<float, 16>           viewMatrix{};
    std::array<float, 16>           projectionMatrix{};
    std::string                     depthConvention;
};

/** @brief Backend support is explicit; candidates need not be produced in every frame. */
struct FrameCaptureCapabilities {
    std::string                  backend;
    std::vector<FrameAttachment> attachments;
    uint64_t                     generation = 0;
    size_t                       maxBytes   = 256u * 1024u * 1024u;
};

/**
 * @brief Single-provider frame observation boundary, independent of graphics headers.
 * @thread Engine main thread only; no callbacks or scripts are invoked by operations.
 * @lifetime Query the provider for each tool call; snapshots retain owning CPU leases.
 */
class EVENGINE_API_FOUNDATION_INLINE IRenderFrameCapture {
public:
    static constexpr const char* capabilityName = "IRenderFrameCapture";
    /** @brief Destroy the service after withdrawing its registration. */
    virtual ~IRenderFrameCapture() = default;
    /** @brief Inspect backend support, without GPU work. */
    [[nodiscard]] virtual FrameCaptureCapabilities capabilities() const = 0;
    /** @brief Queue next normal frame. @cost Readback scales with selected pixel count, once per request. */
    [[nodiscard]] virtual Result<std::string> request(FrameCaptureRequest request) = 0;
    /** @brief Inspect an immutable CPU snapshot; never waits on a GPU fence. */
    [[nodiscard]] virtual Result<FrameCaptureSnapshot> snapshot(std::string_view token) = 0;
    /** @brief Cancel or release a token; in-flight resources remain backend-owned until safe. */
    [[nodiscard]] virtual Result<FrameCapturePhase> release(std::string_view token) = 0;
    /** @brief Invalidate all tokens on provider or game generation changes. */
    virtual void invalidate() = 0;
};

/**
 * @brief Graphics-owned state machine and immutable CPU snapshot authority.
 * @thread Main-thread only; supplied clock must not reenter the store.
 * @lifetime Backend owns GPU staging independently until its completion signal.
 */
class EVENGINE_API_FOUNDATION FrameCaptureStore final : public IRenderFrameCapture {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    /** @brief Construct a bounded store with an injectable monotonic clock. */
    FrameCaptureStore();
    /** @brief Construct a bounded store with an injected monotonic clock. */
    explicit FrameCaptureStore(Clock clock);
    /** @brief Destroy CPU snapshots; backend must retire its GPU staging first. */
    ~FrameCaptureStore() override;
    FrameCaptureStore(const FrameCaptureStore&)            = delete;
    FrameCaptureStore& operator=(const FrameCaptureStore&) = delete;
    /** @brief Set backend support at provider initialization. */
    void configure(FrameCaptureCapabilities capabilities);
    /** @brief Inspect supported attachment classes and memory budget. */
    [[nodiscard]] FrameCaptureCapabilities capabilities() const override;
    /** @brief Reserve a request. @cost Selected attachments require per-pixel readback at recording. */
    [[nodiscard]] Result<std::string> request(FrameCaptureRequest request) override;
    /** @brief Return immutable owning metadata without GPU access. */
    [[nodiscard]] Result<FrameCaptureSnapshot> snapshot(std::string_view token) override;
    /** @brief Release a snapshot or cancel an in-flight request. */
    [[nodiscard]] Result<FrameCapturePhase> release(std::string_view token) override;
    /** @brief Revoke all tokens and increment the generation. */
    void invalidate() override;
    /** @brief Expire queued and ready entries; also called by tool operations. */
    void collect();
    /** @brief Find pending work, or an empty value when idle. */
    [[nodiscard]] std::optional<std::pair<std::string, FrameCaptureRequest>> queued();
    /** @brief Reserve CPU plus GPU storage before recording any commands. */
    [[nodiscard]] Result<void> begin(std::string_view token, uint64_t frameId, size_t bytes);
    /** @brief Mark a recorded batch submitted to the GPU. */
    [[nodiscard]] Result<void> submitted(std::string_view token);
    /** @brief Atomically publish all copied attachments; no partial ready result. */
    [[nodiscard]] Result<void> complete(std::string_view token, FrameCaptureSnapshot snapshot);
    /** @brief Publish a terminal diagnostic; retain reservation until retire(). */
    [[nodiscard]] Result<void> fail(std::string_view token, std::string reason);
    /** @brief Release staging reservations after GPU completion, including cancelled work. */
    void retire(std::string_view token);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve
