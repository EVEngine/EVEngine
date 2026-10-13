#include "combat/CombatMotionWarp.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "combat/CombatTarget.h"

// CombatMotionWarp.cpp
namespace eve::combat {
namespace {

bool finite3(CombatVector3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

double lengthXZ(CombatVector3 value) { return std::hypot(value.x, value.z); }

CombatVector3 normalizedXZ(CombatVector3 value) {
    const double magnitude = lengthXZ(value);
    if (magnitude <= 0.0) return {};
    return {value.x / magnitude, 0.0, value.z / magnitude};
}

}  // namespace

Result<void> CombatWarpRequest::validate() const {
    if (!finite3(attacker) || !finite3(target) || !finite3(attackerFacing))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "warp poses are invalid", "pose"));
    if (!std::isfinite(desiredDistance) || desiredDistance < 0.0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "desired warp distance is invalid", "desiredDistance"));
    if (!std::isfinite(maxTranslation) || maxTranslation < 0.0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "max warp translation is invalid", "maxTranslation"));
    if (!std::isfinite(remainingBudget) || remainingBudget < 0.0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "warp budget is invalid", "remainingBudget"));
    return Result<void>::success();
}

Result<CombatWarpResult> CombatMotionWarp::solve(const CombatWarpRequest& request) {
    auto valid = request.validate();
    if (!valid) return Result<CombatWarpResult>::failure(valid.status());

    CombatWarpResult result;
    result.facing = request.attackerFacing;
    CombatVector3 delta{request.target.x - request.attacker.x, 0.0, request.target.z - request.attacker.z};
    const double  distance = lengthXZ(delta);
    if (request.facing && distance > 0.0) result.facing = normalizedXZ(delta);
    if (!request.horizontal || distance <= 0.0)
        return Result<CombatWarpResult>::success(result, Status::success(StatusCode::NoOp));

    const double error = distance - request.desiredDistance;
    if (std::abs(error) <= 1e-6) return Result<CombatWarpResult>::success(result, Status::success(StatusCode::NoOp));

    CombatVector3 direction = normalizedXZ(delta);
    if (error < 0.0) direction = {-direction.x, 0.0, -direction.z};
    double step = std::min(std::abs(error), request.maxTranslation);
    if (step > request.remainingBudget) {
        result.budgetExceeded = true;
        return Result<CombatWarpResult>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "motion warp would exceed remaining budget", "budget"));
    }
    result.translation    = {direction.x * step, 0.0, direction.z * step};
    result.budgetConsumed = step;
    return Result<CombatWarpResult>::success(result, Status::success(StatusCode::Applied));
}

}  // namespace eve::combat

// CombatTarget.cpp
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
    if (!owner.isValid())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "owner is nil", "owner"));
    locks_.emplace(owner.format(), CombatLockState{owner, std::nullopt, false});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatTargetRuntime::unregisterOwner(SubjectRef owner) {
    if (!locks_.erase(owner.format()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatLockState> CombatTargetRuntime::softLock(SubjectRef                                owner,
                                                      const std::vector<CombatTargetCandidate>& candidates) {
    auto found = locks_.find(owner.format());
    if (found == locks_.end())
        return Result<CombatLockState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    const auto ranked      = sortedUnique(candidates, owner);
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
    found->second.target   = target;
    found->second.hardLock = true;
    return Result<CombatLockState>::success(found->second, Status::success(StatusCode::Applied));
}

Result<CombatLockState> CombatTargetRuntime::cycle(SubjectRef                                owner,
                                                   const std::vector<CombatTargetCandidate>& candidates) {
    auto found = locks_.find(owner.format());
    if (found == locks_.end())
        return Result<CombatLockState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lock owner was not found", owner.format()));
    const auto ranked      = sortedUnique(candidates, owner);
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
    if (!target.isValid())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "target is nil", "target"));
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
