#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include "common/Exception.h"

#include <box3d/box3d.h>

#include <cmath>
#include <vector>

namespace eve::physics {
namespace {

b3BodyType parseBodyType(const std::string &type) {
    if (type == "static") return b3_staticBody;
    if (type == "kinematic") return b3_kinematicBody;
    if (type == "dynamic") return b3_dynamicBody;
    throw eve::Exception("Body3D.setType: unknown body type '%s'", type.c_str());
}

const char *bodyTypeName(b3BodyType t) {
    switch (t) {
        case b3_staticBody: return "static";
        case b3_kinematicBody: return "kinematic";
        case b3_dynamicBody: return "dynamic";
        default: return "static";
    }
}

b3ShapeDef makeShapeDef(float density, float friction, float restitution) {
    b3ShapeDef def                 = b3DefaultShapeDef();
    def.density                    = density;
    def.baseMaterial.friction      = friction;
    def.baseMaterial.restitution   = restitution;
    def.enableContactEvents        = true;
    def.enableSensorEvents         = true;
    return def;
}

}  // namespace

Body3D::Body3D(World3D *world, b3BodyId bodyId, int id)
    : world_(world), bodyId_(bodyId), id_(id) {}

Body3D::~Body3D() {
    if (isValid() && world_ && world_->isValid()) {
        // Invalidate shapes that still reference this body.
        std::vector<Shape3D *> shapes(world_->shapes_.begin(), world_->shapes_.end());
        for (Shape3D *s : shapes) {
            if (s && s->getBody() == this) {
                world_->forgetShape(s);
                s->invalidate();
            }
        }
        b3Body_SetUserData(bodyId_, nullptr);
        b3DestroyBody(bodyId_);
        world_->forgetBody(this);
    }
    bodyId_ = {};
    world_  = nullptr;
}

bool Body3D::isValid() const { return b3Body_IsValid(bodyId_); }

void Body3D::invalidate() {
    if (isValid()) b3Body_SetUserData(bodyId_, nullptr);
    bodyId_ = {};
    world_  = nullptr;
}

void Body3D::destroy() {
    if (!isValid() || !world_ || !world_->isValid()) {
        invalidate();
        return;
    }
    std::vector<Shape3D *> shapes(world_->shapes_.begin(), world_->shapes_.end());
    for (Shape3D *s : shapes) {
        if (s && s->getBody() == this) {
            world_->forgetShape(s);
            // Destroy the underlying shape before invalidating the wrapper.
            if (s->isValid()) b3DestroyShape(s->raw(), false);
            s->invalidate();
        }
    }
    b3Body_SetUserData(bodyId_, nullptr);
    b3DestroyBody(bodyId_);
    world_->forgetBody(this);
    bodyId_ = {};
    world_  = nullptr;
}

void Body3D::setPosition(float x, float y, float z) {
    if (!isValid()) return;
    b3Body_SetTransform(bodyId_, b3Pos{x, y, z}, b3Body_GetRotation(bodyId_));
}

float Body3D::getX() const {
    if (!isValid()) return 0.f;
    return static_cast<float>(b3Body_GetPosition(bodyId_).x);
}

float Body3D::getY() const {
    if (!isValid()) return 0.f;
    return static_cast<float>(b3Body_GetPosition(bodyId_).y);
}

float Body3D::getZ() const {
    if (!isValid()) return 0.f;
    return static_cast<float>(b3Body_GetPosition(bodyId_).z);
}

void Body3D::setRotation(float qx, float qy, float qz, float qw) {
    if (!isValid()) return;
    b3Quat q;
    q.v = b3Vec3{qx, qy, qz};
    q.s = qw;
    float len = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (len > 1e-8f) {
        q.v.x /= len;
        q.v.y /= len;
        q.v.z /= len;
        q.s /= len;
    } else {
        q = b3Quat_identity;
    }
    b3Body_SetTransform(bodyId_, b3Body_GetPosition(bodyId_), q);
}

float Body3D::getRotX() const {
    if (!isValid()) return 0.f;
    return b3Body_GetRotation(bodyId_).v.x;
}

float Body3D::getRotY() const {
    if (!isValid()) return 0.f;
    return b3Body_GetRotation(bodyId_).v.y;
}

float Body3D::getRotZ() const {
    if (!isValid()) return 0.f;
    return b3Body_GetRotation(bodyId_).v.z;
}

float Body3D::getRotW() const {
    if (!isValid()) return 1.f;
    return b3Body_GetRotation(bodyId_).s;
}

void Body3D::setLinearVelocity(float vx, float vy, float vz) {
    if (!isValid()) return;
    b3Body_SetLinearVelocity(bodyId_, b3Vec3{vx, vy, vz});
}

float Body3D::getLinearVelocityX() const {
    if (!isValid()) return 0.f;
    return b3Body_GetLinearVelocity(bodyId_).x;
}

float Body3D::getLinearVelocityY() const {
    if (!isValid()) return 0.f;
    return b3Body_GetLinearVelocity(bodyId_).y;
}

float Body3D::getLinearVelocityZ() const {
    if (!isValid()) return 0.f;
    return b3Body_GetLinearVelocity(bodyId_).z;
}

float Body3D::getMass() const {
    if (!isValid()) return 0.f;
    return b3Body_GetMass(bodyId_);
}

void Body3D::setAngularVelocity(float wx, float wy, float wz) {
    if (!isValid()) return;
    b3Body_SetAngularVelocity(bodyId_, b3Vec3{wx, wy, wz});
}

float Body3D::getAngularVelocityX() const {
    if (!isValid()) return 0.f;
    return b3Body_GetAngularVelocity(bodyId_).x;
}

float Body3D::getAngularVelocityY() const {
    if (!isValid()) return 0.f;
    return b3Body_GetAngularVelocity(bodyId_).y;
}

float Body3D::getAngularVelocityZ() const {
    if (!isValid()) return 0.f;
    return b3Body_GetAngularVelocity(bodyId_).z;
}

void Body3D::applyForce(float fx, float fy, float fz) {
    if (!isValid()) return;
    b3Body_ApplyForceToCenter(bodyId_, b3Vec3{fx, fy, fz}, true);
}

void Body3D::applyForceAt(float fx, float fy, float fz, float x, float y, float z) {
    if (!isValid()) return;
    b3Body_ApplyForce(bodyId_, b3Vec3{fx, fy, fz}, b3Pos{x, y, z}, true);
}

void Body3D::applyLinearImpulse(float ix, float iy, float iz) {
    if (!isValid()) return;
    b3Body_ApplyLinearImpulseToCenter(bodyId_, b3Vec3{ix, iy, iz}, true);
}

void Body3D::applyAngularImpulse(float ix, float iy, float iz) {
    if (!isValid()) return;
    b3Body_ApplyAngularImpulse(bodyId_, b3Vec3{ix, iy, iz}, true);
}

void Body3D::setType(const std::string &bodyType) {
    if (!isValid()) return;
    b3Body_SetType(bodyId_, parseBodyType(bodyType));
}

std::string Body3D::getType() const {
    if (!isValid()) return "static";
    return bodyTypeName(b3Body_GetType(bodyId_));
}

void Body3D::setFixedRotation(bool fixed) {
    if (!isValid()) return;
    b3MotionLocks locks = b3Body_GetMotionLocks(bodyId_);
    locks.angularX = fixed;
    locks.angularY = fixed;
    locks.angularZ = fixed;
    b3Body_SetMotionLocks(bodyId_, locks);
}

bool Body3D::isFixedRotation() const {
    if (!isValid()) return false;
    b3MotionLocks locks = b3Body_GetMotionLocks(bodyId_);
    return locks.angularX && locks.angularY && locks.angularZ;
}

void Body3D::setActive(bool active) {
    if (!isValid()) return;
    if (active)
        b3Body_Enable(bodyId_);
    else
        b3Body_Disable(bodyId_);
}

bool Body3D::isActive() const { return isValid() ? b3Body_IsEnabled(bodyId_) : false; }

void Body3D::setBullet(bool bullet) {
    if (!isValid()) return;
    b3Body_SetBullet(bodyId_, bullet);
}

bool Body3D::isBullet() const { return isValid() ? b3Body_IsBullet(bodyId_) : false; }

void Body3D::setAwake(bool awake) {
    if (!isValid()) return;
    b3Body_SetAwake(bodyId_, awake);
}

bool Body3D::isAwake() const { return isValid() ? b3Body_IsAwake(bodyId_) : false; }

Shape3D *Body3D::newBoxShape(float width, float height, float depth, float density,
                             float friction, float restitution) {
    if (!isValid() || !world_) throw eve::Exception("Body3D.newBoxShape: body destroyed");
    if (width <= 0.f || height <= 0.f || depth <= 0.f)
        throw eve::Exception("Body3D.newBoxShape: width/height/depth must be > 0");

    float hx = width * 0.5f;
    float hy = height * 0.5f;
    float hz = depth * 0.5f;

    b3BoxHull  box = b3MakeBoxHull(hx, hy, hz);
    b3ShapeDef def = makeShapeDef(density, friction, restitution);
    b3ShapeId  id  = b3CreateHullShape(bodyId_, &def, &box.base);

    auto *shape = new Shape3D(world_, this, id, Shape3D::Kind::Box, hx, hy, hz);
    b3Shape_SetUserData(id, shape);
    world_->shapes_.insert(shape);
    return shape;
}

Shape3D *Body3D::newSphereShape(float radius, float density, float friction, float restitution) {
    if (!isValid() || !world_) throw eve::Exception("Body3D.newSphereShape: body destroyed");
    if (radius <= 0.f) throw eve::Exception("Body3D.newSphereShape: radius must be > 0");

    b3Sphere sphere;
    sphere.center = b3Vec3_zero;
    sphere.radius = radius;

    b3ShapeDef def = makeShapeDef(density, friction, restitution);
    b3ShapeId  id  = b3CreateSphereShape(bodyId_, &def, &sphere);

    auto *shape = new Shape3D(world_, this, id, Shape3D::Kind::Sphere, radius, 0.f, 0.f);
    b3Shape_SetUserData(id, shape);
    world_->shapes_.insert(shape);
    return shape;
}

Shape3D *Body3D::newCapsuleShape(float height, float radius, float density, float friction,
                                 float restitution) {
    if (!isValid() || !world_) throw eve::Exception("Body3D.newCapsuleShape: body destroyed");
    if (height < 0.f || radius <= 0.f)
        throw eve::Exception("Body3D.newCapsuleShape: height >= 0 and radius > 0 required");

    float half = height * 0.5f;
    b3Capsule capsule;
    capsule.center1 = b3Vec3{0.f, -half, 0.f};
    capsule.center2 = b3Vec3{0.f, half, 0.f};
    capsule.radius  = radius;

    b3ShapeDef def = makeShapeDef(density, friction, restitution);
    b3ShapeId  id  = b3CreateCapsuleShape(bodyId_, &def, &capsule);

    auto *shape = new Shape3D(world_, this, id, Shape3D::Kind::Capsule, half, radius, 0.f);
    b3Shape_SetUserData(id, shape);
    world_->shapes_.insert(shape);
    return shape;
}

}  // namespace eve::physics
