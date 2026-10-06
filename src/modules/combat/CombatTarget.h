#pragma once
#include "common/Export.h"

/** @file CombatTarget.h @brief Soft/hard lock-on ownership for action combat. */

#include "common/Result.h"
#include "common/SubjectRef.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eve::combat {

/** @brief Candidate scored by an external sensing/query adapter. */
struct CombatTargetCandidate {
    SubjectRef subject;
    double     score = 0.0;
    double     x     = 0.0;
    double     y     = 0.0;
    double     z     = 0.0;
};

/** @brief Owning lock-on state for one fighter. */
struct CombatLockState {
    SubjectRef                owner;
    std::optional<SubjectRef> target;
    bool                      hardLock = false;
};

/**
 * @brief Owner-thread lock-on authority.
 *
 * Sensing ranks candidates; this runtime uniquely owns the current primary
 * target and hard-lock flag. Missing or dead targets clear observably.
 */
class EVENGINE_API_BACKENDS CombatTargetRuntime {
public:
    /** @brief Ensure an owner exists with no target. */
    [[nodiscard]] Result<void> registerOwner(SubjectRef owner);
    /** @brief Remove one owner and its lock. */
    [[nodiscard]] Result<void> unregisterOwner(SubjectRef owner);
    /** @brief Soft-select the highest-score candidate that is not the owner. */
    [[nodiscard]] Result<CombatLockState> softLock(SubjectRef owner,
                                                   const std::vector<CombatTargetCandidate>& candidates);
    /** @brief Hard-lock onto an explicit target; rejects nil or self. */
    [[nodiscard]] Result<CombatLockState> hardLock(SubjectRef owner, SubjectRef target);
    /** @brief Cycle soft-lock to the next candidate after the current target. */
    [[nodiscard]] Result<CombatLockState> cycle(SubjectRef owner,
                                                const std::vector<CombatTargetCandidate>& candidates);
    /** @brief Clear the current target while keeping the owner registered. */
    [[nodiscard]] Result<CombatLockState> clear(SubjectRef owner);
    /** @brief Forget a subject wherever it appears as a target. */
    [[nodiscard]] Result<void> forgetTarget(SubjectRef target);
    /** @brief Return owning lock state or NotFound. */
    [[nodiscard]] Result<CombatLockState> state(SubjectRef owner) const;

private:
    std::map<std::string, CombatLockState, std::less<>> locks_;
};

}  // namespace eve::combat
