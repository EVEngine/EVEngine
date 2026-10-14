// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Geometry/ConvexHullBuilder.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <algorithm>
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltJoint.h"
namespace eve::physics::jolt {
eve::Result<JPH::RefConst<JPH::Shape>> cook(const ShapeGeometry3D& g, float density, JPH::uint64 userData) {
    using R = eve::Result<JPH::RefConst<JPH::Shape>>;
    if (!std::isfinite(density) || density < 0 || !isFinite(g.localX, g.localY, g.localZ) ||
        !isFinite(g.localQx, g.localQy, g.localQz) || !std::isfinite(g.localQw))
        return rejected<JPH::RefConst<JPH::Shape>>("Invalid shape parameters", eve::DiagnosticCode::InvalidArgument);
    std::unique_ptr<JPH::ConvexShapeSettings> settings;
    if (g.kind == ShapeKind3D::Box && isFinite(g.a, g.b, g.c) && g.a > 0 && g.b > 0 && g.c > 0)
        settings = std::make_unique<JPH::BoxShapeSettings>(JPH::Vec3(g.a, g.b, g.c), std::min({0.05f, g.a, g.b, g.c}));
    else if (g.kind == ShapeKind3D::Sphere && std::isfinite(g.a) && g.a > 0)
        settings = std::make_unique<JPH::SphereShapeSettings>(g.a);
    else if (g.kind == ShapeKind3D::Capsule && isFinite(g.a, g.b, 0) && g.a >= 0 && g.b > 0)
        settings = std::make_unique<JPH::CapsuleShapeSettings>(g.a, g.b);
    else if (g.kind == ShapeKind3D::ConvexHull) {
        if (g.hullVertices.size() < 12 || g.hullVertices.size() % 3 || g.hullVertices.size() / 3 > 100000 ||
            g.hullMaxVertices < 4 || g.hullMaxVertices > 254)
            return rejected<JPH::RefConst<JPH::Shape>>("Invalid hull", eve::DiagnosticCode::InvalidArgument);
        auto hull = std::make_unique<JPH::ConvexHullShapeSettings>();
        for (size_t i = 0; i < g.hullVertices.size(); i += 3) {
            if (!isFinite(g.hullVertices[i], g.hullVertices[i + 1], g.hullVertices[i + 2]))
                return rejected<JPH::RefConst<JPH::Shape>>("Invalid hull", eve::DiagnosticCode::InvalidArgument);
            hull->mPoints.emplace_back(g.hullVertices[i], g.hullVertices[i + 1], g.hullVertices[i + 2]);
        }
        JPH::ConvexHullBuilder builder(hull->mPoints);
        const char*            error = nullptr;
        const auto             built = builder.Initialize(g.hullMaxVertices, hull->mHullTolerance, error);
        if (built != JPH::ConvexHullBuilder::EResult::Success &&
            built != JPH::ConvexHullBuilder::EResult::MaxVerticesReached)
            return rejected<JPH::RefConst<JPH::Shape>>(error ? error : "Invalid hull",
                                                       eve::DiagnosticCode::InvalidArgument);
        JPH::Vec3 center;
        float     volume = 0;
        builder.GetCenterOfMassAndVolume(center, volume);
        if (!std::isfinite(volume) || volume <= 0)
            return rejected<JPH::RefConst<JPH::Shape>>("Hull must have positive volume",
                                                       eve::DiagnosticCode::InvalidArgument);
        // Keep source order for deterministic cooking; faces borrow indices from the original points.
        std::vector<bool> used(hull->mPoints.size(), false);
        for (const auto* face : builder.GetFaces()) {
            const auto* edge = face->mFirstEdge;
            do {
                used[static_cast<size_t>(edge->mStartIdx)] = true;
                edge                                       = edge->mNextEdge;
            } while (edge != face->mFirstEdge);
        }
        JPH::Array<JPH::Vec3> reduced;
        for (size_t i = 0; i < used.size(); ++i)
            if (used[i]) reduced.push_back(hull->mPoints[i]);
        hull->mPoints = std::move(reduced);
        settings      = std::move(hull);
    } else if (g.kind == ShapeKind3D::TriangleMesh || g.kind == ShapeKind3D::HeightField)
        return cookMesh(g, userData);
    else
        return rejected<JPH::RefConst<JPH::Shape>>("Invalid primitive dimensions",
                                                   eve::DiagnosticCode::InvalidArgument);
    settings->mDensity  = density;
    settings->mUserData = userData;
    auto result         = settings->Create();
    if (result.HasError())
        return rejected<JPH::RefConst<JPH::Shape>>(result.GetError().c_str(), eve::DiagnosticCode::InvalidArgument);
    return R::success(result.Get());
}
eve::Result<void> replaceShapes(BodyState& body, const std::vector<std::shared_ptr<ShapeState>>& shapes) {
    if (!body.valid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    for (const auto& shape : shapes)
        if (shape->native->MustBeStatic() && body.api().GetMotionType(body.id) != JPH::EMotionType::Static)
            return rejected<void>("Concave shapes require a static body", eve::DiagnosticCode::InvalidArgument);
    JPH::RefConst<JPH::Shape> native;
    if (shapes.empty())
        native = new JPH::EmptyShape;
    else {
        JPH::StaticCompoundShapeSettings settings;
        for (const auto& s : shapes) {
            const auto& g = s->geometry;
            JPH::Quat   q(g.localQx, g.localQy, g.localQz, g.localQw);
            if (!std::isfinite(q.LengthSq()) || q.LengthSq() < 1.e-12f)
                return rejected<void>("Invalid local rotation", eve::DiagnosticCode::InvalidArgument);
            settings.AddShape({g.localX, g.localY, g.localZ}, q.Normalized(), s->native);
        }
        auto result = settings.Create();
        if (result.HasError()) return rejected<void>(result.GetError().c_str(), eve::DiagnosticCode::InvalidArgument);
        native = result.Get();
    }
    auto mass = body.mass;
    if (!native->MustBeStatic()) {
        mass = native->GetMassProperties();
        if (!(mass.mMass > 0)) mass.SetMassAndInertiaOfSolidBox({1, 1, 1}, 1);
    }
    const auto previous = body.api().GetShape(body.id)->GetCenterOfMass();
    std::erase_if(body.world.lock()->listener->solved, [&](const auto& entry) {
        return entry.first.GetBody1ID() == body.id || entry.first.GetBody2ID() == body.id;
    });
    body.api().SetShape(body.id, native, false, JPH::EActivation::Activate);
    if (!native->MustBeStatic()) {
        auto* nativeBody = body.world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body.id);
        nativeBody->GetMotionPropertiesUnchecked()->SetMassProperties(body.dofs, mass);
        body.mass = mass;
    }
    notifyJointShapeChanged(*body.world.lock(), body.id, native->GetCenterOfMass() - previous);
    return success();
}
ShapeBackend::~ShapeBackend() { destroy(); }
bool ShapeBackend::isValid() const noexcept { return shape && shape->valid(); }
void ShapeBackend::bindOwner(Shape3D& owner) noexcept { shape->owner = &owner; }
void ShapeBackend::invalidate() noexcept {
    shape->alive = false;
    shape->owner = nullptr;
}
void ShapeBackend::destroy() noexcept {
    if (isValid()) {
        auto b    = shape->body.lock();
        auto list = b->shapes;
        std::erase(list, shape);
        auto result = replaceShapes(*b, list);
        if (result)
            b->shapes = std::move(list);
        else
            result.ignore("Destructor cannot report shape removal failure; parent retains native resources");
    }
    invalidate();
}
eve::Result<void> ShapeBackend::rebuild(const ShapeGeometry3D& geometry, ShapeBehavior3D behavior,
                                        std::span<const SurfaceMaterial3D> materials) {
    if (!isValid()) return rejected<void>("Shape expired", eve::DiagnosticCode::StaleHandle);
    for (const auto& material : materials) {
        auto checked = validateMaterial(material);
        if (!checked) return checked;
    }
    if (!geometry.meshMaterialIndices.empty() &&
        *std::max_element(geometry.meshMaterialIndices.begin(), geometry.meshMaterialIndices.end()) >= materials.size())
        return rejected<void>("Missing mesh material", eve::DiagnosticCode::InvalidArgument);
    auto cooked = cook(geometry, shape->density, reinterpret_cast<JPH::uint64>(shape.get()));
    if (!cooked) return eve::Result<void>::failure(cooked.status());
    auto old         = shape->native;
    auto oldGeometry = shape->geometry;
    shape->native    = std::move(cooked).takeValue();
    shape->geometry  = geometry;
    auto b           = shape->body.lock();
    auto result      = replaceShapes(*b, b->shapes);
    if (!result) {
        shape->native   = old;
        shape->geometry = std::move(oldGeometry);
        return result;
    }
    shape->meshMaterials.assign(materials.begin(), materials.end());
    shape->sensor = behavior.sensor;
    return success();
}
}  // namespace eve::physics::jolt
