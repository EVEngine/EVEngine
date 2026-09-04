#pragma once

#include "MeshVfxRenderBatch.h"

#include <cstdint>

namespace eve::stylize {

/** @brief Result of one synchronous backend submission operation. */
enum class MeshVfxSubmitStatus : std::uint8_t {
    Accepted,
    Skipped,
    Failed,
    DeviceUnavailable,
};

/** @brief Aggregate completion state for one render queue submission. */
enum class MeshVfxQueueSubmitStatus : std::uint8_t {
    Complete,
    Partial,
    Failed,
    DeviceUnavailable,
};

/** @brief Counters and terminal state produced by queue execution. */
struct MeshVfxSubmissionReport {
    MeshVfxQueueSubmitStatus status = MeshVfxQueueSubmitStatus::Complete;
    std::uint32_t acceptedBatches = 0;
    std::uint32_t skippedBatches = 0;
    std::uint32_t acceptedDraws = 0;
    std::uint32_t skippedDraws = 0;
    std::uint32_t failedDraws = 0;
};

/**
 * @brief Immediate graphics-backend contract for a mesh VFX render queue.
 *
 * Calls occur synchronously on the caller's render thread. Implementations must
 * not retain references to keys or draws after a method returns. No unknown
 * callback is invoked while the executor holds engine state or a lock.
 */
class IMeshVfxBatchSink {
public:
    virtual ~IMeshVfxBatchSink() = default;

    /**
     * @brief Begins a consecutive group sharing immutable GPU state.
     * @param key Pipeline, material, mesh, pass and blend identity.
     * @return Accepted, Skipped, or DeviceUnavailable.
     */
    virtual MeshVfxSubmitStatus beginBatch(const MeshVfxBatchKey& key) = 0;

    /**
     * @brief Submits one ordered instance in the active batch.
     * @param draw Stable instance and LOD upload decision.
     * @return Accepted, Skipped, or DeviceUnavailable.
     */
    virtual MeshVfxSubmitStatus submitDraw(const MeshVfxBatchedDraw& draw) = 0;

    /**
     * @brief Ends the active batch and flushes backend-local state if needed.
     * @return Accepted, Skipped, or DeviceUnavailable.
     */
    virtual MeshVfxSubmitStatus endBatch() = 0;
};

/** @brief Executes a planned render queue against one immediate backend sink. */
class MeshVfxBatchExecutor {
public:
    /**
     * @brief Submits all batches in order until completion or device loss.
     * @param queue Immutable queue produced for the current frame.
     * @param sink Render-thread backend implementation; never retained.
     * @return Detailed completion status and counters.
     */
    [[nodiscard]] MeshVfxSubmissionReport submit(const MeshVfxRenderQueue& queue, IMeshVfxBatchSink& sink) const;
};

} // namespace eve::stylize
