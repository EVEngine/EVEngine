#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "particles/ParticleAppearanceValue.h"
namespace eve::particles {
/** @brief Snapshot creates or updates a birth; death closes it at the exact simulation boundary. */
enum class ParticleRecordEventKind { Snapshot, Death };
/** @brief Journal lifecycle; failures are sticky until an explicit new recording is prepared. */
enum class ParticleRecordState { Disabled, Recording, Exhausted, InvalidInput };
/** @brief Owning presentation event without simulation or resource references. */
struct ParticleRecordEvent {
    double                  seconds     = 0;
    std::uint64_t           birthSerial = 0;
    ParticleRecordEventKind kind        = ParticleRecordEventKind::Snapshot;
    ParticleAppearance      appearance;
};
/** @brief Bounded owner-thread particle recording buffer with explicit time and sticky errors.
 * @ownership Owns every event and its appearance; retains no input references or callbacks.
 * @thread Exclusive owner-thread access. Event appends never allocate after prepare.
 */
class EVENGINE_API_DOMAINS ParticleEventJournal {
public:
    /** @brief Start a new empty journal with a bounded event budget, replacing prior contents only on success.
     * @cost Linear in event capacity for reservation, once per recording outside simulation steps.
     * @return Success or invalid budget/allocation diagnostic. Maximum 2 million events.
     */
    [[nodiscard]] Result<void> prepare(std::size_t maximumEvents);
    /** @brief Append an owning event at a nondecreasing explicit time; never allocates or calls user code.
     * @return Success or sticky failure; disabled journals report NoOp. Errors never append a partial event.
     */
    [[nodiscard]] Result<void> append(ParticleRecordEvent event);
    /** @brief Finish valid recording and transfer all owning events, disabling further appends.
     * @cost Constant-time vector ownership transfer per completed recording; no event copy or allocation.
     * @return Complete owned buffer or diagnostic; failed/disabled journals cannot publish and remain unchanged.
     * @remarks Producer must close live births before this call; trajectory conversion validates event completeness.
     */
    [[nodiscard]] Result<std::vector<ParticleRecordEvent>> finish();
    /** @brief Mark active recording invalid after a producer failure; disabled journals remain disabled. */
    void invalidate() noexcept;
    /** @brief Disable valid recording, preserving sticky failures and owned events for inspection. */
    void disable() noexcept;
    /** @brief Current lifecycle and failure state. */
    [[nodiscard]] ParticleRecordState state() const noexcept;
    /** @brief Borrow recorded events; valid until prepare, append or journal destruction.
     * @ownership Borrowed view only; copy before modifying the journal or transferring across threads.
     */
    [[nodiscard]] std::span<const ParticleRecordEvent> events() const noexcept;

private:
    std::vector<ParticleRecordEvent> events_;
    std::size_t                      limit_ = 0;
    ParticleRecordState              state_ = ParticleRecordState::Disabled;
};
}  // namespace eve::particles
