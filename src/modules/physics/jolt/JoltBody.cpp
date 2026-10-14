// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Body/MotionProperties.h>
#include "physics/jolt/JoltInternal.h"
namespace eve::physics::jolt {
BodyBackend::~BodyBackend() { destroy(); }
bool BodyBackend::isValid() const noexcept { return body && body->attached && body->valid(); }
void BodyBackend::bindOwner(Body3D& owner) noexcept { body->owner = &owner; }
void BodyBackend::invalidate() noexcept {
    body->owner    = nullptr;
    body->attached = false;
}
void BodyBackend::destroy() noexcept {
    if (body && body->valid()) {
        auto  state = body->world.lock();
        auto& api   = body->api();
        if (api.IsAdded(body->id)) api.RemoveBody(body->id);
        api.DestroyBody(body->id);
        state->bodies.erase(body->id.GetIndexAndSequenceNumber());
    }
    if (body) {
        body->alive    = false;
        body->owner    = nullptr;
        body->attached = false;
    }
}
eve::Result<std::unique_ptr<IShapeBackend3D>> BodyBackend::createShape(const ShapeGeometry3D& geometry, float density,
                                                                       float friction, float restitution) {
    if (!isValid()) return rejected<std::unique_ptr<IShapeBackend3D>>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(density, friction, restitution) || density < 0 || friction < 0 || restitution < 0 || restitution > 1)
        return rejected<std::unique_ptr<IShapeBackend3D>>("Invalid material", eve::DiagnosticCode::InvalidArgument);
    auto shape                  = std::make_shared<ShapeState>();
    shape->body                 = body;
    shape->geometry             = geometry;
    shape->density              = density;
    shape->material.friction    = friction;
    shape->material.restitution = restitution;
    if (geometry.kind == ShapeKind3D::TriangleMesh) shape->meshMaterials.push_back(shape->material);
    auto cooked = cook(geometry, density, reinterpret_cast<JPH::uint64>(shape.get()));
    if (!cooked) return eve::Result<std::unique_ptr<IShapeBackend3D>>::failure(cooked.status());
    shape->native = std::move(cooked).takeValue();
    auto list     = body->shapes;
    list.push_back(shape);
    auto result = replaceShapes(*body, list);
    if (!result) return eve::Result<std::unique_ptr<IShapeBackend3D>>::failure(result.status());
    body->shapes = std::move(list);
    return eve::Result<std::unique_ptr<IShapeBackend3D>>::success(std::make_unique<ShapeBackend>(shape));
}
}  // namespace eve::physics::jolt
