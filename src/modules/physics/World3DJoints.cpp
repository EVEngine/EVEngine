#include "physics/World3D.h"

#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Mechanism3D.h"
#include "common/Exception.h"

#include <box3d/box3d.h>

#include <cmath>

namespace eve::physics {
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

b3Vec3 normalizedAxis(float x, float y, float z, const char *operation) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 1e-8f) throw eve::Exception("%s: axis length must be > 0", operation);
    return b3Vec3{x / length, y / length, z / length};
}

}  // namespace

Joint3D *World3D::newWeldJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY,
                               float anchorZ, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newWeldJoint");
    const float values[] = {anchorX, anchorY, anchorZ};
    requireFiniteJointParams(values, 3, "World3D.newWeldJoint");
    const b3Pos anchor{anchorX, anchorY, anchorZ};
    const b3WorldTransform xfA = b3Body_GetTransform(bodyA->raw());
    const b3WorldTransform xfB = b3Body_GetTransform(bodyB->raw());
    b3WeldJointDef def = b3DefaultWeldJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    def.base.localFrameA.p = b3Body_GetLocalPoint(bodyA->raw(), anchor);
    def.base.localFrameB.p = b3Body_GetLocalPoint(bodyB->raw(), anchor);
    def.base.localFrameA.q = b3InvMulQuat(xfA.q, b3Quat_identity);
    def.base.localFrameB.q = b3InvMulQuat(xfB.q, b3Quat_identity);
    def.base.collideConnected = collideConnected;
    const PhysicsJointHandle runtimeHandle = nextJointRuntimeHandle();
    const b3JointId id = b3CreateWeldJoint(worldId_, &def);
    auto *joint = new Joint3D(this, bodyA, bodyB, id, runtimeHandle, Joint3D::Kind::Weld, nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    jointHandles_[runtimeHandle] = joint;
    return joint;
}

Joint3D *World3D::newMotorJoint(Body3D *bodyA, Body3D *bodyB, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newMotorJoint");
    b3MotorJointDef def = b3DefaultMotorJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    def.base.collideConnected = collideConnected;
    const PhysicsJointHandle runtimeHandle = nextJointRuntimeHandle();
    const b3JointId id = b3CreateMotorJoint(worldId_, &def);
    auto *joint = new Joint3D(this, bodyA, bodyB, id, runtimeHandle, Joint3D::Kind::Motor, nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    jointHandles_[runtimeHandle] = joint;
    return joint;
}

Joint3D *World3D::newParallelJoint(Body3D *bodyA, Body3D *bodyB, float axisX, float axisY,
                                   float axisZ, bool collideConnected) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newParallelJoint");
    const float values[] = {axisX, axisY, axisZ};
    requireFiniteJointParams(values, 3, "World3D.newParallelJoint");
    const b3Vec3 worldAxis = normalizedAxis(axisX, axisY, axisZ, "World3D.newParallelJoint");
    b3ParallelJointDef def = b3DefaultParallelJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    def.base.localFrameA.q = b3ComputeQuatBetweenUnitVectors(
        b3Vec3_axisZ, b3Body_GetLocalVector(bodyA->raw(), worldAxis));
    def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(
        b3Vec3_axisZ, b3Body_GetLocalVector(bodyB->raw(), worldAxis));
    def.base.collideConnected = collideConnected;
    const PhysicsJointHandle runtimeHandle = nextJointRuntimeHandle();
    const b3JointId id = b3CreateParallelJoint(worldId_, &def);
    auto *joint =
        new Joint3D(this, bodyA, bodyB, id, runtimeHandle, Joint3D::Kind::Parallel, nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    jointHandles_[runtimeHandle] = joint;
    return joint;
}

Joint3D *World3D::newFilterJoint(Body3D *bodyA, Body3D *bodyB) {
    requireDistinctBodies(this, bodyA, bodyB, "World3D.newFilterJoint");
    b3FilterJointDef def = b3DefaultFilterJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    const PhysicsJointHandle runtimeHandle = nextJointRuntimeHandle();
    const b3JointId id = b3CreateFilterJoint(worldId_, &def);
    auto *joint =
        new Joint3D(this, bodyA, bodyB, id, runtimeHandle, Joint3D::Kind::Filter, nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    jointHandles_[runtimeHandle] = joint;
    return joint;
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
    const b3Vec3 axis = normalizedAxis(axisX, axisY, axisZ, "World3D.newShaft");
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
    const b3Vec3 axis = normalizedAxis(axisX, axisY, axisZ, "World3D.newRatchet");
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
    const b3Vec3 hingeAxis = normalizedAxis(axisX, axisY, axisZ, "World3D.newCrankSlider");
    const b3Vec3 slideAxis =
        normalizedAxis(slideAxisX, slideAxisY, slideAxisZ, "World3D.newCrankSlider");
    if (std::fabs(b3Dot(hingeAxis, slideAxis)) > 0.999f)
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

}  // namespace eve::physics
