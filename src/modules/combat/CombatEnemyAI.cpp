#include "combat/CombatEnemyAI.h"

#include <cmath>
#include <optional>
#include <utility>

namespace eve::combat {
namespace {

Result<void> invalid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

double distance3(double ax, double ay, double az, double bx, double by, double bz) {
    const double dx = ax - bx;
    const double dy = ay - by;
    const double dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

Result<void> CombatEnemyDefinition::validate() const {
    if (!subject.isValid()) return invalid("enemy subject is nil", "subject");
    if (ownerId.empty()) return invalid("enemy owner id is empty", "ownerId");
    if (!std::isfinite(nearRadius) || nearRadius <= 0.0 || !std::isfinite(midRadius) || midRadius < nearRadius)
        return invalid("enemy radii are invalid", "radius");
    if (attackCooldownTicks == 0) return invalid("enemy attack cooldown must be positive", "attackCooldownTicks");
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
    if (!subject.isValid()) return invalid("subject is nil", "subject");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return invalid("pose is invalid", "pose");
    poses_[subject.format()] = Pose{x, y, z};
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatEnemyIntentSource::setLightAction(SubjectRef subject, LogicalId actionId) {
    auto found = enemies_.find(subject.format());
    if (found == enemies_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "enemy was not found", subject.format()));
    if (actionId.format().empty()) return invalid("light action id is empty", "actionId");
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
