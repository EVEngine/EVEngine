#include "physics/World3D.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/PhysicsCapabilities.h"

#include "common/Exception.h"
#include "common/Profile.h"
#include "event/Event.h"

#include <box3d/box3d.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace eve::physics {
namespace {

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

}  // namespace

World3D::World3D(float gravityX, float gravityY, float gravityZ, bool sleep) {
    b3WorldDef def = b3DefaultWorldDef();
    def.gravity    = b3Vec3{gravityX, gravityY, gravityZ};
    def.enableSleep = sleep;
    worldId_        = b3CreateWorld(&def);
    registerCameraObstructionWorld(this);
}

World3D::~World3D() { destroy(); }

bool World3D::isValid() const { return !destroyed_ && b3World_IsValid(worldId_); }

void World3D::destroy() {
    if (destroyed_) return;
    unregisterCameraObstructionWorld(this);
    destroyed_ = true;

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

void World3D::update(float dt) { updateFull(dt, 4); }

void World3D::updateFull(float dt, int subStepCount) {
    EV_PROFILE_MODULE("physics", "World3D::update");
    if (!isValid()) return;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;
    if (subStepCount < 1) subStepCount = 1;
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

void World3D::destroyBody(Body3D *body) {
    if (!body) return;
    body->destroy();
}

void World3D::forgetBody(Body3D *body) { bodies_.erase(body); }
void World3D::forgetShape(Shape3D *shape) { shapes_.erase(shape); }

void World3D::emitContactEvents() {
    if (!isValid()) return;

    auto *ev = eve::ModuleManager::getInstance<eve::event::Event>("Event");
    if (!ev) return;

    b3ContactEvents contacts = b3World_GetContactEvents(worldId_);
    for (int i = 0; i < contacts.beginCount; ++i) {
        Body3D *a = bodyFromShape(contacts.beginEvents[i].shapeIdA);
        Body3D *b = bodyFromShape(contacts.beginEvents[i].shapeIdB);
        if (!a || !b) continue;
        std::vector<eve::event::Variant> args = {eve::event::Variant::makeInt(a->getId()),
                                                 eve::event::Variant::makeInt(b->getId())};
        ev->push(new eve::event::Message("begincontact3d", args));
    }
    for (int i = 0; i < contacts.endCount; ++i) {
        Body3D *a = bodyFromShape(contacts.endEvents[i].shapeIdA);
        Body3D *b = bodyFromShape(contacts.endEvents[i].shapeIdB);
        if (!a || !b) continue;
        std::vector<eve::event::Variant> args = {eve::event::Variant::makeInt(a->getId()),
                                                 eve::event::Variant::makeInt(b->getId())};
        ev->push(new eve::event::Message("endcontact3d", args));
    }
}

int World3D::rayCast(float x1, float y1, float z1, float x2, float y2, float z2) {
    return rayCastFiltered(x1, y1, z1, x2, y2, z2, ~uint64_t{0});
}

int World3D::rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2,
                             uint64_t maskBits) {
    rayHitBodyId_   = -1;
    rayHitX_        = 0.f;
    rayHitY_        = 0.f;
    rayHitZ_        = 0.f;
    rayHitNormalX_  = 0.f;
    rayHitNormalY_  = 0.f;
    rayHitNormalZ_  = 0.f;
    rayHitFraction_ = 0.f;
    if (!isValid()) return -1;

    b3Pos  origin{x1, y1, z1};
    b3Vec3 translation{x2 - x1, y2 - y1, z2 - z1};
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.maskBits      = maskBits;
    b3RayResult   hit    = b3World_CastRayClosest(worldId_, origin, translation, filter);
    if (!hit.hit) return -1;

    Body3D *body = bodyFromShape(hit.shapeId);
    if (!body) return -1;

    rayHitBodyId_   = body->getId();
    rayHitX_        = static_cast<float>(hit.point.x);
    rayHitY_        = static_cast<float>(hit.point.y);
    rayHitZ_        = static_cast<float>(hit.point.z);
    rayHitNormalX_  = hit.normal.x;
    rayHitNormalY_  = hit.normal.y;
    rayHitNormalZ_  = hit.normal.z;
    rayHitFraction_ = hit.fraction;
    return rayHitBodyId_;
}

int World3D::queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    queryBodyIds_.clear();
    if (!isValid()) return 0;

    struct Collector {
        std::vector<int>       *ids = nullptr;
        std::unordered_set<int> seen;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<Collector *>(context);
            Body3D *b  = bodyFromShape(shapeId);
            if (!b) return true;
            int id = b->getId();
            if (self->seen.insert(id).second) self->ids->push_back(id);
            return true;
        }
    } cb;
    cb.ids = &queryBodyIds_;

    b3AABB aabb;
    aabb.lowerBound = b3Vec3{std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ)};
    aabb.upperBound = b3Vec3{std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ)};
    b3QueryFilter filter = b3DefaultQueryFilter();
    b3World_OverlapAABB(worldId_, aabb, filter, &Collector::callback, &cb);
    return static_cast<int>(queryBodyIds_.size());
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

}  // namespace eve::physics
