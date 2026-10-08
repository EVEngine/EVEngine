#include "combat/MeleeHit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace eve::combat {
namespace {

bool finitePoint(MeleePoint3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

MeleePoint3 add(MeleePoint3 a, MeleePoint3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

MeleePoint3 sub(MeleePoint3 a, MeleePoint3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

MeleePoint3 scale(MeleePoint3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }

double dot(MeleePoint3 a, MeleePoint3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

double lengthSq(MeleePoint3 a) { return dot(a, a); }

double length(MeleePoint3 a) { return std::sqrt(lengthSq(a)); }

MeleePoint3 normalizeOrZero(MeleePoint3 a) {
    const double mag = length(a);
    return mag > 0.0 ? scale(a, 1.0 / mag) : MeleePoint3{};
}

MeleePoint3 rotateYaw(MeleePoint3 local, double yaw) {
    const double c = std::cos(yaw);
    const double s = std::sin(yaw);
    return {local.x * c - local.z * s, local.y, local.x * s + local.z * c};
}

struct CapsuleWorld {
    MeleePoint3 a;
    MeleePoint3 b;
    double      radius = 0.0;
};

CapsuleWorld capsuleAt(const MeleeShape& shape, const MeleePose& pose, MeleePoint3 localOffset) {
    const MeleePoint3 center = add(pose.position, rotateYaw(localOffset, pose.yawRadians));
    const MeleePoint3 axis = rotateYaw({0.0, shape.halfHeight, 0.0}, pose.yawRadians);
    return {sub(center, axis), add(center, axis), shape.radius};
}

MeleePoint3 closestOnSegment(MeleePoint3 p, MeleePoint3 a, MeleePoint3 b) {
    const MeleePoint3 ab = sub(b, a);
    const double denom = lengthSq(ab);
    if (denom <= 0.0) return a;
    const double t = std::clamp(dot(sub(p, a), ab) / denom, 0.0, 1.0);
    return add(a, scale(ab, t));
}

bool spheresOverlap(MeleePoint3 a, double ra, MeleePoint3 b, double rb, MeleePoint3& contact, MeleePoint3& normal) {
    const MeleePoint3 delta = sub(b, a);
    const double dist = length(delta);
    const double limit = ra + rb;
    if (dist > limit) return false;
    normal = dist > 0.0 ? scale(delta, 1.0 / dist) : MeleePoint3{1.0, 0.0, 0.0};
    contact = add(a, scale(normal, ra));
    return true;
}

bool capsuleSphereOverlap(const CapsuleWorld& capsule, MeleePoint3 center, double radius, MeleePoint3& contact,
                          MeleePoint3& normal) {
    const MeleePoint3 closest = closestOnSegment(center, capsule.a, capsule.b);
    return spheresOverlap(closest, capsule.radius, center, radius, contact, normal);
}

bool capsuleCapsuleOverlap(const CapsuleWorld& a, const CapsuleWorld& b, MeleePoint3& contact, MeleePoint3& normal) {
    // Sample closest approach via discrete segment samples; deterministic and adequate for arena scale.
    constexpr int kSamples = 8;
    double best = std::numeric_limits<double>::infinity();
    MeleePoint3 bestA{};
    MeleePoint3 bestB{};
    for (int i = 0; i <= kSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSamples);
        const MeleePoint3 pa = add(a.a, scale(sub(a.b, a.a), t));
        const MeleePoint3 pb = closestOnSegment(pa, b.a, b.b);
        const double d = lengthSq(sub(pb, pa));
        if (d < best) {
            best = d;
            bestA = pa;
            bestB = pb;
        }
    }
    return spheresOverlap(bestA, a.radius, bestB, b.radius, contact, normal);
}

bool shapesOverlap(const MeleeShape& attackShape, const MeleePose& attackPose, MeleePoint3 attackOffset,
                   const MeleeShape& hurtShape, const MeleePose& hurtPose, MeleePoint3& contact,
                   MeleePoint3& normal) {
    if (attackShape.kind == MeleeShapeKind::Sphere && hurtShape.kind == MeleeShapeKind::Sphere) {
        const MeleePoint3 a = add(attackPose.position, rotateYaw(attackOffset, attackPose.yawRadians));
        const MeleePoint3 b = hurtPose.position;
        return spheresOverlap(a, attackShape.radius, b, hurtShape.radius, contact, normal);
    }
    if (attackShape.kind == MeleeShapeKind::Capsule && hurtShape.kind == MeleeShapeKind::Sphere) {
        return capsuleSphereOverlap(capsuleAt(attackShape, attackPose, attackOffset), hurtPose.position,
                                    hurtShape.radius, contact, normal);
    }
    if (attackShape.kind == MeleeShapeKind::Sphere && hurtShape.kind == MeleeShapeKind::Capsule) {
        const bool hit =
            capsuleSphereOverlap(capsuleAt(hurtShape, hurtPose, {}),
                                 add(attackPose.position, rotateYaw(attackOffset, attackPose.yawRadians)),
                                 attackShape.radius, contact, normal);
        if (hit) normal = scale(normal, -1.0);
        return hit;
    }
    return capsuleCapsuleOverlap(capsuleAt(attackShape, attackPose, attackOffset),
                                 capsuleAt(hurtShape, hurtPose, {}), contact, normal);
}

std::string hurtKey(SubjectRef subject, std::string_view hurtboxId) {
    return subject.format() + "|" + std::string(hurtboxId);
}

}  // namespace

Result<void> MeleeShape::validate() const {
    if (!std::isfinite(radius) || radius <= 0.0) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "melee shape radius is invalid", "radius"));
    if (kind == MeleeShapeKind::Capsule && (!std::isfinite(halfHeight) || halfHeight < 0.0))
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "melee capsule halfHeight is invalid", "halfHeight"));
    if (kind != MeleeShapeKind::Sphere && kind != MeleeShapeKind::Capsule)
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "melee shape kind is invalid", "kind"));
    return Result<void>::success();
}

Result<void> MeleePose::validate() const {
    if (!finitePoint(position) || !std::isfinite(yawRadians)) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "melee pose is invalid", "pose"));
    return Result<void>::success();
}

Result<void> MeleeHurtboxDefinition::validate() const {
    if (!subject.isValid()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hurtbox subject is nil", "subject"));
    if (hurtboxId.empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hurtbox id is empty", "hurtboxId"));
    if (bodyPart.empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hurtbox body part is empty", "bodyPart"));
    return shape.validate();
}

Result<void> MeleeHitboxDefinition::validate() const {
    if (hitboxId.empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hitbox id is empty", "hitboxId"));
    if (!finitePoint(localOffset)) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hitbox local offset is invalid", "localOffset"));
    if (!std::isfinite(healthDamage) || healthDamage < 0.0) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hitbox health damage is invalid", "healthDamage"));
    if (!std::isfinite(poiseDamage) || poiseDamage < 0.0) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hitbox poise damage is invalid", "poiseDamage"));
    if (damageType.empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hitbox damage type is empty", "damageType"));
    if (!std::isfinite(knockbackSpeed) || knockbackSpeed < 0.0)
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hitbox knockback speed is invalid", "knockbackSpeed"));
    if (!std::isfinite(knockbackLift) || knockbackLift < 0.0)
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hitbox knockback lift is invalid", "knockbackLift"));
    return shape.validate();
}

Result<void> MeleeHitRuntime::registerHitbox(MeleeHitboxDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return valid;
    hitboxes_[definition.hitboxId] = std::move(definition);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MeleeHitRuntime::registerHurtbox(MeleeHurtboxDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return valid;
    const std::string key = hurtKey(definition.subject, definition.hurtboxId);
    HurtboxState state;
    state.definition = std::move(definition);
    hurtboxes_[key] = std::move(state);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MeleeHitRuntime::clearHurtboxes(SubjectRef subject) {
    if (!subject.isValid()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "subject is nil", "subject"));
    const std::string prefix = subject.format() + "|";
    for (auto it = hurtboxes_.begin(); it != hurtboxes_.end();) {
        if (it->first.rfind(prefix, 0) == 0)
            it = hurtboxes_.erase(it);
        else
            ++it;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MeleeHitRuntime::setHurtboxPose(SubjectRef subject, std::string_view hurtboxId, MeleePose pose) {
    auto valid = pose.validate();
    if (!valid) return valid;
    const auto found = hurtboxes_.find(hurtKey(subject, hurtboxId));
    if (found == hurtboxes_.end())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "hurtbox was not found", "hurtboxId"));
    found->second.pose = pose;
    found->second.hasPose = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MeleeHitRuntime::armHitbox(SubjectRef subject, std::string_view hitboxId,
                                        action::ActionExecutionId execution) {
    if (!subject.isValid()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "subject is nil", "subject"));
    if (hitboxId.empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "hitbox id is empty", "hitboxId"));
    if (!hitboxes_.contains(std::string(hitboxId)))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "hitbox catalog entry missing",
                                                       std::string(hitboxId)));
    if (!poses_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "melee pose source is unavailable", "poseSource"));
    auto pose = poses_->pose(subject, hitboxId);
    if (!pose) return Result<void>::failure(pose.status());
    auto valid = pose.value().validate();
    if (!valid) return valid;
    ArmedKey key{subject.format(), std::string(hitboxId), execution.value()};
    ArmedHitbox armed;
    armed.subject = subject;
    armed.hitboxId = std::string(hitboxId);
    armed.execution = execution;
    armed.previous = pose.value();
    armed.current = pose.value();
    armed.hasPose = true;
    armed_[std::move(key)] = std::move(armed);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MeleeHitRuntime::disarmHitbox(SubjectRef subject, std::string_view hitboxId,
                                           action::ActionExecutionId execution) {
    ArmedKey key{subject.format(), std::string(hitboxId), execution.value()};
    const auto found = armed_.find(key);
    if (found == armed_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    for (auto it = hitMemory_.begin(); it != hitMemory_.end();) {
        if (std::get<0>(it->first) == execution.value() && std::get<1>(it->first) == hitboxId)
            it = hitMemory_.erase(it);
        else
            ++it;
    }
    armed_.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<MeleeHitFrame> MeleeHitRuntime::advance(SimulationTick tick) {
    if (tick.value() < lastTick_.value())
        return Result<MeleeHitFrame>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "melee tick must be non-decreasing", "tick"));
    if (!poses_ && !armed_.empty())
        return Result<MeleeHitFrame>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "melee pose source is unavailable", "poseSource"));

    MeleeHitFrame frame;
    frame.tick = tick;
    for (auto& [key, armed] : armed_) {
        (void)key;
        auto pose = poses_->pose(armed.subject, armed.hitboxId);
        if (!pose) return Result<MeleeHitFrame>::failure(pose.status());
        auto valid = pose.value().validate();
        if (!valid) return Result<MeleeHitFrame>::failure(valid.status());
        armed.previous = armed.current;
        armed.current = pose.value();
        const auto& definition = hitboxes_.at(armed.hitboxId);
        for (const auto& [hurtKeyName, hurt] : hurtboxes_) {
            (void)hurtKeyName;
            if (!hurt.hasPose) continue;
            if (hurt.definition.subject == armed.subject) continue;
            HitMemoryKey memory{armed.execution.value(), armed.hitboxId, hurt.definition.subject.format(),
                                hurt.definition.hurtboxId};
            if (hitMemory_.contains(memory)) continue;

            MeleePoint3 contact{};
            MeleePoint3 normal{};
            const bool previousHit =
                shapesOverlap(definition.shape, armed.previous, definition.localOffset, hurt.definition.shape,
                              hurt.pose, contact, normal);
            const bool currentHit =
                shapesOverlap(definition.shape, armed.current, definition.localOffset, hurt.definition.shape,
                              hurt.pose, contact, normal);
            if (!previousHit && !currentHit) continue;

            MeleeHitEvent event;
            event.source = armed.subject;
            event.target = hurt.definition.subject;
            event.hitboxId = armed.hitboxId;
            event.hurtboxId = hurt.definition.hurtboxId;
            event.bodyPart = hurt.definition.bodyPart;
            event.contact = contact;
            event.normal = normal;
            event.healthDamage = definition.healthDamage;
            event.poiseDamage = definition.poiseDamage;
            event.damageType = definition.damageType;
            event.knockback       = {normal.x * definition.knockbackSpeed, definition.knockbackLift,
                                     normal.z * definition.knockbackSpeed};
            event.actionExecution = armed.execution;
            hitMemory_[memory] = true;
            frame.hits.push_back(std::move(event));
        }
    }
    lastTick_ = tick;
    return Result<MeleeHitFrame>::success(std::move(frame), Status::success(StatusCode::Applied));
}

Result<std::vector<DamageOutcome>> MeleeHitRuntime::applyHits(
    DamageRuntime& damage, std::map<std::string, CombatState, std::less<>>& states,
    const std::vector<MeleeHitEvent>& hits,
    const std::map<std::string, double, std::less<>>& bodyPartMultipliers) const {
    std::vector<DamageOutcome> outcomes;
    outcomes.reserve(hits.size());
    for (const auto& hit : hits) {
        const auto found = states.find(hit.target.format());
        if (found == states.end())
            return Result<std::vector<DamageOutcome>>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "damage target was not found", hit.target.format()));
        DamageRequest request;
        request.source = hit.source;
        request.target = hit.target;
        request.actionExecution = hit.actionExecution;
        request.damageType = hit.damageType;
        request.healthDamage = hit.healthDamage;
        request.poiseDamage = hit.poiseDamage;
        request.knockback       = hit.knockback;
        const auto mult = bodyPartMultipliers.find(hit.bodyPart);
        if (mult != bodyPartMultipliers.end()) request.incomingDamageMultiplier = mult->second;
        auto outcome = damage.apply(found->second, request);
        if (!outcome) return Result<std::vector<DamageOutcome>>::failure(outcome.status());
        outcomes.push_back(std::move(outcome).takeValue());
    }
    return Result<std::vector<DamageOutcome>>::success(std::move(outcomes),
                                                       Status::success(StatusCode::Applied));
}

}  // namespace eve::combat
