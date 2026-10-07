#include "combat/CombatTarget.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::combat {
namespace {

std::vector<CombatTargetCandidate> sortedUnique(std::vector<CombatTargetCandidate> candidates, SubjectRef owner) {
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [&](const CombatTargetCandidate& candidate) {
                                        return !candidate.subject.isValid() || candidate.subject == owner ||
                                               !std::isfinite(candidate.score);
                                    }),
                     candidates.end());
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const CombatTargetCandidate& a, const CombatTargetCandidate& b) {
                         if (a.score != b.score) return a.score > b.score;
                         return a.subject.format() < b.subject.format();
                     });
    return candidates;
}

}  // namespace

Result<void> CombatTargetRuntime::registerOwner(SubjectRef owner) {
    if (!owner.isValid()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("owner is nil"), std::move("owner")));
    locks_.emplace(owner.format(), CombatLockState{owner, std::nullopt, false});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatTargetRuntime::unregisterOwner(SubjectRef owner) {
    if (!locks_.erase(owner.format()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatLockState> CombatTargetRuntime::softLock(SubjectRef owner,
                                                      const std::vector<CombatTargetCandidate>& candidates) {
    auto found = locks_.find(owner.format());
    if (found == locks_.end())
        return Result<CombatLockState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    const auto ranked = sortedUnique(candidates, owner);
    found->second.hardLock = false;
    if (ranked.empty()) {
        found->second.target.reset();
    } else {
        found->second.target = ranked.front().subject;
    }
    return Result<CombatLockState>::success(found->second, Status::success(StatusCode::Applied));
}

Result<CombatLockState> CombatTargetRuntime::hardLock(SubjectRef owner, SubjectRef target) {
    auto found = locks_.find(owner.format());
    if (found == locks_.end())
        return Result<CombatLockState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    if (!target.isValid() || target == owner)
        return Result<CombatLockState>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "hard-lock target is invalid", "target"));
    found->second.target = target;
    found->second.hardLock = true;
    return Result<CombatLockState>::success(found->second, Status::success(StatusCode::Applied));
}

Result<CombatLockState> CombatTargetRuntime::cycle(SubjectRef owner,
                                                   const std::vector<CombatTargetCandidate>& candidates) {
    auto found = locks_.find(owner.format());
    if (found == locks_.end())
        return Result<CombatLockState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    const auto ranked = sortedUnique(candidates, owner);
    found->second.hardLock = false;
    if (ranked.empty()) {
        found->second.target.reset();
        return Result<CombatLockState>::success(found->second, Status::success(StatusCode::Applied));
    }
    std::size_t index = 0;
    if (found->second.target) {
        for (std::size_t i = 0; i < ranked.size(); ++i) {
            if (ranked[i].subject == *found->second.target) {
                index = (i + 1) % ranked.size();
                break;
            }
        }
    }
    found->second.target = ranked[index].subject;
    return Result<CombatLockState>::success(found->second, Status::success(StatusCode::Applied));
}

Result<CombatLockState> CombatTargetRuntime::clear(SubjectRef owner) {
    auto found = locks_.find(owner.format());
    if (found == locks_.end())
        return Result<CombatLockState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    found->second.target.reset();
    found->second.hardLock = false;
    return Result<CombatLockState>::success(found->second, Status::success(StatusCode::Applied));
}

Result<void> CombatTargetRuntime::forgetTarget(SubjectRef target) {
    if (!target.isValid()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("target is nil"), std::move("target")));
    for (auto& [key, lock] : locks_) {
        (void)key;
        if (lock.target && *lock.target == target) {
            lock.target.reset();
            lock.hardLock = false;
        }
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatLockState> CombatTargetRuntime::state(SubjectRef owner) const {
    const auto found = locks_.find(owner.format());
    if (found == locks_.end())
        return Result<CombatLockState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    return Result<CombatLockState>::success(found->second);
}

}  // namespace eve::combat
