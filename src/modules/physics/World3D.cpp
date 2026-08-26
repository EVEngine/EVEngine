#include "physics/World3D.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/PhysicsCapabilities.h"
#include "physics/Joint3D.h"

#include "common/Exception.h"
#include "common/Profile.h"
#include "event/Event.h"

#include <box3d/box3d.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_set>

namespace eve::physics {
namespace {

constexpr uint64_t combineModeMask = 0x7ull;
constexpr int frictionModeShift = 32;
constexpr int restitutionModeShift = 35;

float combineMaterialValue(float a, uint64_t materialA, float b, uint64_t materialB,
                           int shift, bool friction) {
    const uint64_t modeA = (materialA >> shift) & combineModeMask;
    const uint64_t modeB = (materialB >> shift) & combineModeMask;
    const uint64_t mode = std::max(modeA, modeB);
    switch (mode) {
        case 1: return 0.5f * (a + b);
        case 2: return std::min(a, b);
        case 3: return a * b;
        case 4: return std::max(a, b);
        default: return friction ? std::sqrt(a * b) : std::max(a, b);
    }
}

float mixFriction(float a, uint64_t materialA, float b, uint64_t materialB) {
    return combineMaterialValue(a, materialA, b, materialB, frictionModeShift, true);
}

float mixRestitution(float a, uint64_t materialA, float b, uint64_t materialB) {
    return combineMaterialValue(a, materialA, b, materialB, restitutionModeShift, false);
}

uint64_t stablePairKey(int idA, int idB) {
    const uint32_t low = static_cast<uint32_t>(std::min(idA, idB));
    const uint32_t high = static_cast<uint32_t>(std::max(idA, idB));
    return (static_cast<uint64_t>(high) << 32) | low;
}

bool pairContains(uint64_t key, int id) {
    const uint32_t value = static_cast<uint32_t>(id);
    return static_cast<uint32_t>(key) == value || static_cast<uint32_t>(key >> 32) == value;
}

b3BodyType parseBodyType(const std::string &type) {
    if (type == "static") return b3_staticBody;
    if (type == "kinematic") return b3_kinematicBody;
    if (type == "dynamic") return b3_dynamicBody;
    throw eve::Exception("World3D.newBody: unknown body type '%s' (use static|kinematic|dynamic)",
                         type.c_str());
}

Body3D *bodyFromShape(b3ShapeId shapeId) {
    if (!b3Shape_IsValid(shapeId)) return nullptr;
    return static_cast<Body3D *>(b3Body_GetUserData(b3Shape_GetBody(shapeId)));
}

Shape3D *shapeFromShape(b3ShapeId shapeId) {
    if (!b3Shape_IsValid(shapeId)) return nullptr;
    return static_cast<Shape3D *>(b3Shape_GetUserData(shapeId));
}

b3Quat normalizedQueryQuat(float qx, float qy, float qz, float qw, const char *operation) {
    if (!std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) || !std::isfinite(qw))
        throw eve::Exception("%s: quaternion must be finite", operation);
    const float lengthSquared = qx * qx + qy * qy + qz * qz + qw * qw;
    if (!(lengthSquared > 1e-12f))
        throw eve::Exception("%s: quaternion must be non-zero", operation);
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    return b3Quat{{qx * inverseLength, qy * inverseLength, qz * inverseLength},
                  qw * inverseLength};
}

void makeBoxProxyPoints(b3Vec3 (&points)[8], float width, float height, float depth, b3Quat q,
                        const char *operation) {
    if (!(width > 0.f) || !(height > 0.f) || !(depth > 0.f) || !std::isfinite(width) ||
        !std::isfinite(height) || !std::isfinite(depth))
        throw eve::Exception("%s: box dimensions must be finite and > 0", operation);
    const float hx = 0.5f * width, hy = 0.5f * height, hz = 0.5f * depth;
    int point = 0;
    for (int z = -1; z <= 1; z += 2)
        for (int y = -1; y <= 1; y += 2)
            for (int x = -1; x <= 1; x += 2)
                points[point++] = b3RotateVector(
                    q, b3Vec3{static_cast<float>(x) * hx, static_cast<float>(y) * hy,
                              static_cast<float>(z) * hz});
}

}  // namespace

World3D::World3D(float gravityX, float gravityY, float gravityZ, bool sleep) {
    b3WorldDef def = b3DefaultWorldDef();
    def.gravity    = b3Vec3{gravityX, gravityY, gravityZ};
    def.enableSleep = sleep;
    contactHertz_ = def.contactHertz;
    contactDampingRatio_ = def.contactDampingRatio;
    contactPushOutSpeed_ = def.contactSpeed;
    worldId_        = b3CreateWorld(&def);
    registerCameraObstructionWorld(this);
    b3World_SetFrictionCallback(worldId_, &mixFriction);
    b3World_SetRestitutionCallback(worldId_, &mixRestitution);
    b3World_SetCustomFilterCallback(worldId_, &World3D::customFilterCallback, this);
    b3World_SetPreSolveCallback(worldId_, &World3D::preSolveCallback, this);
}

void World3D::setContinuousCollisionEnabled(bool enabled) {
    if (isValid()) b3World_EnableContinuous(worldId_, enabled);
}

bool World3D::isContinuousCollisionEnabled() const {
    return isValid() && b3World_IsContinuousEnabled(worldId_);
}

void World3D::setRestitutionThreshold(float speed) {
    if (!std::isfinite(speed) || speed < 0.f)
        throw eve::Exception("World3D.setRestitutionThreshold: speed must be finite and >= 0");
    if (isValid()) b3World_SetRestitutionThreshold(worldId_, speed);
}

float World3D::getRestitutionThreshold() const {
    return isValid() ? b3World_GetRestitutionThreshold(worldId_) : 0.f;
}

void World3D::setContactTuning(float hertz, float dampingRatio, float pushOutSpeed) {
    if (!std::isfinite(hertz) || !(hertz > 0.f))
        throw eve::Exception("World3D.setContactTuning: hertz must be finite and > 0");
    if (!std::isfinite(dampingRatio) || dampingRatio < 0.f)
        throw eve::Exception("World3D.setContactTuning: dampingRatio must be finite and >= 0");
    if (!std::isfinite(pushOutSpeed) || pushOutSpeed < 0.f)
        throw eve::Exception("World3D.setContactTuning: pushOutSpeed must be finite and >= 0");
    contactHertz_ = hertz;
    contactDampingRatio_ = dampingRatio;
    contactPushOutSpeed_ = pushOutSpeed;
    if (isValid()) b3World_SetContactTuning(worldId_, hertz, dampingRatio, pushOutSpeed);
}

void World3D::setContactRecycleDistance(float distance) {
    if (!std::isfinite(distance) || distance < 0.f)
        throw eve::Exception("World3D.setContactRecycleDistance: distance must be finite and >= 0");
    if (isValid()) b3World_SetContactRecycleDistance(worldId_, distance);
}

float World3D::getContactRecycleDistance() const {
    return isValid() ? b3World_GetContactRecycleDistance(worldId_) : 0.f;
}

void World3D::setMaximumLinearSpeed(float speed) {
    if (!std::isfinite(speed) || !(speed > 0.f))
        throw eve::Exception("World3D.setMaximumLinearSpeed: speed must be finite and > 0");
    if (isValid()) b3World_SetMaximumLinearSpeed(worldId_, speed);
}

float World3D::getMaximumLinearSpeed() const {
    return isValid() ? b3World_GetMaximumLinearSpeed(worldId_) : 0.f;
}

void World3D::setWarmStartingEnabled(bool enabled) {
    if (isValid()) b3World_EnableWarmStarting(worldId_, enabled);
}

bool World3D::isWarmStartingEnabled() const {
    return isValid() && b3World_IsWarmStartingEnabled(worldId_);
}

int World3D::explode(float x, float y, float z, float radius, float falloff,
                     float impulsePerArea, int maskBits) {
    explosionResults_.clear();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("World3D.explode: center must be finite");
    if (!std::isfinite(radius) || radius < 0.f || !std::isfinite(falloff) || falloff < 0.f)
        throw eve::Exception("World3D.explode: radius and falloff must be finite and >= 0");
    if (!std::isfinite(impulsePerArea))
        throw eve::Exception("World3D.explode: impulsePerArea must be finite");
    if (!isValid() || impulsePerArea == 0.f) return 0;

    struct Before {
        Body3D *body = nullptr;
        b3Vec3 linear{};
        b3Vec3 angular{};
    };
    std::vector<Before> before;
    before.reserve(bodies_.size());
    for (Body3D *body : bodies_) {
        if (!body || !body->isValid() || b3Body_GetType(body->raw()) != b3_dynamicBody)
            continue;
        before.push_back({body, b3Body_GetLinearVelocity(body->raw()),
                          b3Body_GetAngularVelocity(body->raw())});
    }

    b3ExplosionDef def = b3DefaultExplosionDef();
    def.position = b3Pos{x, y, z};
    def.radius = radius;
    def.falloff = falloff;
    def.impulsePerArea = impulsePerArea;
    def.maskBits = static_cast<uint32_t>(maskBits);
    b3World_Explode(worldId_, &def);

    for (const Before &entry : before) {
        if (!entry.body->isValid()) continue;
        const b3Vec3 linear = b3Body_GetLinearVelocity(entry.body->raw());
        const b3Vec3 angular = b3Body_GetAngularVelocity(entry.body->raw());
        const b3Vec3 dv = linear - entry.linear;
        const b3Vec3 dw = angular - entry.angular;
        if (b3LengthSquared(dv) <= 1e-12f && b3LengthSquared(dw) <= 1e-12f) continue;
        explosionResults_.push_back({entry.body->getId(), dv.x, dv.y, dv.z,
                                     dw.x, dw.y, dw.z});
    }
    std::sort(explosionResults_.begin(), explosionResults_.end(),
              [](const ExplosionResult &a, const ExplosionResult &b) {
                  return a.bodyId < b.bodyId;
              });
    return static_cast<int>(explosionResults_.size());
}

const World3D::ExplosionResult &World3D::explosionResultAt(int index,
                                                           const char *operation) const {
    if (index < 0 || index >= static_cast<int>(explosionResults_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return explosionResults_[static_cast<size_t>(index)];
}

#define EV_EXPLOSION_INT_GETTER(name, member)                                      \
    int World3D::name(int index) const { return explosionResultAt(index, #name).member; }

EV_EXPLOSION_INT_GETTER(getExplosionResultBodyId, bodyId)
#undef EV_EXPLOSION_INT_GETTER

#define EV_EXPLOSION_FLOAT_GETTER(name, member)                                    \
    float World3D::name(int index) const { return explosionResultAt(index, #name).member; }

EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaVelocityX, deltaVX)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaVelocityY, deltaVY)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaVelocityZ, deltaVZ)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaAngularVelocityX, deltaWX)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaAngularVelocityY, deltaWY)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaAngularVelocityZ, deltaWZ)
#undef EV_EXPLOSION_FLOAT_GETTER

float World3D::getBoundsMinX() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).lowerBound.x) : 0.f;
}
float World3D::getBoundsMinY() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).lowerBound.y) : 0.f;
}
float World3D::getBoundsMinZ() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).lowerBound.z) : 0.f;
}
float World3D::getBoundsMaxX() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).upperBound.x) : 0.f;
}
float World3D::getBoundsMaxY() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).upperBound.y) : 0.f;
}
float World3D::getBoundsMaxZ() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).upperBound.z) : 0.f;
}

#define EV_COUNTER_GETTER(name, member)                                             \
    int World3D::name() const { return isValid() ? b3World_GetCounters(worldId_).member : 0; }

EV_COUNTER_GETTER(getBodyCount, bodyCount)
EV_COUNTER_GETTER(getShapeCount, shapeCount)
EV_COUNTER_GETTER(getContactCount, contactCount)
EV_COUNTER_GETTER(getJointCount, jointCount)
EV_COUNTER_GETTER(getIslandCount, islandCount)
EV_COUNTER_GETTER(getAwakeContactCount, awakeContactCount)
EV_COUNTER_GETTER(getRecycledContactCount, recycledContactCount)
EV_COUNTER_GETTER(getStaticTreeHeight, staticTreeHeight)
EV_COUNTER_GETTER(getDynamicTreeHeight, treeHeight)
EV_COUNTER_GETTER(getMemoryByteCount, byteCount)
#undef EV_COUNTER_GETTER

int World3D::getAwakeBodyCount() const {
    return isValid() ? b3World_GetAwakeBodyCount(worldId_) : 0;
}

#define EV_PROFILE_GETTER(name, member)                                             \
    float World3D::name() const { return isValid() ? b3World_GetProfile(worldId_).member : 0.f; }

EV_PROFILE_GETTER(getProfileStepMs, step)
EV_PROFILE_GETTER(getProfilePairsMs, pairs)
EV_PROFILE_GETTER(getProfileCollideMs, collide)
EV_PROFILE_GETTER(getProfileSolveMs, solve)
EV_PROFILE_GETTER(getProfileBulletsMs, bullets)
EV_PROFILE_GETTER(getProfileSensorsMs, sensors)
#undef EV_PROFILE_GETTER

World3D::~World3D() { destroy(); }

bool World3D::isValid() const { return !destroyed_ && b3World_IsValid(worldId_); }

void World3D::destroy() {
    if (destroyed_) return;
    unregisterCameraObstructionWorld(this);
    destroyed_ = true;

    std::vector<Joint3D *> joints(joints_.begin(), joints_.end());
    for (Joint3D *joint : joints) {
        if (joint) joint->invalidate();
    }
    joints_.clear();
    disabledBodyPairs_.clear();
    disabledShapePairs_.clear();

    std::vector<Body3D *> bodies(bodies_.begin(), bodies_.end());
    for (Body3D *b : bodies) {
        if (b) b->invalidate();
    }
    bodies_.clear();

    std::vector<Shape3D *> shapes(shapes_.begin(), shapes_.end());
    for (Shape3D *s : shapes) {
        if (s) s->invalidate();
    }
    shapes_.clear();

    if (b3World_IsValid(worldId_)) {
        b3DestroyWorld(worldId_);
    }
    clearContactEvents();
    shapeRecords_.clear();
    shapeHandles_.clear();
    worldId_ = {};
}

bool World3D::sphereCast(float x1, float y1, float z1, float x2, float y2, float z2,
                         float radius, uint64_t maskBits, int ignoredBodyId,
                         CameraSphereHit3D* out) const {
    if (out) *out = CameraSphereHit3D{};
    if (!out || !isValid() || radius < 0.f) return false;

    struct Collector {
        CameraSphereHit3D* out = nullptr;
        int ignoredBodyId = -1;
        static float callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
                              uint64_t, int, int, void* context) {
            auto* self = static_cast<Collector*>(context);
            Body3D* body = bodyFromShape(shapeId);
            if (!body || body->getId() == self->ignoredBodyId) return -1.f;
            if (!self->out->hit || fraction < self->out->fraction) {
                self->out->hit = true;
                self->out->bodyId = body->getId();
                self->out->fraction = fraction;
                self->out->x = static_cast<float>(point.x);
                self->out->y = static_cast<float>(point.y);
                self->out->z = static_cast<float>(point.z);
                self->out->nx = normal.x;
                self->out->ny = normal.y;
                self->out->nz = normal.z;
            }
            return fraction;
        }
    } collector{out, ignoredBodyId};

    const b3Vec3 point{0.f, 0.f, 0.f};
    const b3ShapeProxy proxy{&point, 1, radius};
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.maskBits = maskBits;
    b3World_CastShape(worldId_, b3Pos{x1, y1, z1}, &proxy,
                      b3Vec3{x2 - x1, y2 - y1, z2 - z1}, filter,
                      &Collector::callback, &collector);
    return out->hit;
}

Joint3D *World3D::newDistanceJoint(Body3D *bodyA, Body3D *bodyB, float anchorAX,
                                   float anchorAY, float anchorAZ, float anchorBX,
                                   float anchorBY, float anchorBZ, float length,
                                   bool collideConnected) {
    if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this || bodyA == bodyB)
        throw eve::Exception("World3D.newDistanceJoint: bodies must be distinct and belong to this world");
    const float values[] = {anchorAX, anchorAY, anchorAZ, anchorBX, anchorBY, anchorBZ, length};
    for (float value : values) {
        if (!std::isfinite(value))
            throw eve::Exception("World3D.newDistanceJoint: parameters must be finite");
    }
    if (length < 0.f)
        throw eve::Exception("World3D.newDistanceJoint: length must be >= 0");
    b3DistanceJointDef def = b3DefaultDistanceJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    def.base.localFrameA.p = b3Body_GetLocalPoint(bodyA->raw(), b3Pos{anchorAX, anchorAY, anchorAZ});
    def.base.localFrameB.p = b3Body_GetLocalPoint(bodyB->raw(), b3Pos{anchorBX, anchorBY, anchorBZ});
    def.base.collideConnected = collideConnected;
    def.length = length;
    b3JointId id = b3CreateDistanceJoint(worldId_, &def);
    auto *joint = new Joint3D(this, bodyA, bodyB, id, Joint3D::Kind::Distance, nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    return joint;
}

Joint3D *World3D::newRevoluteJoint(Body3D *bodyA, Body3D *bodyB, float anchorX,
                                   float anchorY, float anchorZ, float axisX, float axisY,
                                   float axisZ, bool collideConnected) {
    if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this || bodyA == bodyB)
        throw eve::Exception("World3D.newRevoluteJoint: bodies must be distinct and belong to this world");
    const float values[] = {anchorX, anchorY, anchorZ, axisX, axisY, axisZ};
    for (float value : values) {
        if (!std::isfinite(value))
            throw eve::Exception("World3D.newRevoluteJoint: parameters must be finite");
    }
    const float axisLength = std::sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
    if (axisLength <= 1e-8f)
        throw eve::Exception("World3D.newRevoluteJoint: axis length must be > 0");
    const b3Vec3 worldAxis{axisX / axisLength, axisY / axisLength, axisZ / axisLength};
    b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    const b3Pos anchor{anchorX, anchorY, anchorZ};
    def.base.localFrameA.p = b3Body_GetLocalPoint(bodyA->raw(), anchor);
    def.base.localFrameB.p = b3Body_GetLocalPoint(bodyB->raw(), anchor);
    const b3Vec3 localAxisA = b3Body_GetLocalVector(bodyA->raw(), worldAxis);
    const b3Vec3 localAxisB = b3Body_GetLocalVector(bodyB->raw(), worldAxis);
    def.base.localFrameA.q = b3ComputeQuatBetweenUnitVectors(b3Vec3_axisZ, localAxisA);
    def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(b3Vec3_axisZ, localAxisB);
    def.base.collideConnected = collideConnected;
    b3JointId id = b3CreateRevoluteJoint(worldId_, &def);
    auto *joint = new Joint3D(this, bodyA, bodyB, id, Joint3D::Kind::Revolute, nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    return joint;
}

Joint3D *World3D::newPrismaticJoint(Body3D *bodyA, Body3D *bodyB, float anchorX,
                                    float anchorY, float anchorZ, float axisX, float axisY,
                                    float axisZ, bool collideConnected) {
    if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this || bodyA == bodyB)
        throw eve::Exception("World3D.newPrismaticJoint: bodies must be distinct and belong to this world");
    const float values[] = {anchorX, anchorY, anchorZ, axisX, axisY, axisZ};
    for (float value : values) {
        if (!std::isfinite(value))
            throw eve::Exception("World3D.newPrismaticJoint: parameters must be finite");
    }
    const float axisLength = std::sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
    if (axisLength <= 1e-8f)
        throw eve::Exception("World3D.newPrismaticJoint: axis length must be > 0");
    const b3Vec3 worldAxis{axisX / axisLength, axisY / axisLength, axisZ / axisLength};
    const b3Pos anchor{anchorX, anchorY, anchorZ};
    b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    def.base.localFrameA.p = b3Body_GetLocalPoint(bodyA->raw(), anchor);
    def.base.localFrameB.p = b3Body_GetLocalPoint(bodyB->raw(), anchor);
    def.base.localFrameA.q = b3ComputeQuatBetweenUnitVectors(
        b3Vec3_axisX, b3Body_GetLocalVector(bodyA->raw(), worldAxis));
    def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(
        b3Vec3_axisX, b3Body_GetLocalVector(bodyB->raw(), worldAxis));
    def.base.collideConnected = collideConnected;
    const b3JointId id = b3CreatePrismaticJoint(worldId_, &def);
    auto *joint = new Joint3D(this, bodyA, bodyB, id, Joint3D::Kind::Prismatic,
                              nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    return joint;
}

Joint3D *World3D::newSphericalJoint(Body3D *bodyA, Body3D *bodyB, float anchorX,
                                    float anchorY, float anchorZ, float axisX, float axisY,
                                    float axisZ, bool collideConnected) {
    if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this || bodyA == bodyB)
        throw eve::Exception("World3D.newSphericalJoint: bodies must be distinct and belong to this world");
    const float values[] = {anchorX, anchorY, anchorZ, axisX, axisY, axisZ};
    for (float value : values) {
        if (!std::isfinite(value))
            throw eve::Exception("World3D.newSphericalJoint: parameters must be finite");
    }
    const float axisLength = std::sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
    if (axisLength <= 1e-8f)
        throw eve::Exception("World3D.newSphericalJoint: axis length must be > 0");
    const b3Vec3 worldAxis{axisX / axisLength, axisY / axisLength, axisZ / axisLength};
    const b3Pos anchor{anchorX, anchorY, anchorZ};
    b3SphericalJointDef def = b3DefaultSphericalJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    def.base.localFrameA.p = b3Body_GetLocalPoint(bodyA->raw(), anchor);
    def.base.localFrameB.p = b3Body_GetLocalPoint(bodyB->raw(), anchor);
    def.base.localFrameA.q = b3ComputeQuatBetweenUnitVectors(
        b3Vec3_axisZ, b3Body_GetLocalVector(bodyA->raw(), worldAxis));
    def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(
        b3Vec3_axisZ, b3Body_GetLocalVector(bodyB->raw(), worldAxis));
    def.base.collideConnected = collideConnected;
    const b3JointId id = b3CreateSphericalJoint(worldId_, &def);
    auto *joint = new Joint3D(this, bodyA, bodyB, id, Joint3D::Kind::Spherical,
                              nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    return joint;
}

Joint3D *World3D::newWheelJoint(Body3D *bodyA, Body3D *bodyB, float anchorX,
                                float anchorY, float anchorZ, float suspensionAxisX,
                                float suspensionAxisY, float suspensionAxisZ, float wheelAxisX,
                                float wheelAxisY, float wheelAxisZ, bool collideConnected) {
    if (!isValid() || !bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this || bodyA == bodyB)
        throw eve::Exception("World3D.newWheelJoint: bodies must be distinct and belong to this world");
    const float values[] = {anchorX, anchorY, anchorZ, suspensionAxisX, suspensionAxisY,
                            suspensionAxisZ, wheelAxisX, wheelAxisY, wheelAxisZ};
    for (float value : values) {
        if (!std::isfinite(value))
            throw eve::Exception("World3D.newWheelJoint: parameters must be finite");
    }
    const float suspensionLength = std::sqrt(suspensionAxisX * suspensionAxisX +
                                             suspensionAxisY * suspensionAxisY +
                                             suspensionAxisZ * suspensionAxisZ);
    const float wheelLength =
        std::sqrt(wheelAxisX * wheelAxisX + wheelAxisY * wheelAxisY + wheelAxisZ * wheelAxisZ);
    if (suspensionLength <= 1e-8f || wheelLength <= 1e-8f)
        throw eve::Exception("World3D.newWheelJoint: both axis lengths must be > 0");
    const b3Vec3 suspensionAxis{suspensionAxisX / suspensionLength,
                                suspensionAxisY / suspensionLength,
                                suspensionAxisZ / suspensionLength};
    const b3Vec3 wheelAxis{wheelAxisX / wheelLength, wheelAxisY / wheelLength,
                           wheelAxisZ / wheelLength};
    if (std::fabs(b3Dot(suspensionAxis, wheelAxis)) > 0.999f)
        throw eve::Exception("World3D.newWheelJoint: suspension and wheel axes must not be parallel");
    const b3Pos anchor{anchorX, anchorY, anchorZ};
    b3WheelJointDef def = b3DefaultWheelJointDef();
    def.base.bodyIdA = bodyA->raw();
    def.base.bodyIdB = bodyB->raw();
    def.base.localFrameA.p = b3Body_GetLocalPoint(bodyA->raw(), anchor);
    def.base.localFrameB.p = b3Body_GetLocalPoint(bodyB->raw(), anchor);
    def.base.localFrameA.q = b3ComputeQuatBetweenUnitVectors(
        b3Vec3_axisX, b3Body_GetLocalVector(bodyA->raw(), suspensionAxis));
    def.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(
        b3Vec3_axisZ, b3Body_GetLocalVector(bodyB->raw(), wheelAxis));
    def.base.collideConnected = collideConnected;
    const b3JointId id = b3CreateWheelJoint(worldId_, &def);
    auto *joint =
        new Joint3D(this, bodyA, bodyB, id, Joint3D::Kind::Wheel, nextJointId());
    b3Joint_SetUserData(id, joint);
    joints_.insert(joint);
    return joint;
}

void World3D::forgetJoint(Joint3D *joint) { joints_.erase(joint); }

void World3D::update(float dt) { updateFull(dt, 4); }

void World3D::updateFull(float dt, int subStepCount) {
    EV_PROFILE_MODULE("physics", "World3D::update");
    clearContactEvents();
    if (!isValid()) return;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;
    if (subStepCount < 1) subStepCount = 1;
    for (Shape3D *shape : shapes_) {
        if (shape && shape->isOneWay()) shape->refreshOneWayWorldData();
    }
    b3World_Step(worldId_, dt, subStepCount);
    emitContactEvents();
}

void World3D::setGravity(float gx, float gy, float gz) {
    if (!isValid()) return;
    b3World_SetGravity(worldId_, b3Vec3{gx, gy, gz});
}

float World3D::getGravityX() const {
    if (!isValid()) return 0.f;
    return b3World_GetGravity(worldId_).x;
}

float World3D::getGravityY() const {
    if (!isValid()) return 0.f;
    return b3World_GetGravity(worldId_).y;
}

float World3D::getGravityZ() const {
    if (!isValid()) return 0.f;
    return b3World_GetGravity(worldId_).z;
}

void World3D::setHitEventThreshold(float speed) {
    if (!std::isfinite(speed) || speed < 0.f)
        throw eve::Exception("World3D.setHitEventThreshold: speed must be finite and >= 0");
    if (isValid()) b3World_SetHitEventThreshold(worldId_, speed);
}

float World3D::getHitEventThreshold() const {
    return isValid() ? b3World_GetHitEventThreshold(worldId_) : 0.f;
}

void World3D::refreshContactFilter(Shape3D *shape) {
    if (!shape || !shape->isValid()) return;
    b3Shape_SetFilter(shape->raw(), b3Shape_GetFilter(shape->raw()), true);
}

void World3D::setBodyPairCollisionEnabled(Body3D *bodyA, Body3D *bodyB, bool enabled) {
    if (!bodyA || !bodyB || bodyA == bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this)
        throw eve::Exception(
            "World3D.setBodyPairCollisionEnabled: distinct live bodies must belong to this world");
    const uint64_t key = stablePairKey(bodyA->getId(), bodyB->getId());
    for (Shape3D *shape : shapes_) {
        if (shape && (shape->getBody() == bodyA || shape->getBody() == bodyB))
            shape->enableContactOverrideFiltering();
    }
    if (enabled) disabledBodyPairs_.erase(key); else disabledBodyPairs_.insert(key);
    for (Shape3D *shape : shapes_) {
        if (shape && (shape->getBody() == bodyA || shape->getBody() == bodyB))
            refreshContactFilter(shape);
    }
}

bool World3D::isBodyPairCollisionEnabled(Body3D *bodyA, Body3D *bodyB) const {
    if (!bodyA || !bodyB || bodyA == bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this)
        throw eve::Exception(
            "World3D.isBodyPairCollisionEnabled: distinct live bodies must belong to this world");
    return disabledBodyPairs_.count(stablePairKey(bodyA->getId(), bodyB->getId())) == 0;
}

void World3D::setShapePairCollisionEnabled(Shape3D *shapeA, Shape3D *shapeB, bool enabled) {
    if (!shapeA || !shapeB || shapeA == shapeB || !shapeA->isValid() || !shapeB->isValid() ||
        !shapeA->getBody() || !shapeB->getBody() ||
        shapeA->getBody()->getWorld() != this || shapeB->getBody()->getWorld() != this)
        throw eve::Exception(
            "World3D.setShapePairCollisionEnabled: distinct live shapes must belong to this world");
    const uint64_t key = stablePairKey(shapeA->getId(), shapeB->getId());
    shapeA->enableContactOverrideFiltering();
    shapeB->enableContactOverrideFiltering();
    if (enabled) disabledShapePairs_.erase(key); else disabledShapePairs_.insert(key);
    refreshContactFilter(shapeA);
    refreshContactFilter(shapeB);
}

bool World3D::isShapePairCollisionEnabled(Shape3D *shapeA, Shape3D *shapeB) const {
    if (!shapeA || !shapeB || shapeA == shapeB || !shapeA->isValid() || !shapeB->isValid() ||
        !shapeA->getBody() || !shapeB->getBody() ||
        shapeA->getBody()->getWorld() != this || shapeB->getBody()->getWorld() != this)
        throw eve::Exception(
            "World3D.isShapePairCollisionEnabled: distinct live shapes must belong to this world");
    return disabledShapePairs_.count(stablePairKey(shapeA->getId(), shapeB->getId())) == 0;
}

bool World3D::customFilterCallback(b3ShapeId shapeIdA, b3ShapeId shapeIdB, void *context) {
    auto *world = static_cast<World3D *>(context);
    Shape3D *shapeA = shapeFromShape(shapeIdA);
    Shape3D *shapeB = shapeFromShape(shapeIdB);
    Body3D *bodyA = bodyFromShape(shapeIdA);
    Body3D *bodyB = bodyFromShape(shapeIdB);
    if (!world || !shapeA || !shapeB || !bodyA || !bodyB) return false;
    if (world->disabledBodyPairs_.count(stablePairKey(bodyA->getId(), bodyB->getId())) != 0)
        return false;
    return world->disabledShapePairs_.count(stablePairKey(shapeA->getId(), shapeB->getId())) == 0;
}

void World3D::removeCollisionOverridesForBody(int bodyId) {
    for (auto it = disabledBodyPairs_.begin(); it != disabledBodyPairs_.end();) {
        if (pairContains(*it, bodyId)) it = disabledBodyPairs_.erase(it); else ++it;
    }
}

void World3D::removeCollisionOverridesForShape(int shapeId) {
    for (auto it = disabledShapePairs_.begin(); it != disabledShapePairs_.end();) {
        if (pairContains(*it, shapeId)) it = disabledShapePairs_.erase(it); else ++it;
    }
}

int World3D::nextBodyId() { return nextId_++; }

Body3D *World3D::newBody(const std::string &bodyType, float x, float y, float z) {
    if (!isValid()) throw eve::Exception("World3D.newBody: world destroyed");

    b3BodyDef def = b3DefaultBodyDef();
    def.type      = parseBodyType(bodyType);
    def.position  = b3Pos{x, y, z};
    def.rotation  = b3Quat_identity;

    b3BodyId raw  = b3CreateBody(worldId_, &def);
    Body3D  *body = new Body3D(this, raw, nextBodyId());
    b3Body_SetUserData(raw, body);
    bodies_.insert(body);
    return body;
}

b3QueryFilter World3D::makeQueryFilter() const {
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.categoryBits = queryCategoryBits_;
    filter.maskBits = queryMaskBits_;
    return filter;
}

bool World3D::shouldIgnoreQueryShape(b3ShapeId shapeId) const {
    Body3D *body = bodyFromShape(shapeId);
    Shape3D *shape = shapeFromShape(shapeId);
    if (!body || !shape) return true;
    return body->getId() == queryIgnoredBodyId_ || shape->getId() == queryIgnoredShapeId_;
}

void World3D::setQueryFilter(int categoryBits, int maskBits) {
    queryCategoryBits_ = static_cast<uint32_t>(categoryBits);
    queryMaskBits_ = static_cast<uint32_t>(maskBits);
}

void World3D::resetQueryFilter() {
    queryCategoryBits_ = 0xFFFFFFFFu;
    queryMaskBits_ = 0xFFFFFFFFu;
}

void World3D::setQueryIgnoredBodyId(int bodyId) {
    if (bodyId == 0 || bodyId < -1)
        throw eve::Exception("World3D.setQueryIgnoredBodyId: use a positive stable id or -1");
    queryIgnoredBodyId_ = bodyId;
}

void World3D::setQueryIgnoredShapeId(int shapeId) {
    if (shapeId == 0 || shapeId < -1)
        throw eve::Exception("World3D.setQueryIgnoredShapeId: use a positive stable id or -1");
    queryIgnoredShapeId_ = shapeId;
}

void World3D::clearQueryIgnores() {
    queryIgnoredBodyId_ = -1;
    queryIgnoredShapeId_ = -1;
}

void World3D::destroyBody(Body3D *body) {
    if (!body) return;
    body->destroy();
}

void World3D::forgetBody(Body3D *body) {
    if (body) removeCollisionOverridesForBody(body->getId());
    bodies_.erase(body);
}
void World3D::forgetShape(Shape3D *shape) {
    if (shape) removeCollisionOverridesForShape(shape->getId());
    shapes_.erase(shape);
    for (auto it = shapeHandles_.begin(); it != shapeHandles_.end();) {
        if (it->second == shape)
            it = shapeHandles_.erase(it);
        else
            ++it;
    }
}

void World3D::registerShapeHandle(Shape3D *shape) {
    if (!shape || !shape->getBody() || B3_IS_NULL(shape->raw())) return;
    for (auto it = shapeHandles_.begin(); it != shapeHandles_.end();) {
        if (it->second == shape)
            it = shapeHandles_.erase(it);
        else
            ++it;
    }
    shapeHandles_[b3StoreShapeId(shape->raw())] = shape;
    shapeRecords_[b3StoreShapeId(shape->raw())] =
        EventShape{shape->getBody()->getId(), shape->getId(), shape->getTag()};
}

bool World3D::preSolveCallback(b3ShapeId shapeIdA, b3ShapeId shapeIdB, b3Pos point,
                               b3Vec3 normal, void *context) {
    auto *world = static_cast<World3D *>(context);
    if (!world) return true;
    auto findShape = [world](b3ShapeId id) -> Shape3D * {
        const auto found = world->shapeHandles_.find(b3StoreShapeId(id));
        return found == world->shapeHandles_.end() ? nullptr : found->second;
    };
    Shape3D *shapeA = findShape(shapeIdA);
    Shape3D *shapeB = findShape(shapeIdB);
    if (shapeA && shapeA->isOneWay() &&
        !shapeA->allowsOneWayContact(point.x, point.y, point.z, normal.x, normal.y, normal.z))
        return false;
    if (shapeB && shapeB->isOneWay() &&
        !shapeB->allowsOneWayContact(point.x, point.y, point.z, -normal.x, -normal.y,
                                     -normal.z))
        return false;
    return true;
}

void World3D::updateShapeTag(Shape3D *shape) {
    if (!shape) return;
    for (auto &[key, record] : shapeRecords_) {
        (void)key;
        if (record.shapeId == shape->getId()) record.shapeTag = shape->getTag();
    }
}

World3D::EventShape World3D::eventShapeFrom(b3ShapeId shapeId) const {
    auto found = shapeRecords_.find(b3StoreShapeId(shapeId));
    if (found != shapeRecords_.end()) return found->second;
    Shape3D *shape = shapeFromShape(shapeId);
    Body3D *body = bodyFromShape(shapeId);
    if (!shape || !body) return {};
    return EventShape{body->getId(), shape->getId(), shape->getTag()};
}

void World3D::clearContactEvents() {
    beginContacts_.clear();
    endContacts_.clear();
    beginTriggers_.clear();
    endTriggers_.clear();
    hits_.clear();
    jointStressEvents_.clear();
    contactPoints_.clear();
}

int World3D::queryBodyContacts(int bodyId, int maxPoints) {
    contactPoints_.clear();
    if (bodyId <= 0)
        throw eve::Exception("World3D.queryBodyContacts: bodyId must be a positive stable id");
    constexpr int maxContactPoints = 4096;
    if (maxPoints < 1 || maxPoints > maxContactPoints)
        throw eve::Exception("World3D.queryBodyContacts: maxPoints must be in [1, 4096]");
    if (!isValid()) return 0;

    Body3D *target = nullptr;
    for (Body3D *body : bodies_) {
        if (body && body->isValid() && body->getId() == bodyId) {
            target = body;
            break;
        }
    }
    if (!target) return 0;

    constexpr int maxContactPairs = 4096;
    const int capacity = std::min(b3Body_GetContactCapacity(target->raw()), maxContactPairs);
    if (capacity <= 0) return 0;
    std::vector<b3ContactData> contacts(static_cast<size_t>(capacity));
    const int contactCount = b3Body_GetContactData(target->raw(), contacts.data(), capacity);
    contactPoints_.reserve(static_cast<size_t>(std::min(maxPoints, contactCount * 2)));

    for (int contactIndex = 0; contactIndex < contactCount; ++contactIndex) {
        const b3ContactData &contact = contacts[static_cast<size_t>(contactIndex)];
        Shape3D *shapeA = shapeFromShape(contact.shapeIdA);
        Shape3D *shapeB = shapeFromShape(contact.shapeIdB);
        Body3D *bodyA = bodyFromShape(contact.shapeIdA);
        Body3D *bodyB = bodyFromShape(contact.shapeIdB);
        if (!shapeA || !shapeB || !bodyA || !bodyB) continue;
        const bool targetIsA = bodyA == target;
        if (!targetIsA && bodyB != target) continue;
        Shape3D *ownShape = targetIsA ? shapeA : shapeB;
        Shape3D *otherShape = targetIsA ? shapeB : shapeA;
        Body3D *otherBody = targetIsA ? bodyB : bodyA;
        const b3Pos centerOfMass = b3Body_GetWorldCenterOfMass(target->raw());

        for (int manifoldIndex = 0; manifoldIndex < contact.manifoldCount; ++manifoldIndex) {
            const b3Manifold &manifold = contact.manifolds[manifoldIndex];
            const b3Vec3 normal = targetIsA ? manifold.normal : -manifold.normal;
            for (int pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex) {
                if (static_cast<int>(contactPoints_.size()) >= maxContactPoints) break;
                const b3ManifoldPoint &point = manifold.points[pointIndex];
                const b3Pos worldPoint =
                    b3OffsetPos(centerOfMass, targetIsA ? point.anchorA : point.anchorB);
                contactPoints_.push_back({ownShape->getId(),
                                          ownShape->getTag(),
                                          otherBody->getId(),
                                          otherShape->getId(),
                                          otherShape->getTag(),
                                          point.featureId,
                                          static_cast<float>(worldPoint.x),
                                          static_cast<float>(worldPoint.y),
                                          static_cast<float>(worldPoint.z),
                                          normal.x,
                                          normal.y,
                                          normal.z,
                                          point.separation,
                                          point.normalImpulse,
                                          point.totalNormalImpulse,
                                          point.normalVelocity,
                                          point.persisted});
            }
        }
    }

    std::sort(contactPoints_.begin(), contactPoints_.end(),
              [](const ContactPointResult &a, const ContactPointResult &b) {
                  if (a.shapeId != b.shapeId) return a.shapeId < b.shapeId;
                  if (a.otherShapeId != b.otherShapeId) return a.otherShapeId < b.otherShapeId;
                  if (a.featureId != b.featureId) return a.featureId < b.featureId;
                  if (a.x != b.x) return a.x < b.x;
                  if (a.y != b.y) return a.y < b.y;
                  return a.z < b.z;
              });
    if (static_cast<int>(contactPoints_.size()) > maxPoints)
        contactPoints_.resize(static_cast<size_t>(maxPoints));
    return static_cast<int>(contactPoints_.size());
}

void World3D::emitContactEvents() {
    if (!isValid()) return;

    auto *ev = eve::ModuleManager::getInstance<eve::event::Event>("Event");

    b3ContactEvents contacts = b3World_GetContactEvents(worldId_);
    for (int i = 0; i < contacts.beginCount; ++i) {
        EventShape a = eventShapeFrom(contacts.beginEvents[i].shapeIdA);
        EventShape b = eventShapeFrom(contacts.beginEvents[i].shapeIdB);
        if (a.shapeId == 0 || b.shapeId == 0) continue;
        if (a.shapeId > b.shapeId) std::swap(a, b);
        beginContacts_.push_back({a, b});
        if (ev) {
            std::vector<eve::event::Variant> args = {
                eve::event::Variant::makeInt(a.bodyId), eve::event::Variant::makeInt(b.bodyId),
                eve::event::Variant::makeInt(a.shapeId), eve::event::Variant::makeInt(b.shapeId),
                eve::event::Variant::makeInt(a.shapeTag), eve::event::Variant::makeInt(b.shapeTag)};
            ev->push(new eve::event::Message("begincontact3d", args));
        }
    }
    for (int i = 0; i < contacts.endCount; ++i) {
        EventShape a = eventShapeFrom(contacts.endEvents[i].shapeIdA);
        EventShape b = eventShapeFrom(contacts.endEvents[i].shapeIdB);
        if (a.shapeId == 0 || b.shapeId == 0) continue;
        if (a.shapeId > b.shapeId) std::swap(a, b);
        endContacts_.push_back({a, b});
        if (ev) {
            std::vector<eve::event::Variant> args = {
                eve::event::Variant::makeInt(a.bodyId), eve::event::Variant::makeInt(b.bodyId),
                eve::event::Variant::makeInt(a.shapeId), eve::event::Variant::makeInt(b.shapeId),
                eve::event::Variant::makeInt(a.shapeTag), eve::event::Variant::makeInt(b.shapeTag)};
            ev->push(new eve::event::Message("endcontact3d", args));
        }
    }

    for (int i = 0; i < contacts.hitCount; ++i) {
        const b3ContactHitEvent &source = contacts.hitEvents[i];
        EventShape a = eventShapeFrom(source.shapeIdA);
        EventShape b = eventShapeFrom(source.shapeIdB);
        if (a.shapeId == 0 || b.shapeId == 0) continue;

        float normalImpulse = 0.f;
        if (b3Contact_IsValid(source.contactId)) {
            const b3ContactData data = b3Contact_GetData(source.contactId);
            for (int manifoldIndex = 0; manifoldIndex < data.manifoldCount; ++manifoldIndex) {
                const b3Manifold &manifold = data.manifolds[manifoldIndex];
                for (int pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex)
                    normalImpulse += manifold.points[pointIndex].totalNormalImpulse;
            }
        }

        b3Vec3 normal = source.normal;
        if (a.shapeId > b.shapeId) {
            std::swap(a, b);
            normal = -normal;
        }
        hits_.push_back({a,
                         b,
                         static_cast<float>(source.point.x),
                         static_cast<float>(source.point.y),
                         static_cast<float>(source.point.z),
                         normal.x,
                         normal.y,
                         normal.z,
                         source.approachSpeed,
                         normalImpulse});
        if (ev) {
            std::vector<eve::event::Variant> args = {
                eve::event::Variant::makeInt(a.bodyId), eve::event::Variant::makeInt(b.bodyId),
                eve::event::Variant::makeInt(a.shapeId), eve::event::Variant::makeInt(b.shapeId),
                eve::event::Variant::makeInt(a.shapeTag), eve::event::Variant::makeInt(b.shapeTag)};
            ev->push(new eve::event::Message("hit3d", args));
        }
    }

    b3SensorEvents sensors = b3World_GetSensorEvents(worldId_);
    for (int i = 0; i < sensors.beginCount; ++i) {
        EventShape sensor = eventShapeFrom(sensors.beginEvents[i].sensorShapeId);
        EventShape visitor = eventShapeFrom(sensors.beginEvents[i].visitorShapeId);
        if (sensor.shapeId == 0 || visitor.shapeId == 0) continue;
        beginTriggers_.push_back({sensor, visitor});
        if (ev) {
            std::vector<eve::event::Variant> args = {
                eve::event::Variant::makeInt(sensor.bodyId),
                eve::event::Variant::makeInt(visitor.bodyId),
                eve::event::Variant::makeInt(sensor.shapeId),
                eve::event::Variant::makeInt(visitor.shapeId),
                eve::event::Variant::makeInt(sensor.shapeTag),
                eve::event::Variant::makeInt(visitor.shapeTag)};
            ev->push(new eve::event::Message("begintrigger3d", args));
        }
    }
    for (int i = 0; i < sensors.endCount; ++i) {
        EventShape sensor = eventShapeFrom(sensors.endEvents[i].sensorShapeId);
        EventShape visitor = eventShapeFrom(sensors.endEvents[i].visitorShapeId);
        if (sensor.shapeId == 0 || visitor.shapeId == 0) continue;
        endTriggers_.push_back({sensor, visitor});
        if (ev) {
            std::vector<eve::event::Variant> args = {
                eve::event::Variant::makeInt(sensor.bodyId),
                eve::event::Variant::makeInt(visitor.bodyId),
                eve::event::Variant::makeInt(sensor.shapeId),
                eve::event::Variant::makeInt(visitor.shapeId),
                eve::event::Variant::makeInt(sensor.shapeTag),
                eve::event::Variant::makeInt(visitor.shapeTag)};
            ev->push(new eve::event::Message("endtrigger3d", args));
        }
    }

    const b3JointEvents jointEvents = b3World_GetJointEvents(worldId_);
    for (int i = 0; i < jointEvents.count; ++i) {
        const b3JointEvent &source = jointEvents.jointEvents[i];
        auto *joint = static_cast<Joint3D *>(source.userData);
        if (!joint || joints_.find(joint) == joints_.end() || !joint->isValid() ||
            joint->raw().index1 != source.jointId.index1 ||
            joint->raw().world0 != source.jointId.world0 ||
            joint->raw().generation != source.jointId.generation)
            continue;
        const b3Vec3 force = b3Joint_GetConstraintForce(source.jointId);
        const b3Vec3 torque = b3Joint_GetConstraintTorque(source.jointId);
        jointStressEvents_.push_back(
            {joint->getId(), joint->getBodyAId(), joint->getBodyBId(),
             static_cast<int>(joint->kind_), force.x, force.y, force.z, torque.x, torque.y,
             torque.z});
        if (ev) {
            std::vector<eve::event::Variant> args = {
                eve::event::Variant::makeInt(joint->getId()),
                eve::event::Variant::makeInt(joint->getBodyAId()),
                eve::event::Variant::makeInt(joint->getBodyBId()),
                eve::event::Variant::makeInt(static_cast<int>(joint->kind_))};
            ev->push(new eve::event::Message("jointstress3d", args));
        }
    }

    // Invalid handles are retained only until the step that can report their end event.
    // This keeps destruction-safe metadata without allowing the history map to grow forever.
    for (auto it = shapeRecords_.begin(); it != shapeRecords_.end();) {
        if (!b3Shape_IsValid(b3LoadShapeId(it->first)))
            it = shapeRecords_.erase(it);
        else
            ++it;
    }
    for (auto it = shapeHandles_.begin(); it != shapeHandles_.end();) {
        if (!b3Shape_IsValid(b3LoadShapeId(it->first)))
            it = shapeHandles_.erase(it);
        else
            ++it;
    }
}

int World3D::rayCast(float x1, float y1, float z1, float x2, float y2, float z2) {
    rayCastAll(x1, y1, z1, x2, y2, z2, 1);
    return rayHitBodyId_;
}

int World3D::rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2,
                             uint64_t maskBits) {
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.maskBits = maskBits;
    rayCastAllInternal(x1, y1, z1, x2, y2, z2, 1, filter);
    return rayHitBodyId_;
}

int World3D::rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2,
                        int maxHits) {
    return rayCastAllInternal(x1, y1, z1, x2, y2, z2, maxHits, makeQueryFilter());
}

int World3D::rayCastAllInternal(float x1, float y1, float z1, float x2, float y2,
                                float z2, int maxHits, b3QueryFilter filter) {
    rayHitBodyId_   = -1;
    rayHitShapeId_  = -1;
    rayHitShapeTag_ = 0;
    rayHitMaterialId_ = 0;
    rayHitTriangleIndex_ = -1;
    rayHitX_        = 0.f;
    rayHitY_        = 0.f;
    rayHitZ_        = 0.f;
    rayHitNormalX_  = 0.f;
    rayHitNormalY_  = 0.f;
    rayHitNormalZ_  = 0.f;
    rayHitFraction_ = 0.f;
    rayResults_.clear();
    if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(z1) ||
        !std::isfinite(x2) || !std::isfinite(y2) || !std::isfinite(z2))
        throw eve::Exception("World3D.rayCastAll: endpoints must be finite");
    constexpr int maxRayResults = 4096;
    if (maxHits < 1 || maxHits > maxRayResults)
        throw eve::Exception("World3D.rayCastAll: maxHits must be in [1, 4096]");
    if (!isValid()) return 0;

    b3Pos  origin{x1, y1, z1};
    b3Vec3 translation{x2 - x1, y2 - y1, z2 - z1};
    struct RayCollector {
        World3D *world = nullptr;
        int capacity = 0;

        static bool better(const RayResult &a, const RayResult &b) {
            return a.fraction < b.fraction ||
                   (a.fraction == b.fraction && a.shapeId < b.shapeId);
        }

        float clipFraction() const {
            if (static_cast<int>(world->rayResults_.size()) < capacity) return 1.f;
            const auto worst = std::max_element(world->rayResults_.begin(),
                                                world->rayResults_.end(), better);
            return worst->fraction;
        }

        static float callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
                              uint64_t materialId, int triangleIndex, int, void *context) {
            auto *self = static_cast<RayCollector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return -1.f;
            Body3D *body = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (!body || !shape) return -1.f;

            RayResult candidate{body->getId(),
                                shape->getId(),
                                shape->getTag(),
                                static_cast<int>(static_cast<uint32_t>(materialId)),
                                triangleIndex,
                                static_cast<float>(point.x),
                                static_cast<float>(point.y),
                                static_cast<float>(point.z),
                                normal.x,
                                normal.y,
                                normal.z,
                                fraction};
            auto &results = self->world->rayResults_;
            if (static_cast<int>(results.size()) < self->capacity) {
                results.push_back(candidate);
            } else {
                auto worst = std::max_element(results.begin(), results.end(), better);
                if (better(candidate, *worst)) *worst = candidate;
            }
            return self->clipFraction();
        }
    } collector{this, maxHits};
    rayResults_.reserve(static_cast<size_t>(maxHits));
    b3World_CastRay(worldId_, origin, translation, filter, &RayCollector::callback, &collector);
    std::sort(rayResults_.begin(), rayResults_.end(), RayCollector::better);
    if (!rayResults_.empty()) {
        const RayResult &hit = rayResults_.front();
        rayHitBodyId_ = hit.bodyId;
        rayHitShapeId_ = hit.shapeId;
        rayHitShapeTag_ = hit.shapeTag;
        rayHitMaterialId_ = hit.materialId;
        rayHitTriangleIndex_ = hit.triangleIndex;
        rayHitX_ = hit.x;
        rayHitY_ = hit.y;
        rayHitZ_ = hit.z;
        rayHitNormalX_ = hit.normalX;
        rayHitNormalY_ = hit.normalY;
        rayHitNormalZ_ = hit.normalZ;
        rayHitFraction_ = hit.fraction;
    }
    return static_cast<int>(rayResults_.size());
}

int World3D::queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    queryBodyIds_.clear();
    queryShapes_.clear();
    if (!isValid()) return 0;

    struct Collector {
        World3D *world = nullptr;
        std::vector<int>       *bodyIds = nullptr;
        std::vector<std::pair<int, int>> *shapes = nullptr;
        std::unordered_set<int> seen;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<Collector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
            Body3D *b  = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (!b || !shape) return true;
            int id = b->getId();
            if (self->seen.insert(id).second) self->bodyIds->push_back(id);
            self->shapes->emplace_back(shape->getId(), shape->getTag());
            return true;
        }
    } cb;
    cb.world = this;
    cb.bodyIds = &queryBodyIds_;
    cb.shapes = &queryShapes_;

    b3AABB aabb;
    aabb.lowerBound = b3Vec3{std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ)};
    aabb.upperBound = b3Vec3{std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ)};
    b3QueryFilter filter = makeQueryFilter();
    b3World_OverlapAABB(worldId_, aabb, filter, &Collector::callback, &cb);
    std::sort(queryBodyIds_.begin(), queryBodyIds_.end());
    std::sort(queryShapes_.begin(), queryShapes_.end());
    return static_cast<int>(queryBodyIds_.size());
}

int World3D::overlapProxy(b3Pos origin, const b3ShapeProxy &proxy) {
    queryBodyIds_.clear();
    queryShapes_.clear();
    if (!isValid()) return 0;

    struct Collector {
        World3D *world = nullptr;
        std::vector<int> *bodyIds = nullptr;
        std::vector<std::pair<int, int>> *shapes = nullptr;
        std::unordered_set<int> seen;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<Collector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
            Body3D *body = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (body && shape) {
                if (self->seen.insert(body->getId()).second)
                    self->bodyIds->push_back(body->getId());
                self->shapes->emplace_back(shape->getId(), shape->getTag());
            }
            return true;
        }
    } collector;
    collector.world = this;
    collector.bodyIds = &queryBodyIds_;
    collector.shapes = &queryShapes_;
    b3QueryFilter filter = makeQueryFilter();
    b3World_OverlapShape(worldId_, origin, &proxy, filter, &Collector::callback, &collector);
    // Broad-phase traversal order is unspecified; stable ids make script behavior deterministic.
    std::sort(queryBodyIds_.begin(), queryBodyIds_.end());
    std::sort(queryShapes_.begin(), queryShapes_.end());
    return static_cast<int>(queryBodyIds_.size());
}

int World3D::querySphere(float x, float y, float z, float radius) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !(radius >= 0.f) ||
        !std::isfinite(radius))
        throw eve::Exception("World3D.querySphere: coordinates and radius must be finite; radius >= 0");
    const b3Vec3 point = b3Vec3_zero;
    const b3ShapeProxy proxy{&point, 1, radius};
    return overlapProxy(b3Pos{x, y, z}, proxy);
}

int World3D::queryCapsule(float ax, float ay, float az, float bx, float by, float bz,
                          float radius) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) ||
        !std::isfinite(by) || !std::isfinite(bz) || !(radius >= 0.f) || !std::isfinite(radius))
        throw eve::Exception("World3D.queryCapsule: coordinates and radius must be finite; radius >= 0");
    // Relative points preserve Box3D's large-world precision.
    const b3Vec3 points[2] = {b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}};
    const b3ShapeProxy proxy{points, 2, radius};
    return overlapProxy(b3Pos{ax, ay, az}, proxy);
}

int World3D::queryBox(float x, float y, float z, float width, float height, float depth, float qx,
                      float qy, float qz, float qw) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("World3D.queryBox: center must be finite");
    const b3Quat rotation = normalizedQueryQuat(qx, qy, qz, qw, "World3D.queryBox");
    b3Vec3 points[8];
    makeBoxProxyPoints(points, width, height, depth, rotation, "World3D.queryBox");
    const b3ShapeProxy proxy{points, 8, 0.f};
    return overlapProxy(b3Pos{x, y, z}, proxy);
}

int World3D::castProxyAll(b3Pos origin, const b3ShapeProxy &proxy, b3Vec3 translation,
                          int maxHits) {
    shapeCastBodyId_ = -1;
    shapeCastShapeId_ = -1;
    shapeCastShapeTag_ = 0;
    shapeCastMaterialId_ = 0;
    shapeCastTriangleIndex_ = -1;
    shapeCastX_ = shapeCastY_ = shapeCastZ_ = 0.f;
    shapeCastNormalX_ = shapeCastNormalY_ = shapeCastNormalZ_ = 0.f;
    shapeCastFraction_ = 0.f;
    shapeCastResults_.clear();
    constexpr int maxShapeCastResults = 4096;
    if (maxHits < 1 || maxHits > maxShapeCastResults)
        throw eve::Exception("World3D Shape Cast: maxHits must be in [1, 4096]");
    if (!isValid()) return 0;

    struct CastCollector {
        World3D *world = nullptr;
        int capacity = 0;

        static bool better(const ShapeCastResult &a, const ShapeCastResult &b) {
            return a.fraction < b.fraction ||
                   (a.fraction == b.fraction && a.shapeId < b.shapeId);
        }

        float clipFraction() const {
            if (static_cast<int>(world->shapeCastResults_.size()) < capacity) return 1.f;
            const auto worst = std::max_element(world->shapeCastResults_.begin(),
                                                world->shapeCastResults_.end(), better);
            return worst->fraction;
        }

        static float callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
                              uint64_t materialId, int triangleIndex, int, void *context) {
            auto *self = static_cast<CastCollector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return -1.f;
            Body3D *body = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (!body || !shape) return -1.f;

            ShapeCastResult candidate{body->getId(),
                                      shape->getId(),
                                      shape->getTag(),
                                      static_cast<int>(static_cast<uint32_t>(materialId)),
                                      triangleIndex,
                                      static_cast<float>(point.x),
                                      static_cast<float>(point.y),
                                      static_cast<float>(point.z),
                                      normal.x,
                                      normal.y,
                                      normal.z,
                                      fraction};
            auto &results = self->world->shapeCastResults_;
            if (static_cast<int>(results.size()) < self->capacity) {
                results.push_back(candidate);
            } else {
                auto worst = std::max_element(results.begin(), results.end(), better);
                if (better(candidate, *worst)) *worst = candidate;
            }
            return self->clipFraction();
        }
    } collector{this, maxHits};
    shapeCastResults_.reserve(static_cast<size_t>(maxHits));
    b3QueryFilter filter = makeQueryFilter();
    b3World_CastShape(worldId_, origin, &proxy, translation, filter, &CastCollector::callback,
                      &collector);
    std::sort(shapeCastResults_.begin(), shapeCastResults_.end(), CastCollector::better);
    if (!shapeCastResults_.empty()) {
        const ShapeCastResult &hit = shapeCastResults_.front();
        shapeCastBodyId_ = hit.bodyId;
        shapeCastShapeId_ = hit.shapeId;
        shapeCastShapeTag_ = hit.shapeTag;
        shapeCastMaterialId_ = hit.materialId;
        shapeCastTriangleIndex_ = hit.triangleIndex;
        shapeCastX_ = hit.x;
        shapeCastY_ = hit.y;
        shapeCastZ_ = hit.z;
        shapeCastNormalX_ = hit.normalX;
        shapeCastNormalY_ = hit.normalY;
        shapeCastNormalZ_ = hit.normalZ;
        shapeCastFraction_ = hit.fraction;
    }
    return static_cast<int>(shapeCastResults_.size());
}

int World3D::castSphere(float x, float y, float z, float radius, float dx, float dy, float dz) {
    castSphereAll(x, y, z, radius, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz,
                           int maxHits) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !(radius >= 0.f) ||
        !std::isfinite(radius) || !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
        throw eve::Exception("World3D.castSphere: all values must be finite; radius >= 0");
    const b3Vec3 point = b3Vec3_zero;
    const b3ShapeProxy proxy{&point, 1, radius};
    return castProxyAll(b3Pos{x, y, z}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

int World3D::castCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius,
                         float dx, float dy, float dz) {
    castCapsuleAll(ax, ay, az, bx, by, bz, radius, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz,
                            float radius, float dx, float dy, float dz, int maxHits) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) ||
        !std::isfinite(by) || !std::isfinite(bz) || !(radius >= 0.f) || !std::isfinite(radius) ||
        !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
        throw eve::Exception("World3D.castCapsule: all values must be finite; radius >= 0");
    const b3Vec3 points[2] = {b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}};
    const b3ShapeProxy proxy{points, 2, radius};
    return castProxyAll(b3Pos{ax, ay, az}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

int World3D::castBox(float x, float y, float z, float width, float height, float depth, float qx,
                     float qy, float qz, float qw, float dx, float dy, float dz) {
    castBoxAll(x, y, z, width, height, depth, qx, qy, qz, qw, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castBoxAll(float x, float y, float z, float width, float height, float depth, float qx,
                        float qy, float qz, float qw, float dx, float dy, float dz, int maxHits) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(dx) ||
        !std::isfinite(dy) || !std::isfinite(dz))
        throw eve::Exception("World3D.castBox: center and translation must be finite");
    const b3Quat rotation = normalizedQueryQuat(qx, qy, qz, qw, "World3D.castBox");
    b3Vec3 points[8];
    makeBoxProxyPoints(points, width, height, depth, rotation, "World3D.castBox");
    const b3ShapeProxy proxy{points, 8, 0.f};
    return castProxyAll(b3Pos{x, y, z}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

int World3D::closestPoint(float x, float y, float z, float maxDistance) {
    closestBodyId_ = -1;
    closestShapeId_ = -1;
    closestShapeTag_ = 0;
    closestX_ = closestY_ = closestZ_ = 0.f;
    closestNormalX_ = closestNormalY_ = closestNormalZ_ = 0.f;
    closestDistance_ = 0.f;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !(maxDistance >= 0.f) || !std::isfinite(maxDistance))
        throw eve::Exception(
            "World3D.closestPoint: target must be finite; maxDistance finite and >= 0");
    constexpr double floatLimit = static_cast<double>(FLT_MAX);
    const double distance = static_cast<double>(maxDistance);
    if (distance * distance > floatLimit ||
        std::fabs(static_cast<double>(x)) + distance > floatLimit ||
        std::fabs(static_cast<double>(y)) + distance > floatLimit ||
        std::fabs(static_cast<double>(z)) + distance > floatLimit)
        throw eve::Exception("World3D.closestPoint: search bounds are too large");
    if (!isValid()) return -1;

    struct ClosestCollector {
        World3D *world = nullptr;
        b3Vec3 target = b3Vec3_zero;
        float bestDistanceSquared = 0.f;
        int bodyId = -1;
        int stableShapeId = -1;
        int shapeTag = 0;
        b3Vec3 point = b3Vec3_zero;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<ClosestCollector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
            Body3D *body = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (!body || !shape) return true;
            const b3Vec3 point = b3Shape_GetClosestPoint(shapeId, self->target);
            const b3Vec3 delta = self->target - point;
            const float distanceSquared = b3LengthSquared(delta);
            const int bodyId = body->getId();
            const int stableShapeId = shape->getId();
            constexpr float tieTolerance = 1e-7f;
            if (distanceSquared < self->bestDistanceSquared - tieTolerance ||
                (std::fabs(distanceSquared - self->bestDistanceSquared) <= tieTolerance &&
                 (self->bodyId < 0 || bodyId < self->bodyId ||
                  (bodyId == self->bodyId && stableShapeId < self->stableShapeId)))) {
                self->bestDistanceSquared = distanceSquared;
                self->bodyId = bodyId;
                self->stableShapeId = stableShapeId;
                self->shapeTag = shape->getTag();
                self->point = point;
            }
            return true;
        }
    } collector;
    collector.world = this;
    collector.target = b3Vec3{x, y, z};
    collector.bestDistanceSquared = maxDistance * maxDistance;

    const b3Vec3 extent{maxDistance, maxDistance, maxDistance};
    const b3AABB bounds{collector.target - extent, collector.target + extent};
    const b3QueryFilter filter = makeQueryFilter();
    b3World_OverlapAABB(worldId_, bounds, filter, &ClosestCollector::callback, &collector);
    if (collector.bodyId < 0) return -1;

    closestBodyId_ = collector.bodyId;
    closestShapeId_ = collector.stableShapeId;
    closestShapeTag_ = collector.shapeTag;
    closestX_ = collector.point.x;
    closestY_ = collector.point.y;
    closestZ_ = collector.point.z;
    closestDistance_ = std::sqrt(std::max(0.f, collector.bestDistanceSquared));
    if (closestDistance_ > 1e-6f) {
        const float inverseDistance = 1.f / closestDistance_;
        closestNormalX_ = (x - closestX_) * inverseDistance;
        closestNormalY_ = (y - closestY_) * inverseDistance;
        closestNormalZ_ = (z - closestZ_) * inverseDistance;
    }
    return closestBodyId_;
}

void World3D::setMoverUp(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("World3D.setMoverUp: values must be finite");
    const float lengthSquared = x * x + y * y + z * z;
    if (lengthSquared <= 1e-12f)
        throw eve::Exception("World3D.setMoverUp: direction must be non-zero");
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    moverUp_ = b3Vec3{x * inverseLength, y * inverseLength, z * inverseLength};
}

void World3D::setMoverSlopeLimit(float degrees) {
    if (!std::isfinite(degrees))
        throw eve::Exception("World3D.setMoverSlopeLimit: degrees must be finite");
    degrees = std::clamp(degrees, 0.f, 89.9f);
    moverSlopeCos_ = std::cos(degrees * 0.01745329251994329577f);
}

bool World3D::moveCapsule(float ax, float ay, float az, float bx, float by, float bz,
                          float radius, float dx, float dy, float dz) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) ||
        !std::isfinite(by) || !std::isfinite(bz) || !(radius > 0.f) || !std::isfinite(radius) ||
        !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
        throw eve::Exception("World3D.moveCapsule: all values must be finite; radius > 0");

    moverDeltaX_ = moverDeltaY_ = moverDeltaZ_ = 0.f;
    moverNormalX_ = moverNormalY_ = moverNormalZ_ = 0.f;
    moverPlaneCount_ = 0;
    moverIterations_ = 0;
    moverGroundDot_ = -1.f;
    moverGrounded_ = false;
    if (!isValid()) return false;

    constexpr int planeCapacity = 16;
    struct PlaneCollector {
        World3D *world = nullptr;
        b3CollisionPlane planes[planeCapacity]{};
        int count = 0;
        b3Vec3 desired = b3Vec3_zero;
        b3Vec3 up = b3Vec3_axisY;
        b3Vec3 bestNormal = b3Vec3_zero;
        float bestDot = 0.f;
        float maxUpDot = -1.f;

        static bool callback(b3ShapeId shapeId, const b3PlaneResult *results, int resultCount,
                             void *context) {
            auto *self = static_cast<PlaneCollector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
            for (int i = 0; i < resultCount && self->count < planeCapacity; ++i) {
                const b3Plane &plane = results[i].plane;
                self->planes[self->count++] = b3CollisionPlane{
                    .plane = plane, .pushLimit = FLT_MAX, .push = 0.f, .clipVelocity = true};
                const float d = b3Dot(plane.normal, self->desired);
                if (d < self->bestDot) {
                    self->bestDot = d;
                    self->bestNormal = plane.normal;
                }
                self->maxUpDot = std::max(self->maxUpDot, b3Dot(plane.normal, self->up));
            }
            return true;
        }
    } collector;
    collector.world = this;
    collector.up = moverUp_;

    const b3Pos start{ax, ay, az};
    b3Pos current = start;
    const b3Pos target{ax + dx, ay + dy, az + dz};
    b3Capsule mover{b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}, radius};
    b3QueryFilter filter = makeQueryFilter();
    constexpr float toleranceSquared = 1e-8f;
    bool collided = false;

    for (int pass = 0; pass < 5; ++pass) {
        collector.count = 0;
        collector.desired = target - current;
        b3World_CollideMover(worldId_, current, &mover, filter, &PlaneCollector::callback,
                             &collector);
        moverPlaneCount_ += collector.count;
        collided = collided || collector.count > 0;

        const b3Vec3 targetDelta = target - current;
        b3PlaneSolverResult solved = b3SolvePlanes(targetDelta, collector.planes, collector.count);
        b3Vec3 delta = solved.delta;
        struct MoverFilter {
            World3D *world = nullptr;
            static bool callback(b3ShapeId shapeId, void *context) {
                auto *self = static_cast<MoverFilter *>(context);
                return !self->world->shouldIgnoreQueryShape(shapeId);
            }
        } moverFilter{this};
        const float fraction = b3World_CastMover(worldId_, current, &mover, delta, filter,
                                                 &MoverFilter::callback, &moverFilter);
        if (fraction < 1.f) collided = true;
        delta = fraction * delta;
        current = current + delta;
        moverIterations_ = pass + 1;

        if (b3LengthSquared(delta) <= toleranceSquared) break;
        if (b3LengthSquared(target - current) <= toleranceSquared) break;
    }

    const b3Vec3 total = current - start;
    moverDeltaX_ = total.x;
    moverDeltaY_ = total.y;
    moverDeltaZ_ = total.z;
    moverNormalX_ = collector.bestNormal.x;
    moverNormalY_ = collector.bestNormal.y;
    moverNormalZ_ = collector.bestNormal.z;
    moverGroundDot_ = collector.maxUpDot;
    moverGrounded_ = moverGroundDot_ >= moverSlopeCos_;
    return collided;
}

int World3D::getQueryBodyId(int index) const {
    if (index < 0 || index >= static_cast<int>(queryBodyIds_.size()))
        throw eve::Exception("World3D.getQueryBodyId: index out of range");
    return queryBodyIds_[static_cast<size_t>(index)];
}

bool World3D::pointProbe(float x, float y, float z, float radius, ClothContact3D *out) const {
    if (out) *out = ClothContact3D{};
    if (!isValid() || radius <= 0.f) return false;

    const b3Vec3 target{x, y, z};
    for (Shape3D *s : shapes_) {
        if (!s || !s->isValid() || s->isSensor()) continue;
        const b3Vec3 closest = b3Shape_GetClosestPoint(s->raw(), target);
        const b3Vec3 delta   = target - closest;
        const float d = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const float depth = radius - d;
        if (depth > 0.f && depth > out->depth) {
            out->hit   = true;
            out->depth = depth;
            if (d > 1e-6f) {
                out->nx = delta.x / d;
                out->ny = delta.y / d;
                out->nz = delta.z / d;
            } else {
                out->nx = 0.f;
                out->ny = 1.f;
                out->nz = 0.f;
            }
            out->body = s->getBody();
        }
    }
    return out->hit;
}

const World3D::RayResult &World3D::rayResultAt(int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(rayResults_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return rayResults_[static_cast<size_t>(index)];
}

#define EV_RAY_INT_GETTER(name, member)                                              \
    int World3D::name(int index) const { return rayResultAt(index, #name).member; }

EV_RAY_INT_GETTER(getRayResultBodyId, bodyId)
EV_RAY_INT_GETTER(getRayResultShapeId, shapeId)
EV_RAY_INT_GETTER(getRayResultShapeTag, shapeTag)
EV_RAY_INT_GETTER(getRayResultMaterialId, materialId)
EV_RAY_INT_GETTER(getRayResultTriangleIndex, triangleIndex)

#undef EV_RAY_INT_GETTER

#define EV_RAY_FLOAT_GETTER(name, member)                                            \
    float World3D::name(int index) const { return rayResultAt(index, #name).member; }

EV_RAY_FLOAT_GETTER(getRayResultX, x)
EV_RAY_FLOAT_GETTER(getRayResultY, y)
EV_RAY_FLOAT_GETTER(getRayResultZ, z)
EV_RAY_FLOAT_GETTER(getRayResultNormalX, normalX)
EV_RAY_FLOAT_GETTER(getRayResultNormalY, normalY)
EV_RAY_FLOAT_GETTER(getRayResultNormalZ, normalZ)
EV_RAY_FLOAT_GETTER(getRayResultFraction, fraction)

#undef EV_RAY_FLOAT_GETTER

const World3D::ShapeCastResult &World3D::shapeCastResultAt(int index,
                                                           const char *operation) const {
    if (index < 0 || index >= static_cast<int>(shapeCastResults_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return shapeCastResults_[static_cast<size_t>(index)];
}

#define EV_CAST_INT_GETTER(name, member)                                             \
    int World3D::name(int index) const { return shapeCastResultAt(index, #name).member; }

EV_CAST_INT_GETTER(getShapeCastResultBodyId, bodyId)
EV_CAST_INT_GETTER(getShapeCastResultShapeId, shapeId)
EV_CAST_INT_GETTER(getShapeCastResultShapeTag, shapeTag)
EV_CAST_INT_GETTER(getShapeCastResultMaterialId, materialId)
EV_CAST_INT_GETTER(getShapeCastResultTriangleIndex, triangleIndex)

#undef EV_CAST_INT_GETTER

#define EV_CAST_FLOAT_GETTER(name, member)                                           \
    float World3D::name(int index) const { return shapeCastResultAt(index, #name).member; }

EV_CAST_FLOAT_GETTER(getShapeCastResultX, x)
EV_CAST_FLOAT_GETTER(getShapeCastResultY, y)
EV_CAST_FLOAT_GETTER(getShapeCastResultZ, z)
EV_CAST_FLOAT_GETTER(getShapeCastResultNormalX, normalX)
EV_CAST_FLOAT_GETTER(getShapeCastResultNormalY, normalY)
EV_CAST_FLOAT_GETTER(getShapeCastResultNormalZ, normalZ)
EV_CAST_FLOAT_GETTER(getShapeCastResultFraction, fraction)

#undef EV_CAST_FLOAT_GETTER

int World3D::getQueryShapeId(int index) const {
    if (index < 0 || index >= static_cast<int>(queryShapes_.size()))
        throw eve::Exception("World3D.getQueryShapeId: index out of range");
    return queryShapes_[static_cast<size_t>(index)].first;
}

int World3D::getQueryShapeTag(int index) const {
    if (index < 0 || index >= static_cast<int>(queryShapes_.size()))
        throw eve::Exception("World3D.getQueryShapeTag: index out of range");
    return queryShapes_[static_cast<size_t>(index)].second;
}

const World3D::ContactEvent &World3D::contactEventAt(const std::vector<ContactEvent> &events,
                                                     int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(events.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return events[static_cast<size_t>(index)];
}

const World3D::TriggerEvent &World3D::triggerEventAt(const std::vector<TriggerEvent> &events,
                                                     int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(events.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return events[static_cast<size_t>(index)];
}

const World3D::HitEvent &World3D::hitEventAt(int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(hits_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return hits_[static_cast<size_t>(index)];
}

const World3D::JointStressEvent &World3D::jointStressEventAt(int index,
                                                             const char *operation) const {
    if (index < 0 || index >= static_cast<int>(jointStressEvents_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return jointStressEvents_[static_cast<size_t>(index)];
}

#define EV_CONTACT_GETTER(name, events, member)                                      \
    int World3D::name(int index) const {                                             \
        return contactEventAt(events, index, #name).member;                          \
    }

EV_CONTACT_GETTER(getBeginContactBodyAId, beginContacts_, shapeA.bodyId)
EV_CONTACT_GETTER(getBeginContactBodyBId, beginContacts_, shapeB.bodyId)
EV_CONTACT_GETTER(getBeginContactShapeAId, beginContacts_, shapeA.shapeId)
EV_CONTACT_GETTER(getBeginContactShapeBId, beginContacts_, shapeB.shapeId)
EV_CONTACT_GETTER(getBeginContactShapeATag, beginContacts_, shapeA.shapeTag)
EV_CONTACT_GETTER(getBeginContactShapeBTag, beginContacts_, shapeB.shapeTag)
EV_CONTACT_GETTER(getEndContactBodyAId, endContacts_, shapeA.bodyId)
EV_CONTACT_GETTER(getEndContactBodyBId, endContacts_, shapeB.bodyId)
EV_CONTACT_GETTER(getEndContactShapeAId, endContacts_, shapeA.shapeId)
EV_CONTACT_GETTER(getEndContactShapeBId, endContacts_, shapeB.shapeId)
EV_CONTACT_GETTER(getEndContactShapeATag, endContacts_, shapeA.shapeTag)
EV_CONTACT_GETTER(getEndContactShapeBTag, endContacts_, shapeB.shapeTag)

#undef EV_CONTACT_GETTER

#define EV_TRIGGER_GETTER(name, events, member)                                      \
    int World3D::name(int index) const {                                             \
        return triggerEventAt(events, index, #name).member;                          \
    }

EV_TRIGGER_GETTER(getBeginTriggerSensorBodyId, beginTriggers_, sensor.bodyId)
EV_TRIGGER_GETTER(getBeginTriggerVisitorBodyId, beginTriggers_, visitor.bodyId)
EV_TRIGGER_GETTER(getBeginTriggerSensorShapeId, beginTriggers_, sensor.shapeId)
EV_TRIGGER_GETTER(getBeginTriggerVisitorShapeId, beginTriggers_, visitor.shapeId)
EV_TRIGGER_GETTER(getBeginTriggerSensorShapeTag, beginTriggers_, sensor.shapeTag)
EV_TRIGGER_GETTER(getBeginTriggerVisitorShapeTag, beginTriggers_, visitor.shapeTag)
EV_TRIGGER_GETTER(getEndTriggerSensorBodyId, endTriggers_, sensor.bodyId)
EV_TRIGGER_GETTER(getEndTriggerVisitorBodyId, endTriggers_, visitor.bodyId)
EV_TRIGGER_GETTER(getEndTriggerSensorShapeId, endTriggers_, sensor.shapeId)
EV_TRIGGER_GETTER(getEndTriggerVisitorShapeId, endTriggers_, visitor.shapeId)
EV_TRIGGER_GETTER(getEndTriggerSensorShapeTag, endTriggers_, sensor.shapeTag)
EV_TRIGGER_GETTER(getEndTriggerVisitorShapeTag, endTriggers_, visitor.shapeTag)

#undef EV_TRIGGER_GETTER

#define EV_HIT_INT_GETTER(name, member)                                              \
    int World3D::name(int index) const { return hitEventAt(index, #name).member; }

EV_HIT_INT_GETTER(getHitBodyAId, shapeA.bodyId)
EV_HIT_INT_GETTER(getHitBodyBId, shapeB.bodyId)
EV_HIT_INT_GETTER(getHitShapeAId, shapeA.shapeId)
EV_HIT_INT_GETTER(getHitShapeBId, shapeB.shapeId)
EV_HIT_INT_GETTER(getHitShapeATag, shapeA.shapeTag)
EV_HIT_INT_GETTER(getHitShapeBTag, shapeB.shapeTag)

#undef EV_HIT_INT_GETTER

#define EV_HIT_FLOAT_GETTER(name, member)                                            \
    float World3D::name(int index) const { return hitEventAt(index, #name).member; }

EV_HIT_FLOAT_GETTER(getHitPointX, pointX)
EV_HIT_FLOAT_GETTER(getHitPointY, pointY)
EV_HIT_FLOAT_GETTER(getHitPointZ, pointZ)
EV_HIT_FLOAT_GETTER(getHitNormalX, normalX)
EV_HIT_FLOAT_GETTER(getHitNormalY, normalY)
EV_HIT_FLOAT_GETTER(getHitNormalZ, normalZ)
EV_HIT_FLOAT_GETTER(getHitApproachSpeed, approachSpeed)
EV_HIT_FLOAT_GETTER(getHitNormalImpulse, normalImpulse)

#undef EV_HIT_FLOAT_GETTER

#define EV_JOINT_STRESS_INT_GETTER(name, member)                                    \
    int World3D::name(int index) const { return jointStressEventAt(index, #name).member; }
EV_JOINT_STRESS_INT_GETTER(getJointStressJointId, jointId)
EV_JOINT_STRESS_INT_GETTER(getJointStressBodyAId, bodyAId)
EV_JOINT_STRESS_INT_GETTER(getJointStressBodyBId, bodyBId)
EV_JOINT_STRESS_INT_GETTER(getJointStressKind, kind)
#undef EV_JOINT_STRESS_INT_GETTER

#define EV_JOINT_STRESS_FLOAT_GETTER(name, member)                                  \
    float World3D::name(int index) const { return jointStressEventAt(index, #name).member; }
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressForceX, forceX)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressForceY, forceY)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressForceZ, forceZ)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressTorqueX, torqueX)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressTorqueY, torqueY)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressTorqueZ, torqueZ)
#undef EV_JOINT_STRESS_FLOAT_GETTER

const World3D::ContactPointResult &World3D::contactPointAt(int index,
                                                           const char *operation) const {
    if (index < 0 || index >= static_cast<int>(contactPoints_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return contactPoints_[static_cast<size_t>(index)];
}

#define EV_CONTACT_POINT_INT_GETTER(name, member)                                    \
    int World3D::name(int index) const { return contactPointAt(index, #name).member; }

EV_CONTACT_POINT_INT_GETTER(getContactPointShapeId, shapeId)
EV_CONTACT_POINT_INT_GETTER(getContactPointShapeTag, shapeTag)
EV_CONTACT_POINT_INT_GETTER(getContactPointOtherBodyId, otherBodyId)
EV_CONTACT_POINT_INT_GETTER(getContactPointOtherShapeId, otherShapeId)
EV_CONTACT_POINT_INT_GETTER(getContactPointOtherShapeTag, otherShapeTag)

#undef EV_CONTACT_POINT_INT_GETTER

#define EV_CONTACT_POINT_FLOAT_GETTER(name, member)                                  \
    float World3D::name(int index) const { return contactPointAt(index, #name).member; }

EV_CONTACT_POINT_FLOAT_GETTER(getContactPointX, x)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointY, y)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointZ, z)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalX, normalX)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalY, normalY)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalZ, normalZ)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointSeparation, separation)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalImpulse, normalImpulse)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointTotalNormalImpulse, totalNormalImpulse)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalVelocity, normalVelocity)

#undef EV_CONTACT_POINT_FLOAT_GETTER

bool World3D::isContactPointPersisted(int index) const {
    return contactPointAt(index, "isContactPointPersisted").persisted;
}

}  // namespace eve::physics
