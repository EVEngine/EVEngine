#include "physics/Shape3D.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

#include "common/Exception.h"

#include <box3d/box3d.h>

namespace eve::physics {
namespace {

b3ShapeDef makeShapeDef(float density, float friction, float restitution, bool sensor) {
    b3ShapeDef def                 = b3DefaultShapeDef();
    def.density                    = density;
    def.baseMaterial.friction      = friction;
    def.baseMaterial.restitution   = restitution;
    def.isSensor                   = sensor;
    def.enableContactEvents        = !sensor;
    def.enableSensorEvents         = true;
    return def;
}

}  // namespace

Shape3D::Shape3D(World3D *world, Body3D *body, b3ShapeId shapeId, Kind kind, float a, float b,
                 float c)
    : world_(world), body_(body), shapeId_(shapeId), kind_(kind), a_(a), b_(b), c_(c) {}

Shape3D::~Shape3D() {
    if (isValid() && body_ && body_->isValid()) {
        b3Shape_SetUserData(shapeId_, nullptr);
        b3DestroyShape(shapeId_, true);
        if (world_) world_->forgetShape(this);
    }
    shapeId_ = {};
    body_    = nullptr;
    world_   = nullptr;
}

bool Shape3D::isValid() const { return b3Shape_IsValid(shapeId_); }

void Shape3D::invalidate() {
    if (isValid()) b3Shape_SetUserData(shapeId_, nullptr);
    shapeId_ = {};
    body_    = nullptr;
    world_   = nullptr;
}

void Shape3D::destroy() {
    if (!isValid() || !body_ || !body_->isValid()) {
        invalidate();
        return;
    }
    b3Shape_SetUserData(shapeId_, nullptr);
    b3DestroyShape(shapeId_, true);
    if (world_) world_->forgetShape(this);
    shapeId_ = {};
    body_    = nullptr;
    world_   = nullptr;
}

void Shape3D::recreate(bool sensor) {
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setSensor: body destroyed");

    float density     = isValid() ? b3Shape_GetDensity(shapeId_) : 1.f;
    float friction    = isValid() ? b3Shape_GetFriction(shapeId_) : 0.2f;
    float restitution = isValid() ? b3Shape_GetRestitution(shapeId_) : 0.f;

    if (isValid()) {
        b3Shape_SetUserData(shapeId_, nullptr);
        b3DestroyShape(shapeId_, false);
        shapeId_ = {};
    }

    b3ShapeDef def = makeShapeDef(density, friction, restitution, sensor);
    switch (kind_) {
        case Kind::Box: {
            b3BoxHull box = b3MakeBoxHull(a_, b_, c_);
            shapeId_      = b3CreateHullShape(body_->raw(), &def, &box.base);
            break;
        }
        case Kind::Sphere: {
            b3Sphere sphere;
            sphere.center = b3Vec3_zero;
            sphere.radius = a_;
            shapeId_      = b3CreateSphereShape(body_->raw(), &def, &sphere);
            break;
        }
        case Kind::Capsule: {
            b3Capsule capsule;
            capsule.center1 = b3Vec3{0.f, -a_, 0.f};
            capsule.center2 = b3Vec3{0.f, a_, 0.f};
            capsule.radius  = b_;
            shapeId_        = b3CreateCapsuleShape(body_->raw(), &def, &capsule);
            break;
        }
    }
    b3Shape_SetUserData(shapeId_, this);
}

void Shape3D::setSensor(bool sensor) {
    if (!body_ || !body_->isValid()) return;
    if (isValid() && b3Shape_IsSensor(shapeId_) == sensor) return;
    recreate(sensor);
}

bool Shape3D::isSensor() const { return isValid() ? b3Shape_IsSensor(shapeId_) : false; }

void Shape3D::setFriction(float friction) {
    if (!isValid()) return;
    b3Shape_SetFriction(shapeId_, friction);
}

float Shape3D::getFriction() const { return isValid() ? b3Shape_GetFriction(shapeId_) : 0.f; }

void Shape3D::setRestitution(float restitution) {
    if (!isValid()) return;
    b3Shape_SetRestitution(shapeId_, restitution);
}

float Shape3D::getRestitution() const {
    return isValid() ? b3Shape_GetRestitution(shapeId_) : 0.f;
}

void Shape3D::setDensity(float density) {
    if (!isValid()) return;
    b3Shape_SetDensity(shapeId_, density, true);
}

float Shape3D::getDensity() const { return isValid() ? b3Shape_GetDensity(shapeId_) : 0.f; }

bool Shape3D::testPoint(float x, float y, float z) const {
    if (!isValid() || !body_ || !body_->isValid()) return false;

    b3Vec3 point{x, y, z};
    b3ShapeProxy proxy;
    proxy.points = &point;
    proxy.count  = 1;
    proxy.radius = 0.f;

    b3WorldTransform wt = b3Body_GetTransform(body_->raw());
    b3Transform xf;
    xf.p = b3Vec3{static_cast<float>(wt.p.x), static_cast<float>(wt.p.y),
                  static_cast<float>(wt.p.z)};
    xf.q = wt.q;

    switch (kind_) {
        case Kind::Box: {
            const b3HullData *hull = b3Shape_GetHull(shapeId_);
            if (!hull) return false;
            return b3OverlapHull(hull, xf, &proxy);
        }
        case Kind::Sphere: {
            b3Sphere sphere = b3Shape_GetSphere(shapeId_);
            return b3OverlapSphere(&sphere, xf, &proxy);
        }
        case Kind::Capsule: {
            b3Capsule capsule = b3Shape_GetCapsule(shapeId_);
            return b3OverlapCapsule(&capsule, xf, &proxy);
        }
    }
    return false;
}

}  // namespace eve::physics
