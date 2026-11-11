#pragma once
/** @file RTSFrameRestore.h
 * @brief rts module-interface: independent event queues and publication watermarks prepared for restore.
 * @remarks No persistent schema, ECS mutations, callbacks, scripts or tools. Simulation clocks remain external.
 */
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::rts {
class RTS;
struct RTSFrameState;
/** @brief Owning native queue candidate, independent of source and destination lifetimes.
 * @thread Prepare and publish on owner thread outside world operations and callbacks.
 */
class EVENGINE_API_DOMAINS PreparedRTSFrameRestore final {
public:
    /** @brief Release unpublished queues. */
    ~PreparedRTSFrameRestore();
    /** @brief Transfer candidate. */
    PreparedRTSFrameRestore(PreparedRTSFrameRestore&&) noexcept;
    /** @brief Replace candidate. */
    PreparedRTSFrameRestore& operator=(PreparedRTSFrameRestore&&) noexcept;
    PreparedRTSFrameRestore(const PreparedRTSFrameRestore&)            = delete;
    PreparedRTSFrameRestore& operator=(const PreparedRTSFrameRestore&) = delete;
    /** @brief Validate and copy exact queue order, shared frame sequence and gameplay next sequence.
     * @return Candidate or duplicate/out-of-order sequences and invalid watermarks, without altering source.
     * @remarks Historical SubjectRefs may be retired; preparation does not resolve or replay them. Coordinator
     * joins roots, clocks, actions and consumer acknowledgements separately at the same checkpoint boundary.
     * @cost Copies all retained event payloads once per restore attempt; validation merges three frame streams.
     * @throws std::bad_alloc Preparation failed without publication.
     */
    [[nodiscard]] static Result<PreparedRTSFrameRestore> prepare(const RTSFrameState& captured);

private:
    friend class RTS;
    struct Impl;
    explicit PreparedRTSFrameRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
