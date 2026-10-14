// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/StateRecorder.h>
#include "physics/jolt/JoltDistanceConstraint.h"
namespace eve::physics::jolt {
class DistanceConstraint final : public JPH::TwoBodyConstraint {
public:
    DistanceConstraint(JPH::Body&, JPH::Body&, const JPH::DistanceConstraintSettings&);
    JPH::EConstraintSubType GetSubType() const override { return JPH::EConstraintSubType::User1; }
    void                    NotifyShapeChanged(const JPH::BodyID&, JPH::Vec3Arg) override;
    void                    ResetWarmStart() override;
    void                    SetupVelocityConstraint(float) override;
    void                    WarmStartVelocityConstraint(float) override;
    bool                    SolveVelocityConstraint(float) override;
    bool                    SolvePositionConstraint(float, float) override;
    JPH::Mat44              GetConstraintToBody1Matrix() const override { return spring.GetConstraintToBody1Matrix(); }
    JPH::Mat44              GetConstraintToBody2Matrix() const override { return spring.GetConstraintToBody2Matrix(); }
    JPH::Ref<JPH::ConstraintSettings> GetConstraintSettings() const override { return spring.GetConstraintSettings(); }
    void                              SaveState(JPH::StateRecorder&) const override;
    void                              RestoreState(JPH::StateRecorder&) override;
    void                              setLength(float);
    void                              setSpring(bool, float, float);
    void                              setLimits(bool, float, float);
    void                              setMotor(bool, float, float);
    float                             GetTotalLambdaPosition() const;

private:
    JPH::DistanceConstraint spring, limits;
    JPH::AxisConstraintPart motor;
    JPH::Vec3               axis          = JPH::Vec3::sAxisX();
    bool                    springEnabled = false, limitsEnabled = false, motorEnabled = false;
    bool                    solveSpring = true, solveLimits = false, solveMotor = false;
    float                   hertz = 0, damping = 0, length = 0, minimum = 0, maximum = FLT_MAX, speed = 0, maxForce = 0;
    void                    configure();
};

DistanceConstraint::DistanceConstraint(JPH::Body& a, JPH::Body& b, const JPH::DistanceConstraintSettings& settings)
    : TwoBodyConstraint(a, b, settings), spring(a, b, settings), limits(a, b, settings), length(settings.mMinDistance) {
    configure();
}
void DistanceConstraint::configure() {
    const bool soft  = springEnabled && (!limitsEnabled || minimum < maximum);
    solveSpring      = !soft || hertz > 0;
    solveLimits      = soft && limitsEnabled;
    solveMotor       = soft && motorEnabled;
    const float rest = !soft && limitsEnabled && minimum == maximum ? minimum : length;
    spring.SetDistance(rest, rest);
    spring.SetLimitsSpringSettings({JPH::ESpringMode::FrequencyAndDamping, soft ? hertz : 0.f, damping});
    limits.SetDistance(minimum, maximum);
    ResetWarmStart();
}
void DistanceConstraint::setLength(float value) {
    length = value;
    configure();
}
void DistanceConstraint::setSpring(bool enabled, float frequency, float ratio) {
    springEnabled = enabled;
    hertz         = frequency;
    damping       = ratio;
    configure();
}
void DistanceConstraint::setLimits(bool enabled, float lower, float upper) {
    limitsEnabled = enabled;
    minimum       = lower;
    maximum       = upper;
    configure();
}
void DistanceConstraint::setMotor(bool enabled, float velocity, float force) {
    motorEnabled = enabled;
    speed        = velocity;
    maxForce     = force;
    configure();
}
void DistanceConstraint::NotifyShapeChanged(const JPH::BodyID& id, JPH::Vec3Arg delta) {
    spring.NotifyShapeChanged(id, delta);
    limits.NotifyShapeChanged(id, delta);
    ResetWarmStart();
}
void DistanceConstraint::ResetWarmStart() {
    spring.ResetWarmStart();
    limits.ResetWarmStart();
    motor.Deactivate();
}
void DistanceConstraint::SetupVelocityConstraint(float dt) {
    if (solveSpring) spring.SetupVelocityConstraint(dt);
    if (solveLimits) limits.SetupVelocityConstraint(dt);
    if (solveMotor) {
        const auto p1 = mBody1->GetCenterOfMassTransform() * spring.GetConstraintToBody1Matrix().GetTranslation();
        const auto p2 = mBody2->GetCenterOfMassTransform() * spring.GetConstraintToBody2Matrix().GetTranslation();
        axis          = JPH::Vec3(p2 - p1).NormalizedOr(JPH::Vec3::sAxisX());
        motor.CalculateConstraintProperties(*mBody1, JPH::Vec3(p1 - mBody1->GetCenterOfMassPosition()), *mBody2,
                                            JPH::Vec3(p2 - mBody2->GetCenterOfMassPosition()), axis, -speed);
    } else
        motor.Deactivate();
}
void DistanceConstraint::WarmStartVelocityConstraint(float ratio) {
    if (solveSpring) spring.WarmStartVelocityConstraint(ratio);
    if (solveLimits) limits.WarmStartVelocityConstraint(ratio);
    if (solveMotor) motor.WarmStart(*mBody1, *mBody2, axis, ratio);
}
bool DistanceConstraint::SolveVelocityConstraint(float dt) {
    bool changed = false;
    if (solveMotor) changed = motor.SolveVelocityConstraint(*mBody1, *mBody2, axis, -dt * maxForce, dt * maxForce);
    if (solveSpring) changed = spring.SolveVelocityConstraint(dt) || changed;
    if (solveLimits) changed = limits.SolveVelocityConstraint(dt) || changed;
    return changed;
}
bool DistanceConstraint::SolvePositionConstraint(float dt, float baumgarte) {
    bool changed = false;
    if (solveSpring) changed = spring.SolvePositionConstraint(dt, baumgarte);
    if (solveLimits) changed = limits.SolvePositionConstraint(dt, baumgarte) || changed;
    return changed;
}
float DistanceConstraint::GetTotalLambdaPosition() const {
    return (solveSpring ? spring.GetTotalLambdaPosition() : 0.f) +
           (solveLimits ? limits.GetTotalLambdaPosition() : 0.f) + motor.GetTotalLambda();
}
void DistanceConstraint::SaveState(JPH::StateRecorder& stream) const {
    TwoBodyConstraint::SaveState(stream);
    spring.SaveState(stream);
    limits.SaveState(stream);
    motor.SaveState(stream);
}
void DistanceConstraint::RestoreState(JPH::StateRecorder& stream) {
    TwoBodyConstraint::RestoreState(stream);
    spring.RestoreState(stream);
    limits.RestoreState(stream);
    motor.RestoreState(stream);
}
JPH::Ref<JPH::TwoBodyConstraint> createDistanceConstraint(JPH::Body& a, JPH::Body& b,
                                                          const JPH::DistanceConstraintSettings& settings) {
    return new DistanceConstraint(a, b, settings);
}
void setDistanceLength(JPH::TwoBodyConstraint& value, float length) {
    static_cast<DistanceConstraint&>(value).setLength(length);
}
void setDistanceSpring(JPH::TwoBodyConstraint& value, bool enabled, float hertz, float damping) {
    static_cast<DistanceConstraint&>(value).setSpring(enabled, hertz, damping);
}
void setDistanceLimits(JPH::TwoBodyConstraint& value, bool enabled, float lo, float hi) {
    static_cast<DistanceConstraint&>(value).setLimits(enabled, lo, hi);
}
void setDistanceMotor(JPH::TwoBodyConstraint& value, bool enabled, float speed, float force) {
    static_cast<DistanceConstraint&>(value).setMotor(enabled, speed, force);
}
float distanceImpulse(const JPH::TwoBodyConstraint& value) {
    return static_cast<const DistanceConstraint&>(value).GetTotalLambdaPosition();
}
}  // namespace eve::physics::jolt
