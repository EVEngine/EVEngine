#include <cmath>
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/box3d/Box3DJoint.h"
#include "physics/box3d/Box3DWorld.h"
namespace eve::physics::box3d {
namespace {


void requireFiniteJointParams(const float* values, int count, const char* operation) {
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) throw eve::Exception("%s: parameters must be finite", operation);
    }
}

void requireDistinctBodies(World3D* world, Body3D* bodyA, Body3D* bodyB, const char* operation) {
    if (!world || !world->isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != world || bodyB->getWorld() != world || bodyA == bodyB)
        throw eve::Exception("%s: bodies must be distinct and belong to this world", operation);
}

b3Vec3 normalizedAxis(float x, float y, float z, const char* operation) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 1e-8f) throw eve::Exception("%s: axis length must be > 0", operation);
    return b3Vec3{x / length, y / length, z / length};
}


}  // namespace

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newWeldJoint(Body3D& bodyARef, Body3D& bodyBRef,
                                                                         float anchorX, float anchorY, float anchorZ,
                                                                         bool collideConnected) {
    return checked("physics.world3d.newWeldJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        requireDistinctBodies(owner_, bodyA, bodyB, "World3D.newWeldJoint");
        const float values[] = {anchorX, anchorY, anchorZ};
        requireFiniteJointParams(values, 3, "World3D.newWeldJoint");
        const b3Pos            anchor{anchorX, anchorY, anchorZ};
        const b3WorldTransform xfA = b3Body_GetTransform(box3d::Access::bodyId(*bodyA));
        const b3WorldTransform xfB = b3Body_GetTransform(box3d::Access::bodyId(*bodyB));
        b3WeldJointDef         def = b3DefaultWeldJointDef();
        def.base.bodyIdA           = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB           = box3d::Access::bodyId(*bodyB);
        def.base.localFrameA.p     = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyA), anchor);
        def.base.localFrameB.p     = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyB), anchor);
        def.base.localFrameA.q     = b3InvMulQuat(xfA.q, b3Quat_identity);
        def.base.localFrameB.q     = b3InvMulQuat(xfB.q, b3Quat_identity);
        def.base.collideConnected  = collideConnected;
        const b3JointId id         = b3CreateWeldJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Weld));
    });
}

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newMotorJoint(Body3D& bodyARef, Body3D& bodyBRef,
                                                                          bool collideConnected) {
    return checked("physics.world3d.newMotorJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        requireDistinctBodies(owner_, bodyA, bodyB, "World3D.newMotorJoint");
        b3MotorJointDef def       = b3DefaultMotorJointDef();
        def.base.bodyIdA          = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB          = box3d::Access::bodyId(*bodyB);
        def.base.collideConnected = collideConnected;
        const b3JointId id        = b3CreateMotorJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Motor));
    });
}

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newParallelJoint(Body3D& bodyARef, Body3D& bodyBRef,
                                                                             float axisX, float axisY, float axisZ,
                                                                             bool collideConnected) {
    return checked("physics.world3d.newParallelJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        requireDistinctBodies(owner_, bodyA, bodyB, "World3D.newParallelJoint");
        const float values[] = {axisX, axisY, axisZ};
        requireFiniteJointParams(values, 3, "World3D.newParallelJoint");
        const b3Vec3       worldAxis = normalizedAxis(axisX, axisY, axisZ, "World3D.newParallelJoint");
        b3ParallelJointDef def       = b3DefaultParallelJointDef();
        def.base.bodyIdA             = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB             = box3d::Access::bodyId(*bodyB);
        def.base.localFrameA.q       = b3ComputeQuatBetweenUnitVectors(
            b3Vec3_axisZ, b3Body_GetLocalVector(box3d::Access::bodyId(*bodyA), worldAxis));
        def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(
            b3Vec3_axisZ, b3Body_GetLocalVector(box3d::Access::bodyId(*bodyB), worldAxis));
        def.base.collideConnected = collideConnected;
        const b3JointId id        = b3CreateParallelJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Parallel));
    });
}

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newFilterJoint(Body3D& bodyARef, Body3D& bodyBRef) {
    return checked("physics.world3d.newFilterJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        requireDistinctBodies(owner_, bodyA, bodyB, "World3D.newFilterJoint");
        b3FilterJointDef def = b3DefaultFilterJointDef();
        def.base.bodyIdA     = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB     = box3d::Access::bodyId(*bodyB);
        const b3JointId id   = b3CreateFilterJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Filter));
    });
}

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newDistanceJoint(Body3D& bodyARef, Body3D& bodyBRef,
                                                                             float anchorAX, float anchorAY,
                                                                             float anchorAZ, float anchorBX,
                                                                             float anchorBY, float anchorBZ,
                                                                             float length, bool collideConnected) {
    return checked("physics.world3d.newDistanceJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() || bodyA->getWorld() != owner_ ||
            bodyB->getWorld() != owner_ || bodyA == bodyB)
            throw eve::Exception("World3D.newDistanceJoint: bodies must be distinct and belong to this world");
        const float values[] = {anchorAX, anchorAY, anchorAZ, anchorBX, anchorBY, anchorBZ, length};
        for (float value : values) {
            if (!std::isfinite(value)) throw eve::Exception("World3D.newDistanceJoint: parameters must be finite");
        }
        if (length < 0.f) throw eve::Exception("World3D.newDistanceJoint: length must be >= 0");
        b3DistanceJointDef def = b3DefaultDistanceJointDef();
        def.base.bodyIdA       = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB       = box3d::Access::bodyId(*bodyB);
        def.base.localFrameA.p =
            b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyA), b3Pos{anchorAX, anchorAY, anchorAZ});
        def.base.localFrameB.p =
            b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyB), b3Pos{anchorBX, anchorBY, anchorBZ});
        def.base.collideConnected = collideConnected;
        def.length                = length;
        b3JointId id              = b3CreateDistanceJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Distance));
    });
}

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newRevoluteJoint(Body3D& bodyARef, Body3D& bodyBRef,
                                                                             float anchorX, float anchorY,
                                                                             float anchorZ, float axisX, float axisY,
                                                                             float axisZ, bool collideConnected) {
    return checked("physics.world3d.newRevoluteJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() || bodyA->getWorld() != owner_ ||
            bodyB->getWorld() != owner_ || bodyA == bodyB)
            throw eve::Exception("World3D.newRevoluteJoint: bodies must be distinct and belong to this world");
        const float values[] = {anchorX, anchorY, anchorZ, axisX, axisY, axisZ};
        for (float value : values) {
            if (!std::isfinite(value)) throw eve::Exception("World3D.newRevoluteJoint: parameters must be finite");
        }
        const float axisLength = std::sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
        if (axisLength <= 1e-8f) throw eve::Exception("World3D.newRevoluteJoint: axis length must be > 0");
        const b3Vec3       worldAxis{axisX / axisLength, axisY / axisLength, axisZ / axisLength};
        b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
        def.base.bodyIdA       = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB       = box3d::Access::bodyId(*bodyB);
        const b3Pos anchor{anchorX, anchorY, anchorZ};
        def.base.localFrameA.p    = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyA), anchor);
        def.base.localFrameB.p    = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyB), anchor);
        const b3Vec3 localAxisA   = b3Body_GetLocalVector(box3d::Access::bodyId(*bodyA), worldAxis);
        const b3Vec3 localAxisB   = b3Body_GetLocalVector(box3d::Access::bodyId(*bodyB), worldAxis);
        def.base.localFrameA.q    = b3ComputeQuatBetweenUnitVectors(b3Vec3_axisZ, localAxisA);
        def.base.localFrameB.q    = b3ComputeQuatBetweenUnitVectors(b3Vec3_axisZ, localAxisB);
        def.base.collideConnected = collideConnected;
        b3JointId id              = b3CreateRevoluteJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Revolute));
    });
}

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newPrismaticJoint(Body3D& bodyARef, Body3D& bodyBRef,
                                                                              float anchorX, float anchorY,
                                                                              float anchorZ, float axisX, float axisY,
                                                                              float axisZ, bool collideConnected) {
    return checked("physics.world3d.newPrismaticJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() || bodyA->getWorld() != owner_ ||
            bodyB->getWorld() != owner_ || bodyA == bodyB)
            throw eve::Exception("World3D.newPrismaticJoint: bodies must be distinct and belong to this world");
        const float values[] = {anchorX, anchorY, anchorZ, axisX, axisY, axisZ};
        for (float value : values) {
            if (!std::isfinite(value)) throw eve::Exception("World3D.newPrismaticJoint: parameters must be finite");
        }
        const float axisLength = std::sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
        if (axisLength <= 1e-8f) throw eve::Exception("World3D.newPrismaticJoint: axis length must be > 0");
        const b3Vec3        worldAxis{axisX / axisLength, axisY / axisLength, axisZ / axisLength};
        const b3Pos         anchor{anchorX, anchorY, anchorZ};
        b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
        def.base.bodyIdA        = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB        = box3d::Access::bodyId(*bodyB);
        def.base.localFrameA.p  = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyA), anchor);
        def.base.localFrameB.p  = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyB), anchor);
        def.base.localFrameA.q  = b3ComputeQuatBetweenUnitVectors(
            b3Vec3_axisX, b3Body_GetLocalVector(box3d::Access::bodyId(*bodyA), worldAxis));
        def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(
            b3Vec3_axisX, b3Body_GetLocalVector(box3d::Access::bodyId(*bodyB), worldAxis));
        def.base.collideConnected = collideConnected;
        const b3JointId id        = b3CreatePrismaticJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Prismatic));
    });
}

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newSphericalJoint(Body3D& bodyARef, Body3D& bodyBRef,
                                                                              float anchorX, float anchorY,
                                                                              float anchorZ, float axisX, float axisY,
                                                                              float axisZ, bool collideConnected) {
    return checked("physics.world3d.newSphericalJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() || bodyA->getWorld() != owner_ ||
            bodyB->getWorld() != owner_ || bodyA == bodyB)
            throw eve::Exception("World3D.newSphericalJoint: bodies must be distinct and belong to this world");
        const float values[] = {anchorX, anchorY, anchorZ, axisX, axisY, axisZ};
        for (float value : values) {
            if (!std::isfinite(value)) throw eve::Exception("World3D.newSphericalJoint: parameters must be finite");
        }
        const float axisLength = std::sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
        if (axisLength <= 1e-8f) throw eve::Exception("World3D.newSphericalJoint: axis length must be > 0");
        const b3Vec3        worldAxis{axisX / axisLength, axisY / axisLength, axisZ / axisLength};
        const b3Pos         anchor{anchorX, anchorY, anchorZ};
        b3SphericalJointDef def = b3DefaultSphericalJointDef();
        def.base.bodyIdA        = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB        = box3d::Access::bodyId(*bodyB);
        def.base.localFrameA.p  = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyA), anchor);
        def.base.localFrameB.p  = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyB), anchor);
        def.base.localFrameA.q  = b3ComputeQuatBetweenUnitVectors(
            b3Vec3_axisZ, b3Body_GetLocalVector(box3d::Access::bodyId(*bodyA), worldAxis));
        def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(
            b3Vec3_axisZ, b3Body_GetLocalVector(box3d::Access::bodyId(*bodyB), worldAxis));
        def.base.collideConnected = collideConnected;
        const b3JointId id        = b3CreateSphericalJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Spherical));
    });
}

eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newWheelJoint(Body3D& bodyARef, Body3D& bodyBRef,
                                                                          float anchorX, float anchorY, float anchorZ,
                                                                          float suspensionAxisX, float suspensionAxisY,
                                                                          float suspensionAxisZ, float wheelAxisX,
                                                                          float wheelAxisY, float wheelAxisZ,
                                                                          bool collideConnected) {
    return checked("physics.world3d.newWheelJoint", [&] {
        auto* bodyA = &bodyARef;
        auto* bodyB = &bodyBRef;

        if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() || bodyA->getWorld() != owner_ ||
            bodyB->getWorld() != owner_ || bodyA == bodyB)
            throw eve::Exception("World3D.newWheelJoint: bodies must be distinct and belong to this world");
        const float values[] = {anchorX,         anchorY,    anchorZ,    suspensionAxisX, suspensionAxisY,
                                suspensionAxisZ, wheelAxisX, wheelAxisY, wheelAxisZ};
        for (float value : values) {
            if (!std::isfinite(value)) throw eve::Exception("World3D.newWheelJoint: parameters must be finite");
        }
        const float suspensionLength = std::sqrt(suspensionAxisX * suspensionAxisX + suspensionAxisY * suspensionAxisY +
                                                 suspensionAxisZ * suspensionAxisZ);
        const float wheelLength =
            std::sqrt(wheelAxisX * wheelAxisX + wheelAxisY * wheelAxisY + wheelAxisZ * wheelAxisZ);
        if (suspensionLength <= 1e-8f || wheelLength <= 1e-8f)
            throw eve::Exception("World3D.newWheelJoint: both axis lengths must be > 0");
        const b3Vec3 suspensionAxis{suspensionAxisX / suspensionLength, suspensionAxisY / suspensionLength,
                                    suspensionAxisZ / suspensionLength};
        const b3Vec3 wheelAxis{wheelAxisX / wheelLength, wheelAxisY / wheelLength, wheelAxisZ / wheelLength};
        if (std::fabs(b3Dot(suspensionAxis, wheelAxis)) > 0.999f)
            throw eve::Exception("World3D.newWheelJoint: suspension and wheel axes must not be parallel");
        const b3Pos     anchor{anchorX, anchorY, anchorZ};
        b3WheelJointDef def    = b3DefaultWheelJointDef();
        def.base.bodyIdA       = box3d::Access::bodyId(*bodyA);
        def.base.bodyIdB       = box3d::Access::bodyId(*bodyB);
        def.base.localFrameA.p = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyA), anchor);
        def.base.localFrameB.p = b3Body_GetLocalPoint(box3d::Access::bodyId(*bodyB), anchor);
        def.base.localFrameA.q = b3ComputeQuatBetweenUnitVectors(
            b3Vec3_axisX, b3Body_GetLocalVector(box3d::Access::bodyId(*bodyA), suspensionAxis));
        def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(
            b3Vec3_axisZ, b3Body_GetLocalVector(box3d::Access::bodyId(*bodyB), wheelAxis));
        def.base.collideConnected = collideConnected;
        const b3JointId id        = b3CreateWheelJoint(worldId_, &def);

        if (!b3Joint_IsValid(id))
            return eve::Result<std::unique_ptr<IJointBackend3D>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected constraint creation"));
        return eve::Result<std::unique_ptr<IJointBackend3D>>::success(adoptJoint(id, JointKind3D::Wheel));
    });
}

}  // namespace eve::physics::box3d
