#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/Joint3D.h"

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

void requireFinite(float value, const char *operation, const char *parameter) {
    if (!std::isfinite(value))
        throw eve::Exception("%s: %s must be finite", operation, parameter);
}

void requireNonNegative(float value, const char *operation, const char *parameter) {
    requireFinite(value, operation, parameter);
    if (value < 0.f)
        throw eve::Exception("%s: %s must be >= 0", operation, parameter);
}

b3Vec3 checkedVector(float x, float y, float z, const char *operation,
                     const char *parameter) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("%s: %s components must be finite", operation, parameter);
    return {x, y, z};
}

b3Quat normalizedQuaternion(float qx, float qy, float qz, float qw, const char *operation) {
    requireFinite(qx, operation, "qx");
    requireFinite(qy, operation, "qy");
    requireFinite(qz, operation, "qz");
    requireFinite(qw, operation, "qw");
    const float lengthSquared = qx * qx + qy * qy + qz * qz + qw * qw;
    if (lengthSquared <= 1e-16f)
        throw eve::Exception("%s: quaternion length must be > 0", operation);
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    return b3Quat{{qx * inverseLength, qy * inverseLength, qz * inverseLength},
                  qw * inverseLength};
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

b3HullData *createCheckedHull(const std::vector<float> &vertices, int maxVertices,
                              const char *operation) {
    if (vertices.size() < 12 || vertices.size() % 3 != 0)
        throw eve::Exception("%s: vertices must contain at least four packed XYZ points",
                             operation);
    if (vertices.size() / 3 > 100000)
        throw eve::Exception("%s: source point count must be <= 100000", operation);
    if (maxVertices < 4 || maxVertices > 254)
        throw eve::Exception("%s: maxVertices must be in [4, 254]", operation);
    std::vector<b3Vec3> points;
    points.reserve(vertices.size() / 3);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        if (!std::isfinite(vertices[i]) || !std::isfinite(vertices[i + 1]) ||
            !std::isfinite(vertices[i + 2]))
            throw eve::Exception("%s: all vertex components must be finite", operation);
        points.push_back({vertices[i], vertices[i + 1], vertices[i + 2]});
    }
    b3HullData *hull = b3CreateHull(points.data(), static_cast<int>(points.size()), maxVertices);
    if (!hull)
        throw eve::Exception("%s: points do not form a valid three-dimensional convex hull",
                             operation);
    return hull;
}

b3MeshData *createCheckedMesh(const std::vector<float> &vertices,
                              const std::vector<int32_t> &indices, bool weldVertices,
                              float weldTolerance, bool identifyEdges, bool useMedianSplit,
                              const char *operation) {
    if (vertices.size() < 9 || vertices.size() % 3 != 0)
        throw eve::Exception("%s: vertices must contain at least three packed XYZ points",
                             operation);
    if (indices.size() < 3 || indices.size() % 3 != 0)
        throw eve::Exception("%s: indices must contain complete triangles", operation);
    if (vertices.size() / 3 > 1000000 || indices.size() / 3 > 2000000)
        throw eve::Exception("%s: mesh exceeds 1000000 vertices or 2000000 triangles",
                             operation);
    if (!std::isfinite(weldTolerance) || weldTolerance < 0.f)
        throw eve::Exception("%s: weldTolerance must be finite and >= 0", operation);
    std::vector<b3Vec3> points;
    points.reserve(vertices.size() / 3);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        if (!std::isfinite(vertices[i]) || !std::isfinite(vertices[i + 1]) ||
            !std::isfinite(vertices[i + 2]))
            throw eve::Exception("%s: all vertex components must be finite", operation);
        points.push_back({vertices[i], vertices[i + 1], vertices[i + 2]});
    }
    for (int32_t index : indices) {
        if (index < 0 || static_cast<size_t>(index) >= points.size())
            throw eve::Exception("%s: triangle index is outside the vertex array", operation);
    }
    std::vector<int32_t> mutableIndices = indices;
    b3MeshDef def{};
    def.vertices = points.data();
    def.indices = mutableIndices.data();
    def.vertexCount = static_cast<int>(points.size());
    def.triangleCount = static_cast<int>(indices.size() / 3);
    def.weldVertices = weldVertices;
    def.weldTolerance = weldTolerance;
    def.identifyEdges = identifyEdges;
    def.useMedianSplit = useMedianSplit;
    b3MeshData *mesh = b3CreateMesh(&def, nullptr, 0);
    if (!mesh || mesh->triangleCount != def.triangleCount) {
        if (mesh) b3DestroyMesh(mesh);
        throw eve::Exception("%s: mesh contains degenerate or zero-area triangles", operation);
    }
    return mesh;
}

b3HeightFieldData *createCheckedHeightField(int countX, int countZ, float cellSizeX,
                                            float cellSizeZ,
                                            const std::vector<float> &heights,
                                            float globalMin, float globalMax,
                                            bool clockwiseWinding, const char *operation) {
    if (countX < 2 || countZ < 2)
        throw eve::Exception("%s: countX and countZ must be >= 2", operation);
    const size_t sampleCount = static_cast<size_t>(countX) * static_cast<size_t>(countZ);
    if (sampleCount > 16000000)
        throw eve::Exception("%s: sample count must be <= 16000000", operation);
    if (heights.size() != sampleCount)
        throw eve::Exception("%s: heights size must equal countX * countZ", operation);
    if (!(cellSizeX > 0.f) || !(cellSizeZ > 0.f) || !std::isfinite(cellSizeX) ||
        !std::isfinite(cellSizeZ))
        throw eve::Exception("%s: cell sizes must be finite and > 0", operation);
    if (!std::isfinite(globalMin) || !std::isfinite(globalMax) || globalMin > globalMax)
        throw eve::Exception("%s: global height range must be finite and ordered", operation);
    for (float height : heights) {
        if (!std::isfinite(height) || height < globalMin || height > globalMax)
            throw eve::Exception("%s: every height must be finite and inside global range",
                                 operation);
    }
    std::vector<float> mutableHeights = heights;
    b3HeightFieldDef def{};
    def.heights = mutableHeights.data();
    def.scale = {cellSizeX, 1.f, cellSizeZ};
    def.countX = countX;
    def.countZ = countZ;
    def.globalMinimumHeight = globalMin;
    def.globalMaximumHeight = globalMax;
    def.clockwiseWinding = clockwiseWinding;
    return b3CreateHeightField(&def);
}

}  // namespace

Body3D::Body3D(World3D *world, b3BodyId bodyId, int id, PhysicsBodyHandle runtimeHandle)
    : world_(world), bodyId_(bodyId), id_(id), runtimeHandle_(runtimeHandle) {}

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
        b3Body_SetUserData(bodyId_, nullptr);
        b3DestroyBody(bodyId_);
        world_->forgetBody(this);
    }
    bodyId_ = {};
    world_  = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
}

bool Body3D::isValid() const { return b3Body_IsValid(bodyId_); }

void Body3D::invalidate() {
    if (isValid()) b3Body_SetUserData(bodyId_, nullptr);
    bodyId_ = {};
    world_  = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
}

void Body3D::destroy() {
    if (!isValid() || !world_ || !world_->isValid()) {
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
            if (s->isValid()) b3DestroyShape(s->raw(), false);
            s->invalidate();
        }
    }
    b3Body_SetUserData(bodyId_, nullptr);
    b3DestroyBody(bodyId_);
    world_->forgetBody(this);
    bodyId_ = {};
    world_  = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
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

std::vector<float> Body3D::localToWorldPoint(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    const b3Pos value = b3Body_GetWorldPoint(
        bodyId_, checkedVector(x, y, z, "Body3D.localToWorldPoint", "point"));
    return {static_cast<float>(value.x), static_cast<float>(value.y),
            static_cast<float>(value.z)};
}

std::vector<float> Body3D::worldToLocalPoint(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    const b3Vec3 value = b3Body_GetLocalPoint(
        bodyId_, checkedVector(x, y, z, "Body3D.worldToLocalPoint", "point"));
    return {value.x, value.y, value.z};
}

std::vector<float> Body3D::localToWorldVector(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    const b3Vec3 value = b3Body_GetWorldVector(
        bodyId_, checkedVector(x, y, z, "Body3D.localToWorldVector", "vector"));
    return {value.x, value.y, value.z};
}

std::vector<float> Body3D::worldToLocalVector(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    const b3Vec3 value = b3Body_GetLocalVector(
        bodyId_, checkedVector(x, y, z, "Body3D.worldToLocalVector", "vector"));
    return {value.x, value.y, value.z};
}

std::vector<float> Body3D::getLocalPointVelocity(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    const b3Vec3 value = b3Body_GetLocalPointVelocity(
        bodyId_, checkedVector(x, y, z, "Body3D.getLocalPointVelocity", "point"));
    return {value.x, value.y, value.z};
}

std::vector<float> Body3D::getWorldPointVelocity(float x, float y, float z) const {
    if (!isValid()) return {0.f, 0.f, 0.f};
    const b3Vec3 value = b3Body_GetWorldPointVelocity(
        bodyId_, checkedVector(x, y, z, "Body3D.getWorldPointVelocity", "point"));
    return {value.x, value.y, value.z};
}

void Body3D::applyForce(float fx, float fy, float fz) {
    if (!isValid()) return;
    b3Body_ApplyForceToCenter(bodyId_, b3Vec3{fx, fy, fz}, true);
}

void Body3D::applyForceAt(float fx, float fy, float fz, float x, float y, float z) {
    if (!isValid()) return;
    b3Body_ApplyForce(bodyId_, b3Vec3{fx, fy, fz}, b3Pos{x, y, z}, true);
}

void Body3D::applyTorque(float tx, float ty, float tz) {
    if (!isValid()) return;
    b3Body_ApplyTorque(bodyId_, b3Vec3{tx, ty, tz}, true);
}

void Body3D::applyLinearImpulse(float ix, float iy, float iz) {
    if (!isValid()) return;
    b3Body_ApplyLinearImpulseToCenter(bodyId_, b3Vec3{ix, iy, iz}, true);
}

void Body3D::applyLinearImpulseAt(float ix, float iy, float iz, float x, float y, float z) {
    if (!isValid()) return;
    b3Body_ApplyLinearImpulse(bodyId_, b3Vec3{ix, iy, iz}, b3Pos{x, y, z}, true);
}

void Body3D::applyAngularImpulse(float ix, float iy, float iz) {
    if (!isValid()) return;
    b3Body_ApplyAngularImpulse(bodyId_, b3Vec3{ix, iy, iz}, true);
}

void Body3D::applyLocalForce(float fx, float fy, float fz, float x, float y, float z) {
    if (!isValid()) return;
    const b3Vec3 force = b3Body_GetWorldVector(
        bodyId_, checkedVector(fx, fy, fz, "Body3D.applyLocalForce", "force"));
    const b3Pos point = b3Body_GetWorldPoint(
        bodyId_, checkedVector(x, y, z, "Body3D.applyLocalForce", "point"));
    b3Body_ApplyForce(bodyId_, force, point, true);
}

void Body3D::applyLocalForceToCenter(float fx, float fy, float fz) {
    if (!isValid()) return;
    const b3Vec3 force = checkedVector(fx, fy, fz, "Body3D.applyLocalForceToCenter",
                                       "force");
    b3Body_ApplyForceToCenter(bodyId_, b3Body_GetWorldVector(bodyId_, force), true);
}

void Body3D::applyLocalTorque(float tx, float ty, float tz) {
    if (!isValid()) return;
    const b3Vec3 torque = checkedVector(tx, ty, tz, "Body3D.applyLocalTorque", "torque");
    b3Body_ApplyTorque(bodyId_, b3Body_GetWorldVector(bodyId_, torque), true);
}

void Body3D::applyLocalLinearImpulse(float ix, float iy, float iz, float x, float y,
                                     float z) {
    if (!isValid()) return;
    const b3Vec3 impulse = b3Body_GetWorldVector(
        bodyId_, checkedVector(ix, iy, iz, "Body3D.applyLocalLinearImpulse", "impulse"));
    const b3Pos point = b3Body_GetWorldPoint(
        bodyId_, checkedVector(x, y, z, "Body3D.applyLocalLinearImpulse", "point"));
    b3Body_ApplyLinearImpulse(bodyId_, impulse, point, true);
}

void Body3D::applyLocalLinearImpulseToCenter(float ix, float iy, float iz) {
    if (!isValid()) return;
    const b3Vec3 impulse = checkedVector(ix, iy, iz,
                                         "Body3D.applyLocalLinearImpulseToCenter", "impulse");
    b3Body_ApplyLinearImpulseToCenter(bodyId_, b3Body_GetWorldVector(bodyId_, impulse), true);
}

void Body3D::applyLocalAngularImpulse(float ix, float iy, float iz) {
    if (!isValid()) return;
    const b3Vec3 impulse =
        checkedVector(ix, iy, iz, "Body3D.applyLocalAngularImpulse", "impulse");
    b3Body_ApplyAngularImpulse(bodyId_, b3Body_GetWorldVector(bodyId_, impulse), true);
}

void Body3D::setTargetTransform(float x, float y, float z, float qx, float qy, float qz,
                                float qw, float timeStep) {
    if (!isValid()) return;
    requireFinite(x, "Body3D.setTargetTransform", "x");
    requireFinite(y, "Body3D.setTargetTransform", "y");
    requireFinite(z, "Body3D.setTargetTransform", "z");
    requireFinite(timeStep, "Body3D.setTargetTransform", "timeStep");
    if (timeStep <= 0.f)
        throw eve::Exception("Body3D.setTargetTransform: timeStep must be > 0");
    const b3Quat rotation =
        normalizedQuaternion(qx, qy, qz, qw, "Body3D.setTargetTransform");
    b3Body_SetTargetTransform(bodyId_, b3WorldTransform{b3Pos{x, y, z}, rotation}, timeStep,
                              true);
}

void Body3D::setLinearDamping(float damping) {
    if (!isValid()) return;
    requireNonNegative(damping, "Body3D.setLinearDamping", "damping");
    b3Body_SetLinearDamping(bodyId_, damping);
}

float Body3D::getLinearDamping() const {
    return isValid() ? b3Body_GetLinearDamping(bodyId_) : 0.f;
}

void Body3D::setAngularDamping(float damping) {
    if (!isValid()) return;
    requireNonNegative(damping, "Body3D.setAngularDamping", "damping");
    b3Body_SetAngularDamping(bodyId_, damping);
}

float Body3D::getAngularDamping() const {
    return isValid() ? b3Body_GetAngularDamping(bodyId_) : 0.f;
}

void Body3D::setGravityScale(float scale) {
    if (!isValid()) return;
    requireFinite(scale, "Body3D.setGravityScale", "scale");
    b3Body_SetGravityScale(bodyId_, scale);
}

float Body3D::getGravityScale() const {
    return isValid() ? b3Body_GetGravityScale(bodyId_) : 0.f;
}

void Body3D::setSleepEnabled(bool enabled) {
    if (!isValid()) return;
    b3Body_EnableSleep(bodyId_, enabled);
}

bool Body3D::isSleepEnabled() const {
    return isValid() ? b3Body_IsSleepEnabled(bodyId_) : false;
}

void Body3D::setSleepThreshold(float threshold) {
    if (!isValid()) return;
    requireNonNegative(threshold, "Body3D.setSleepThreshold", "threshold");
    b3Body_SetSleepThreshold(bodyId_, threshold);
}

float Body3D::getSleepThreshold() const {
    return isValid() ? b3Body_GetSleepThreshold(bodyId_) : 0.f;
}

void Body3D::setMotionLocks(bool linearX, bool linearY, bool linearZ, bool angularX,
                            bool angularY, bool angularZ) {
    if (!isValid()) return;
    b3Body_SetMotionLocks(bodyId_,
                          b3MotionLocks{linearX, linearY, linearZ, angularX, angularY,
                                        angularZ});
}

#define EV_BODY_LOCK_GETTER(name, member)                          \
    bool Body3D::name() const {                                    \
        return isValid() ? b3Body_GetMotionLocks(bodyId_).member : false; \
    }
EV_BODY_LOCK_GETTER(isLinearXLocked, linearX)
EV_BODY_LOCK_GETTER(isLinearYLocked, linearY)
EV_BODY_LOCK_GETTER(isLinearZLocked, linearZ)
EV_BODY_LOCK_GETTER(isAngularXLocked, angularX)
EV_BODY_LOCK_GETTER(isAngularYLocked, angularY)
EV_BODY_LOCK_GETTER(isAngularZLocked, angularZ)
#undef EV_BODY_LOCK_GETTER

void Body3D::setMassProperties(float mass, float centerX, float centerY, float centerZ,
                               float inertiaXX, float inertiaYY, float inertiaZZ,
                               float inertiaXY, float inertiaXZ, float inertiaYZ) {
    if (!isValid()) return;
    constexpr const char *operation = "Body3D.setMassProperties";
    if (b3Body_GetType(bodyId_) != b3_dynamicBody)
        throw eve::Exception("%s: body must be dynamic", operation);
    requireFinite(mass, operation, "mass");
    requireFinite(centerX, operation, "centerX");
    requireFinite(centerY, operation, "centerY");
    requireFinite(centerZ, operation, "centerZ");
    requireFinite(inertiaXX, operation, "inertiaXX");
    requireFinite(inertiaYY, operation, "inertiaYY");
    requireFinite(inertiaZZ, operation, "inertiaZZ");
    requireFinite(inertiaXY, operation, "inertiaXY");
    requireFinite(inertiaXZ, operation, "inertiaXZ");
    requireFinite(inertiaYZ, operation, "inertiaYZ");
    if (!(mass > 0.f)) throw eve::Exception("%s: mass must be > 0", operation);

    // Sylvester's criterion for a symmetric positive-definite 3x3 tensor.
    const double xx = inertiaXX, yy = inertiaYY, zz = inertiaZZ;
    const double xy = inertiaXY, xz = inertiaXZ, yz = inertiaYZ;
    const double minor2 = xx * yy - xy * xy;
    const double determinant =
        xx * (yy * zz - yz * yz) - xy * (xy * zz - yz * xz) +
        xz * (xy * yz - yy * xz);
    if (!(xx > 0.0 && minor2 > 0.0 && determinant > 0.0) ||
        !std::isfinite(minor2) || !std::isfinite(determinant))
        throw eve::Exception("%s: inertia tensor must be positive definite", operation);

    b3MassData data{};
    data.mass = mass;
    data.center = {centerX, centerY, centerZ};
    data.inertia = {{inertiaXX, inertiaXY, inertiaXZ},
                    {inertiaXY, inertiaYY, inertiaYZ},
                    {inertiaXZ, inertiaYZ, inertiaZZ}};
    b3Body_SetMassData(bodyId_, data);
}

void Body3D::resetMassProperties() {
    if (!isValid()) return;
    b3Body_ApplyMassFromShapes(bodyId_);
}

#define EV_BODY_INERTIA_GETTER(name, column, member)               \
    float Body3D::name() const {                                   \
        return isValid() ? b3Body_GetMassData(bodyId_).inertia.column.member : 0.f; \
    }
EV_BODY_INERTIA_GETTER(getInertiaXX, cx, x)
EV_BODY_INERTIA_GETTER(getInertiaYY, cy, y)
EV_BODY_INERTIA_GETTER(getInertiaZZ, cz, z)
EV_BODY_INERTIA_GETTER(getInertiaXY, cx, y)
EV_BODY_INERTIA_GETTER(getInertiaXZ, cx, z)
EV_BODY_INERTIA_GETTER(getInertiaYZ, cy, z)
#undef EV_BODY_INERTIA_GETTER

#define EV_BODY_CENTER_GETTER(name, functionName, member)          \
    float Body3D::name() const {                                   \
        return isValid() ? static_cast<float>(functionName(bodyId_).member) : 0.f; \
    }
EV_BODY_CENTER_GETTER(getLocalCenterX, b3Body_GetLocalCenterOfMass, x)
EV_BODY_CENTER_GETTER(getLocalCenterY, b3Body_GetLocalCenterOfMass, y)
EV_BODY_CENTER_GETTER(getLocalCenterZ, b3Body_GetLocalCenterOfMass, z)
EV_BODY_CENTER_GETTER(getWorldCenterX, b3Body_GetWorldCenterOfMass, x)
EV_BODY_CENTER_GETTER(getWorldCenterY, b3Body_GetWorldCenterOfMass, y)
EV_BODY_CENTER_GETTER(getWorldCenterZ, b3Body_GetWorldCenterOfMass, z)
#undef EV_BODY_CENTER_GETTER

void Body3D::setType(const std::string &bodyType) {
    if (!isValid()) return;
    const b3BodyType type = parseBodyType(bodyType);
    if (type != b3_staticBody && world_) {
        for (Shape3D *shape : world_->shapes_) {
            if (shape && shape->getBody() == this &&
                (shape->kind_ == Shape3D::Kind::TriangleMesh ||
                 shape->kind_ == Shape3D::Kind::HeightField))
                throw eve::Exception(
                    "Body3D.setType: triangle-mesh and height-field colliders require a static body");
        }
    }
    b3Body_SetType(bodyId_, type);
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

    auto *shape = new Shape3D(world_, this, id, world_->nextShapeRuntimeHandle(), Shape3D::Kind::Box, hx, hy, hz);
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

    auto *shape =
        new Shape3D(world_, this, id, world_->nextShapeRuntimeHandle(), Shape3D::Kind::Sphere, radius, 0.f, 0.f);
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

    auto *shape =
        new Shape3D(world_, this, id, world_->nextShapeRuntimeHandle(), Shape3D::Kind::Capsule, half, radius, 0.f);
    b3Shape_SetUserData(id, shape);
    world_->shapes_.insert(shape);
    return shape;
}

Shape3D *Body3D::newConvexHullShape(const std::vector<float> &vertices, int maxVertices,
                                    float density, float friction, float restitution) {
    if (!isValid() || !world_)
        throw eve::Exception("Body3D.newConvexHullShape: body destroyed");
    b3HullData *hull = createCheckedHull(vertices, maxVertices, "Body3D.newConvexHullShape");
    b3ShapeDef def = makeShapeDef(density, friction, restitution);
    b3ShapeId id = b3CreateHullShape(bodyId_, &def, hull);
    b3DestroyHull(hull);

    auto *shape = new Shape3D(world_, this, id, world_->nextShapeRuntimeHandle(), Shape3D::Kind::ConvexHull, 0.f, 0.f,
                              0.f, vertices, maxVertices);
    b3Shape_SetUserData(id, shape);
    world_->shapes_.insert(shape);
    return shape;
}

Shape3D *Body3D::newTriangleMeshShape(const std::vector<float> &vertices,
                                      const std::vector<int32_t> &indices, bool weldVertices,
                                      float weldTolerance, bool identifyEdges,
                                      bool useMedianSplit) {
    if (!isValid() || !world_)
        throw eve::Exception("Body3D.newTriangleMeshShape: body destroyed");
    if (b3Body_GetType(bodyId_) != b3_staticBody)
        throw eve::Exception("Body3D.newTriangleMeshShape: triangle meshes require a static body");
    b3MeshData *mesh = createCheckedMesh(vertices, indices, weldVertices, weldTolerance,
                                         identifyEdges, useMedianSplit,
                                         "Body3D.newTriangleMeshShape");
    b3ShapeDef def = makeShapeDef(0.f, 0.2f, 0.f);
    b3ShapeId id = b3CreateMeshShape(bodyId_, &def, mesh, b3Vec3_one);
    if (B3_IS_NULL(id)) {
        b3DestroyMesh(mesh);
        throw eve::Exception("Body3D.newTriangleMeshShape: Box3D rejected the mesh shape");
    }
    auto *shape =
        new Shape3D(world_, this, id, world_->nextShapeRuntimeHandle(), Shape3D::Kind::TriangleMesh, 0.f, 0.f, 0.f, {},
                    64, vertices, indices, mesh, weldVertices, weldTolerance, identifyEdges, useMedianSplit);
    b3Shape_SetUserData(id, shape);
    world_->shapes_.insert(shape);
    return shape;
}

Shape3D *Body3D::newHeightFieldShape(int countX, int countZ, float cellSizeX,
                                     float cellSizeZ, const std::vector<float> &heights,
                                     float globalMin, float globalMax,
                                     bool clockwiseWinding) {
    if (!isValid() || !world_)
        throw eve::Exception("Body3D.newHeightFieldShape: body destroyed");
    if (b3Body_GetType(bodyId_) != b3_staticBody)
        throw eve::Exception("Body3D.newHeightFieldShape: height fields require a static body");
    b3HeightFieldData *heightData = createCheckedHeightField(
        countX, countZ, cellSizeX, cellSizeZ, heights, globalMin, globalMax,
        clockwiseWinding, "Body3D.newHeightFieldShape");
    b3ShapeDef def = makeShapeDef(0.f, 0.2f, 0.f);
    b3ShapeId id = b3CreateHeightFieldShape(bodyId_, &def, heightData);
    if (B3_IS_NULL(id)) {
        b3DestroyHeightField(heightData);
        throw eve::Exception("Body3D.newHeightFieldShape: Box3D rejected the height field");
    }
    auto *shape = new Shape3D(world_, this, id, world_->nextShapeRuntimeHandle(), Shape3D::Kind::HeightField, 0.f, 0.f,
                              0.f, {}, 64, {}, {}, nullptr, true, 0.001f, true, false, heights, countX, countZ,
                              cellSizeX, cellSizeZ, globalMin, globalMax, clockwiseWinding, heightData);
    b3Shape_SetUserData(id, shape);
    world_->shapes_.insert(shape);
    return shape;
}

}  // namespace eve::physics
