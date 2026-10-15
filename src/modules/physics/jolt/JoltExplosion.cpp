// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include "physics/World3D.h"
#include "physics/jolt/JoltQueryInternal.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
namespace {
float projectedArea(const ShapeState& shape, JPH::Vec3Arg direction) {
    const auto& g = shape.geometry;
    switch (g.kind) {
        case ShapeKind3D::Sphere: return JPH::JPH_PI * g.a * g.a;
        case ShapeKind3D::Capsule:
            return JPH::JPH_PI * g.b * g.b + 4 * g.a * g.b * JPH::Vec3::sAxisY().Cross(direction).Length();
        case ShapeKind3D::Box:
            return 4 * (g.b * g.c * std::abs(direction.GetX()) + g.a * g.c * std::abs(direction.GetY()) +
                        g.a * g.b * std::abs(direction.GetZ()));
        case ShapeKind3D::ConvexHull: {
            const auto& hull = static_cast<const JPH::ConvexHullShape&>(*shape.native);
            float       area = 0;
            for (JPH::uint face = 0; face < hull.GetNumFaces(); ++face) {
                JPH::uint  indices[256];
                const auto count  = hull.GetFaceVertices(face, 256, indices);
                const auto origin = hull.GetPoint(indices[0]);
                for (JPH::uint i = 1; i + 1 < count; ++i)
                    area += .5f * std::max(0.f, (hull.GetPoint(indices[i]) - origin)
                                                    .Cross(hull.GetPoint(indices[i + 1]) - origin)
                                                    .Dot(direction));
            }
            return area;
        }
        default: return 0;
    }
}
}  // namespace
eve::Result<int> WorldBackend::explode(float x, float y, float z, float radius, float falloff, float strength,
                                       int mask) {
    if (!isValid()) return rejected<int>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x, y, z) || !isFinite(radius, falloff, strength) || radius < 0 || falloff < 0 ||
        !std::isfinite(radius + falloff))
        return rejected<int>("Invalid explosion", eve::DiagnosticCode::InvalidArgument);
    owner->explosionResults_.clear();
    struct Impulse {
        JPH::RVec3                 point;
        JPH::Vec3                  impulse;
        std::shared_ptr<BodyState> body;
    };
    std::vector<Impulse> impulses;
    const JPH::RVec3     center(x, y, z);
    // Prepare and validate every impulse before mutating any body.
    for (const auto& [id, body] : state->bodies) {
        if (!body->active || !body->owner || body->api().GetMotionType(body->id) != JPH::EMotionType::Dynamic) continue;
        for (const auto& shape : body->shapes) {
            if (!shape->owner || shape->native->GetType() != JPH::EShapeType::Convex ||
                !(shape->filter.categoryBits & uint32_t(mask)) || shape->owner->getExplosionScale() == 0)
                continue;
            auto        point    = closestConvexPoint(*shape, center);
            const float distance = float((point - center).Length());
            if (distance > radius + falloff) continue;
            const auto& g = shape->geometry;
            const auto  rotation =
                body->api().GetRotation(body->id) * JPH::Quat(g.localQx, g.localQy, g.localQz, g.localQw);
            if (distance <= 1.e-6f)
                point = body->api().GetWorldTransform(body->id) *
                        (JPH::Vec3(g.localX, g.localY, g.localZ) +
                         JPH::Quat(g.localQx, g.localQy, g.localQz, g.localQw) * shape->native->GetCenterOfMass());
            const auto  direction = JPH::Vec3(point - center).NormalizedOr(JPH::Vec3::sAxisX());
            const float scale =
                distance > radius && falloff > 0 ? std::clamp((radius + falloff - distance) / falloff, 0.f, 1.f) : 1.f;
            const float magnitude = strength * projectedArea(*shape, rotation.Conjugated() * direction) * scale *
                                    shape->owner->getExplosionScale();
            if (!std::isfinite(magnitude))
                return rejected<int>("Explosion impulse overflow", eve::DiagnosticCode::InvalidArgument);
            impulses.push_back({point, direction * magnitude, body});
        }
    }
    struct Before {
        std::shared_ptr<BodyState> body;
        JPH::Vec3                  linear, angular;
    };
    std::vector<Before> before;
    for (const auto& impulse : impulses) {
        auto body = impulse.body;
        if (std::none_of(before.begin(), before.end(), [&](const Before& entry) { return entry.body == body; }))
            before.push_back({body, body->api().GetLinearVelocity(body->id), body->api().GetAngularVelocity(body->id)});
        body->api().AddImpulse(body->id, impulse.impulse, impulse.point);
    }
    for (const auto& entry : before) {
        const auto dv = entry.body->api().GetLinearVelocity(entry.body->id) - entry.linear;
        const auto dw = entry.body->api().GetAngularVelocity(entry.body->id) - entry.angular;
        if (dv.LengthSq() <= 1.e-12f && dw.LengthSq() <= 1.e-12f) continue;
        owner->explosionResults_.push_back(
            {entry.body->owner->getId(), dv.GetX(), dv.GetY(), dv.GetZ(), dw.GetX(), dw.GetY(), dw.GetZ()});
    }
    std::sort(owner->explosionResults_.begin(), owner->explosionResults_.end(),
              [](const auto& a, const auto& b) { return a.bodyId < b.bodyId; });
    return eve::Result<int>::success(int(owner->explosionResults_.size()));
}
}  // namespace eve::physics::jolt
