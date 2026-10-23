#pragma once
#include "physics/backend/ShapeBackend3D.h"
namespace eve::physics::jolt {
struct State;
struct BodyState;
struct ShapeState;
// Internal provider implementation; all operations inherit the neutral interface contract.
class ShapeBackend final : public IShapeBackend3D {
public:
    std::shared_ptr<ShapeState> shape;
    explicit ShapeBackend(std::shared_ptr<ShapeState> value) : shape(std::move(value)) {}
    ~ShapeBackend() override;
    eve::Result<ShapePlane3D>      oneWayPlane(const ShapeGeometry3D& geometry, PhysicsVector3D normal,
                                               float offset) const override;
    bool                           isValid() const noexcept override;
    void                           destroy() noexcept override;
    void                           invalidate() noexcept override;
    void                           bindOwner(Shape3D& owner) noexcept override;
    eve::Result<void>              rebuild(const ShapeGeometry3D& geometry, ShapeBehavior3D behavior,
                                           std::span<const SurfaceMaterial3D> materials) override;
    eve::Result<float>             density() const override;
    eve::Result<void>              setDensity(float value, bool updateMass) override;
    eve::Result<float>             friction() const override;
    eve::Result<void>              setFriction(float value) override;
    eve::Result<float>             restitution() const override;
    eve::Result<void>              setRestitution(float value) override;
    eve::Result<SurfaceMaterial3D> material() const override;
    eve::Result<void>              setMaterial(SurfaceMaterial3D value) override;
    eve::Result<ShapeFilter3D>     filter() const override;
    eve::Result<void>              setFilter(ShapeFilter3D value, bool updatePairs) override;
    eve::Result<bool>              isSensor() const override;
    eve::Result<void>              enableHitEvents(bool enabled) override;
    eve::Result<void>              enablePreSolveEvents(bool enabled) override;
    eve::Result<int>               meshMaterialCount() const override;
    eve::Result<SurfaceMaterial3D> meshMaterial(int slot) const override;
    eve::Result<void>              setMeshMaterial(SurfaceMaterial3D value, int slot) override;
    eve::Result<bool>              testPoint(float x, float y, float z) const override;
};
}  // namespace eve::physics::jolt
