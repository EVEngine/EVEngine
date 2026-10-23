#include "physics/box3d/Box3DShape.h"
#include "common/Assert.h"
#include <cmath>
#include "common/Exception.h"
#include "physics/Shape3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/box3d/Box3DShapeCook.h"
namespace eve::physics::box3d {
namespace {
b3SurfaceMaterial nativeMaterial(SurfaceMaterial3D value) {
    auto material              = b3DefaultSurfaceMaterial();
    material.friction          = value.friction;
    material.restitution       = value.restitution;
    material.rollingResistance = value.rollingResistance;
    material.tangentVelocity   = {value.tangentVelocity.x, value.tangentVelocity.y, value.tangentVelocity.z};
    material.userMaterialId    = value.userMaterialId;
    material.customColor       = value.customColor;
    return material;
}
SurfaceMaterial3D materialValue(b3SurfaceMaterial value) {
    return {value.friction,          value.restitution,
            value.rollingResistance, {value.tangentVelocity.x, value.tangentVelocity.y, value.tangentVelocity.z},
            value.userMaterialId,    value.customColor};
}
class Box3DShape final : public IShapeBackend3D {
    template <class F>
    auto checked(const char* name, F&& operation) const -> decltype(operation()) {
        using R = decltype(operation());
        if (!isValid())
            return R::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "Shape is no longer valid", name));
        try {
            return operation();
        } catch (const eve::Exception& error) {
            return R::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(), name));
        }
    }

public:
    Box3DShape(b3BodyId body, float density, float friction, float restitution)
        : body_(body), initialDensity_(density) {
        initialMaterial_.friction    = friction;
        initialMaterial_.restitution = restitution;
    }
    ~Box3DShape() override { destroy(); }
    bool isValid() const noexcept override { return b3Shape_IsValid(shape_); }
    void destroy() noexcept override {
        if (isValid()) {
            b3Shape_SetUserData(shape_, nullptr);
            b3DestroyShape(shape_, true);
        }
        shape_ = {};
        mesh_.reset();
        height_.reset();
        owner_ = nullptr;
    }
    void invalidate() noexcept override {
        if (isValid()) b3Shape_SetUserData(shape_, nullptr);
        shape_ = {};
        owner_ = nullptr;
    }
    void bindOwner(Shape3D& owner) noexcept override {
        owner_ = &owner;
        if (isValid()) b3Shape_SetUserData(shape_, owner_);
    }
    eve::Result<ShapePlane3D> oneWayPlane(const ShapeGeometry3D& g, PhysicsVector3D normal,
                                          float offset) const override {
        return checked("physics.shape3d.oneWayPlane", [&] {
            const auto   bodyTransform = b3Body_GetTransform(body_);
            const b3Quat localRotation{{g.localQx, g.localQy, g.localQz}, g.localQw};
            const auto   worldRotation = b3MulQuat(bodyTransform.q, localRotation);
            const auto   n             = b3RotateVector(worldRotation, b3Vec3{normal.x, normal.y, normal.z});
            const auto   bodyOffset    = b3RotateVector(bodyTransform.q, b3Vec3{g.localX, g.localY, g.localZ});
            const auto   point         = b3OffsetPos(b3OffsetPos(bodyTransform.p, bodyOffset), offset * n);
            return eve::Result<ShapePlane3D>::success({point.x, point.y, point.z, n.x, n.y, n.z});
        });
    }
    b3ShapeId         nativeId() const { return shape_; }
    eve::Result<void> rebuild(const ShapeGeometry3D& g, ShapeBehavior3D behavior,
                              std::span<const SurfaceMaterial3D> materials) override {
        if (!b3Body_IsValid(body_))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "Shape body is no longer valid"));
        try {
            b3ShapeDef def   = b3DefaultShapeDef();
            def.density      = isValid() ? b3Shape_GetDensity(shape_) : initialDensity_;
            def.baseMaterial = isValid() ? b3Shape_GetSurfaceMaterial(shape_) : nativeMaterial(initialMaterial_);
            if (isValid()) def.filter = b3Shape_GetFilter(shape_);
            def.isSensor              = behavior.sensor;
            def.enableContactEvents   = !behavior.sensor;
            def.enableSensorEvents    = true;
            def.enableHitEvents       = behavior.hitEvents && !behavior.sensor;
            def.enablePreSolveEvents  = behavior.preSolveEvents && !behavior.sensor;
            def.enableCustomFiltering = behavior.customFiltering;
            def.explosionScale        = behavior.explosionScale;
            std::vector<b3SurfaceMaterial> nativeMaterials;
            for (auto value : materials) nativeMaterials.push_back(nativeMaterial(value));
            if (!nativeMaterials.empty()) {
                def.materials     = nativeMaterials.data();
                def.materialCount = static_cast<int>(nativeMaterials.size());
            }
            const b3Transform transform{{g.localX, g.localY, g.localZ}, {{g.localQx, g.localQy, g.localQz}, g.localQw}};
            decltype(mesh_)   nextMesh{nullptr, b3DestroyMesh};
            decltype(height_) nextHeight{nullptr, b3DestroyHeightField};
            b3ShapeId         next{};
            switch (g.kind) {
                case ShapeKind3D::Box: {
                    if (!(g.a > 0.f && g.b > 0.f && g.c > 0.f) || !std::isfinite(g.a + g.b + g.c))
                        throw eve::Exception("Box dimensions must be finite and positive");
                    auto box = b3MakeBoxHull(g.a, g.b, g.c);
                    next     = b3CreateTransformedHullShape(body_, &def, &box.base, transform, b3Vec3_one);
                    break;
                }
                case ShapeKind3D::Sphere: {
                    if (!(g.a > 0.f) || !std::isfinite(g.a))
                        throw eve::Exception("Sphere radius must be finite and positive");
                    const b3Sphere sphere{transform.p, g.a};
                    next = b3CreateSphereShape(body_, &def, &sphere);
                    break;
                }
                case ShapeKind3D::Capsule: {
                    if (!(g.a >= 0.f && g.b > 0.f) || !std::isfinite(g.a + g.b))
                        throw eve::Exception("Capsule dimensions must be finite and valid");
                    auto            axis = b3RotateVector(transform.q, b3Vec3{0.f, g.a, 0.f});
                    const b3Capsule capsule{transform.p - axis, transform.p + axis, g.b};
                    next = b3CreateCapsuleShape(body_, &def, &capsule);
                    break;
                }
                case ShapeKind3D::ConvexHull: {
                    std::unique_ptr<b3HullData, decltype(&b3DestroyHull)> hull(
                        createCheckedHull(g.hullVertices, g.hullMaxVertices, "shape.rebuild"), b3DestroyHull);
                    next = b3CreateTransformedHullShape(body_, &def, hull.get(), transform, b3Vec3_one);
                    break;
                }
                case ShapeKind3D::TriangleMesh: {
                    if (b3Body_GetType(body_) != b3_staticBody)
                        throw eve::Exception("Triangle meshes require a static body");
                    nextMesh.reset(createCheckedMesh(g.meshVertices, g.meshIndices, g.meshWeldVertices,
                                                     g.meshWeldTolerance, g.meshIdentifyEdges, g.meshUseMedianSplit,
                                                     transform, &g.meshMaterialIndices, "shape.rebuild"));
                    next = b3CreateMeshShape(body_, &def, nextMesh.get(), b3Vec3_one);
                    break;
                }
                case ShapeKind3D::HeightField: {
                    if (b3Body_GetType(body_) != b3_staticBody)
                        throw eve::Exception("Height fields require a static body");
                    nextHeight.reset(createCheckedHeightField(g.heightCountX, g.heightCountZ, g.heightCellSizeX,
                                                              g.heightCellSizeZ, g.heightValues, g.heightGlobalMin,
                                                              g.heightGlobalMax, g.heightClockwise, "shape.rebuild"));
                    next = b3CreateHeightFieldShape(body_, &def, nextHeight.get());
                    break;
                }
            }
            if (!b3Shape_IsValid(next))
                return eve::Result<void>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected shape creation"));
            if (isValid()) {
                b3Shape_SetUserData(shape_, nullptr);
                b3DestroyShape(shape_, true);
            }
            shape_  = next;
            mesh_   = std::move(nextMesh);
            height_ = std::move(nextHeight);
            kind_   = g.kind;
            b3Shape_SetUserData(shape_, owner_);
            return eve::Result<void>::success();
        } catch (const eve::Exception& error) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(), "physics.shape3d.rebuild"));
        }
    }
    eve::Result<float> density() const override {
        return checked("physics.shape3d.density",
                       [&] { return eve::Result<float>::success(b3Shape_GetDensity(shape_)); });
    }
    eve::Result<void> setDensity(float value, bool updateMass) override {
        return checked("physics.shape3d.setDensity", [&] {
            b3Shape_SetDensity(shape_, value, updateMass);
            return eve::Result<void>::success();
        });
    }
    eve::Result<float> friction() const override {
        return checked("physics.shape3d.friction",
                       [&] { return eve::Result<float>::success(b3Shape_GetFriction(shape_)); });
    }
    eve::Result<void> setFriction(float value) override {
        return checked("physics.shape3d.setFriction", [&] {
            b3Shape_SetFriction(shape_, value);
            return eve::Result<void>::success();
        });
    }
    eve::Result<float> restitution() const override {
        return checked("physics.shape3d.restitution",
                       [&] { return eve::Result<float>::success(b3Shape_GetRestitution(shape_)); });
    }
    eve::Result<void> setRestitution(float value) override {
        return checked("physics.shape3d.setRestitution", [&] {
            b3Shape_SetRestitution(shape_, value);
            return eve::Result<void>::success();
        });
    }
    eve::Result<SurfaceMaterial3D> material() const override {
        return checked("physics.shape3d.material", [&] {
            return eve::Result<SurfaceMaterial3D>::success(materialValue(b3Shape_GetSurfaceMaterial(shape_)));
        });
    }
    eve::Result<void> setMaterial(SurfaceMaterial3D value) override {
        return checked("physics.shape3d.setMaterial", [&] {
            b3Shape_SetSurfaceMaterial(shape_, nativeMaterial(value));
            return eve::Result<void>::success();
        });
    }
    eve::Result<ShapeFilter3D> filter() const override {
        return checked("physics.shape3d.filter", [&] {
            return eve::Result<ShapeFilter3D>::success(ShapeFilter3D{b3Shape_GetFilter(shape_).categoryBits,
                                                                     b3Shape_GetFilter(shape_).maskBits,
                                                                     b3Shape_GetFilter(shape_).groupIndex});
        });
    }
    eve::Result<void> setFilter(ShapeFilter3D value, bool updatePairs) override {
        return checked("physics.shape3d.setFilter", [&] {
            b3Shape_SetFilter(shape_, b3Filter{value.categoryBits, value.maskBits, value.groupIndex}, updatePairs);
            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> isSensor() const override {
        return checked("physics.shape3d.isSensor",
                       [&] { return eve::Result<bool>::success(b3Shape_IsSensor(shape_)); });
    }
    eve::Result<void> enableHitEvents(bool enabled) override {
        return checked("physics.shape3d.enableHitEvents", [&] {
            b3Shape_EnableHitEvents(shape_, enabled);
            return eve::Result<void>::success();
        });
    }
    eve::Result<void> enablePreSolveEvents(bool enabled) override {
        return checked("physics.shape3d.enablePreSolveEvents", [&] {
            b3Shape_EnablePreSolveEvents(shape_, enabled);
            return eve::Result<void>::success();
        });
    }
    eve::Result<int> meshMaterialCount() const override {
        return checked("physics.shape3d.meshMaterialCount",
                       [&] { return eve::Result<int>::success(b3Shape_GetMeshMaterialCount(shape_)); });
    }
    eve::Result<SurfaceMaterial3D> meshMaterial(int slot) const override {
        return checked("physics.shape3d.meshMaterial", [&] {
            if (kind_ != ShapeKind3D::TriangleMesh || slot < 0 || slot >= b3Shape_GetMeshMaterialCount(shape_))
                throw eve::Exception("Mesh material slot is out of range");
            return eve::Result<SurfaceMaterial3D>::success(materialValue(b3Shape_GetMeshSurfaceMaterial(shape_, slot)));
        });
    }
    eve::Result<void> setMeshMaterial(SurfaceMaterial3D value, int slot) override {
        return checked("physics.shape3d.setMeshMaterial", [&] {
            if (kind_ != ShapeKind3D::TriangleMesh || slot < 0 || slot >= b3Shape_GetMeshMaterialCount(shape_))
                throw eve::Exception("Mesh material slot is out of range");
            b3Shape_SetMeshMaterial(shape_, nativeMaterial(value), slot);
            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> testPoint(float x, float y, float z) const override {
        return checked("physics.shape3d.testPoint", [&] {
            const auto overlap = [&]() -> bool {
                b3Vec3       point{x, y, z};
                b3ShapeProxy proxy;
                proxy.points = &point;
                proxy.count  = 1;
                proxy.radius = 0.f;

                b3WorldTransform wt = b3Body_GetTransform(body_);
                b3Transform      xf;
                xf.p = b3Vec3{static_cast<float>(wt.p.x), static_cast<float>(wt.p.y), static_cast<float>(wt.p.z)};
                xf.q = wt.q;

                switch (kind_) {
                    case ShapeKind3D::Box:
                    case ShapeKind3D::ConvexHull: {
                        const b3HullData* hull = b3Shape_GetHull(shape_);
                        if (!hull) return false;
                        return b3OverlapHull(hull, xf, &proxy);
                    }
                    case ShapeKind3D::Sphere: {
                        b3Sphere sphere = b3Shape_GetSphere(shape_);
                        return b3OverlapSphere(&sphere, xf, &proxy);
                    }
                    case ShapeKind3D::Capsule: {
                        b3Capsule capsule = b3Shape_GetCapsule(shape_);
                        return b3OverlapCapsule(&capsule, xf, &proxy);
                    }
                    case ShapeKind3D::TriangleMesh: {
                        const b3Mesh mesh = b3Shape_GetMesh(shape_);
                        return mesh.data ? b3OverlapMesh(&mesh, xf, &proxy) : false;
                    }
                    case ShapeKind3D::HeightField: {
                        const b3HeightFieldData* heightField = b3Shape_GetHeightField(shape_);
                        return heightField ? b3OverlapHeightField(heightField, xf, &proxy) : false;
                    }
                }
                return false;
            };
            return eve::Result<bool>::success(overlap());
        });
    }

private:
    b3BodyId                                                            body_{};
    b3ShapeId                                                           shape_{};
    ShapeKind3D                                                         kind_  = ShapeKind3D::Box;
    Shape3D*                                                            owner_ = nullptr;
    float                                                               initialDensity_;
    SurfaceMaterial3D                                                   initialMaterial_;
    std::unique_ptr<b3MeshData, decltype(&b3DestroyMesh)>               mesh_{nullptr, b3DestroyMesh};
    std::unique_ptr<b3HeightFieldData, decltype(&b3DestroyHeightField)> height_{nullptr, b3DestroyHeightField};
};
}  // namespace

eve::Result<std::unique_ptr<IShapeBackend3D>> makeShape(b3BodyId body, const ShapeGeometry3D& geometry, float density,
                                                        float friction, float restitution) {
    if (!std::isfinite(density) || density < 0.f || !std::isfinite(friction) || friction < 0.f ||
        !std::isfinite(restitution) || restitution < 0.f)
        return eve::Result<std::unique_ptr<IShapeBackend3D>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Shape material values must be finite and non-negative"));
    auto shape  = std::make_unique<Box3DShape>(body, density, friction, restitution);
    auto result = shape->rebuild(geometry, {}, {});
    if (!result.ok()) return eve::Result<std::unique_ptr<IShapeBackend3D>>::failure(result.status());
    return eve::Result<std::unique_ptr<IShapeBackend3D>>::success(std::move(shape));
}
b3ShapeId Access::shapeId(const Shape3D& shape) {
    const auto* backend = dynamic_cast<const Box3DShape*>(shape.backend_.get());
    EV_ASSERT(backend != nullptr, "Box3D composition requires a Box3D shape");
    return backend->nativeId();
}
}  // namespace eve::physics::box3d
