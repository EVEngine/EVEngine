#include "combat/CombatCharacter.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::combat {
namespace {

bool finite2(double x, double z) { return std::isfinite(x) && std::isfinite(z); }

bool finite3(CombatVector3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

double lengthXZ(CombatVector3 value) { return std::hypot(value.x, value.z); }

CombatVector3 normalizedXZ(CombatVector3 value) {
    const double magnitude = lengthXZ(value);
    if (magnitude <= 0.0) return {};
    return {value.x / magnitude, 0.0, value.z / magnitude};
}

CombatVector3 scaleXZ(CombatVector3 value, double scale) { return {value.x * scale, 0.0, value.z * scale}; }

Result<void> invalid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

}  // namespace

Result<void> CombatCharacterDefinition::validate() const {
    if (!subject.isValid()) return invalid("subject is nil", "subject");
    if (ownerId.empty()) return invalid("owner id is empty", "ownerId");
    if (!finite3(initialPosition) || !std::isfinite(maximumSpeed) || maximumSpeed <= 0.0 ||
        !std::isfinite(acceleration) || acceleration <= 0.0 || !std::isfinite(jumpSpeed) || jumpSpeed < 0.0 ||
        !std::isfinite(gravity) || gravity < 0.0 || !std::isfinite(dodgeSpeed) || dodgeSpeed <= 0.0 ||
        !std::isfinite(dodgeDuration) || dodgeDuration <= 0.0 || !std::isfinite(capsuleRadius) || capsuleRadius <= 0.0)
        return invalid("character movement limits are invalid", "movement");
    return Result<void>::success();
}

Result<void> CombatCharacterRuntime::registerSubject(CombatCharacterDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return valid;
    const std::string key = definition.subject.format();
    if (states_.contains(key))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists, "character subject already exists", key));
    CombatCharacterState state;
    state.subject       = definition.subject;
    state.ownerId       = std::move(definition.ownerId);
    state.position      = definition.initialPosition;
    state.facing        = {1.0, 0.0, 0.0};
    state.maximumSpeed  = definition.maximumSpeed;
    state.acceleration  = definition.acceleration;
    state.jumpSpeed     = definition.jumpSpeed;
    state.gravity       = definition.gravity;
    state.dodgeSpeed    = definition.dodgeSpeed;
    state.dodgeDuration = definition.dodgeDuration;
    state.capsuleRadius = definition.capsuleRadius;
    state.maxAirJumps   = definition.maxAirJumps;
    state.mode          = CombatCharacterMode::Grounded;
    states_.emplace(key, std::move(state));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::unregisterSubject(SubjectRef subject) {
    if (!subject.isValid()) return invalid("subject is nil", "subject");
    if (!states_.erase(subject.format()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatCharacterState> CombatCharacterRuntime::state(SubjectRef subject) const {
    const auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<CombatCharacterState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    return Result<CombatCharacterState>::success(found->second);
}

Result<void> CombatCharacterRuntime::setMoveIntent(SubjectRef subject, CombatVector3 direction, double speedFraction) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    if (found->second.mode == CombatCharacterMode::Dead || found->second.mode == CombatCharacterMode::Stunned)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    if (!finite2(direction.x, direction.z) || !std::isfinite(speedFraction) || speedFraction < 0.0 ||
        speedFraction > 1.0)
        return invalid("move intent is invalid", "moveIntent");
    found->second.moveDirection     = normalizedXZ(direction);
    found->second.moveSpeedFraction = speedFraction;
    if (lengthXZ(found->second.moveDirection) > 0.0) found->second.facing = found->second.moveDirection;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::jump(SubjectRef subject) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    auto& state = found->second;
    if (state.mode == CombatCharacterMode::Dead || state.mode == CombatCharacterMode::Stunned ||
        state.mode == CombatCharacterMode::Dodging)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    if (state.mode == CombatCharacterMode::Grounded) {
        state.velocity.y   = state.jumpSpeed;
        state.mode         = CombatCharacterMode::Airborne;
        state.airJumpsUsed = 0;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    if (state.mode == CombatCharacterMode::Airborne || state.mode == CombatCharacterMode::Attacking) {
        if (state.airJumpsUsed >= state.maxAirJumps) return Result<void>::success(Status::success(StatusCode::NoOp));
        state.velocity.y = state.jumpSpeed;
        state.airJumpsUsed += 1;
        if (state.mode == CombatCharacterMode::Attacking) state.mode = CombatCharacterMode::Airborne;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    return Result<void>::success(Status::success(StatusCode::NoOp));
}

Result<void> CombatCharacterRuntime::dodge(SubjectRef subject, std::optional<CombatVector3> direction) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    auto& state = found->second;
    if (state.mode == CombatCharacterMode::Dead || state.mode == CombatCharacterMode::Stunned ||
        state.mode == CombatCharacterMode::Dodging)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    CombatVector3 dodgeDir = state.facing;
    if (direction) {
        if (!finite2(direction->x, direction->z)) return invalid("dodge direction is invalid", "direction");
        dodgeDir = normalizedXZ(*direction);
        if (lengthXZ(dodgeDir) <= 0.0) dodgeDir = state.facing;
    }
    state.facing            = dodgeDir;
    state.velocity          = {dodgeDir.x * state.dodgeSpeed, 0.0, dodgeDir.z * state.dodgeSpeed};
    state.mode              = CombatCharacterMode::Dodging;
    state.modeTimeRemaining = state.dodgeDuration;
    state.invulnerable      = true;
    state.rootMotionDelta.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::dodgeRelative(SubjectRef subject, CombatVector3 lockTarget,
                                                   CombatDodgeRelative relative) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    if (!finite3(lockTarget)) return invalid("lock target is invalid", "lockTarget");
    CombatVector3 toTarget =
        normalizedXZ({lockTarget.x - found->second.position.x, 0.0, lockTarget.z - found->second.position.z});
    if (lengthXZ(toTarget) <= 0.0) toTarget = found->second.facing;
    CombatVector3 direction = toTarget;
    switch (relative) {
        case CombatDodgeRelative::Forward: direction = toTarget; break;
        case CombatDodgeRelative::Back: direction = {-toTarget.x, 0.0, -toTarget.z}; break;
        case CombatDodgeRelative::Left: direction = {toTarget.z, 0.0, -toTarget.x}; break;
        case CombatDodgeRelative::Right: direction = {-toTarget.z, 0.0, toTarget.x}; break;
    }
    return dodge(subject, direction);
}

Result<void> CombatCharacterRuntime::beginAttack(SubjectRef subject) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    if (found->second.mode == CombatCharacterMode::Dead || found->second.mode == CombatCharacterMode::Stunned ||
        found->second.mode == CombatCharacterMode::Dodging)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    found->second.mode              = CombatCharacterMode::Attacking;
    found->second.modeTimeRemaining = 0.0;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::endAttack(SubjectRef subject) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    if (found->second.mode != CombatCharacterMode::Attacking)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    found->second.mode = found->second.position.y > 0.0 ? CombatCharacterMode::Airborne : CombatCharacterMode::Grounded;
    found->second.rootMotionDelta.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::applyStun(SubjectRef subject, Duration duration) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    if (duration < Duration::zero()) return invalid("stun duration is invalid", "duration");
    if (found->second.mode == CombatCharacterMode::Dead)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    found->second.mode              = CombatCharacterMode::Stunned;
    found->second.modeTimeRemaining = duration.seconds();
    found->second.invulnerable      = false;
    found->second.moveSpeedFraction = 0.0;
    found->second.moveDirection     = {};
    found->second.rootMotionDelta.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::applyImpulse(SubjectRef subject, CombatVector3 impulse) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    if (!finite3(impulse)) return invalid("impulse is invalid", "impulse");
    if (found->second.mode == CombatCharacterMode::Dead)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    found->second.velocity.x += impulse.x;
    found->second.velocity.y += impulse.y;
    found->second.velocity.z += impulse.z;
    if (impulse.y > 0.0 && found->second.mode == CombatCharacterMode::Grounded)
        found->second.mode = CombatCharacterMode::Airborne;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::applyDamageReaction(SubjectRef subject, HitReaction reaction,
                                                         Duration stunDuration, Impulse3 knockback) {
    if (reaction == HitReaction::Death) return kill(subject);
    if (reaction == HitReaction::Flinch || reaction == HitReaction::Stagger || reaction == HitReaction::Knockdown) {
        auto stunned = applyStun(subject, stunDuration);
        if (!stunned) return stunned;
    }
    if (knockback.x != 0.0 || knockback.y != 0.0 || knockback.z != 0.0)
        return applyImpulse(subject, {knockback.x, knockback.y, knockback.z});
    return Result<void>::success(Status::success(StatusCode::NoOp));
}

Result<void> CombatCharacterRuntime::kill(SubjectRef subject) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    found->second.mode              = CombatCharacterMode::Dead;
    found->second.modeTimeRemaining = 0.0;
    found->second.invulnerable      = false;
    found->second.velocity          = {};
    found->second.moveSpeedFraction = 0.0;
    found->second.rootMotionDelta.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::setRootMotionDelta(SubjectRef subject, CombatVector3 delta) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    if (!finite3(delta)) return invalid("root motion delta is invalid", "delta");
    found->second.rootMotionDelta = delta;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::setFacing(SubjectRef subject, CombatVector3 facing) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    if (!finite2(facing.x, facing.z)) return invalid("facing is invalid", "facing");
    CombatVector3 planar = normalizedXZ(facing);
    if (lengthXZ(planar) <= 0.0) return invalid("facing is degenerate", "facing");
    found->second.facing = planar;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatCharacterRuntime::setTimeFrozen(SubjectRef subject, bool frozen) {
    auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character subject was not found", subject.format()));
    found->second.timeFrozen = frozen;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatCharacterAdvance> CombatCharacterRuntime::advance(const SimulationStep& step) {
    if (step.tick.value() < lastTick_.value())
        return Result<CombatCharacterAdvance>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "character tick must be non-decreasing", "tick"));
    if (step.delta < Duration::zero())
        return Result<CombatCharacterAdvance>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "character delta is invalid", "delta"));

    const double           dt = step.delta.seconds();
    CombatCharacterAdvance advance;
    advance.tick                                                       = step.tick;
    std::map<std::string, CombatCharacterState, std::less<>> candidate = states_;

    for (auto& [key, state] : candidate) {
        (void)key;
        const CombatCharacterMode previousMode     = state.mode;
        const CombatVector3       previousPosition = state.position;

        if (state.mode == CombatCharacterMode::Dead || state.timeFrozen) continue;

        double ground = 0.0;
        if (ground_) {
            auto sampled = ground_->sampleHeight(state.position.x, state.position.z);
            if (!sampled) return Result<CombatCharacterAdvance>::failure(sampled.status());
            ground = sampled.value();
            if (!std::isfinite(ground))
                return Result<CombatCharacterAdvance>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "ground height is invalid", "ground"));
        }

        if (state.mode == CombatCharacterMode::Dodging || state.mode == CombatCharacterMode::Stunned) {
            state.modeTimeRemaining = std::max(0.0, state.modeTimeRemaining - dt);
            if (state.modeTimeRemaining <= 0.0) {
                if (state.mode == CombatCharacterMode::Dodging) {
                    advance.events.push_back(
                        {state.subject, CombatCharacterEventKind::DodgeEnded, state.mode, state.position, step.tick});
                    state.invulnerable = false;
                }
                state.mode = state.position.y > ground ? CombatCharacterMode::Airborne : CombatCharacterMode::Grounded;
            }
        }

        CombatVector3 unconstrained = state.position;
        if (state.mode == CombatCharacterMode::Dodging) {
            unconstrained.x += state.velocity.x * dt;
            unconstrained.z += state.velocity.z * dt;
        } else if (state.mode == CombatCharacterMode::Attacking && state.rootMotionDelta) {
            unconstrained = {state.position.x + state.rootMotionDelta->x, state.position.y + state.rootMotionDelta->y,
                             state.position.z + state.rootMotionDelta->z};
            state.rootMotionDelta.reset();
        } else if (state.mode == CombatCharacterMode::Stunned) {
            unconstrained.x += state.velocity.x * dt;
            unconstrained.z += state.velocity.z * dt;
        } else {
            const double  targetSpeed = state.maximumSpeed * state.moveSpeedFraction;
            CombatVector3 desired     = {state.moveDirection.x * targetSpeed, 0.0, state.moveDirection.z * targetSpeed};
            CombatVector3 planar      = {state.velocity.x, 0.0, state.velocity.z};
            CombatVector3 deltaV      = {desired.x - planar.x, 0.0, desired.z - planar.z};
            const double  deltaMag    = lengthXZ(deltaV);
            const double  maxDelta    = state.acceleration * dt;
            if (deltaMag > maxDelta && deltaMag > 0.0) deltaV = scaleXZ(deltaV, maxDelta / deltaMag);
            state.velocity.x += deltaV.x;
            state.velocity.z += deltaV.z;
            unconstrained.x += state.velocity.x * dt;
            unconstrained.z += state.velocity.z * dt;
        }

        if (probe_) {
            auto resolved = probe_->resolve(previousPosition, unconstrained, state.capsuleRadius);
            if (!resolved) return Result<CombatCharacterAdvance>::failure(resolved.status());
            unconstrained = resolved.value();
        }
        state.position = unconstrained;

        if (ground_) {
            auto sampled = ground_->sampleHeight(state.position.x, state.position.z);
            if (!sampled) return Result<CombatCharacterAdvance>::failure(sampled.status());
            ground = sampled.value();
            if (!std::isfinite(ground))
                return Result<CombatCharacterAdvance>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "ground height is invalid", "ground"));
        }

        if (state.mode == CombatCharacterMode::Airborne || state.mode == CombatCharacterMode::Attacking ||
            state.mode == CombatCharacterMode::Stunned ||
            (state.mode == CombatCharacterMode::Grounded && state.position.y > ground)) {
            state.velocity.y -= state.gravity * dt;
            state.position.y += state.velocity.y * dt;
            if (state.position.y <= ground) {
                state.position.y   = ground;
                state.velocity.y   = 0.0;
                state.airJumpsUsed = 0;
                if (state.mode == CombatCharacterMode::Airborne) {
                    state.mode = CombatCharacterMode::Grounded;
                    advance.events.push_back(
                        {state.subject, CombatCharacterEventKind::Landed, state.mode, state.position, step.tick});
                }
            } else if (state.mode == CombatCharacterMode::Grounded) {
                state.mode = CombatCharacterMode::Airborne;
            }
        }

        if (previousMode != state.mode)
            advance.events.push_back(
                {state.subject, CombatCharacterEventKind::ModeChanged, state.mode, state.position, step.tick});
        if (previousPosition.x != state.position.x || previousPosition.y != state.position.y ||
            previousPosition.z != state.position.z)
            advance.events.push_back(
                {state.subject, CombatCharacterEventKind::Moved, state.mode, state.position, step.tick});
    }

    states_   = std::move(candidate);
    lastTick_ = step.tick;
    return Result<CombatCharacterAdvance>::success(std::move(advance), Status::success(StatusCode::Applied));
}

std::vector<CombatCharacterState> CombatCharacterRuntime::states() const {
    std::vector<CombatCharacterState> result;
    result.reserve(states_.size());
    for (const auto& [key, state] : states_) {
        (void)key;
        result.push_back(state);
    }
    return result;
}

}  // namespace eve::combat
