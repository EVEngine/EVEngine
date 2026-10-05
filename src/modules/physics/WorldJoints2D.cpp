#include "physics/World.h"

#include "physics/Body.h"
#include "physics/Joint2D.h"
#include "physics/Mechanism2D.h"
#include "common/Exception.h"

#include <Box2D/Box2D.h>

#include <cmath>

namespace eve::physics {
namespace {

void requireFiniteParams(const float *values, int count, const char *operation) {
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i]))
            throw eve::Exception("%s: parameters must be finite", operation);
    }
}

void requireDistinctBodies(World *world, Body *bodyA, Body *bodyB, const char *operation) {
    if (!world || !world->isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != world || bodyB->getWorld() != world || bodyA == bodyB)
        throw eve::Exception("%s: bodies must be distinct and belong to this world", operation);
}

b2Vec2 normalizedAxisPixels(float x, float y, const char *operation) {
    const float length = std::sqrt(x * x + y * y);
    if (length <= 1e-8f) throw eve::Exception("%s: axis length must be > 0", operation);
    return b2Vec2(x / length, y / length);
}

Joint2D *adoptJoint(World *world, Body *bodyA, Body *bodyB, b2Joint *raw, Joint2D::Kind kind) {
    const PhysicsJointHandle runtimeHandle = world->nextJointRuntimeHandle();
    auto *joint = new Joint2D(world, bodyA, bodyB, raw, runtimeHandle, kind, world->nextJointId());
    raw->SetUserData(joint);
    world->joints_.insert(joint);
    world->jointHandles_[runtimeHandle] = joint;
    return joint;
}

}  // namespace

void World::forgetJoint(Joint2D *joint) {
    if (!joint) return;
    joints_.erase(joint);
    if (joint->runtimeHandle().isValid()) jointHandles_.erase(joint->runtimeHandle());
    for (auto it = jointHandles_.begin(); it != jointHandles_.end();) {
        if (it->second == joint)
            it = jointHandles_.erase(it);
        else
            ++it;
    }
}

void World::forgetMechanism(Mechanism2D *mechanism) {
    if (!mechanism) return;
    mechanisms_.erase(mechanism);
}

int World::nextJointId() { return nextJointId_++; }

int World::nextMechanismId() { return nextMechanismId_++; }

PhysicsJointHandle World::nextJointRuntimeHandle() {
    if (nextJointHandleIndex_ == PhysicsJointHandle::invalidIndex)
        throw eve::Exception("World.newJoint: process-local joint handle space exhausted");
    return PhysicsJointHandle(nextJointHandleIndex_++, 1u);
}

Joint2D *World::findJoint(PhysicsJointHandle handle) const {
    if (!isValid() || handle.isInvalid()) return nullptr;
    const auto found = jointHandles_.find(handle);
    if (found == jointHandles_.end() || !found->second || !found->second->isValid()) return nullptr;
    return found->second;
}

Joint2D *World::newDistanceJoint(Body *bodyA, Body *bodyB, float anchorAX, float anchorAY,
                                 float anchorBX, float anchorBY, float lengthPixels,
                                 bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World.newDistanceJoint");
    const float values[] = {anchorAX, anchorAY, anchorBX, anchorBY, lengthPixels};
    requireFiniteParams(values, 5, "World.newDistanceJoint");
    if (lengthPixels < 0.f)
        throw eve::Exception("World.newDistanceJoint: length must be >= 0");
    b2DistanceJointDef def;
    def.bodyA = bodyA->raw();
    def.bodyB = bodyB->raw();
    def.collideConnected = collideConnected;
    def.localAnchorA = bodyA->raw()->GetLocalPoint(b2Vec2(toMeters(anchorAX), toMeters(anchorAY)));
    def.localAnchorB = bodyB->raw()->GetLocalPoint(b2Vec2(toMeters(anchorBX), toMeters(anchorBY)));
    def.length = toMeters(lengthPixels);
    return adoptJoint(this, bodyA, bodyB, world_->CreateJoint(&def), Joint2D::Kind::Distance);
}

Joint2D *World::newRevoluteJoint(Body *bodyA, Body *bodyB, float anchorX, float anchorY,
                                 bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World.newRevoluteJoint");
    const float values[] = {anchorX, anchorY};
    requireFiniteParams(values, 2, "World.newRevoluteJoint");
    b2RevoluteJointDef def;
    def.Initialize(bodyA->raw(), bodyB->raw(), b2Vec2(toMeters(anchorX), toMeters(anchorY)));
    def.collideConnected = collideConnected;
    return adoptJoint(this, bodyA, bodyB, world_->CreateJoint(&def), Joint2D::Kind::Revolute);
}

Joint2D *World::newPrismaticJoint(Body *bodyA, Body *bodyB, float anchorX, float anchorY,
                                  float axisX, float axisY, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World.newPrismaticJoint");
    const float values[] = {anchorX, anchorY, axisX, axisY};
    requireFiniteParams(values, 4, "World.newPrismaticJoint");
    const b2Vec2 axis = normalizedAxisPixels(axisX, axisY, "World.newPrismaticJoint");
    b2PrismaticJointDef def;
    def.Initialize(bodyA->raw(), bodyB->raw(), b2Vec2(toMeters(anchorX), toMeters(anchorY)), axis);
    def.collideConnected = collideConnected;
    return adoptJoint(this, bodyA, bodyB, world_->CreateJoint(&def), Joint2D::Kind::Prismatic);
}

Joint2D *World::newWeldJoint(Body *bodyA, Body *bodyB, float anchorX, float anchorY,
                             bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World.newWeldJoint");
    const float values[] = {anchorX, anchorY};
    requireFiniteParams(values, 2, "World.newWeldJoint");
    b2WeldJointDef def;
    def.Initialize(bodyA->raw(), bodyB->raw(), b2Vec2(toMeters(anchorX), toMeters(anchorY)));
    def.collideConnected = collideConnected;
    return adoptJoint(this, bodyA, bodyB, world_->CreateJoint(&def), Joint2D::Kind::Weld);
}

Joint2D *World::newWheelJoint(Body *bodyA, Body *bodyB, float anchorX, float anchorY, float axisX,
                              float axisY, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World.newWheelJoint");
    const float values[] = {anchorX, anchorY, axisX, axisY};
    requireFiniteParams(values, 4, "World.newWheelJoint");
    const b2Vec2 axis = normalizedAxisPixels(axisX, axisY, "World.newWheelJoint");
    b2WheelJointDef def;
    def.Initialize(bodyA->raw(), bodyB->raw(), b2Vec2(toMeters(anchorX), toMeters(anchorY)), axis);
    def.collideConnected = collideConnected;
    return adoptJoint(this, bodyA, bodyB, world_->CreateJoint(&def), Joint2D::Kind::Wheel);
}

Joint2D *World::newMotorJoint(Body *bodyA, Body *bodyB, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World.newMotorJoint");
    b2MotorJointDef def;
    def.Initialize(bodyA->raw(), bodyB->raw());
    def.collideConnected = collideConnected;
    return adoptJoint(this, bodyA, bodyB, world_->CreateJoint(&def), Joint2D::Kind::Motor);
}

Joint2D *World::newGearJoint(Joint2D *joint1, Joint2D *joint2, float ratio) {
    if (!isValid() || !joint1 || !joint2 || !joint1->isValid() || !joint2->isValid() ||
        joint1->getWorld() != this || joint2->getWorld() != this || joint1 == joint2)
        throw eve::Exception("World.newGearJoint: joints must be distinct and belong to this world");
    if ((joint1->getKind() != "revolute" && joint1->getKind() != "prismatic") ||
        (joint2->getKind() != "revolute" && joint2->getKind() != "prismatic"))
        throw eve::Exception("World.newGearJoint: both joints must be revolute or prismatic");
    if (!std::isfinite(ratio) || std::fabs(ratio) < 1e-8f)
        throw eve::Exception("World.newGearJoint: ratio must be finite and non-zero");
    // Gear joint attaches the two outer bodies of the child joints.
    Body *bodyA = findBodyById(joint1->getBodyBId());
    Body *bodyB = findBodyById(joint2->getBodyBId());
    if (!bodyA) bodyA = findBodyById(joint1->getBodyAId());
    if (!bodyB) bodyB = findBodyById(joint2->getBodyAId());
    if (!bodyA || !bodyB)
        throw eve::Exception("World.newGearJoint: child joints must reference live bodies");
    b2GearJointDef def;
    def.bodyA = bodyA->raw();
    def.bodyB = bodyB->raw();
    def.joint1 = joint1->raw();
    def.joint2 = joint2->raw();
    def.ratio = ratio;
    Joint2D *gear = adoptJoint(this, bodyA, bodyB, world_->CreateJoint(&def), Joint2D::Kind::Gear);
    gear->setGearMembers(joint1, joint2);
    return gear;
}

Mechanism2D *World::newShaft(Body *support, Body *rotor, float anchorX, float anchorY,
                             bool collideConnected) {
    requireDistinctBodies(this, support, rotor, "World.newShaft");
    Joint2D *drive = newRevoluteJoint(support, rotor, anchorX, anchorY, collideConnected);
    auto *mechanism =
        new Mechanism2D(this, Mechanism2D::Kind::Shaft, nextMechanismId(), drive, nullptr, nullptr,
                        nullptr, 1, 0.f);
    mechanisms_.insert(mechanism);
    return mechanism;
}

Mechanism2D *World::newRatchet(Body *frame, Body *wheel, float anchorX, float anchorY, int direction,
                               float engagementTorque, bool collideConnected) {
    requireDistinctBodies(this, frame, wheel, "World.newRatchet");
    if (direction != 1 && direction != -1)
        throw eve::Exception("World.newRatchet: direction must be +1 or -1");
    if (!std::isfinite(engagementTorque) || engagementTorque < 0.f)
        throw eve::Exception("World.newRatchet: engagementTorque must be finite and >= 0");
    Joint2D *drive = newRevoluteJoint(frame, wheel, anchorX, anchorY, collideConnected);
    auto *mechanism =
        new Mechanism2D(this, Mechanism2D::Kind::Ratchet, nextMechanismId(), drive, nullptr,
                        nullptr, nullptr, direction, engagementTorque);
    mechanisms_.insert(mechanism);
    return mechanism;
}

Mechanism2D *World::newCrankSlider(Body *frame, Body *crank, Body *rod, Body *slider,
                                   float crankAnchorX, float crankAnchorY, float crankPinX,
                                   float crankPinY, float sliderPinX, float sliderPinY,
                                   float slideAxisX, float slideAxisY, bool collideConnected) {
    requireDistinctBodies(this, frame, crank, "World.newCrankSlider");
    requireDistinctBodies(this, crank, rod, "World.newCrankSlider");
    requireDistinctBodies(this, rod, slider, "World.newCrankSlider");
    requireDistinctBodies(this, frame, slider, "World.newCrankSlider");
    if (frame == rod || crank == slider)
        throw eve::Exception("World.newCrankSlider: bodies must form a four-bar chain");
    const float values[] = {crankAnchorX, crankAnchorY, crankPinX,   crankPinY,  sliderPinX,
                            sliderPinY,   slideAxisX,   slideAxisY};
    requireFiniteParams(values, 8, "World.newCrankSlider");
    (void)normalizedAxisPixels(slideAxisX, slideAxisY, "World.newCrankSlider");

    Joint2D *drive = newRevoluteJoint(frame, crank, crankAnchorX, crankAnchorY, collideConnected);
    Joint2D *crankPin = newRevoluteJoint(crank, rod, crankPinX, crankPinY, collideConnected);
    Joint2D *sliderPin = newRevoluteJoint(rod, slider, sliderPinX, sliderPinY, collideConnected);
    Joint2D *prismatic =
        newPrismaticJoint(frame, slider, sliderPinX, sliderPinY, slideAxisX, slideAxisY,
                          collideConnected);
    auto *mechanism =
        new Mechanism2D(this, Mechanism2D::Kind::CrankSlider, nextMechanismId(), drive, crankPin,
                        sliderPin, prismatic, 1, 0.f);
    mechanisms_.insert(mechanism);
    return mechanism;
}

}  // namespace eve::physics
