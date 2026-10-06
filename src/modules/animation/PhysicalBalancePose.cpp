#include "animation/PhysicalBalancePose.h"

#include "animation/AnimControlMath.h"
#include "animation/AnimMath.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimationTime.h"
#include "common/Exception.h"

#include <cmath>
#include <string>

namespace eve::animation {
namespace {

constexpr float kTwoPi = 6.283185307179586f;
constexpr float kImpulseEps = 1e-8f;

void quatMul(float ax, float ay, float az, float aw, float bx, float by, float bz, float bw, float& ox, float& oy,
             float& oz, float& ow) {
    multiplyQuat(ax, ay, az, aw, bx, by, bz, bw, ox, oy, oz, ow);
}

void quatFromAxisAngle(float ax, float ay, float az, float angle, float& qx, float& qy, float& qz, float& qw) {
    const float half = 0.5f * angle;
    const float s    = std::sin(half);
    qx               = ax * s;
    qy               = ay * s;
    qz               = az * s;
    qw               = std::cos(half);
}

void applyParentRotation(TransformTRS& local, float qx, float qy, float qz, float qw) {
    float ox = 0.f, oy = 0.f, oz = 0.f, ow = 1.f;
    quatMul(qx, qy, qz, qw, local.qx, local.qy, local.qz, local.qw, ox, oy, oz, ow);
    local.qx = ox;
    local.qy = oy;
    local.qz = oz;
    local.qw = ow;
    local.normalizeRotation();
}

void stepTowardZero(float dt, float omega, float zeta, float& y, float& yd) {
    if (dt <= 0.f || omega <= 0.f) return;
    const float kp = omega * omega;
    const float kd = 2.f * zeta * omega;
    yd += dt * (-kp * y - kd * yd);
    y += dt * yd;
}

}  // namespace

PhysicalBalancePose::PhysicalBalancePose(AnimSkeleton* skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("PhysicalBalancePose: skeleton is null");
    ensureBones();
    skeleton_->applyBindPose(&pose_);
    skeleton_->applyBindPose(&target_);
    hasTarget_ = true;
    writeOverlayPose();
}

void PhysicalBalancePose::ensureBones() {
    const int n = skeleton_->getBoneCount();
    if (pose_.getBoneCount() != n) pose_.resize(n);
    if (target_.getBoneCount() != n) target_.resize(n);
    if (static_cast<int>(masses_.size()) != n) {
        masses_.assign(static_cast<size_t>(n), 1.f);
        recoils_.assign(static_cast<size_t>(n), Recoil{});
    }
    if (n <= 0) {
        supportBone_ = 0;
        balanceBone_ = 0;
        return;
    }
    if (!boneInRange(supportBone_)) supportBone_ = 0;
    if (!boneInRange(balanceBone_)) balanceBone_ = n > 1 ? 1 : 0;
}

bool PhysicalBalancePose::boneInRange(int boneIndex) const {
    return boneIndex >= 0 && boneIndex < skeleton_->getBoneCount();
}

eve::Result<void> PhysicalBalancePose::requireFinitePositive(const char* name, float value, bool allowZero) {
    if (!std::isfinite(value) || value < 0.f || (!allowZero && value <= 0.f))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, std::string("physical balance ") + name + " must be finite and " +
                                                      (allowZero ? "non-negative" : "positive")));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setSupportBone(int boneIndex) {
    if (!boneInRange(boneIndex))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "physical balance support bone is out of range"));
    supportBone_ = boneIndex;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setBalanceBone(int boneIndex) {
    if (!boneInRange(boneIndex))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "physical balance balance bone is out of range"));
    balanceBone_ = boneIndex;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setBoneMass(int boneIndex, float mass) {
    if (!boneInRange(boneIndex))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "physical balance bone mass index is out of range"));
    auto ok = requireFinitePositive("bone mass", mass, true);
    if (!ok) return ok;
    masses_[static_cast<size_t>(boneIndex)] = mass;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

float PhysicalBalancePose::getBoneMass(int boneIndex) const {
    if (!boneInRange(boneIndex)) return 0.f;
    return masses_[static_cast<size_t>(boneIndex)];
}

eve::Result<void> PhysicalBalancePose::setRecovery(float frequencyHz, float dampingZeta) {
    auto freq = requireFinitePositive("recovery frequency", frequencyHz, false);
    if (!freq) return freq;
    if (!std::isfinite(dampingZeta) || dampingZeta < 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "physical balance recovery damping must be finite and non-negative"));
    const float omega = kTwoPi * frequencyHz;
    const float height = std::max(pendulumHeight_, 1e-4f);
    if (omega * omega <= gravity_ / height)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "physical balance recovery frequency is too low to overcome gravity at the pendulum height"));
    recoveryHz_   = frequencyHz;
    recoveryZeta_ = dampingZeta;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setRecoil(float frequencyHz, float dampingZeta) {
    auto freq = requireFinitePositive("recoil frequency", frequencyHz, false);
    if (!freq) return freq;
    if (!std::isfinite(dampingZeta) || dampingZeta < 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "physical balance recoil damping must be finite and non-negative"));
    recoilHz_   = frequencyHz;
    recoilZeta_ = dampingZeta;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setGravity(float metersPerSecondSquared) {
    auto ok = requireFinitePositive("gravity", metersPerSecondSquared, true);
    if (!ok) return ok;
    const float omega  = kTwoPi * recoveryHz_;
    const float height = std::max(pendulumHeight_, 1e-4f);
    if (omega * omega <= metersPerSecondSquared / height)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "physical balance gravity would overcome the current recovery frequency"));
    gravity_ = metersPerSecondSquared;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setPendulumHeight(float meters) {
    auto ok = requireFinitePositive("pendulum height", meters, false);
    if (!ok) return ok;
    const float omega = kTwoPi * recoveryHz_;
    if (omega * omega <= gravity_ / meters)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "physical balance pendulum height would make gravity stronger than recovery"));
    pendulumHeight_ = meters;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setInertia(float inertia) {
    auto ok = requireFinitePositive("inertia", inertia, false);
    if (!ok) return ok;
    inertia_ = inertia;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setRecoilInertia(float inertia) {
    auto ok = requireFinitePositive("recoil inertia", inertia, false);
    if (!ok) return ok;
    recoilInertia_ = inertia;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setMaxLean(float radians) {
    auto ok = requireFinitePositive("max lean", radians, false);
    if (!ok) return ok;
    maxLean_ = radians;
    leanX_   = clampf(leanX_, -maxLean_, maxLean_);
    leanZ_   = clampf(leanZ_, -maxLean_, maxLean_);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::setTargetPose(const AnimPose* target) {
    if (!target)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "physical balance target pose is null"));
    if (target->getBoneCount() != skeleton_->getBoneCount())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "physical balance target pose bone count does not match the skeleton"));
    target_.copyFrom(target);
    hasTarget_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void PhysicalBalancePose::resetDynamics() {
    leanX_ = leanZ_ = 0.f;
    leanVelX_ = leanVelZ_ = 0.f;
    pendingLeanTx_ = pendingLeanTz_ = 0.f;
    for (Recoil& recoil : recoils_) recoil = Recoil{};
}

eve::Result<void> PhysicalBalancePose::snapToTarget() {
    if (!hasTarget_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "physical balance has no target pose to snap to"));
    resetDynamics();
    writeOverlayPose();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> PhysicalBalancePose::applyImpulse(int boneIndex, float impulseX, float impulseY, float impulseZ,
                                                    float pointX, float pointY, float pointZ) {
    if (!boneInRange(boneIndex))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "physical balance impulse bone is out of range"));
    if (!std::isfinite(impulseX) || !std::isfinite(impulseY) || !std::isfinite(impulseZ) || !std::isfinite(pointX) ||
        !std::isfinite(pointY) || !std::isfinite(pointZ))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "physical balance impulse values must be finite"));
    const float mag2 = impulseX * impulseX + impulseY * impulseY + impulseZ * impulseZ;
    if (mag2 < kImpulseEps)
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));

    pose_.computeWorld(skeleton_);
    const TransformTRS& support = pose_.world(supportBone_);
    const float         rx      = pointX - support.px;
    const float         ry      = pointY - support.py;
    const float         rz      = pointZ - support.pz;
    pendingLeanTx_ += ry * impulseZ - rz * impulseY;
    pendingLeanTz_ += rx * impulseY - ry * impulseX;

    const TransformTRS& bone = pose_.world(boneIndex);
    Recoil&             rec  = recoils_[static_cast<size_t>(boneIndex)];
    const float         brx  = pointX - bone.px;
    const float         bry  = pointY - bone.py;
    const float         brz  = pointZ - bone.pz;
    rec.pendingTx += bry * impulseZ - brz * impulseY;
    rec.pendingTy += brz * impulseX - brx * impulseZ;
    rec.pendingTz += brx * impulseY - bry * impulseX;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void PhysicalBalancePose::writeOverlayPose() {
    pose_.copyFrom(&target_);
    const int n = pose_.getBoneCount();
    for (int i = 0; i < n; ++i) {
        const Recoil& rec = recoils_[static_cast<size_t>(i)];
        if (std::fabs(rec.x) < 1e-8f && std::fabs(rec.y) < 1e-8f && std::fabs(rec.z) < 1e-8f) continue;
        float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
        float rx = 0.f, ry = 0.f, rz = 0.f, rw = 1.f;
        float tx = 0.f, ty = 0.f, tz = 0.f, tw = 1.f;
        quatFromAxisAngle(1.f, 0.f, 0.f, rec.x, qx, qy, qz, qw);
        quatFromAxisAngle(0.f, 1.f, 0.f, rec.y, rx, ry, rz, rw);
        quatMul(rx, ry, rz, rw, qx, qy, qz, qw, tx, ty, tz, tw);
        quatFromAxisAngle(0.f, 0.f, 1.f, rec.z, qx, qy, qz, qw);
        float ox = 0.f, oy = 0.f, oz = 0.f, ow = 1.f;
        quatMul(qx, qy, qz, qw, tx, ty, tz, tw, ox, oy, oz, ow);
        applyParentRotation(pose_.local(i), ox, oy, oz, ow);
    }
    if (boneInRange(balanceBone_) && (std::fabs(leanX_) > 1e-8f || std::fabs(leanZ_) > 1e-8f)) {
        float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
        float rx = 0.f, ry = 0.f, rz = 0.f, rw = 1.f;
        quatFromAxisAngle(1.f, 0.f, 0.f, leanX_, qx, qy, qz, qw);
        quatFromAxisAngle(0.f, 0.f, 1.f, leanZ_, rx, ry, rz, rw);
        float ox = 0.f, oy = 0.f, oz = 0.f, ow = 1.f;
        quatMul(qx, qy, qz, qw, rx, ry, rz, rw, ox, oy, oz, ow);
        applyParentRotation(pose_.local(balanceBone_), ox, oy, oz, ow);
    }
    refreshDiagnostics();
}

void PhysicalBalancePose::refreshDiagnostics() {
    pose_.computeWorld(skeleton_);
    const int n = pose_.getBoneCount();
    float     massSum = 0.f;
    comX_ = comY_ = comZ_ = 0.f;
    for (int i = 0; i < n; ++i) {
        const float mass = masses_[static_cast<size_t>(i)];
        if (mass <= 0.f) continue;
        const TransformTRS& world = pose_.world(i);
        comX_ += world.px * mass;
        comY_ += world.py * mass;
        comZ_ += world.pz * mass;
        massSum += mass;
    }
    if (massSum > 1e-8f) {
        const float inv = 1.f / massSum;
        comX_ *= inv;
        comY_ *= inv;
        comZ_ *= inv;
    }
    if (boneInRange(supportBone_)) {
        const TransformTRS& support = pose_.world(supportBone_);
        supportX_                   = support.px;
        supportY_                   = support.py;
        supportZ_                   = support.pz;
    }
}

void PhysicalBalancePose::updateUnchecked(float dt) {
    if (dt <= 0.f || !hasTarget_) return;
    ensureBones();

    leanVelX_ += pendingLeanTx_ / inertia_;
    leanVelZ_ += pendingLeanTz_ / inertia_;
    pendingLeanTx_ = pendingLeanTz_ = 0.f;

    const float omega  = kTwoPi * recoveryHz_;
    const float height = std::max(pendulumHeight_, 1e-4f);
    const float grav   = gravity_ / height;
    const float kp     = std::max(omega * omega - grav, 1e-4f);
    const float kd     = 2.f * recoveryZeta_ * omega;
    leanVelX_ += dt * (-kp * leanX_ - kd * leanVelX_);
    leanVelZ_ += dt * (-kp * leanZ_ - kd * leanVelZ_);
    leanX_ += dt * leanVelX_;
    leanZ_ += dt * leanVelZ_;
    const float clampedX = clampf(leanX_, -maxLean_, maxLean_);
    const float clampedZ = clampf(leanZ_, -maxLean_, maxLean_);
    if (clampedX != leanX_) leanVelX_ = 0.f;
    if (clampedZ != leanZ_) leanVelZ_ = 0.f;
    leanX_ = clampedX;
    leanZ_ = clampedZ;

    const float recoilOmega = kTwoPi * recoilHz_;
    for (Recoil& rec : recoils_) {
        rec.vx += rec.pendingTx / recoilInertia_;
        rec.vy += rec.pendingTy / recoilInertia_;
        rec.vz += rec.pendingTz / recoilInertia_;
        rec.pendingTx = rec.pendingTy = rec.pendingTz = 0.f;
        stepTowardZero(dt, recoilOmega, recoilZeta_, rec.x, rec.vx);
        stepTowardZero(dt, recoilOmega, recoilZeta_, rec.y, rec.vy);
        stepTowardZero(dt, recoilOmega, recoilZeta_, rec.z, rec.vz);
        rec.x = clampf(rec.x, -maxLean_, maxLean_);
        rec.y = clampf(rec.y, -maxLean_, maxLean_);
        rec.z = clampf(rec.z, -maxLean_, maxLean_);
    }

    writeOverlayPose();
}

eve::Result<void> PhysicalBalancePose::advance(const eve::SimulationStep& step) {
    auto seconds = detail::secondsForStep(step, hasLastTick_, lastTick_, "PhysicalBalancePose");
    if (!seconds) return eve::Result<void>::failure(seconds.status());
    updateUnchecked(std::move(seconds).takeValue());
    lastTick_    = step.tick;
    hasLastTick_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void PhysicalBalancePose::update(float dt) {
    auto step = detail::legacyStep(dt, hasLastTick_, lastTick_, "PhysicalBalancePose");
    if (!step) {
        step.ignore("legacy PhysicalBalancePose update");
        return;
    }
    advance(std::move(step).takeValue()).ignore("legacy PhysicalBalancePose update");
}

AnimPose* PhysicalBalancePose::getPose() { return &pose_; }
AnimPose* PhysicalBalancePose::getTargetPose() { return &target_; }

}  // namespace eve::animation
