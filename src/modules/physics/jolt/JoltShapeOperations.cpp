// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
eve::Result<ShapePlane3D> ShapeBackend::oneWayPlane(const ShapeGeometry3D& g, PhysicsVector3D n, float offset) const {
    if (!isValid()) return rejected<ShapePlane3D>("Shape expired", eve::DiagnosticCode::StaleHandle);
    const auto body = shape->body.lock();
    const auto pose = body->api().GetWorldTransform(body->id);
    const auto normal =
        pose.Multiply3x3(JPH::Quat(g.localQx, g.localQy, g.localQz, g.localQw) * JPH::Vec3(n.x, n.y, n.z));
    const auto point = pose * JPH::Vec3(g.localX, g.localY, g.localZ) + normal * offset;
    return eve::Result<ShapePlane3D>::success(
        {point.GetX(), point.GetY(), point.GetZ(), normal.GetX(), normal.GetY(), normal.GetZ()});
}

eve::Result<float> ShapeBackend::density() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(shape->density);
}
eve::Result<void> ShapeBackend::setDensity(float density, bool updateMass) {
    if (!isValid()) return rejected<void>("Shape expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(density) || density < 0)
        return rejected<void>("Invalid density", eve::DiagnosticCode::InvalidArgument);
    if (density == shape->density) return success();
    auto cooked = cook(shape->geometry, density, reinterpret_cast<JPH::uint64>(shape.get()));
    if (!cooked) return eve::Result<void>::failure(cooked.status());
    auto previous = shape->native;
    shape->native = std::move(cooked).takeValue();
    if (!updateMass) {
        // Native compounds retain the old leaf until the next explicit mass/topology rebuild.
        // Geometry and attribution are unchanged; only the pending density differs.
        shape->density = density;
        return success();
    }
    auto body     = shape->body.lock();
    auto replaced = replaceShapes(*body, body->shapes);
    if (!replaced) {
        shape->native = previous;
        return replaced;
    }
    shape->density = density;
    return success();
}

eve::Result<float> ShapeBackend::friction() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(shape->material.friction);
}
eve::Result<void> ShapeBackend::setFriction(float value) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(value) || value < 0)
        return rejected<void>("Invalid material value", eve::DiagnosticCode::InvalidArgument);
    shape->material.friction = value;
    return success();
}
eve::Result<float> ShapeBackend::restitution() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(shape->material.restitution);
}
eve::Result<void> ShapeBackend::setRestitution(float value) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(value) || value < 0 || value > 1)
        return rejected<void>("Invalid material value", eve::DiagnosticCode::InvalidArgument);
    shape->material.restitution = value;
    return success();
}
eve::Result<SurfaceMaterial3D> ShapeBackend::material() const {
    using R [[maybe_unused]] = eve::Result<SurfaceMaterial3D>;
    if (!isValid()) return rejected<SurfaceMaterial3D>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(shape->material);
}
eve::Result<void> validateMaterial(SurfaceMaterial3D value) {
    if (!isFinite(value.friction, value.restitution, value.rollingResistance) || value.friction < 0 ||
        value.restitution < 0 || value.restitution > 1 || value.rollingResistance < 0)
        return rejected<void>("Invalid material", eve::DiagnosticCode::InvalidArgument);
    if (!isFinite(value.tangentVelocity.x, value.tangentVelocity.y, value.tangentVelocity.z) ||
        ((value.userMaterialId >> 32) & 7) > 4 || ((value.userMaterialId >> 35) & 7) > 4)
        return rejected<void>("Invalid material policy", eve::DiagnosticCode::InvalidArgument);
    return success();
}
eve::Result<void> ShapeBackend::setMaterial(SurfaceMaterial3D value) {
    if (!isValid()) return rejected<void>("Shape expired", eve::DiagnosticCode::StaleHandle);
    auto checked = validateMaterial(value);
    if (!checked) return checked;
    shape->material = value;
    return success();
}

eve::Result<ShapeFilter3D> ShapeBackend::filter() const {
    using R [[maybe_unused]] = eve::Result<ShapeFilter3D>;
    if (!isValid()) return rejected<ShapeFilter3D>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(shape->filter);
}
eve::Result<void> ShapeBackend::setFilter(ShapeFilter3D value, bool updatePairs) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    (void)updatePairs;
    shape->filter = value;
    auto b        = shape->body.lock();
    b->api().InvalidateContactCache(b->id);
    if (b->active) b->api().ActivateBody(b->id);
    return success();
}
eve::Result<bool> ShapeBackend::isSensor() const {
    using R [[maybe_unused]] = eve::Result<bool>;
    if (!isValid()) return rejected<bool>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(shape->sensor);
}
eve::Result<void> ShapeBackend::enableHitEvents(bool enabled) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    (void)enabled;
    return success();
}
eve::Result<void> ShapeBackend::enablePreSolveEvents(bool enabled) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    (void)enabled;
    auto body = shape->body.lock();
    body->api().InvalidateContactCache(body->id);
    if (body->active) body->api().ActivateBody(body->id);
    return success();
}
eve::Result<int> ShapeBackend::meshMaterialCount() const {
    if (!isValid()) return rejected<int>("Shape expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<int>::success(static_cast<int>(shape->meshMaterials.size()));
}
eve::Result<SurfaceMaterial3D> ShapeBackend::meshMaterial(int slot) const {
    if (!isValid()) return rejected<SurfaceMaterial3D>("Shape expired", eve::DiagnosticCode::StaleHandle);
    if (slot < 0 || size_t(slot) >= shape->meshMaterials.size())
        return rejected<SurfaceMaterial3D>("Invalid material slot", eve::DiagnosticCode::InvalidArgument);
    return eve::Result<SurfaceMaterial3D>::success(shape->meshMaterials[size_t(slot)]);
}
eve::Result<void> ShapeBackend::setMeshMaterial(SurfaceMaterial3D value, int slot) {
    if (!isValid()) return rejected<void>("Shape expired", eve::DiagnosticCode::StaleHandle);
    if (slot < 0 || size_t(slot) >= shape->meshMaterials.size())
        return rejected<void>("Invalid material slot", eve::DiagnosticCode::InvalidArgument);
    auto checked = validateMaterial(value);
    if (!checked) return checked;
    shape->meshMaterials[size_t(slot)] = value;
    return success();
}

eve::Result<bool> ShapeBackend::testPoint(float x, float y, float z) const {
    if (!isValid()) return rejected<bool>("Shape expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x, y, z)) return rejected<bool>("Invalid point", eve::DiagnosticCode::InvalidArgument);
    const auto  body = shape->body.lock();
    const auto& g    = shape->geometry;
    const auto  pose = body->api().GetWorldTransform(body->id) *
                      JPH::RMat44::sRotationTranslation(JPH::Quat(g.localQx, g.localQy, g.localQz, g.localQw),
                                                        JPH::RVec3(g.localX, g.localY, g.localZ));
    const auto local =
        JPH::Vec3(pose.InversedRotationTranslation() * JPH::RVec3(x, y, z)) - shape->native->GetCenterOfMass();
    JPH::AllHitCollisionCollector<JPH::CollidePointCollector> hits;
    shape->native->CollidePoint(local, JPH::SubShapeIDCreator(), hits);
    return eve::Result<bool>::success(!hits.mHits.empty());
}
}  // namespace eve::physics::jolt
