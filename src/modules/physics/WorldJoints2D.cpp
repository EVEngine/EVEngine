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

namespace {
Joint2D *createLegacy(World &world, Body *a, Body *b, Joint2DSettings settings) {
    requireDistinctBodies(&world, a, b, "World.newJoint");
    auto result = Joint2D::create(world, *a, *b, settings);
    if (!result) throw eve::Exception("%s", result.status().describe().c_str());
    return std::move(result).takeValue().release();
}
}  // namespace
Joint2D *World::newDistanceJoint(Body *a, Body *b, float ax, float ay, float bx, float by, float length, bool collide) {
    Joint2DSettings s;
    s.kind             = Joint2DKind::Distance;
    s.anchorA          = {toMeters(ax), toMeters(ay)};
    s.anchorB          = {toMeters(bx), toMeters(by)};
    s.length           = toMeters(length);
    s.collideConnected = collide;
    return createLegacy(*this, a, b, s);
}
Joint2D *World::newRevoluteJoint(Body *a, Body *b, float x, float y, bool collide) {
    Joint2DSettings s;
    s.kind             = Joint2DKind::Revolute;
    s.anchorA          = {toMeters(x), toMeters(y)};
    s.collideConnected = collide;
    return createLegacy(*this, a, b, s);
}
Joint2D *World::newPrismaticJoint(Body *a, Body *b, float x, float y, float ax, float ay, bool collide) {
    Joint2DSettings s;
    s.kind             = Joint2DKind::Prismatic;
    s.anchorA          = {toMeters(x), toMeters(y)};
    s.axis             = {ax, ay};
    s.collideConnected = collide;
    return createLegacy(*this, a, b, s);
}
Joint2D *World::newWeldJoint(Body *a, Body *b, float x, float y, bool collide) {
    Joint2DSettings s;
    s.kind             = Joint2DKind::Weld;
    s.anchorA          = {toMeters(x), toMeters(y)};
    s.collideConnected = collide;
    return createLegacy(*this, a, b, s);
}
Joint2D *World::newWheelJoint(Body *a, Body *b, float x, float y, float ax, float ay, bool collide) {
    Joint2DSettings s;
    s.kind             = Joint2DKind::Wheel;
    s.anchorA          = {toMeters(x), toMeters(y)};
    s.axis             = {ax, ay};
    s.collideConnected = collide;
    return createLegacy(*this, a, b, s);
}
Joint2D *World::newMotorJoint(Body *a, Body *b, bool collide) {
    Joint2DSettings s;
    s.kind             = Joint2DKind::Motor;
    s.collideConnected = collide;
    return createLegacy(*this, a, b, s);
}
Joint2D *World::newGearJoint(Joint2D *first, Joint2D *second, float ratio) {
    if (!first || !second) throw eve::Exception("World.newGearJoint: live source joints required");
    auto result = Joint2D::createGear(*this, *first, *second, ratio);
    if (!result) throw eve::Exception("%s", result.status().describe().c_str());
    return std::move(result).takeValue().release();
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
