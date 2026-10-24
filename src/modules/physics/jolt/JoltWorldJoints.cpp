// eve-hot-path
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include "physics/jolt/JoltJoint.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
namespace {
using JointResult = eve::Result<std::unique_ptr<IJointBackend3D>>;
JointResult createJoint(WorldBackend& world, Body3D& a, Body3D& b, JointKind3D kind, JPH::RVec3Arg anchorA,
                        JPH::RVec3Arg anchorB, JPH::Vec3Arg axis, JPH::Vec3Arg wheelAxis, float length, bool collide) {
    if (!world.isValid())
        return rejected<std::unique_ptr<IJointBackend3D>>("World expired", eve::DiagnosticCode::StaleHandle);
    if (&a == &b || !a.isValid() || !b.isValid() || a.getWorld() != world.owner || b.getWorld() != world.owner ||
        !isFinite(float(anchorA.GetX()), float(anchorA.GetY()), float(anchorA.GetZ())) ||
        !isFinite(float(anchorB.GetX()), float(anchorB.GetY()), float(anchorB.GetZ())) ||
        !std::isfinite(axis.LengthSq()) || axis.LengthSq() < 1.e-12f || !std::isfinite(length) || length < 0 ||
        !std::isfinite(wheelAxis.LengthSq()) || wheelAxis.LengthSq() < 1.e-12f)
        return rejected<std::unique_ptr<IJointBackend3D>>("Invalid joint definition",
                                                          eve::DiagnosticCode::InvalidArgument);
    auto record = std::make_shared<JointState>();
    for (const auto& [id, body] : world.state->bodies) {
        if (body->owner == &a) record->a = body;
        if (body->owner == &b) record->b = body;
    }
    auto ba = record->a.lock(), bb = record->b.lock();
    if (!ba || !bb)
        return rejected<std::unique_ptr<IJointBackend3D>>("Bodies are not owned by this provider",
                                                          eve::DiagnosticCode::InvalidArgument);
    auto& na             = *world.state->system.GetBodyLockInterfaceNoLock().TryGetBody(ba->id);
    auto& nb             = *world.state->system.GetBodyLockInterfaceNoLock().TryGetBody(bb->id);
    record->world        = world.state;
    record->kind         = kind;
    record->length       = length;
    record->collide      = true;
    const auto direction = axis.Normalized();
    // Portable frames keep the existing axis conventions used by snapshot reconstruction.
    auto portableFrame = [&](const JPH::Body& body, JPH::RVec3Arg anchor, JPH::Vec3Arg localAxis,
                             JPH::Vec3Arg worldAxis) {
        auto p = body.GetWorldTransform().InversedRotationTranslation() * anchor;
        auto q = JPH::Quat::sFromTo(localAxis, body.GetRotation().Conjugated() * worldAxis);
        if (kind == JointKind3D::Weld) q = body.GetRotation().Conjugated();
        if (kind == JointKind3D::Distance || kind == JointKind3D::Motor || kind == JointKind3D::Filter)
            q = JPH::Quat::sIdentity();
        return std::array<float, 7>{float(p.GetX()), float(p.GetY()), float(p.GetZ()), q.GetX(),
                                    q.GetY(),        q.GetZ(),        q.GetW()};
    };
    record->frames.bodyA = portableFrame(
        na, anchorA,
        kind == JointKind3D::Prismatic || kind == JointKind3D::Wheel ? JPH::Vec3::sAxisX() : JPH::Vec3::sAxisZ(),
        direction);
    record->frames.bodyB =
        portableFrame(nb, anchorB, kind == JointKind3D::Prismatic ? JPH::Vec3::sAxisX() : JPH::Vec3::sAxisZ(),
                      kind == JointKind3D::Wheel ? wheelAxis.Normalized() : direction);
    if (kind == JointKind3D::Distance) {
        JPH::DistanceConstraintSettings settings;
        settings.mPoint1      = anchorA;
        settings.mPoint2      = anchorB;
        settings.mMinDistance = length;
        settings.mMaxDistance = length;
        record->native        = createDistanceConstraint(na, nb, settings);
    } else if (kind != JointKind3D::Filter) {
        JPH::SixDOFConstraintSettings settings;
        using A = JPH::SixDOFConstraintSettings::EAxis;
        for (auto& motor : settings.mMotorSettings) {
            motor.mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping, 0, 0};
            if (kind == JointKind3D::Motor) {
                motor.SetForceLimit(0);
                motor.SetTorqueLimit(0);
            }
        }
        settings.mPosition1 = anchorA;
        settings.mPosition2 = anchorB;
        settings.mAxisX1 = settings.mAxisX2 = direction;
        settings.mAxisY1 = settings.mAxisY2 = direction.GetNormalizedPerpendicular();
        if (kind == JointKind3D::Weld || kind == JointKind3D::Motor) {
            settings.mAxisX1 = settings.mAxisX2 = JPH::Vec3::sAxisX();
            settings.mAxisY1 = settings.mAxisY2 = JPH::Vec3::sAxisY();
        }
        if (kind == JointKind3D::Wheel) {
            auto spin = wheelAxis.Normalized();
            if (std::abs(spin.Dot(direction)) > .999f)
                return rejected<std::unique_ptr<IJointBackend3D>>("Wheel axes are parallel",
                                                                  eve::DiagnosticCode::InvalidArgument);
            settings.mAxisX1 = settings.mAxisX2 = (spin - direction * spin.Dot(direction)).Normalized();
            settings.mAxisY1 = settings.mAxisY2 = direction;
            settings.mSwingType                 = JPH::ESwingType::Pyramid;
        }
        for (int i = 0; i < 6; ++i) settings.MakeFixedAxis(static_cast<A>(i));
        if (kind == JointKind3D::Revolute) settings.MakeFreeAxis(A::RotationX);
        if (kind == JointKind3D::Prismatic) settings.MakeFreeAxis(A::TranslationX);
        if (kind == JointKind3D::Spherical)
            for (int i = 3; i < 6; ++i) settings.MakeFreeAxis(static_cast<A>(i));
        if (kind == JointKind3D::Motor)
            for (int i = 0; i < 6; ++i) settings.MakeFreeAxis(static_cast<A>(i));
        if (kind == JointKind3D::Parallel) {
            for (int i = 0; i < 3; ++i) settings.MakeFreeAxis(static_cast<A>(i));
            settings.MakeFreeAxis(A::RotationX);
        }
        if (kind == JointKind3D::Wheel) {
            settings.MakeFreeAxis(A::TranslationY);
            settings.MakeFreeAxis(A::RotationX);
            settings.MakeFreeAxis(A::RotationY);
        }
        record->native = settings.Create(na, nb);
        if (kind == JointKind3D::Revolute || kind == JointKind3D::Prismatic || kind == JointKind3D::Motor) {
            for (int i = 0; i < 6; ++i) settings.MakeFreeAxis(static_cast<A>(i));
            record->velocity = static_cast<JPH::SixDOFConstraint*>(settings.Create(na, nb));
        }
    }
    if (record->native) world.state->system.AddConstraint(record->native);
    if (record->velocity) world.state->system.AddConstraint(record->velocity);
    auto provider   = std::make_unique<JointBackend>(record);
    auto configured = provider->setCollideConnected(collide);
    if (!configured) return JointResult::failure(configured.status());
    std::erase_if(world.state->joints, [](const auto& weak) { return weak.expired(); });
    world.state->joints.push_back(record);
    if (kind == JointKind3D::Wheel) {
        auto defaults = provider->setWheelSuspension(true, 1, .7f);
        if (!defaults) return JointResult::failure(defaults.status());
    }
    if (kind == JointKind3D::Parallel) {
        auto defaults = provider->setParallelSpring(1, 1, FLT_MAX);
        if (!defaults) return JointResult::failure(defaults.status());
    }
    return JointResult::success(std::move(provider));
}
}  // namespace
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newDistanceJoint(Body3D& a, Body3D& b, float ax, float ay,
                                                                             float az, float bx, float by, float bz,
                                                                             float length, bool collide) {
    return createJoint(*this, a, b, JointKind3D::Distance, {ax, ay, az}, {bx, by, bz}, JPH::Vec3::sAxisX(),
                       JPH::Vec3::sAxisZ(), length, collide);
}
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newWheelJoint(Body3D& a, Body3D& b, float x, float y,
                                                                          float z, float ux, float uy, float uz,
                                                                          float sx, float sy, float sz, bool collide) {
    return createJoint(*this, a, b, JointKind3D::Wheel, {x, y, z}, {x, y, z}, {ux, uy, uz}, {sx, sy, sz}, 0, collide);
}
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newWeldJoint(Body3D& a, Body3D& b, float x, float y,
                                                                         float z, bool collide) {
    return createJoint(*this, a, b, JointKind3D::Weld, {x, y, z}, {x, y, z}, JPH::Vec3::sAxisX(), JPH::Vec3::sAxisZ(),
                       0, collide);
}
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newMotorJoint(Body3D& a, Body3D& b, bool collide) {
    return createJoint(*this, a, b, JointKind3D::Motor, {a.getX(), a.getY(), a.getZ()}, {b.getX(), b.getY(), b.getZ()},
                       JPH::Vec3::sAxisX(), JPH::Vec3::sAxisZ(), 0, collide);
}
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newFilterJoint(Body3D& a, Body3D& b) {
    return createJoint(*this, a, b, JointKind3D::Filter, {0, 0, 0}, {0, 0, 0}, JPH::Vec3::sAxisX(), JPH::Vec3::sAxisZ(),
                       0, false);
}
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newParallelJoint(Body3D& a, Body3D& b, float x, float y,
                                                                             float z, bool collide) {
    return createJoint(*this, a, b, JointKind3D::Parallel, {a.getX(), a.getY(), a.getZ()},
                       {b.getX(), b.getY(), b.getZ()}, {x, y, z}, JPH::Vec3::sAxisZ(), 0, collide);
}
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newRevoluteJoint(Body3D& a, Body3D& b, float x, float y,
                                                                             float z, float ux, float uy, float uz,
                                                                             bool collide) {
    return createJoint(*this, a, b, JointKind3D::Revolute, {x, y, z}, {x, y, z}, {ux, uy, uz}, JPH::Vec3::sAxisZ(), 0,
                       collide);
}
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newPrismaticJoint(Body3D& a, Body3D& b, float x, float y,
                                                                              float z, float ux, float uy, float uz,
                                                                              bool collide) {
    return createJoint(*this, a, b, JointKind3D::Prismatic, {x, y, z}, {x, y, z}, {ux, uy, uz}, JPH::Vec3::sAxisZ(), 0,
                       collide);
}
eve::Result<std::unique_ptr<IJointBackend3D>> WorldBackend::newSphericalJoint(Body3D& a, Body3D& b, float x, float y,
                                                                              float z, float ux, float uy, float uz,
                                                                              bool collide) {
    return createJoint(*this, a, b, JointKind3D::Spherical, {x, y, z}, {x, y, z}, {ux, uy, uz}, JPH::Vec3::sAxisZ(), 0,
                       collide);
}
}  // namespace eve::physics::jolt
