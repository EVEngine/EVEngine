#include "physics/box3d/Box3DJoint.h"
#include <box3d/box3d.h>
#include <cmath>
#include "common/Exception.h"
namespace eve::physics::box3d {
namespace {


void nonNegative(float value, const char* operation, const char* name) {
    if (!std::isfinite(value) || value < 0.f) throw eve::Exception("%s: %s must be finite and >= 0", operation, name);
}

void finite(float value, const char* operation, const char* name) {
    if (!std::isfinite(value)) throw eve::Exception("%s: %s must be finite", operation, name);
}


class Box3DJoint final : public IJointBackend3D {
    template <class F>
    auto checked(const char* operationName, F&& operation) const -> decltype(operation()) {
        using R = decltype(operation());
        if (!isValid())
            return R::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "Joint is no longer valid", operationName));
        try {
            return operation();
        } catch (const eve::Exception& error) {
            return R::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(), operationName));
        }
    }

public:
    using Kind = JointKind3D;
    Box3DJoint(b3JointId id, Kind kind) : jointId_(id), kind_(kind) {}
    ~Box3DJoint() override { destroy(); }
    void bindOwner(Joint3D& owner) noexcept override {
        if (isValid()) b3Joint_SetUserData(jointId_, &owner);
    }
    bool isValid() const noexcept override { return b3Joint_IsValid(jointId_); }
    void destroy() noexcept override {
        if (isValid()) b3DestroyJoint(jointId_, true);
        jointId_ = {};
    }
    void invalidate() noexcept override {
        if (isValid()) b3Joint_SetUserData(jointId_, nullptr);
        jointId_ = {};
    }
    eve::Result<JointFrames3D> localFrames() const override {
        return checked("physics.joint3d.localFrames", [&] {
            const auto a = b3Joint_GetLocalFrameA(jointId_);
            const auto b = b3Joint_GetLocalFrameB(jointId_);
            return eve::Result<JointFrames3D>::success({{static_cast<float>(a.p.x), static_cast<float>(a.p.y),
                                                         static_cast<float>(a.p.z), a.q.v.x, a.q.v.y, a.q.v.z, a.q.s},
                                                        {static_cast<float>(b.p.x), static_cast<float>(b.p.y),
                                                         static_cast<float>(b.p.z), b.q.v.x, b.q.v.y, b.q.v.z, b.q.s}});
        });
    }
    eve::Result<void> setCollideConnected(bool collide) override {
        return checked("physics.joint3d.setCollideConnected", [&] {
            b3Joint_SetCollideConnected(jointId_, collide);
            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> getCollideConnected() const override {
        return checked("physics.joint3d.getCollideConnected", [&] {
            return eve::Result<bool>::success(isValid() ? b3Joint_GetCollideConnected(jointId_) : false);
        });
    }
    eve::Result<void> setForceThreshold(float threshold) override {
        return checked("physics.joint3d.setForceThreshold", [&] {
            nonNegative(threshold, "Joint3D.setForceThreshold", "threshold");
            b3Joint_SetForceThreshold(jointId_, threshold);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getForceThreshold() const override {
        return checked("physics.joint3d.getForceThreshold", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetForceThreshold(jointId_) : 0.f);
        });
    }
    eve::Result<void> setTorqueThreshold(float threshold) override {
        return checked("physics.joint3d.setTorqueThreshold", [&] {
            nonNegative(threshold, "Joint3D.setTorqueThreshold", "threshold");
            b3Joint_SetTorqueThreshold(jointId_, threshold);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getTorqueThreshold() const override {
        return checked("physics.joint3d.getTorqueThreshold", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetTorqueThreshold(jointId_) : 0.f);
        });
    }

    eve::Result<float> getConstraintForceX() const override {
        return checked("physics.joint3d.getConstraintForceX", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetConstraintForce(jointId_).x : 0.f);
        });
    }
    eve::Result<float> getConstraintForceY() const override {
        return checked("physics.joint3d.getConstraintForceY", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetConstraintForce(jointId_).y : 0.f);
        });
    }
    eve::Result<float> getConstraintForceZ() const override {
        return checked("physics.joint3d.getConstraintForceZ", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetConstraintForce(jointId_).z : 0.f);
        });
    }
    eve::Result<float> getConstraintTorqueX() const override {
        return checked("physics.joint3d.getConstraintTorqueX", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetConstraintTorque(jointId_).x : 0.f);
        });
    }
    eve::Result<float> getConstraintTorqueY() const override {
        return checked("physics.joint3d.getConstraintTorqueY", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetConstraintTorque(jointId_).y : 0.f);
        });
    }
    eve::Result<float> getConstraintTorqueZ() const override {
        return checked("physics.joint3d.getConstraintTorqueZ", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetConstraintTorque(jointId_).z : 0.f);
        });
    }
    eve::Result<float> getLinearSeparation() const override {
        return checked("physics.joint3d.getLinearSeparation", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetLinearSeparation(jointId_) : 0.f);
        });
    }
    eve::Result<float> getAngularSeparation() const override {
        return checked("physics.joint3d.getAngularSeparation", [&] {
            return eve::Result<float>::success(isValid() ? b3Joint_GetAngularSeparation(jointId_) : 0.f);
        });
    }
    eve::Result<void> setDistanceLength(float length) override {
        return checked("physics.joint3d.setDistanceLength", [&] {
            requireKind(Kind::Distance, "Joint3D.setDistanceLength");
            nonNegative(length, "Joint3D.setDistanceLength", "length");
            b3DistanceJoint_SetLength(jointId_, length);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getDistanceLength() const override {
        return checked("physics.joint3d.getDistanceLength", [&] {
            requireKind(Kind::Distance, "Joint3D.getDistanceLength");
            return eve::Result<float>::success(b3DistanceJoint_GetLength(jointId_));
        });
    }
    eve::Result<float> getDistanceCurrentLength() const override {
        return checked("physics.joint3d.getDistanceCurrentLength", [&] {
            requireKind(Kind::Distance, "Joint3D.getDistanceCurrentLength");
            return eve::Result<float>::success(b3DistanceJoint_GetCurrentLength(jointId_));
        });
    }
    eve::Result<void> setDistanceSpring(bool enabled, float hertz, float dampingRatio) override {
        return checked("physics.joint3d.setDistanceSpring", [&] {
            requireKind(Kind::Distance, "Joint3D.setDistanceSpring");
            nonNegative(hertz, "Joint3D.setDistanceSpring", "hertz");
            nonNegative(dampingRatio, "Joint3D.setDistanceSpring", "dampingRatio");
            b3DistanceJoint_SetSpringHertz(jointId_, hertz);
            b3DistanceJoint_SetSpringDampingRatio(jointId_, dampingRatio);
            b3DistanceJoint_EnableSpring(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setDistanceLimits(bool enabled, float minimum, float maximum) override {
        return checked("physics.joint3d.setDistanceLimits", [&] {
            requireKind(Kind::Distance, "Joint3D.setDistanceLimits");
            nonNegative(minimum, "Joint3D.setDistanceLimits", "minimum");
            nonNegative(maximum, "Joint3D.setDistanceLimits", "maximum");
            if (minimum > maximum) throw eve::Exception("Joint3D.setDistanceLimits: minimum must be <= maximum");
            b3DistanceJoint_SetLengthRange(jointId_, minimum, maximum);
            b3DistanceJoint_EnableLimit(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setDistanceMotor(bool enabled, float speed, float maxForce) override {
        return checked("physics.joint3d.setDistanceMotor", [&] {
            requireKind(Kind::Distance, "Joint3D.setDistanceMotor");
            finite(speed, "Joint3D.setDistanceMotor", "speed");
            nonNegative(maxForce, "Joint3D.setDistanceMotor", "maxForce");
            b3DistanceJoint_SetMotorSpeed(jointId_, speed);
            b3DistanceJoint_SetMaxMotorForce(jointId_, maxForce);
            b3DistanceJoint_EnableMotor(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setRevoluteSpring(bool enabled, float targetAngle, float hertz, float dampingRatio) override {
        return checked("physics.joint3d.setRevoluteSpring", [&] {
            requireKind(Kind::Revolute, "Joint3D.setRevoluteSpring");
            finite(targetAngle, "Joint3D.setRevoluteSpring", "targetAngle");
            nonNegative(hertz, "Joint3D.setRevoluteSpring", "hertz");
            nonNegative(dampingRatio, "Joint3D.setRevoluteSpring", "dampingRatio");
            b3RevoluteJoint_SetTargetAngle(jointId_, targetAngle);
            b3RevoluteJoint_SetSpringHertz(jointId_, hertz);
            b3RevoluteJoint_SetSpringDampingRatio(jointId_, dampingRatio);
            b3RevoluteJoint_EnableSpring(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setRevoluteLimits(bool enabled, float lower, float upper) override {
        return checked("physics.joint3d.setRevoluteLimits", [&] {
            requireKind(Kind::Revolute, "Joint3D.setRevoluteLimits");
            finite(lower, "Joint3D.setRevoluteLimits", "lower");
            finite(upper, "Joint3D.setRevoluteLimits", "upper");
            if (lower > upper) throw eve::Exception("Joint3D.setRevoluteLimits: lower must be <= upper");
            b3RevoluteJoint_SetLimits(jointId_, lower, upper);
            b3RevoluteJoint_EnableLimit(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setRevoluteMotor(bool enabled, float speed, float maxTorque) override {
        return checked("physics.joint3d.setRevoluteMotor", [&] {
            requireKind(Kind::Revolute, "Joint3D.setRevoluteMotor");
            finite(speed, "Joint3D.setRevoluteMotor", "speed");
            nonNegative(maxTorque, "Joint3D.setRevoluteMotor", "maxTorque");
            b3RevoluteJoint_SetMotorSpeed(jointId_, speed);
            b3RevoluteJoint_SetMaxMotorTorque(jointId_, maxTorque);
            b3RevoluteJoint_EnableMotor(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getRevoluteAngle() const override {
        return checked("physics.joint3d.getRevoluteAngle", [&] {
            requireKind(Kind::Revolute, "Joint3D.getRevoluteAngle");
            return eve::Result<float>::success(b3RevoluteJoint_GetAngle(jointId_));
        });
    }
    eve::Result<float> getRevoluteMotorTorque() const override {
        return checked("physics.joint3d.getRevoluteMotorTorque", [&] {
            requireKind(Kind::Revolute, "Joint3D.getRevoluteMotorTorque");
            return eve::Result<float>::success(b3RevoluteJoint_GetMotorTorque(jointId_));
        });
    }
    eve::Result<void> setPrismaticSpring(bool enabled, float targetTranslation, float hertz,
                                         float dampingRatio) override {
        return checked("physics.joint3d.setPrismaticSpring", [&] {
            requireKind(Kind::Prismatic, "Joint3D.setPrismaticSpring");
            finite(targetTranslation, "Joint3D.setPrismaticSpring", "targetTranslation");
            nonNegative(hertz, "Joint3D.setPrismaticSpring", "hertz");
            nonNegative(dampingRatio, "Joint3D.setPrismaticSpring", "dampingRatio");
            b3PrismaticJoint_SetTargetTranslation(jointId_, targetTranslation);
            b3PrismaticJoint_SetSpringHertz(jointId_, hertz);
            b3PrismaticJoint_SetSpringDampingRatio(jointId_, dampingRatio);
            b3PrismaticJoint_EnableSpring(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setPrismaticLimits(bool enabled, float lower, float upper) override {
        return checked("physics.joint3d.setPrismaticLimits", [&] {
            requireKind(Kind::Prismatic, "Joint3D.setPrismaticLimits");
            finite(lower, "Joint3D.setPrismaticLimits", "lower");
            finite(upper, "Joint3D.setPrismaticLimits", "upper");
            if (lower > upper) throw eve::Exception("Joint3D.setPrismaticLimits: lower must be <= upper");
            b3PrismaticJoint_SetLimits(jointId_, lower, upper);
            b3PrismaticJoint_EnableLimit(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setPrismaticMotor(bool enabled, float speed, float maxForce) override {
        return checked("physics.joint3d.setPrismaticMotor", [&] {
            requireKind(Kind::Prismatic, "Joint3D.setPrismaticMotor");
            finite(speed, "Joint3D.setPrismaticMotor", "speed");
            nonNegative(maxForce, "Joint3D.setPrismaticMotor", "maxForce");
            b3PrismaticJoint_SetMotorSpeed(jointId_, speed);
            b3PrismaticJoint_SetMaxMotorForce(jointId_, maxForce);
            b3PrismaticJoint_EnableMotor(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getPrismaticTranslation() const override {
        return checked("physics.joint3d.getPrismaticTranslation", [&] {
            requireKind(Kind::Prismatic, "Joint3D.getPrismaticTranslation");
            return eve::Result<float>::success(b3PrismaticJoint_GetTranslation(jointId_));
        });
    }
    eve::Result<float> getPrismaticSpeed() const override {
        return checked("physics.joint3d.getPrismaticSpeed", [&] {
            requireKind(Kind::Prismatic, "Joint3D.getPrismaticSpeed");
            return eve::Result<float>::success(b3PrismaticJoint_GetSpeed(jointId_));
        });
    }
    eve::Result<float> getPrismaticMotorForce() const override {
        return checked("physics.joint3d.getPrismaticMotorForce", [&] {
            requireKind(Kind::Prismatic, "Joint3D.getPrismaticMotorForce");
            return eve::Result<float>::success(b3PrismaticJoint_GetMotorForce(jointId_));
        });
    }
    eve::Result<void> setSphericalConeLimit(bool enabled, float angle) override {
        return checked("physics.joint3d.setSphericalConeLimit", [&] {
            requireKind(Kind::Spherical, "Joint3D.setSphericalConeLimit");
            finite(angle, "Joint3D.setSphericalConeLimit", "angle");
            if (angle < 0.f || angle > B3_PI)
                throw eve::Exception("Joint3D.setSphericalConeLimit: angle must be in [0, pi]");
            b3SphericalJoint_SetConeLimit(jointId_, angle);
            b3SphericalJoint_EnableConeLimit(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setSphericalTwistLimits(bool enabled, float lower, float upper) override {
        return checked("physics.joint3d.setSphericalTwistLimits", [&] {
            requireKind(Kind::Spherical, "Joint3D.setSphericalTwistLimits");
            finite(lower, "Joint3D.setSphericalTwistLimits", "lower");
            finite(upper, "Joint3D.setSphericalTwistLimits", "upper");
            if (lower > upper || lower < -0.99f * B3_PI || upper > 0.99f * B3_PI)
                throw eve::Exception("Joint3D.setSphericalTwistLimits: require -0.99*pi <= lower <= upper <= 0.99*pi");
            b3SphericalJoint_SetTwistLimits(jointId_, lower, upper);
            b3SphericalJoint_EnableTwistLimit(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setSphericalMotor(bool enabled, float velocityX, float velocityY, float velocityZ,
                                        float maxTorque) override {
        return checked("physics.joint3d.setSphericalMotor", [&] {
            requireKind(Kind::Spherical, "Joint3D.setSphericalMotor");
            finite(velocityX, "Joint3D.setSphericalMotor", "velocityX");
            finite(velocityY, "Joint3D.setSphericalMotor", "velocityY");
            finite(velocityZ, "Joint3D.setSphericalMotor", "velocityZ");
            nonNegative(maxTorque, "Joint3D.setSphericalMotor", "maxTorque");
            b3SphericalJoint_SetMotorVelocity(jointId_, {velocityX, velocityY, velocityZ});
            b3SphericalJoint_SetMaxMotorTorque(jointId_, maxTorque);
            b3SphericalJoint_EnableMotor(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getSphericalConeAngle() const override {
        return checked("physics.joint3d.getSphericalConeAngle", [&] {
            requireKind(Kind::Spherical, "Joint3D.getSphericalConeAngle");
            return eve::Result<float>::success(b3SphericalJoint_GetConeAngle(jointId_));
        });
    }
    eve::Result<float> getSphericalTwistAngle() const override {
        return checked("physics.joint3d.getSphericalTwistAngle", [&] {
            requireKind(Kind::Spherical, "Joint3D.getSphericalTwistAngle");
            return eve::Result<float>::success(b3SphericalJoint_GetTwistAngle(jointId_));
        });
    }
    eve::Result<void> setWheelSuspension(bool enabled, float hertz, float dampingRatio) override {
        return checked("physics.joint3d.setWheelSuspension", [&] {
            requireKind(Kind::Wheel, "Joint3D.setWheelSuspension");
            nonNegative(hertz, "Joint3D.setWheelSuspension", "hertz");
            nonNegative(dampingRatio, "Joint3D.setWheelSuspension", "dampingRatio");
            b3WheelJoint_SetSuspensionHertz(jointId_, hertz);
            b3WheelJoint_SetSuspensionDampingRatio(jointId_, dampingRatio);
            b3WheelJoint_EnableSuspension(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setWheelSuspensionLimits(bool enabled, float lower, float upper) override {
        return checked("physics.joint3d.setWheelSuspensionLimits", [&] {
            requireKind(Kind::Wheel, "Joint3D.setWheelSuspensionLimits");
            finite(lower, "Joint3D.setWheelSuspensionLimits", "lower");
            finite(upper, "Joint3D.setWheelSuspensionLimits", "upper");
            if (lower > upper) throw eve::Exception("Joint3D.setWheelSuspensionLimits: lower must be <= upper");
            b3WheelJoint_SetSuspensionLimits(jointId_, lower, upper);
            b3WheelJoint_EnableSuspensionLimit(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setWheelSpinMotor(bool enabled, float speed, float maxTorque) override {
        return checked("physics.joint3d.setWheelSpinMotor", [&] {
            requireKind(Kind::Wheel, "Joint3D.setWheelSpinMotor");
            finite(speed, "Joint3D.setWheelSpinMotor", "speed");
            nonNegative(maxTorque, "Joint3D.setWheelSpinMotor", "maxTorque");
            b3WheelJoint_SetSpinMotorSpeed(jointId_, speed);
            b3WheelJoint_SetMaxSpinTorque(jointId_, maxTorque);
            b3WheelJoint_EnableSpinMotor(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setWheelSteering(bool enabled, float targetAngle, float hertz, float dampingRatio,
                                       float maxTorque) override {
        return checked("physics.joint3d.setWheelSteering", [&] {
            requireKind(Kind::Wheel, "Joint3D.setWheelSteering");
            finite(targetAngle, "Joint3D.setWheelSteering", "targetAngle");
            nonNegative(hertz, "Joint3D.setWheelSteering", "hertz");
            nonNegative(dampingRatio, "Joint3D.setWheelSteering", "dampingRatio");
            nonNegative(maxTorque, "Joint3D.setWheelSteering", "maxTorque");
            b3WheelJoint_SetTargetSteeringAngle(jointId_, targetAngle);
            b3WheelJoint_SetSteeringHertz(jointId_, hertz);
            b3WheelJoint_SetSteeringDampingRatio(jointId_, dampingRatio);
            b3WheelJoint_SetMaxSteeringTorque(jointId_, maxTorque);
            b3WheelJoint_EnableSteering(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setWheelSteeringLimits(bool enabled, float lower, float upper) override {
        return checked("physics.joint3d.setWheelSteeringLimits", [&] {
            requireKind(Kind::Wheel, "Joint3D.setWheelSteeringLimits");
            finite(lower, "Joint3D.setWheelSteeringLimits", "lower");
            finite(upper, "Joint3D.setWheelSteeringLimits", "upper");
            if (lower > upper) throw eve::Exception("Joint3D.setWheelSteeringLimits: lower must be <= upper");
            b3WheelJoint_SetSteeringLimits(jointId_, lower, upper);
            b3WheelJoint_EnableSteeringLimit(jointId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getWheelSpinSpeed() const override {
        return checked("physics.joint3d.getWheelSpinSpeed", [&] {
            requireKind(Kind::Wheel, "Joint3D.getWheelSpinSpeed");
            return eve::Result<float>::success(b3WheelJoint_GetSpinSpeed(jointId_));
        });
    }
    eve::Result<float> getWheelSpinTorque() const override {
        return checked("physics.joint3d.getWheelSpinTorque", [&] {
            requireKind(Kind::Wheel, "Joint3D.getWheelSpinTorque");
            return eve::Result<float>::success(b3WheelJoint_GetSpinTorque(jointId_));
        });
    }
    eve::Result<float> getWheelSteeringAngle() const override {
        return checked("physics.joint3d.getWheelSteeringAngle", [&] {
            requireKind(Kind::Wheel, "Joint3D.getWheelSteeringAngle");
            return eve::Result<float>::success(b3WheelJoint_GetSteeringAngle(jointId_));
        });
    }
    eve::Result<float> getWheelSteeringTorque() const override {
        return checked("physics.joint3d.getWheelSteeringTorque", [&] {
            requireKind(Kind::Wheel, "Joint3D.getWheelSteeringTorque");
            return eve::Result<float>::success(b3WheelJoint_GetSteeringTorque(jointId_));
        });
    }
    eve::Result<void> setWeldLinearSpring(float hertz, float dampingRatio) override {
        return checked("physics.joint3d.setWeldLinearSpring", [&] {
            requireKind(Kind::Weld, "Joint3D.setWeldLinearSpring");
            nonNegative(hertz, "Joint3D.setWeldLinearSpring", "hertz");
            nonNegative(dampingRatio, "Joint3D.setWeldLinearSpring", "dampingRatio");
            b3WeldJoint_SetLinearHertz(jointId_, hertz);
            b3WeldJoint_SetLinearDampingRatio(jointId_, dampingRatio);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setWeldAngularSpring(float hertz, float dampingRatio) override {
        return checked("physics.joint3d.setWeldAngularSpring", [&] {
            requireKind(Kind::Weld, "Joint3D.setWeldAngularSpring");
            nonNegative(hertz, "Joint3D.setWeldAngularSpring", "hertz");
            nonNegative(dampingRatio, "Joint3D.setWeldAngularSpring", "dampingRatio");
            b3WeldJoint_SetAngularHertz(jointId_, hertz);
            b3WeldJoint_SetAngularDampingRatio(jointId_, dampingRatio);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getWeldLinearHertz() const override {
        return checked("physics.joint3d.getWeldLinearHertz", [&] {
            requireKind(Kind::Weld, "Joint3D.getWeldLinearHertz");
            return eve::Result<float>::success(b3WeldJoint_GetLinearHertz(jointId_));
        });
    }
    eve::Result<float> getWeldLinearDampingRatio() const override {
        return checked("physics.joint3d.getWeldLinearDampingRatio", [&] {
            requireKind(Kind::Weld, "Joint3D.getWeldLinearDampingRatio");
            return eve::Result<float>::success(b3WeldJoint_GetLinearDampingRatio(jointId_));
        });
    }
    eve::Result<float> getWeldAngularHertz() const override {
        return checked("physics.joint3d.getWeldAngularHertz", [&] {
            requireKind(Kind::Weld, "Joint3D.getWeldAngularHertz");
            return eve::Result<float>::success(b3WeldJoint_GetAngularHertz(jointId_));
        });
    }
    eve::Result<float> getWeldAngularDampingRatio() const override {
        return checked("physics.joint3d.getWeldAngularDampingRatio", [&] {
            requireKind(Kind::Weld, "Joint3D.getWeldAngularDampingRatio");
            return eve::Result<float>::success(b3WeldJoint_GetAngularDampingRatio(jointId_));
        });
    }
    eve::Result<void> setMotorLinearVelocity(float vx, float vy, float vz) override {
        return checked("physics.joint3d.setMotorLinearVelocity", [&] {
            requireKind(Kind::Motor, "Joint3D.setMotorLinearVelocity");
            finite(vx, "Joint3D.setMotorLinearVelocity", "vx");
            finite(vy, "Joint3D.setMotorLinearVelocity", "vy");
            finite(vz, "Joint3D.setMotorLinearVelocity", "vz");
            b3MotorJoint_SetLinearVelocity(jointId_, b3Vec3{vx, vy, vz});

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setMotorAngularVelocity(float wx, float wy, float wz) override {
        return checked("physics.joint3d.setMotorAngularVelocity", [&] {
            requireKind(Kind::Motor, "Joint3D.setMotorAngularVelocity");
            finite(wx, "Joint3D.setMotorAngularVelocity", "wx");
            finite(wy, "Joint3D.setMotorAngularVelocity", "wy");
            finite(wz, "Joint3D.setMotorAngularVelocity", "wz");
            b3MotorJoint_SetAngularVelocity(jointId_, b3Vec3{wx, wy, wz});

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setMotorVelocityLimits(float maxForce, float maxTorque) override {
        return checked("physics.joint3d.setMotorVelocityLimits", [&] {
            requireKind(Kind::Motor, "Joint3D.setMotorVelocityLimits");
            nonNegative(maxForce, "Joint3D.setMotorVelocityLimits", "maxForce");
            nonNegative(maxTorque, "Joint3D.setMotorVelocityLimits", "maxTorque");
            b3MotorJoint_SetMaxVelocityForce(jointId_, maxForce);
            b3MotorJoint_SetMaxVelocityTorque(jointId_, maxTorque);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setMotorLinearSpring(float hertz, float dampingRatio, float maxForce) override {
        return checked("physics.joint3d.setMotorLinearSpring", [&] {
            requireKind(Kind::Motor, "Joint3D.setMotorLinearSpring");
            nonNegative(hertz, "Joint3D.setMotorLinearSpring", "hertz");
            nonNegative(dampingRatio, "Joint3D.setMotorLinearSpring", "dampingRatio");
            nonNegative(maxForce, "Joint3D.setMotorLinearSpring", "maxForce");
            b3MotorJoint_SetLinearHertz(jointId_, hertz);
            b3MotorJoint_SetLinearDampingRatio(jointId_, dampingRatio);
            b3MotorJoint_SetMaxSpringForce(jointId_, maxForce);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setMotorAngularSpring(float hertz, float dampingRatio, float maxTorque) override {
        return checked("physics.joint3d.setMotorAngularSpring", [&] {
            requireKind(Kind::Motor, "Joint3D.setMotorAngularSpring");
            nonNegative(hertz, "Joint3D.setMotorAngularSpring", "hertz");
            nonNegative(dampingRatio, "Joint3D.setMotorAngularSpring", "dampingRatio");
            nonNegative(maxTorque, "Joint3D.setMotorAngularSpring", "maxTorque");
            b3MotorJoint_SetAngularHertz(jointId_, hertz);
            b3MotorJoint_SetAngularDampingRatio(jointId_, dampingRatio);
            b3MotorJoint_SetMaxSpringTorque(jointId_, maxTorque);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getMotorLinearVelocityX() const override {
        return checked("physics.joint3d.getMotorLinearVelocityX", [&] {
            requireKind(Kind::Motor, "Joint3D.getMotorLinearVelocityX");
            return eve::Result<float>::success(b3MotorJoint_GetLinearVelocity(jointId_).x);
        });
    }
    eve::Result<float> getMotorLinearVelocityY() const override {
        return checked("physics.joint3d.getMotorLinearVelocityY", [&] {
            requireKind(Kind::Motor, "Joint3D.getMotorLinearVelocityY");
            return eve::Result<float>::success(b3MotorJoint_GetLinearVelocity(jointId_).y);
        });
    }
    eve::Result<float> getMotorLinearVelocityZ() const override {
        return checked("physics.joint3d.getMotorLinearVelocityZ", [&] {
            requireKind(Kind::Motor, "Joint3D.getMotorLinearVelocityZ");
            return eve::Result<float>::success(b3MotorJoint_GetLinearVelocity(jointId_).z);
        });
    }
    eve::Result<float> getMotorAngularVelocityX() const override {
        return checked("physics.joint3d.getMotorAngularVelocityX", [&] {
            requireKind(Kind::Motor, "Joint3D.getMotorAngularVelocityX");
            return eve::Result<float>::success(b3MotorJoint_GetAngularVelocity(jointId_).x);
        });
    }
    eve::Result<float> getMotorAngularVelocityY() const override {
        return checked("physics.joint3d.getMotorAngularVelocityY", [&] {
            requireKind(Kind::Motor, "Joint3D.getMotorAngularVelocityY");
            return eve::Result<float>::success(b3MotorJoint_GetAngularVelocity(jointId_).y);
        });
    }
    eve::Result<float> getMotorAngularVelocityZ() const override {
        return checked("physics.joint3d.getMotorAngularVelocityZ", [&] {
            requireKind(Kind::Motor, "Joint3D.getMotorAngularVelocityZ");
            return eve::Result<float>::success(b3MotorJoint_GetAngularVelocity(jointId_).z);
        });
    }
    eve::Result<void> setParallelSpring(float hertz, float dampingRatio, float maxTorque) override {
        return checked("physics.joint3d.setParallelSpring", [&] {
            requireKind(Kind::Parallel, "Joint3D.setParallelSpring");
            nonNegative(hertz, "Joint3D.setParallelSpring", "hertz");
            nonNegative(dampingRatio, "Joint3D.setParallelSpring", "dampingRatio");
            nonNegative(maxTorque, "Joint3D.setParallelSpring", "maxTorque");
            b3ParallelJoint_SetSpringHertz(jointId_, hertz);
            b3ParallelJoint_SetSpringDampingRatio(jointId_, dampingRatio);
            b3ParallelJoint_SetMaxTorque(jointId_, maxTorque);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getParallelHertz() const override {
        return checked("physics.joint3d.getParallelHertz", [&] {
            requireKind(Kind::Parallel, "Joint3D.getParallelHertz");
            return eve::Result<float>::success(b3ParallelJoint_GetSpringHertz(jointId_));
        });
    }
    eve::Result<float> getParallelDampingRatio() const override {
        return checked("physics.joint3d.getParallelDampingRatio", [&] {
            requireKind(Kind::Parallel, "Joint3D.getParallelDampingRatio");
            return eve::Result<float>::success(b3ParallelJoint_GetSpringDampingRatio(jointId_));
        });
    }
    eve::Result<float> getParallelMaxTorque() const override {
        return checked("physics.joint3d.getParallelMaxTorque", [&] {
            requireKind(Kind::Parallel, "Joint3D.getParallelMaxTorque");
            return eve::Result<float>::success(b3ParallelJoint_GetMaxTorque(jointId_));
        });
    }

private:
    void requireKind(Kind expected, const char* operation) const {
        if (kind_ != expected) throw eve::Exception("%s: incompatible joint kind", operation);
    }
    b3JointId jointId_{};
    Kind      kind_;
};
}  // namespace
std::unique_ptr<IJointBackend3D> adoptJoint(b3JointId id, JointKind3D kind) {
    return std::make_unique<Box3DJoint>(id, kind);
}
}  // namespace eve::physics::box3d
