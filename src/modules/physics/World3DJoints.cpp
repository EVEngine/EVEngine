#include "physics/World3D.h"

#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Mechanism3D.h"
#include "common/Exception.h"

#include "physics/World3DInternal.h"

#include <cmath>

namespace eve::physics {
using namespace world3d_detail;
namespace {

void requireFiniteJointParams(const float *values, int count, const char *operation) {
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i]))
            throw eve::Exception("%s: parameters must be finite", operation);
    }
}

void requireDistinctBodies(World3D *world, Body3D *bodyA, Body3D *bodyB, const char *operation) {
    if (!world || !world->isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != world || bodyB->getWorld() != world || bodyA == bodyB)
        throw eve::Exception("%s: bodies must be distinct and belong to this world", operation);
}

PhysicsVector3D normalizedAxis(float x, float y, float z, const char *operation) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 1e-8f) throw eve::Exception("%s: axis length must be > 0", operation);
    return PhysicsVector3D{x / length, y / length, z / length};
}

}  // namespace

Joint3D *World3D::newWeldJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY, float anchorZ,
                               bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newWeldJoint");
    const auto handle = nextJointRuntimeHandle();
    auto native = worldValue(rigidBackend_->newWeldJoint(*bodyA, *bodyB, anchorX, anchorY, anchorZ, collideConnected));
    auto joint =
        std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Weld, nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

Joint3D *World3D::newMotorJoint(Body3D *bodyA, Body3D *bodyB, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newMotorJoint");
    const auto handle = nextJointRuntimeHandle();
    auto       native = worldValue(rigidBackend_->newMotorJoint(*bodyA, *bodyB, collideConnected));
    auto       joint =
        std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Motor, nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

Joint3D *World3D::newParallelJoint(Body3D *bodyA, Body3D *bodyB, float axisX, float axisY, float axisZ,
                                   bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newParallelJoint");
    const auto handle = nextJointRuntimeHandle();
    auto native = worldValue(rigidBackend_->newParallelJoint(*bodyA, *bodyB, axisX, axisY, axisZ, collideConnected));
    auto joint  = std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Parallel,
                                            nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

Joint3D *World3D::newFilterJoint(Body3D *bodyA, Body3D *bodyB) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newFilterJoint");
    const auto handle = nextJointRuntimeHandle();
    auto       native = worldValue(rigidBackend_->newFilterJoint(*bodyA, *bodyB));
    auto       joint =
        std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Filter, nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

void World3D::forgetMechanism(Mechanism3D *mechanism) {
    if (!mechanism) return;
    mechanisms_.erase(mechanism);
}

int World3D::nextMechanismId() { return nextMechanismId_++; }

Mechanism3D *World3D::newShaft(Body3D *support, Body3D *rotor, float anchorX, float anchorY,
                               float anchorZ, float axisX, float axisY, float axisZ,
                               bool collideConnected) {
    requireDistinctBodies(this, support, rotor, "World3D.newShaft");
    const PhysicsVector3D axis = normalizedAxis(axisX, axisY, axisZ, "World3D.newShaft");
    Joint3D *drive =
        newRevoluteJoint(support, rotor, anchorX, anchorY, anchorZ, axis.x, axis.y, axis.z,
                         collideConnected);
    auto *mechanism = new Mechanism3D(this, Mechanism3D::Kind::Shaft, nextMechanismId(), drive,
                                      nullptr, nullptr, nullptr, axis.x, axis.y, axis.z, 1, 0.f);
    mechanisms_.insert(mechanism);
    return mechanism;
}

Mechanism3D *World3D::newRatchet(Body3D *frame, Body3D *wheel, float anchorX, float anchorY,
                                 float anchorZ, float axisX, float axisY, float axisZ,
                                 int direction, float engagementTorque, bool collideConnected) {
    requireDistinctBodies(this, frame, wheel, "World3D.newRatchet");
    if (direction != 1 && direction != -1)
        throw eve::Exception("World3D.newRatchet: direction must be +1 or -1");
    if (!std::isfinite(engagementTorque) || engagementTorque < 0.f)
        throw eve::Exception("World3D.newRatchet: engagementTorque must be finite and >= 0");
    const PhysicsVector3D axis = normalizedAxis(axisX, axisY, axisZ, "World3D.newRatchet");
    Joint3D *drive =
        newRevoluteJoint(frame, wheel, anchorX, anchorY, anchorZ, axis.x, axis.y, axis.z,
                         collideConnected);
    auto *mechanism =
        new Mechanism3D(this, Mechanism3D::Kind::Ratchet, nextMechanismId(), drive, nullptr,
                        nullptr, nullptr, axis.x, axis.y, axis.z, direction, engagementTorque);
    mechanisms_.insert(mechanism);
    return mechanism;
}

Mechanism3D *World3D::newCrankSlider(Body3D *frame, Body3D *crank, Body3D *rod, Body3D *slider,
                                     float crankAnchorX, float crankAnchorY, float crankAnchorZ,
                                     float axisX, float axisY, float axisZ, float crankPinX,
                                     float crankPinY, float crankPinZ, float sliderPinX,
                                     float sliderPinY, float sliderPinZ, float slideAxisX,
                                     float slideAxisY, float slideAxisZ, bool collideConnected) {
    requireDistinctBodies(this, frame, crank, "World3D.newCrankSlider");
    requireDistinctBodies(this, crank, rod, "World3D.newCrankSlider");
    requireDistinctBodies(this, rod, slider, "World3D.newCrankSlider");
    requireDistinctBodies(this, frame, slider, "World3D.newCrankSlider");
    if (frame == rod || crank == slider)
        throw eve::Exception("World3D.newCrankSlider: bodies must form a four-bar chain");
    const float values[] = {crankAnchorX, crankAnchorY, crankAnchorZ, axisX,      axisY,
                            axisZ,        crankPinX,    crankPinY,    crankPinZ,  sliderPinX,
                            sliderPinY,   sliderPinZ,   slideAxisX,   slideAxisY, slideAxisZ};
    requireFiniteJointParams(values, 15, "World3D.newCrankSlider");
    const PhysicsVector3D hingeAxis = normalizedAxis(axisX, axisY, axisZ, "World3D.newCrankSlider");
    const PhysicsVector3D slideAxis = normalizedAxis(slideAxisX, slideAxisY, slideAxisZ, "World3D.newCrankSlider");
    if (std::fabs((hingeAxis.x * slideAxis.x + hingeAxis.y * slideAxis.y + hingeAxis.z * slideAxis.z)) > 0.999f)
        throw eve::Exception("World3D.newCrankSlider: hinge and slide axes must not be parallel");

    Joint3D *drive =
        newRevoluteJoint(frame, crank, crankAnchorX, crankAnchorY, crankAnchorZ, hingeAxis.x,
                         hingeAxis.y, hingeAxis.z, collideConnected);
    Joint3D *crankPin =
        newRevoluteJoint(crank, rod, crankPinX, crankPinY, crankPinZ, hingeAxis.x, hingeAxis.y,
                         hingeAxis.z, collideConnected);
    Joint3D *sliderPin =
        newRevoluteJoint(rod, slider, sliderPinX, sliderPinY, sliderPinZ, hingeAxis.x, hingeAxis.y,
                         hingeAxis.z, collideConnected);
    Joint3D *prismatic =
        newPrismaticJoint(frame, slider, sliderPinX, sliderPinY, sliderPinZ, slideAxis.x,
                          slideAxis.y, slideAxis.z, collideConnected);
    auto *mechanism =
        new Mechanism3D(this, Mechanism3D::Kind::CrankSlider, nextMechanismId(), drive, crankPin,
                        sliderPin, prismatic, hingeAxis.x, hingeAxis.y, hingeAxis.z, 1, 0.f);
    mechanisms_.insert(mechanism);
    return mechanism;
}

Joint3D *World3D::newDistanceJoint(Body3D *bodyA, Body3D *bodyB, float anchorAX, float anchorAY, float anchorAZ,
                                   float anchorBX, float anchorBY, float anchorBZ, float length,
                                   bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newDistanceJoint");
    const auto handle = nextJointRuntimeHandle();
    auto native = worldValue(rigidBackend_->newDistanceJoint(*bodyA, *bodyB, anchorAX, anchorAY, anchorAZ, anchorBX,
                                                             anchorBY, anchorBZ, length, collideConnected));
    auto joint  = std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Distance,
                                            nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

Joint3D *World3D::newRevoluteJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY, float anchorZ,
                                   float axisX, float axisY, float axisZ, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newRevoluteJoint");
    const auto handle = nextJointRuntimeHandle();
    auto native = worldValue(rigidBackend_->newRevoluteJoint(*bodyA, *bodyB, anchorX, anchorY, anchorZ, axisX, axisY,
                                                             axisZ, collideConnected));
    auto joint  = std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Revolute,
                                            nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

Joint3D *World3D::newPrismaticJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY, float anchorZ,
                                    float axisX, float axisY, float axisZ, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newPrismaticJoint");
    const auto handle = nextJointRuntimeHandle();
    auto native = worldValue(rigidBackend_->newPrismaticJoint(*bodyA, *bodyB, anchorX, anchorY, anchorZ, axisX, axisY,
                                                              axisZ, collideConnected));
    auto joint  = std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Prismatic,
                                            nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

Joint3D *World3D::newSphericalJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY, float anchorZ,
                                    float axisX, float axisY, float axisZ, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newSphericalJoint");
    const auto handle = nextJointRuntimeHandle();
    auto native = worldValue(rigidBackend_->newSphericalJoint(*bodyA, *bodyB, anchorX, anchorY, anchorZ, axisX, axisY,
                                                              axisZ, collideConnected));
    auto joint  = std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Spherical,
                                            nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

Joint3D *World3D::newWheelJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY, float anchorZ,
                                float suspensionAxisX, float suspensionAxisY, float suspensionAxisZ, float wheelAxisX,
                                float wheelAxisY, float wheelAxisZ, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newWheelJoint");
    const auto handle = nextJointRuntimeHandle();
    auto native = worldValue(rigidBackend_->newWheelJoint(*bodyA, *bodyB, anchorX, anchorY, anchorZ, suspensionAxisX,
                                                          suspensionAxisY, suspensionAxisZ, wheelAxisX, wheelAxisY,
                                                          wheelAxisZ, collideConnected));
    auto joint =
        std::make_unique<Joint3D>(this, bodyA, bodyB, std::move(native), handle, Joint3D::Kind::Wheel, nextJointId());
    joints_.insert(joint.get());
    jointHandles_[handle] = joint.get();
    return joint.release();
}

}  // namespace eve::physics
