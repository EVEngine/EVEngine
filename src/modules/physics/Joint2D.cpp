#include "physics/Joint2D.h"

#include "physics/Body.h"
#include "physics/World.h"
#include "common/Exception.h"

#include <Box2D/Box2D.h>

#include <cmath>
#include <vector>

namespace eve::physics {
namespace {

void nonNegative(float value, const char *operation, const char *name) {
    if (!std::isfinite(value) || value < 0.f)
        throw eve::Exception("%s: %s must be finite and >= 0", operation, name);
}

void finite(float value, const char *operation, const char *name) {
    if (!std::isfinite(value)) throw eve::Exception("%s: %s must be finite", operation, name);
}

}  // namespace

Joint2D::Joint2D(World *world, Body *bodyA, Body *bodyB, b2Joint *joint,
                 PhysicsJointHandle runtimeHandle, Kind kind, int id)
    : world_(world),
      bodyA_(bodyA),
      bodyB_(bodyB),
      joint_(joint),
      runtimeHandle_(runtimeHandle),
      kind_(kind),
      id_(id) {}

Joint2D::~Joint2D() { destroy(); }

bool Joint2D::isValid() const { return joint_ != nullptr && world_ != nullptr && world_->raw() != nullptr; }

void Joint2D::invalidate() {
    if (world_) world_->forgetJoint(this);
    joint_ = nullptr;
    world_ = nullptr;
    bodyA_ = nullptr;
    bodyB_ = nullptr;
    runtimeHandle_ = PhysicsJointHandle::invalid();
    gearJoint1_ = nullptr;
    gearJoint2_ = nullptr;
}

void Joint2D::destroy() {
    if (!isValid()) {
        invalidate();
        return;
    }
    // Gear joints that depend on this joint must be destroyed first.
    if (kind_ != Kind::Gear && world_) {
        std::vector<Joint2D *> gears;
        for (Joint2D *candidate : world_->joints_) {
            if (!candidate || candidate->kind_ != Kind::Gear || !candidate->isValid()) continue;
            if (candidate->gearJoint1_ == this || candidate->gearJoint2_ == this)
                gears.push_back(candidate);
        }
        for (Joint2D *gear : gears) gear->destroy();
    }
    world_->raw()->DestroyJoint(joint_);
    if (world_) world_->forgetJoint(this);
    invalidate();
}

void Joint2D::setGearMembers(Joint2D *joint1, Joint2D *joint2) {
    gearJoint1_ = joint1;
    gearJoint2_ = joint2;
}

std::string Joint2D::getKind() const {
    switch (kind_) {
        case Kind::Distance: return "distance";
        case Kind::Revolute: return "revolute";
        case Kind::Prismatic: return "prismatic";
        case Kind::Weld: return "weld";
        case Kind::Wheel: return "wheel";
        case Kind::Motor: return "motor";
        case Kind::Gear: return "gear";
    }
    return "distance";
}

int Joint2D::getBodyAId() const { return bodyA_ ? bodyA_->getId() : -1; }
int Joint2D::getBodyBId() const { return bodyB_ ? bodyB_->getId() : -1; }

void Joint2D::setCollideConnected(bool collide) {
    if (!isValid()) return;
    // Box2D stores collideConnected on the joint; there is no setter after creation in 2.3,
    // so recreate is required. Keep a soft no-op with a clear exception for scripts.
    if (collide != joint_->GetCollideConnected())
        throw eve::Exception(
            "Joint2D.setCollideConnected: collideConnected is fixed at creation for Box2D joints");
}

bool Joint2D::getCollideConnected() const {
    return isValid() ? joint_->GetCollideConnected() : false;
}

void Joint2D::requireKind(Kind expected, const char *operation) const {
    if (!isValid()) throw eve::Exception("%s: joint destroyed", operation);
    if (kind_ != expected) throw eve::Exception("%s: incompatible joint kind", operation);
}

void Joint2D::setDistanceLength(float lengthPixels) {
    requireKind(Kind::Distance, "Joint2D.setDistanceLength");
    nonNegative(lengthPixels, "Joint2D.setDistanceLength", "length");
    static_cast<b2DistanceJoint *>(joint_)->SetLength(world_->toMeters(lengthPixels));
}
float Joint2D::getDistanceLength() const {
    requireKind(Kind::Distance, "Joint2D.getDistanceLength");
    return world_->toPixels(static_cast<b2DistanceJoint *>(joint_)->GetLength());
}
void Joint2D::setDistanceSpring(float frequencyHz, float dampingRatio) {
    requireKind(Kind::Distance, "Joint2D.setDistanceSpring");
    nonNegative(frequencyHz, "Joint2D.setDistanceSpring", "frequencyHz");
    nonNegative(dampingRatio, "Joint2D.setDistanceSpring", "dampingRatio");
    auto *distance = static_cast<b2DistanceJoint *>(joint_);
    distance->SetFrequency(frequencyHz);
    distance->SetDampingRatio(dampingRatio);
}
float Joint2D::getDistanceFrequency() const {
    requireKind(Kind::Distance, "Joint2D.getDistanceFrequency");
    return static_cast<b2DistanceJoint *>(joint_)->GetFrequency();
}
float Joint2D::getDistanceDampingRatio() const {
    requireKind(Kind::Distance, "Joint2D.getDistanceDampingRatio");
    return static_cast<b2DistanceJoint *>(joint_)->GetDampingRatio();
}

void Joint2D::setRevoluteLimits(bool enabled, float lower, float upper) {
    requireKind(Kind::Revolute, "Joint2D.setRevoluteLimits");
    finite(lower, "Joint2D.setRevoluteLimits", "lower");
    finite(upper, "Joint2D.setRevoluteLimits", "upper");
    if (lower > upper)
        throw eve::Exception("Joint2D.setRevoluteLimits: lower must be <= upper");
    auto *revolute = static_cast<b2RevoluteJoint *>(joint_);
    revolute->SetLimits(lower, upper);
    revolute->EnableLimit(enabled);
}
void Joint2D::setRevoluteMotor(bool enabled, float speed, float maxTorque) {
    requireKind(Kind::Revolute, "Joint2D.setRevoluteMotor");
    finite(speed, "Joint2D.setRevoluteMotor", "speed");
    nonNegative(maxTorque, "Joint2D.setRevoluteMotor", "maxTorque");
    auto *revolute = static_cast<b2RevoluteJoint *>(joint_);
    revolute->SetMotorSpeed(speed);
    revolute->SetMaxMotorTorque(maxTorque);
    revolute->EnableMotor(enabled);
}
float Joint2D::getRevoluteAngle() const {
    requireKind(Kind::Revolute, "Joint2D.getRevoluteAngle");
    return static_cast<b2RevoluteJoint *>(joint_)->GetJointAngle();
}
float Joint2D::getRevoluteSpeed() const {
    requireKind(Kind::Revolute, "Joint2D.getRevoluteSpeed");
    return static_cast<b2RevoluteJoint *>(joint_)->GetJointSpeed();
}
float Joint2D::getRevoluteMotorTorque() const {
    requireKind(Kind::Revolute, "Joint2D.getRevoluteMotorTorque");
    return static_cast<b2RevoluteJoint *>(joint_)->GetMotorTorque(1.f / 60.f);
}

void Joint2D::setPrismaticLimits(bool enabled, float lowerPixels, float upperPixels) {
    requireKind(Kind::Prismatic, "Joint2D.setPrismaticLimits");
    finite(lowerPixels, "Joint2D.setPrismaticLimits", "lower");
    finite(upperPixels, "Joint2D.setPrismaticLimits", "upper");
    if (lowerPixels > upperPixels)
        throw eve::Exception("Joint2D.setPrismaticLimits: lower must be <= upper");
    auto *prismatic = static_cast<b2PrismaticJoint *>(joint_);
    prismatic->SetLimits(world_->toMeters(lowerPixels), world_->toMeters(upperPixels));
    prismatic->EnableLimit(enabled);
}
void Joint2D::setPrismaticMotor(bool enabled, float speedPixels, float maxForcePixels) {
    requireKind(Kind::Prismatic, "Joint2D.setPrismaticMotor");
    finite(speedPixels, "Joint2D.setPrismaticMotor", "speed");
    nonNegative(maxForcePixels, "Joint2D.setPrismaticMotor", "maxForce");
    auto *prismatic = static_cast<b2PrismaticJoint *>(joint_);
    prismatic->SetMotorSpeed(world_->toMeters(speedPixels));
    prismatic->SetMaxMotorForce(world_->toMeters(maxForcePixels));
    prismatic->EnableMotor(enabled);
}
float Joint2D::getPrismaticTranslation() const {
    requireKind(Kind::Prismatic, "Joint2D.getPrismaticTranslation");
    return world_->toPixels(static_cast<b2PrismaticJoint *>(joint_)->GetJointTranslation());
}
float Joint2D::getPrismaticSpeed() const {
    requireKind(Kind::Prismatic, "Joint2D.getPrismaticSpeed");
    return world_->toPixels(static_cast<b2PrismaticJoint *>(joint_)->GetJointSpeed());
}

void Joint2D::setWeldSpring(float frequencyHz, float dampingRatio) {
    requireKind(Kind::Weld, "Joint2D.setWeldSpring");
    nonNegative(frequencyHz, "Joint2D.setWeldSpring", "frequencyHz");
    nonNegative(dampingRatio, "Joint2D.setWeldSpring", "dampingRatio");
    auto *weld = static_cast<b2WeldJoint *>(joint_);
    weld->SetFrequency(frequencyHz);
    weld->SetDampingRatio(dampingRatio);
}
float Joint2D::getWeldFrequency() const {
    requireKind(Kind::Weld, "Joint2D.getWeldFrequency");
    return static_cast<b2WeldJoint *>(joint_)->GetFrequency();
}
float Joint2D::getWeldDampingRatio() const {
    requireKind(Kind::Weld, "Joint2D.getWeldDampingRatio");
    return static_cast<b2WeldJoint *>(joint_)->GetDampingRatio();
}

void Joint2D::setWheelSpring(float frequencyHz, float dampingRatio) {
    requireKind(Kind::Wheel, "Joint2D.setWheelSpring");
    nonNegative(frequencyHz, "Joint2D.setWheelSpring", "frequencyHz");
    nonNegative(dampingRatio, "Joint2D.setWheelSpring", "dampingRatio");
    auto *wheel = static_cast<b2WheelJoint *>(joint_);
    wheel->SetSpringFrequencyHz(frequencyHz);
    wheel->SetSpringDampingRatio(dampingRatio);
}
void Joint2D::setWheelMotor(bool enabled, float speed, float maxTorque) {
    requireKind(Kind::Wheel, "Joint2D.setWheelMotor");
    finite(speed, "Joint2D.setWheelMotor", "speed");
    nonNegative(maxTorque, "Joint2D.setWheelMotor", "maxTorque");
    auto *wheel = static_cast<b2WheelJoint *>(joint_);
    wheel->SetMotorSpeed(speed);
    wheel->SetMaxMotorTorque(maxTorque);
    wheel->EnableMotor(enabled);
}
float Joint2D::getWheelTranslation() const {
    requireKind(Kind::Wheel, "Joint2D.getWheelTranslation");
    return world_->toPixels(static_cast<b2WheelJoint *>(joint_)->GetJointTranslation());
}
float Joint2D::getWheelSpeed() const {
    requireKind(Kind::Wheel, "Joint2D.getWheelSpeed");
    return static_cast<b2WheelJoint *>(joint_)->GetJointSpeed();
}

void Joint2D::setMotorLinearOffset(float xPixels, float yPixels) {
    requireKind(Kind::Motor, "Joint2D.setMotorLinearOffset");
    finite(xPixels, "Joint2D.setMotorLinearOffset", "x");
    finite(yPixels, "Joint2D.setMotorLinearOffset", "y");
    static_cast<b2MotorJoint *>(joint_)->SetLinearOffset(
        b2Vec2(world_->toMeters(xPixels), world_->toMeters(yPixels)));
}
void Joint2D::setMotorAngularOffset(float radians) {
    requireKind(Kind::Motor, "Joint2D.setMotorAngularOffset");
    finite(radians, "Joint2D.setMotorAngularOffset", "radians");
    static_cast<b2MotorJoint *>(joint_)->SetAngularOffset(radians);
}
void Joint2D::setMotorLimits(float maxForcePixels, float maxTorque) {
    requireKind(Kind::Motor, "Joint2D.setMotorLimits");
    nonNegative(maxForcePixels, "Joint2D.setMotorLimits", "maxForce");
    nonNegative(maxTorque, "Joint2D.setMotorLimits", "maxTorque");
    auto *motor = static_cast<b2MotorJoint *>(joint_);
    motor->SetMaxForce(world_->toMeters(maxForcePixels));
    motor->SetMaxTorque(maxTorque);
}
float Joint2D::getMotorLinearOffsetX() const {
    requireKind(Kind::Motor, "Joint2D.getMotorLinearOffsetX");
    return world_->toPixels(static_cast<b2MotorJoint *>(joint_)->GetLinearOffset().x);
}
float Joint2D::getMotorLinearOffsetY() const {
    requireKind(Kind::Motor, "Joint2D.getMotorLinearOffsetY");
    return world_->toPixels(static_cast<b2MotorJoint *>(joint_)->GetLinearOffset().y);
}
float Joint2D::getMotorAngularOffset() const {
    requireKind(Kind::Motor, "Joint2D.getMotorAngularOffset");
    return static_cast<b2MotorJoint *>(joint_)->GetAngularOffset();
}

void Joint2D::setGearRatio(float ratio) {
    requireKind(Kind::Gear, "Joint2D.setGearRatio");
    if (!std::isfinite(ratio) || std::fabs(ratio) < 1e-8f)
        throw eve::Exception("Joint2D.setGearRatio: ratio must be finite and non-zero");
    static_cast<b2GearJoint *>(joint_)->SetRatio(ratio);
}
float Joint2D::getGearRatio() const {
    requireKind(Kind::Gear, "Joint2D.getGearRatio");
    return static_cast<b2GearJoint *>(joint_)->GetRatio();
}

}  // namespace eve::physics
