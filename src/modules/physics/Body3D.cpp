#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/Joint3D.h"

#include "common/Exception.h"


#include <cmath>
#include <vector>

namespace eve::physics {
namespace {

template <class T>
T legacyBodyValue(eve::Result<T> result) {
    if (!result.ok()) throw eve::Exception("%s", result.error()->message().c_str());
    return std::move(result).value();
}
void legacyBodyValue(eve::Result<void> result) {
    if (!result.ok()) throw eve::Exception("%s", result.error()->message().c_str());
}
}  // namespace

Body3D::Body3D(World3D* world, std::unique_ptr<IBodyBackend3D> backend, int id, PhysicsBodyHandle runtimeHandle)
    : world_(world), backend_(std::move(backend)), id_(id), runtimeHandle_(runtimeHandle) {
    EV_PARAM_CHECK(backend_ != nullptr, "Body3D requires a backend");
    backend_->bindOwner(*this);
}

Body3D::~Body3D() {
    if (isValid() && world_ && world_->isValid()) {
        std::vector<Joint3D *> joints(world_->joints_.begin(), world_->joints_.end());
        for (Joint3D *joint : joints) {
            if (joint && (joint->bodyA_ == this || joint->bodyB_ == this)) {
                world_->forgetJoint(joint);
                joint->invalidate();
            }
        }
        // Invalidate shapes that still reference this body.
        std::vector<Shape3D *> shapes(world_->shapes_.begin(), world_->shapes_.end());
        for (Shape3D *s : shapes) {
            if (s && s->getBody() == this) {
                world_->forgetShape(s);
                s->invalidate();
            }
        }
        world_->forgetBody(this);
    }
    if (backend_) backend_->destroy();
    if (backend_) backend_->invalidate();
    world_  = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
}

bool Body3D::isValid() const { return backend_ && backend_->isValid(); }

void Body3D::invalidate() {
    if (backend_) backend_->invalidate();
    world_  = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
}

void Body3D::destroy() {
    if (!isValid() || !world_ || !world_->isValid()) {
        if (backend_) backend_->destroy();
        invalidate();
        return;
    }
    std::vector<Joint3D *> joints(world_->joints_.begin(), world_->joints_.end());
    for (Joint3D *joint : joints) {
        if (joint && (joint->bodyA_ == this || joint->bodyB_ == this)) {
            world_->forgetJoint(joint);
            joint->invalidate();
        }
    }
    std::vector<Shape3D *> shapes(world_->shapes_.begin(), world_->shapes_.end());
    for (Shape3D *s : shapes) {
        if (s && s->getBody() == this) {
            world_->forgetShape(s);
            // Destroy the underlying shape before invalidating the wrapper.
            s->destroy();
        }
    }
    backend_->destroy();
    world_->forgetBody(this);
    if (backend_) backend_->invalidate();
    world_  = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
}

void Body3D::setPosition(float x, float y, float z) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setPosition(x, y, z));
}
float Body3D::getX() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getX());
}
float Body3D::getY() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getY());
}
float Body3D::getZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getZ());
}
void Body3D::setRotation(float qx, float qy, float qz, float qw) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setRotation(qx, qy, qz, qw));
}
float Body3D::getRotX() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getRotX());
}
float Body3D::getRotY() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getRotY());
}
float Body3D::getRotZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getRotZ());
}
float Body3D::getRotW() const {
    if (!isValid()) return 1.f;
    return legacyBodyValue(backend_->getRotW());
}
void Body3D::setLinearVelocity(float vx, float vy, float vz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setLinearVelocity(vx, vy, vz));
}
float Body3D::getLinearVelocityX() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getLinearVelocityX());
}
float Body3D::getLinearVelocityY() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getLinearVelocityY());
}
float Body3D::getLinearVelocityZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getLinearVelocityZ());
}
float Body3D::getMass() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getMass());
}
void Body3D::setAngularVelocity(float wx, float wy, float wz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setAngularVelocity(wx, wy, wz));
}
float Body3D::getAngularVelocityX() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getAngularVelocityX());
}
float Body3D::getAngularVelocityY() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getAngularVelocityY());
}
float Body3D::getAngularVelocityZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getAngularVelocityZ());
}
std::vector<float> Body3D::localToWorldPoint(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    return legacyBodyValue(backend_->localToWorldPoint(x, y, z));
}
eve::Result<PhysicsVector3D> Body3D::localToWorldPointOwned(float x, float y, float z) const {
    return backend_->localToWorldPointOwned(x, y, z);
}
std::vector<float> Body3D::worldToLocalPoint(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    return legacyBodyValue(backend_->worldToLocalPoint(x, y, z));
}
eve::Result<PhysicsVector3D> Body3D::worldToLocalPointOwned(float x, float y, float z) const {
    return backend_->worldToLocalPointOwned(x, y, z);
}
std::vector<float> Body3D::localToWorldVector(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    return legacyBodyValue(backend_->localToWorldVector(x, y, z));
}
eve::Result<PhysicsVector3D> Body3D::localToWorldVectorOwned(float x, float y, float z) const {
    return backend_->localToWorldVectorOwned(x, y, z);
}
std::vector<float> Body3D::worldToLocalVector(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    return legacyBodyValue(backend_->worldToLocalVector(x, y, z));
}
std::vector<float> Body3D::getLocalPointVelocity(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    return legacyBodyValue(backend_->getLocalPointVelocity(x, y, z));
}
eve::Result<PhysicsVector3D> Body3D::getLocalPointVelocityOwned(float x, float y, float z) const {
    return backend_->getLocalPointVelocityOwned(x, y, z);
}
std::vector<float> Body3D::getWorldPointVelocity(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    return legacyBodyValue(backend_->getWorldPointVelocity(x, y, z));
}
void Body3D::applyForce(float fx, float fy, float fz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyForce(fx, fy, fz));
}
void Body3D::applyForceAt(float fx, float fy, float fz, float x, float y, float z) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyForceAt(fx, fy, fz, x, y, z));
}
void Body3D::applyTorque(float tx, float ty, float tz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyTorque(tx, ty, tz));
}
void Body3D::applyLinearImpulse(float ix, float iy, float iz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyLinearImpulse(ix, iy, iz));
}
void Body3D::applyLinearImpulseAt(float ix, float iy, float iz, float x, float y, float z) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyLinearImpulseAt(ix, iy, iz, x, y, z));
}
void Body3D::applyAngularImpulse(float ix, float iy, float iz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyAngularImpulse(ix, iy, iz));
}
void Body3D::applyLocalForce(float fx, float fy, float fz, float x, float y, float z) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyLocalForce(fx, fy, fz, x, y, z));
}
void Body3D::applyLocalForceToCenter(float fx, float fy, float fz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyLocalForceToCenter(fx, fy, fz));
}
void Body3D::applyLocalTorque(float tx, float ty, float tz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyLocalTorque(tx, ty, tz));
}
void Body3D::applyLocalLinearImpulse(float ix, float iy, float iz, float x, float y,
                                     float z) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyLocalLinearImpulse(ix, iy, iz, x, y, z));
}
void Body3D::applyLocalLinearImpulseToCenter(float ix, float iy, float iz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyLocalLinearImpulseToCenter(ix, iy, iz));
}
void Body3D::applyLocalAngularImpulse(float ix, float iy, float iz) {
    if (!isValid()) return;
    legacyBodyValue(backend_->applyLocalAngularImpulse(ix, iy, iz));
}
void Body3D::setTargetTransform(float x, float y, float z, float qx, float qy, float qz,
                                float qw, float timeStep) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setTargetTransform(x, y, z, qx, qy, qz, qw, timeStep));
}
void Body3D::setLinearDamping(float damping) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setLinearDamping(damping));
}
float Body3D::getLinearDamping() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getLinearDamping());
}
void Body3D::setAngularDamping(float damping) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setAngularDamping(damping));
}
float Body3D::getAngularDamping() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getAngularDamping());
}
void Body3D::setGravityScale(float scale) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setGravityScale(scale));
}
float Body3D::getGravityScale() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getGravityScale());
}
void Body3D::setSleepEnabled(bool enabled) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setSleepEnabled(enabled));
}
bool Body3D::isSleepEnabled() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isSleepEnabled());
}
void Body3D::setSleepThreshold(float threshold) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setSleepThreshold(threshold));
}
float Body3D::getSleepThreshold() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getSleepThreshold());
}
void Body3D::setMotionLocks(bool linearX, bool linearY, bool linearZ, bool angularX,
                            bool angularY, bool angularZ) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setMotionLocks(linearX, linearY, linearZ, angularX, angularY, angularZ));
}
bool Body3D::isLinearXLocked() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isLinearXLocked());
}
bool Body3D::isLinearYLocked() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isLinearYLocked());
}
bool Body3D::isLinearZLocked() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isLinearZLocked());
}
bool Body3D::isAngularXLocked() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isAngularXLocked());
}
bool Body3D::isAngularYLocked() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isAngularYLocked());
}
bool Body3D::isAngularZLocked() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isAngularZLocked());
}
void Body3D::setMassProperties(float mass, float centerX, float centerY, float centerZ,
                               float inertiaXX, float inertiaYY, float inertiaZZ,
                               float inertiaXY, float inertiaXZ, float inertiaYZ) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setMassProperties(mass, centerX, centerY, centerZ, inertiaXX, inertiaYY, inertiaZZ,
                                                inertiaXY, inertiaXZ, inertiaYZ));
}
void Body3D::resetMassProperties() {
    if (!isValid()) return;
    legacyBodyValue(backend_->resetMassProperties());
}
float Body3D::getInertiaXX() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getInertiaXX());
}
float Body3D::getInertiaYY() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getInertiaYY());
}
float Body3D::getInertiaZZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getInertiaZZ());
}
float Body3D::getInertiaXY() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getInertiaXY());
}
float Body3D::getInertiaXZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getInertiaXZ());
}
float Body3D::getInertiaYZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getInertiaYZ());
}
float Body3D::getLocalCenterX() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getLocalCenterX());
}
float Body3D::getLocalCenterY() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getLocalCenterY());
}
float Body3D::getLocalCenterZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getLocalCenterZ());
}
float Body3D::getWorldCenterX() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getWorldCenterX());
}
float Body3D::getWorldCenterY() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getWorldCenterY());
}
float Body3D::getWorldCenterZ() const {
    if (!isValid()) return 0.f;
    return legacyBodyValue(backend_->getWorldCenterZ());
}
void Body3D::setType(const std::string &bodyType) {
    if (!isValid()) return;
    if (bodyType != "static" && world_) {
        for (Shape3D *shape : world_->shapes_) {
            if (shape && shape->getBody() == this &&
                (shape->geometry_.kind == Shape3D::Kind::TriangleMesh ||
                 shape->geometry_.kind == Shape3D::Kind::HeightField))
                throw eve::Exception(
                    "Body3D.setType: triangle-mesh and height-field colliders require a static body");
        }
    }
    legacyBodyValue(backend_->setType(bodyType));
}
std::string Body3D::getType() const {
    if (!isValid()) return "static";
    return legacyBodyValue(backend_->getType());
}
void Body3D::setFixedRotation(bool fixed) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setFixedRotation(fixed));
}
bool Body3D::isFixedRotation() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isFixedRotation());
}
void Body3D::setActive(bool active) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setActive(active));
}
bool Body3D::isActive() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isActive());
}
void Body3D::setBullet(bool bullet) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setBullet(bullet));
}
bool Body3D::isBullet() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isBullet());
}
void Body3D::setAwake(bool awake) {
    if (!isValid()) return;
    legacyBodyValue(backend_->setAwake(awake));
}
bool Body3D::isAwake() const {
    if (!isValid()) return false;
    return legacyBodyValue(backend_->isAwake());
}
Shape3D* Body3D::createShape(ShapeGeometry3D geometry, float density, float friction, float restitution) {
    if (!isValid() || !world_) throw eve::Exception("Body3D.createShape: body destroyed");
    auto backend = legacyBodyValue(backend_->createShape(geometry, density, friction, restitution));
    auto shape   = std::make_unique<Shape3D>(world_, this, std::move(backend), world_->nextShapeRuntimeHandle(),
                                             std::move(geometry));
    world_->shapes_.insert(shape.get());
    return shape.release();
}
Shape3D* Body3D::newBoxShape(float width, float height, float depth, float density, float friction, float restitution) {
    ShapeGeometry3D geometry;
    geometry.kind = ShapeKind3D::Box;
    geometry.a    = width * 0.5f;
    geometry.b    = height * 0.5f;
    geometry.c    = depth * 0.5f;
    return createShape(std::move(geometry), density, friction, restitution);
}
Shape3D* Body3D::newSphereShape(float radius, float density, float friction, float restitution) {
    ShapeGeometry3D geometry;
    geometry.kind = ShapeKind3D::Sphere;
    geometry.a    = radius;
    return createShape(std::move(geometry), density, friction, restitution);
}
Shape3D* Body3D::newCapsuleShape(float height, float radius, float density, float friction, float restitution) {
    ShapeGeometry3D geometry;
    geometry.kind = ShapeKind3D::Capsule;
    geometry.a    = height * 0.5f;
    geometry.b    = radius;
    return createShape(std::move(geometry), density, friction, restitution);
}
Shape3D* Body3D::newConvexHullShape(const std::vector<float>& vertices, int maxVertices, float density, float friction,
                                    float restitution) {
    ShapeGeometry3D geometry;
    geometry.kind            = ShapeKind3D::ConvexHull;
    geometry.hullVertices    = vertices;
    geometry.hullMaxVertices = maxVertices;
    return createShape(std::move(geometry), density, friction, restitution);
}
Shape3D* Body3D::newTriangleMeshShape(const std::vector<float>& vertices, const std::vector<int32_t>& indices,
                                      bool weldVertices, float weldTolerance, bool identifyEdges, bool useMedianSplit) {
    ShapeGeometry3D geometry;
    geometry.kind               = ShapeKind3D::TriangleMesh;
    geometry.meshVertices       = vertices;
    geometry.meshIndices        = indices;
    geometry.meshWeldVertices   = weldVertices;
    geometry.meshWeldTolerance  = weldTolerance;
    geometry.meshIdentifyEdges  = identifyEdges;
    geometry.meshUseMedianSplit = useMedianSplit;
    return createShape(std::move(geometry), 0.f, 0.2f, 0.f);
}
Shape3D* Body3D::newHeightFieldShape(int countX, int countZ, float cellSizeX, float cellSizeZ,
                                     const std::vector<float>& heights, float globalMin, float globalMax,
                                     bool clockwiseWinding) {
    ShapeGeometry3D geometry;
    geometry.kind            = ShapeKind3D::HeightField;
    geometry.heightCountX    = countX;
    geometry.heightCountZ    = countZ;
    geometry.heightCellSizeX = cellSizeX;
    geometry.heightCellSizeZ = cellSizeZ;
    geometry.heightValues    = heights;
    geometry.heightGlobalMin = globalMin;
    geometry.heightGlobalMax = globalMax;
    geometry.heightClockwise = clockwiseWinding;
    return createShape(std::move(geometry), 0.f, 0.2f, 0.f);
}
}  // namespace eve::physics
