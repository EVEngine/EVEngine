#include "combat/CombatCharacterPose.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include "combat/ActionWindowState.h"
#include "combat/CombatEnemyAI.h"
#include "combat/CombatLocomotion.h"
#include "combat/CombatLoop.h"
#include "combat/CombatMotionWarp.h"

// CombatCharacterPose.cpp
namespace eve::combat {

Result<MeleePose> CombatCharacterPoseSource::pose(SubjectRef subject, std::string_view) const {
    if (!characters_)
        return Result<MeleePose>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character pose source has no runtime", "characters"));
    auto state = characters_->state(subject);
    if (!state) return Result<MeleePose>::failure(state.status());
    MeleePose pose;
    pose.position   = {state.value().position.x, state.value().position.y + chestHeight_, state.value().position.z};
    pose.yawRadians = std::atan2(-state.value().facing.x, state.value().facing.z);
    return Result<MeleePose>::success(pose);
}

}  // namespace eve::combat

// CombatEnemyAI.cpp
namespace eve::combat {
namespace {

double distance3(double ax, double ay, double az, double bx, double by, double bz) {
    const double dx = ax - bx;
    const double dy = ay - by;
    const double dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

Result<void> CombatEnemyDefinition::validate() const {
    if (!subject.isValid())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "enemy subject is nil", "subject"));
    if (ownerId.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "enemy owner id is empty", "ownerId"));
    if (!std::isfinite(nearRadius) || nearRadius <= 0.0 || !std::isfinite(midRadius) || midRadius < nearRadius)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "enemy radii are invalid", "radius"));
    if (attackCooldownTicks == 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "enemy attack cooldown must be positive", "attackCooldownTicks"));
    return Result<void>::success();
}

Result<void> CombatEnemyIntentSource::registerEnemy(CombatEnemyDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return valid;
    EnemyState state;
    state.definition                            = std::move(definition);
    enemies_[state.definition.subject.format()] = std::move(state);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatEnemyIntentSource::unregisterEnemy(SubjectRef subject) {
    if (!enemies_.erase(subject.format()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "enemy was not found", subject.format()));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatEnemyIntentSource::setPosition(SubjectRef subject, double x, double y, double z) {
    if (!subject.isValid())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "subject is nil", "subject"));
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "pose is invalid", "pose"));
    poses_[subject.format()] = Pose{x, y, z};
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatEnemyIntentSource::setLightAction(SubjectRef subject, LogicalId actionId) {
    auto found = enemies_.find(subject.format());
    if (found == enemies_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "enemy was not found", subject.format()));
    if (actionId.format().empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "light action id is empty", "actionId"));
    found->second.lightAction = std::move(actionId);
    found->second.hasAction   = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::optional<action::AbilityIntent>> CombatEnemyIntentSource::nextIntent(SimulationTick tick) {
    if (!targets_)
        return Result<std::optional<action::AbilityIntent>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "enemy target runtime is unavailable", "targets"));

    std::optional<action::AbilityIntent> emitted;
    for (auto& [key, enemy] : enemies_) {
        (void)key;
        if (enemy.phase == CombatEnemyPhase::Recover &&
            tick.value() >= enemy.phaseStartTick + enemy.definition.recoverTicks)
            enemy.phase = CombatEnemyPhase::Idle;
        if (emitted) continue;
        if (!enemy.hasAction) continue;
        if (enemy.definition.lightGrant.isZero()) continue;

        const auto emitAttack = [&]() {
            action::AbilityIntent intent;
            intent.grantId          = enemy.definition.lightGrant;
            intent.request.actionId = enemy.lightAction;
            enemy.lastAttackTick    = tick.value();
            enemy.phase = enemy.definition.recoverTicks > 0 ? CombatEnemyPhase::Recover : CombatEnemyPhase::Idle;
            enemy.phaseStartTick = tick.value();
            emitted              = std::move(intent);
        };

        if (enemy.phase == CombatEnemyPhase::Telegraph) {
            if (tick.value() >= enemy.phaseStartTick + enemy.definition.telegraphTicks) emitAttack();
            continue;
        }
        if (enemy.phase == CombatEnemyPhase::Recover) continue;

        const auto selfPose = poses_.find(enemy.definition.subject.format());
        if (selfPose == poses_.end()) continue;
        auto lock = targets_->state(enemy.definition.subject);
        if (!lock || !lock.value().target) continue;
        const auto targetPose = poses_.find(lock.value().target->format());
        if (targetPose == poses_.end()) continue;
        const double dist = distance3(selfPose->second.x, selfPose->second.y, selfPose->second.z, targetPose->second.x,
                                      targetPose->second.y, targetPose->second.z);
        if (dist > enemy.definition.nearRadius) continue;
        if (enemy.lastAttackTick != 0 && tick.value() < enemy.lastAttackTick + enemy.definition.attackCooldownTicks)
            continue;

        if (enemy.definition.telegraphTicks > 0) {
            enemy.phase          = CombatEnemyPhase::Telegraph;
            enemy.phaseStartTick = tick.value();
            continue;
        }
        emitAttack();
    }
    return Result<std::optional<action::AbilityIntent>>::success(std::move(emitted));
}

Result<std::vector<CombatEnemySteering>> CombatEnemyIntentSource::nextSteering(SimulationTick) const {
    if (!targets_)
        return Result<std::vector<CombatEnemySteering>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "enemy target runtime is unavailable", "targets"));

    std::vector<CombatEnemySteering> result;
    for (const auto& [key, enemy] : enemies_) {
        (void)key;
        if (enemy.phase != CombatEnemyPhase::Idle) continue;
        const auto selfPose = poses_.find(enemy.definition.subject.format());
        if (selfPose == poses_.end()) continue;
        auto lock = targets_->state(enemy.definition.subject);
        if (!lock || !lock.value().target) continue;
        const auto targetPose = poses_.find(lock.value().target->format());
        if (targetPose == poses_.end()) continue;
        const double dist = distance3(selfPose->second.x, selfPose->second.y, selfPose->second.z, targetPose->second.x,
                                      targetPose->second.y, targetPose->second.z);
        if (dist <= enemy.definition.nearRadius) continue;

        CombatVector3 delta{targetPose->second.x - selfPose->second.x, 0.0, targetPose->second.z - selfPose->second.z};
        const double  magnitude = std::hypot(delta.x, delta.z);
        if (magnitude <= 1e-6) continue;
        CombatEnemySteering steering;
        steering.subject       = enemy.definition.subject;
        steering.moveDirection = {delta.x / magnitude, 0.0, delta.z / magnitude};
        steering.speedFraction = dist <= enemy.definition.midRadius ? 0.7 : 1.0;
        result.push_back(steering);
    }
    return Result<std::vector<CombatEnemySteering>>::success(std::move(result), Status::success(StatusCode::Applied));
}

CombatEnemyBand CombatEnemyIntentSource::band(SubjectRef subject) const {
    const auto found = enemies_.find(subject.format());
    if (found == enemies_.end() || !targets_) return CombatEnemyBand::Far;
    const auto selfPose = poses_.find(subject.format());
    if (selfPose == poses_.end()) return CombatEnemyBand::Far;
    auto lock = targets_->state(subject);
    if (!lock || !lock.value().target) return CombatEnemyBand::Far;
    const auto targetPose = poses_.find(lock.value().target->format());
    if (targetPose == poses_.end()) return CombatEnemyBand::Far;
    const double dist = distance3(selfPose->second.x, selfPose->second.y, selfPose->second.z, targetPose->second.x,
                                  targetPose->second.y, targetPose->second.z);
    if (dist <= found->second.definition.nearRadius) return CombatEnemyBand::Near;
    if (dist <= found->second.definition.midRadius) return CombatEnemyBand::Mid;
    return CombatEnemyBand::Far;
}

CombatEnemyPhase CombatEnemyIntentSource::phase(SubjectRef subject) const {
    const auto found = enemies_.find(subject.format());
    if (found == enemies_.end()) return CombatEnemyPhase::Idle;
    return found->second.phase;
}

bool CombatEnemyIntentSource::isPunishable(SubjectRef subject) const {
    const auto current = phase(subject);
    return current == CombatEnemyPhase::Telegraph || current == CombatEnemyPhase::Recover;
}

}  // namespace eve::combat

// CombatLocomotion.cpp
namespace eve::combat {
namespace {

bool finite(CombatVector2 value) { return std::isfinite(value.x) && std::isfinite(value.z); }

double length(CombatVector2 value) { return std::hypot(value.x, value.z); }

CombatVector2 normalized(CombatVector2 value) {
    const double magnitude = length(value);
    return magnitude > 0.0 ? CombatVector2{value.x / magnitude, value.z / magnitude} : CombatVector2{};
}

Result<void> validateGoal(const CombatNavigationGoal& goal) {
    if (!finite(goal.position) || !std::isfinite(goal.acceptanceRadius) || goal.acceptanceRadius < 0.0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "navigation goal is invalid", "goal"));
    return Result<void>::success();
}

Result<void> validateSteering(const CombatNavigationSteering& steering) {
    if ((steering.phase != CombatNavigationPhase::Moving && steering.phase != CombatNavigationPhase::Arrived) ||
        !finite(steering.direction) || !std::isfinite(steering.speedFraction) || steering.speedFraction < 0.0 ||
        steering.speedFraction > 1.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "navigation provider returned invalid steering", "steering"));
    return Result<void>::success();
}

}  // namespace

Result<void> CombatLocomotionDefinition::validate() const {
    if (!subject.isValid())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "subject is nil", "subject"));
    if (ownerId.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "owner id is empty", "ownerId"));
    if (!finite(initialPosition) || !std::isfinite(maximumSpeed) || maximumSpeed <= 0.0 ||
        !std::isfinite(acceleration) || acceleration <= 0.0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "locomotion limits are invalid", "movement"));
    return Result<void>::success();
}

Result<CombatNavigationSteering> DirectCombatNavigationProvider::steer(const CombatLocomotionState& state,
                                                                       const CombatNavigationGoal&  goal,
                                                                       SimulationTick               tick) {
    (void)tick;
    auto valid = validateGoal(goal);
    if (!valid) return Result<CombatNavigationSteering>::failure(valid.status());
    const CombatVector2 delta{goal.position.x - state.position.x, goal.position.z - state.position.z};
    const double        distance = length(delta);
    if (distance <= goal.acceptanceRadius)
        return Result<CombatNavigationSteering>::success({CombatNavigationPhase::Arrived, {}, 0.0});
    return Result<CombatNavigationSteering>::success({CombatNavigationPhase::Moving, normalized(delta), 1.0});
}

Result<void> CombatLocomotionRuntime::registerSubject(CombatLocomotionDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return valid;
    const std::string key = definition.subject.format();
    if (states_.contains(key))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists, "locomotion subject already exists", key));
    states_.emplace(key, CombatLocomotionState{definition.subject,
                                               std::move(definition.ownerId),
                                               definition.initialPosition,
                                               {},
                                               {1.0, 0.0},
                                               {},
                                               0.0,
                                               definition.maximumSpeed,
                                               definition.acceleration,
                                               std::nullopt});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatLocomotionRuntime::unregisterSubject(SubjectRef subject) {
    if (!subject.isValid())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "subject is nil", "subject"));
    if (states_.erase(subject.format()) == 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format()));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatLocomotionState> CombatLocomotionRuntime::state(SubjectRef subject) const {
    if (!subject.isValid())
        return Result<CombatLocomotionState>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "subject is nil", "subject"));
    const auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<CombatLocomotionState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format()));
    return Result<CombatLocomotionState>::success(found->second);
}

Result<void> CombatLocomotionRuntime::setMoveIntent(SubjectRef subject, CombatVector2 direction, double speedFraction) {
    if (!subject.isValid() || !finite(direction) || !std::isfinite(speedFraction) || speedFraction < 0.0 ||
        speedFraction > 1.0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "movement intent is invalid", "intent"));
    const auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format()));
    found->second.moveDirection     = normalized(direction);
    found->second.moveSpeedFraction = length(direction) > 0.0 ? speedFraction : 0.0;
    found->second.navigationGoal.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatLocomotionRuntime::navigateTo(SubjectRef subject, CombatNavigationGoal goal) {
    if (!subject.isValid())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "subject is nil", "subject"));
    if (!navigation_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "combat navigation provider is unavailable", "navigation"));
    auto valid = validateGoal(goal);
    if (!valid) return valid;
    const auto found = states_.find(subject.format());
    if (found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format()));
    found->second.navigationGoal = goal;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatLocomotionRuntime::stop(SubjectRef subject) {
    const auto found = states_.find(subject.format());
    if (!subject.isValid() || found == states_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format()));
    found->second.moveDirection     = {};
    found->second.moveSpeedFraction = 0.0;
    found->second.navigationGoal.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatLocomotionAdvance> CombatLocomotionRuntime::advance(const SimulationStep& step) {
    if (step.delta < Duration::zero() || step.tick < lastTick_)
        return Result<CombatLocomotionAdvance>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "locomotion step is negative or moves tick backwards", "step"));
    auto                    candidate = states_;
    CombatLocomotionAdvance result;
    result.tick          = step.tick;
    const double seconds = step.delta.seconds();
    for (auto& [key, state] : candidate) {
        (void)key;
        bool                                arrived    = false;
        std::optional<CombatNavigationGoal> activeGoal = state.navigationGoal;
        if (state.navigationGoal) {
            if (!navigation_)
                return Result<CombatLocomotionAdvance>::failure(
                    Diagnostic::error(DiagnosticCode::Unsupported, "active goal has no combat navigation provider",
                                      state.subject.format()));
            auto steering = navigation_->steer(state, *state.navigationGoal, step.tick);
            if (!steering) return Result<CombatLocomotionAdvance>::failure(steering.status());
            auto valid = validateSteering(steering.value());
            if (!valid) return Result<CombatLocomotionAdvance>::failure(valid.status());
            arrived                 = steering.value().phase == CombatNavigationPhase::Arrived;
            state.moveDirection     = arrived ? CombatVector2{} : normalized(steering.value().direction);
            state.moveSpeedFraction = arrived ? 0.0 : steering.value().speedFraction;
            if (arrived) {
                state.navigationGoal.reset();
                state.velocity = {};
            }
        }
        const CombatVector2 previous = state.position;
        const CombatVector2 desired{state.moveDirection.x * state.maximumSpeed * state.moveSpeedFraction,
                                    state.moveDirection.z * state.maximumSpeed * state.moveSpeedFraction};
        const CombatVector2 velocityDelta{desired.x - state.velocity.x, desired.z - state.velocity.z};
        const double        deltaLength  = length(velocityDelta);
        const double        maximumDelta = state.acceleration * seconds;
        const double        scale = deltaLength > maximumDelta && deltaLength > 0.0 ? maximumDelta / deltaLength : 1.0;
        state.velocity.x += velocityDelta.x * scale;
        state.velocity.z += velocityDelta.z * scale;
        state.position.x += state.velocity.x * seconds;
        state.position.z += state.velocity.z * seconds;
        if (activeGoal && !arrived) {
            const CombatVector2 before{activeGoal->position.x - previous.x, activeGoal->position.z - previous.z};
            const CombatVector2 after{activeGoal->position.x - state.position.x,
                                      activeGoal->position.z - state.position.z};
            if (length(after) <= activeGoal->acceptanceRadius || before.x * after.x + before.z * after.z <= 0.0) {
                state.position          = activeGoal->position;
                state.velocity          = {};
                state.moveDirection     = {};
                state.moveSpeedFraction = 0.0;
                state.navigationGoal.reset();
                arrived = true;
            }
        }
        if (length(state.velocity) > 0.0) state.facing = normalized(state.velocity);
        if (state.position.x != previous.x || state.position.z != previous.z)
            result.events.push_back(
                {state.subject, CombatLocomotionEventKind::Moved, previous, state.position, step.tick});
        if (arrived)
            result.events.push_back(
                {state.subject, CombatLocomotionEventKind::Arrived, previous, state.position, step.tick});
    }
    states_   = std::move(candidate);
    lastTick_ = step.tick;
    return Result<CombatLocomotionAdvance>::success(std::move(result), Status::success(StatusCode::Applied));
}

std::vector<CombatLocomotionState> CombatLocomotionRuntime::states() const {
    std::vector<CombatLocomotionState> result;
    result.reserve(states_.size());
    for (const auto& [key, state] : states_) {
        (void)key;
        result.push_back(state);
    }
    return result;
}

}  // namespace eve::combat

// CombatLoop.cpp
namespace eve::combat {
namespace {

bool invulnerable(const CombatActionWindowState* windows, const CombatCharacterRuntime* characters,
                  SubjectRef subject) {
    if (windows && windows->isInvulnerable(subject)) return true;
    if (!characters) return false;
    auto state = characters->state(subject);
    return state && state.value().invulnerable;
}

bool dodging(const CombatCharacterRuntime* characters, SubjectRef subject) {
    if (!characters) return false;
    auto state = characters->state(subject);
    return state && state.value().mode == CombatCharacterMode::Dodging && state.value().invulnerable;
}

}  // namespace

Result<CombatLoopFrame> CombatLoopRuntime::advance(const SimulationStep& step) {
    if (!characters_ || !melee_)
        return Result<CombatLoopFrame>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "combat loop requires character and melee runtimes", "loop"));

    CombatLoopFrame frame;
    frame.tick = step.tick;

    if (enemies_) {
        for (const auto& state : characters_->states()) {
            auto posed = enemies_->setPosition(state.subject, state.position.x, state.position.y, state.position.z);
            if (!posed) return Result<CombatLoopFrame>::failure(posed.status());
        }
        auto steering = enemies_->nextSteering(step.tick);
        if (!steering) return Result<CombatLoopFrame>::failure(steering.status());
        frame.enemySteering = steering.value();
        for (const auto& steer : frame.enemySteering) {
            auto moved = characters_->setMoveIntent(steer.subject, steer.moveDirection, steer.speedFraction);
            if (!moved) return Result<CombatLoopFrame>::failure(moved.status());
        }
    }

    if (cancelResolver_ && cancelSubject_.isValid() && !cancelAbility_.format().empty()) {
        CombatCancelResolveRequest request;
        request.subject        = cancelSubject_;
        request.currentAbility = cancelAbility_;
        request.tick           = step.tick;
        auto resolved          = cancelResolver_->resolve(request);
        if (resolved)
            frame.cancels.push_back(std::move(resolved).takeValue());
        else if (resolved.status().code() != StatusCode::NotFound)
            return Result<CombatLoopFrame>::failure(resolved.status());
    }

    if (feel_) {
        for (const auto& state : characters_->states()) {
            auto frozen = characters_->setTimeFrozen(state.subject, feel_->isFrozen(state.subject));
            if (!frozen) return Result<CombatLoopFrame>::failure(frozen.status());
        }
    }

    if (targets_) {
        for (const auto& state : characters_->states()) {
            if (state.mode != CombatCharacterMode::Attacking) continue;
            if (feel_ && feel_->isFrozen(state.subject)) continue;
            auto lock = targets_->state(state.subject);
            if (!lock || !lock.value().target) continue;
            auto target = characters_->state(*lock.value().target);
            if (!target) continue;
            CombatWarpRequest warp;
            warp.attacker        = state.position;
            warp.target          = target.value().position;
            warp.attackerFacing  = state.facing;
            warp.desiredDistance = 1.4;
            warp.maxTranslation  = 0.12;
            warp.remainingBudget = 0.6;
            auto solved          = CombatMotionWarp::solve(warp);
            if (!solved) {
                if (solved.status().code() == StatusCode::Conflict) continue;
                return Result<CombatLoopFrame>::failure(solved.status());
            }
            auto faced = characters_->setFacing(state.subject, solved.value().facing);
            if (!faced) return Result<CombatLoopFrame>::failure(faced.status());
            if (solved.value().translation.x != 0.0 || solved.value().translation.z != 0.0) {
                auto applied = characters_->setRootMotionDelta(state.subject, solved.value().translation);
                if (!applied) return Result<CombatLoopFrame>::failure(applied.status());
            }
        }
    }

    auto locomotion = characters_->advance(step);
    if (!locomotion) return Result<CombatLoopFrame>::failure(locomotion.status());
    frame.locomotion = std::move(locomotion).takeValue();

    for (const auto& state : characters_->states()) {
        auto posed = melee_->setHurtboxPose(state.subject, "torso",
                                            {{state.position.x, state.position.y + 1.0, state.position.z}, 0.0});
        if (!posed && posed.status().code() != StatusCode::NotFound)
            return Result<CombatLoopFrame>::failure(posed.status());
        (void)posed;
    }

    auto melee = melee_->advance(step.tick);
    if (!melee) return Result<CombatLoopFrame>::failure(melee.status());
    frame.melee = std::move(melee).takeValue();

    if (states_) {
        for (const auto& hit : frame.melee.hits) {
            if (dodging(characters_, hit.target)) {
                frame.perfectDodges.push_back({hit.target, hit.source, hit.hitboxId});
                continue;
            }
            if (invulnerable(windows_, characters_, hit.target)) continue;
            DamageRequest request;
            request.source          = hit.source;
            request.target          = hit.target;
            request.actionExecution = hit.actionExecution;
            request.damageType      = hit.damageType;
            request.healthDamage    = hit.healthDamage;
            request.poiseDamage     = hit.poiseDamage;
            request.knockback       = hit.knockback;
            if (guards_) {
                auto guarded = guards_->mitigate(hit.target, request);
                if (!guarded) return Result<CombatLoopFrame>::failure(guarded.status());
                frame.guards.push_back(guarded.value());
                if (guarded.value().result != GuardResult::None) continue;
            }
            const auto found = states_->find(hit.target.format());
            if (found == states_->end())
                return Result<CombatLoopFrame>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "loop damage target missing", hit.target.format()));
            DamageRuntime damage;
            auto          outcome = damage.apply(found->second, request);
            if (!outcome) return Result<CombatLoopFrame>::failure(outcome.status());
            if (feel_) {
                auto felt = feel_->applyFromOutcome(outcome.value());
                if (!felt) return Result<CombatLoopFrame>::failure(felt.status());
            }
            auto reacted = characters_->applyDamageReaction(
                hit.target, outcome.value().reaction,
                feel_ ? feel_->state(hit.target).hitstunRemaining : Duration::zero(), outcome.value().knockback);
            if (!reacted) return Result<CombatLoopFrame>::failure(reacted.status());
            frame.outcomes.push_back(std::move(outcome).takeValue());
        }
    }

    if (feel_) {
        auto felt = feel_->advance(step);
        if (!felt) return Result<CombatLoopFrame>::failure(felt.status());
    }

    if (cameraFocus_.isValid()) {
        auto focus = characters_->state(cameraFocus_);
        if (focus) {
            CombatCameraFramingRequest framing;
            framing.player       = focus.value().position;
            framing.playerFacing = focus.value().facing;
            if (targets_) {
                auto lock = targets_->state(cameraFocus_);
                if (lock && lock.value().target) {
                    auto locked = characters_->state(*lock.value().target);
                    if (locked) framing.lockTarget = locked.value().position;
                }
            }
            auto view = CombatCameraFraming::solve(framing);
            if (!view) return Result<CombatLoopFrame>::failure(view.status());
            frame.camera = view.value();
        }
    }
    return Result<CombatLoopFrame>::success(std::move(frame), Status::success(StatusCode::Applied));
}

}  // namespace eve::combat
