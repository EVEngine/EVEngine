#include "physics/PhysicsLink.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"

namespace eve::physics {
namespace {

eve::Diagnostic invalidLinkDiagnostic(const char* path, const char* message) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, path);
}

eve::Diagnostic staleLinkDiagnostic(const char* path) {
    return eve::Diagnostic::error(
        eve::DiagnosticCode::StaleHandle,
        "Physics link does not resolve to a live object owned by this world", path);
}

template <typename Link, typename Handle>
eve::Result<Link> attachLink(PhysicsWorldHandle world, Handle handle,
                             const char* path, const char* message) {
    if (world.isInvalid() || handle.isInvalid())
        return eve::Result<Link>::failure(invalidLinkDiagnostic(path, message));
    return eve::Result<Link>::success(Link{world, handle});
}

}  // namespace

eve::Result<PhysicsLink> PhysicsLink::attach(PhysicsWorldHandle world,
                                             PhysicsBodyHandle body) {
    if (world.isInvalid() || body.isInvalid()) {
        return eve::Result<PhysicsLink>::failure(invalidLinkDiagnostic(
            "physics.link", "PhysicsLink requires valid world and body handles"));
    }
    return eve::Result<PhysicsLink>::success({world, body});
}

eve::Result<PhysicsLink> PhysicsLink::fromBody(const Body& body) {
    const World* world = body.getWorld();
    if (!body.isValid() || !world || !world->isValid()) {
        return eve::Result<PhysicsLink>::failure(staleLinkDiagnostic("physics.link.body"));
    }
    return attach(world->runtimeHandle(), body.runtimeHandle());
}

eve::Result<PhysicsLink> PhysicsLink::fromBody(const Body3D& body) {
    const World3D* world = body.getWorld();
    if (!body.isValid() || !world || !world->isValid()) {
        return eve::Result<PhysicsLink>::failure(staleLinkDiagnostic("physics.link.body3d"));
    }
    return attach(world->runtimeHandle(), body.runtimeHandle());
}

eve::Result<Body*> PhysicsLink::resolve(World& world) const {
    if (!isValid() || world.runtimeHandle() != this->world) {
        return eve::Result<Body*>::failure(staleLinkDiagnostic("physics.link.resolve"));
    }
    Body* resolved = world.findBody(body);
    if (!resolved) {
        return eve::Result<Body*>::failure(staleLinkDiagnostic("physics.link.resolve"));
    }
    return eve::Result<Body*>::success(resolved);
}

eve::Result<Body3D*> PhysicsLink::resolve(World3D& world) const {
    if (!isValid() || world.runtimeHandle() != this->world) {
        return eve::Result<Body3D*>::failure(staleLinkDiagnostic("physics.link.resolve3d"));
    }
    Body3D* resolved = world.findBody(body);
    if (!resolved) {
        return eve::Result<Body3D*>::failure(staleLinkDiagnostic("physics.link.resolve3d"));
    }
    return eve::Result<Body3D*>::success(resolved);
}

eve::Result<PhysicsShapeLink> PhysicsShapeLink::attach(PhysicsWorldHandle world,
                                                       PhysicsShapeHandle shape) {
    return attachLink<PhysicsShapeLink>(
        world, shape, "physics.shapeLink",
        "PhysicsShapeLink requires valid world and shape handles");
}

eve::Result<PhysicsShapeLink> PhysicsShapeLink::fromShape(const Shape3D& shape) {
    const Body3D* body = shape.getBody();
    const World3D* world = body ? body->getWorld() : nullptr;
    if (!shape.isValid() || !body || !world || !world->isValid())
        return eve::Result<PhysicsShapeLink>::failure(
            staleLinkDiagnostic("physics.shapeLink.shape"));
    return attach(world->runtimeHandle(), shape.runtimeHandle());
}

eve::Result<Shape3D*> PhysicsShapeLink::resolve(World3D& world) const {
    if (!isValid() || world.runtimeHandle() != this->world)
        return eve::Result<Shape3D*>::failure(staleLinkDiagnostic("physics.shapeLink.resolve"));
    Shape3D* resolved = world.findShape(shape);
    if (!resolved)
        return eve::Result<Shape3D*>::failure(staleLinkDiagnostic("physics.shapeLink.resolve"));
    return eve::Result<Shape3D*>::success(resolved);
}

eve::Result<PhysicsJointLink> PhysicsJointLink::attach(PhysicsWorldHandle world,
                                                       PhysicsJointHandle joint) {
    return attachLink<PhysicsJointLink>(
        world, joint, "physics.jointLink",
        "PhysicsJointLink requires valid world and joint handles");
}

eve::Result<PhysicsJointLink> PhysicsJointLink::fromJoint(const Joint3D& joint) {
    World3D* world = joint.getWorld();
    if (!joint.isValid() || !world || !world->isValid())
        return eve::Result<PhysicsJointLink>::failure(
            staleLinkDiagnostic("physics.jointLink.joint"));
    return attach(world->runtimeHandle(), joint.runtimeHandle());
}

eve::Result<Joint3D*> PhysicsJointLink::resolve(World3D& world) const {
    if (!isValid() || world.runtimeHandle() != this->world)
        return eve::Result<Joint3D*>::failure(staleLinkDiagnostic("physics.jointLink.resolve"));
    Joint3D* resolved = world.findJoint(joint);
    if (!resolved)
        return eve::Result<Joint3D*>::failure(staleLinkDiagnostic("physics.jointLink.resolve"));
    return eve::Result<Joint3D*>::success(resolved);
}

}  // namespace eve::physics
