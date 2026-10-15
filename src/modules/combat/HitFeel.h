#pragma once
#include "common/Export.h"

/** @file HitFeel.h @brief Deterministic hitstop and hitstun ownership for combat. */

#include "combat/Damage.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eve::combat {

/** @brief Authored freeze/stun durations applied after a damage outcome. */
struct HitFeelRequest {
    SubjectRef attacker;
    SubjectRef victim;
    Duration   attackerHitstop = Duration::zero();
    Duration   victimHitstop   = Duration::zero();
    Duration   victimHitstun   = Duration::zero();
    HitReaction reaction       = HitReaction::None;

    /** @brief Validate subjects and finite non-negative durations. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Per-subject freeze or stun remaining after the last commit. */
struct HitFeelState {
    SubjectRef subject;
    Duration   hitstopRemaining = Duration::zero();
    Duration   hitstunRemaining = Duration::zero();
    bool       timeFrozen       = false;
};

/** @brief Observable feel event after apply or advance. */
enum class HitFeelEventKind : std::uint8_t { HitstopStarted, HitstopEnded, HitstunStarted, HitstunEnded };

/** @brief Owning feel event. */
struct HitFeelEvent {
    SubjectRef       subject;
    HitFeelEventKind kind = HitFeelEventKind::HitstopStarted;
    SimulationTick   tick = SimulationTick::zero();
};

/** @brief Complete owning result of one feel advance. */
struct HitFeelAdvance {
    SimulationTick            tick = SimulationTick::zero();
    std::vector<HitFeelEvent> events;
};

/**
 * @brief Owner-thread hitstop/hitstun coordinator.
 *
 * Durations use injected simulation time only. While hitstop remains, callers
 * should treat the subject as time-frozen for animation and hitbox sampling.
 */
class EVENGINE_API_BACKENDS HitFeelRuntime {
public:
    /** @brief Apply one validated feel request; overlapping windows take the max remaining. */
    [[nodiscard]] Result<void> apply(HitFeelRequest request);
    /** @brief Derive a feel request from a damage outcome using fixed reaction tables. */
    [[nodiscard]] Result<void> applyFromOutcome(const DamageOutcome& outcome);
    /** @brief Advance remaining timers with the supplied deterministic step. */
    [[nodiscard]] Result<HitFeelAdvance> advance(const SimulationStep& step);
    /** @brief Return an owning snapshot, or empty defaults when the subject has no feel state. */
    [[nodiscard]] HitFeelState state(SubjectRef subject) const;
    /** @brief Whether the subject currently has positive hitstop remaining. */
    [[nodiscard]] bool isFrozen(SubjectRef subject) const noexcept;
    /** @brief Whether the subject currently has positive hitstun remaining. */
    [[nodiscard]] bool isStunned(SubjectRef subject) const noexcept;
    /** @brief Number of subjects with any remaining feel timer. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return states_.size(); }

private:
    std::map<std::string, HitFeelState, std::less<>> states_;
    SimulationTick                                   lastTick_ = SimulationTick::zero();
};

}  // namespace eve::combat
