#include "physics/World.h"
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/SimulationBackend.h"

#include "common/Exception.h"
#include "platform_event/PlatformEvent.h"
#include "common/Profile.h"

#include <Box2D/Box2D.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace eve::physics {
namespace {
b2BodyType parseBodyType(const std::string &type) {
    if (type == "static") return b2_staticBody;
    if (type == "kinematic") return b2_kinematicBody;
    if (type == "dynamic") return b2_dynamicBody;
    throw eve::Exception("World.newBody: unknown body type '%s' (use static|kinematic|dynamic)",
                         type.c_str());
}

Body *bodyFromFixture(b2Fixture *f) {
    if (!f) return nullptr;
    b2Body *b = f->GetBody();
    if (!b) return nullptr;
    return static_cast<Body *>(b->GetUserData());
}

Fixture *fixtureFromRaw(b2Fixture *f) {
    return f ? static_cast<Fixture *>(f->GetUserData()) : nullptr;
}

// Signed distance from a world point to a shape (meters). Negative when inside.
// `normal` points from the shape toward the point (world space, unit length).
bool shapePointDistance(const b2Shape *shape, const b2Transform &xf, const b2Vec2 &p,
                        float &dist, b2Vec2 &normal) {
    dist   = 0.f;
    normal = b2Vec2(0.f, 1.f);
    if (!shape) return false;

    if (shape->GetType() == b2Shape::e_circle) {
        const auto *circle = static_cast<const b2CircleShape *>(shape);
        const b2Vec2 center = b2Mul(xf, circle->m_p);
        const b2Vec2 d      = p - center;
        const float  r      = circle->m_radius;
        dist = b2Distance(p, center) - r;
        const float len = d.Length();
        if (len > 1e-6f) {
            normal = (1.f / len) * d;
        } else if (r > 0.f) {
            normal = b2Vec2(0.f, 1.f);
        }
        return true;
    }

    if (shape->GetType() == b2Shape::e_polygon) {
        const auto *poly = static_cast<const b2PolygonShape *>(shape);
        const b2Vec2 local = b2MulT(xf, p);
        const int n = poly->m_count;
        if (n < 3) return false;

        // Signed distance to each edge: positive outside for Box2D CCW polygons
        // (m_normals point outward). Inside when every side <= 0.
        float minSide  = std::numeric_limits<float>::max();
        float bestEdge = std::numeric_limits<float>::max();
        b2Vec2 bestN(0.f, 1.f);
        b2Vec2 bestQ;
        bool inside = true;
        for (int i = 0; i < n; ++i) {
            const b2Vec2 &a = poly->m_vertices[i];
            const b2Vec2 &b = poly->m_vertices[(i + 1) % n];
            const float side = b2Dot(local - a, poly->m_normals[i]);
            if (side > 0.f) inside = false;
            if (side < minSide) {
                minSide = side;
                bestN   = poly->m_normals[i];
            }
            // Closest point on the edge segment.
            const b2Vec2 ab = b - a;
            const float len2 = b2Dot(ab, ab);
            float t = 0.f;
            if (len2 > 1e-12f) t = b2Clamp(b2Dot(local - a, ab) / len2, 0.f, 1.f);
            const b2Vec2 q = a + t * ab;
            const float d2 = b2DistanceSquared(local, q);
            if (d2 < bestEdge) {
                bestEdge = d2;
                bestQ    = q;
            }
        }
        if (inside) {
            dist   = minSide;  // negative penetration
            normal = b2Mul(xf.q, bestN);
        } else {
            dist = std::sqrt(bestEdge);
            const b2Vec2 delta = local - bestQ;
            if (delta.LengthSquared() > 1e-8f) {
                normal = b2Mul(xf.q, (1.f / delta.Length()) * delta);
            } else {
                normal = b2Mul(xf.q, bestN);
            }
        }
        return true;
    }

    if (shape->GetType() == b2Shape::e_edge) {
        const auto *edge = static_cast<const b2EdgeShape *>(shape);
        const b2Vec2 a = b2Mul(xf, edge->m_vertex1);
        const b2Vec2 b = b2Mul(xf, edge->m_vertex2);
        const b2Vec2 ab = b - a;
        const float len2 = b2Dot(ab, ab);
        float t = 0.f;
        if (len2 > 1e-12f) t = b2Clamp(b2Dot(p - a, ab) / len2, 0.f, 1.f);
        const b2Vec2 q = a + t * ab;
        const b2Vec2 delta = p - q;
        dist = delta.Length();
        if (dist > 1e-6f) {
            normal = (1.f / dist) * delta;
        } else if (len2 > 1e-12f) {
            normal = (1.f / std::sqrt(len2)) * b2Vec2(-ab.y, ab.x);
        }
        return true;
    }

    if (shape->GetType() == b2Shape::e_chain) {
        const auto *chain = static_cast<const b2ChainShape *>(shape);
        float best = std::numeric_limits<float>::max();
        b2Vec2 bestN(0.f, 1.f);
        for (int i = 0; i + 1 < chain->m_count; ++i) {
            const b2Vec2 a = b2Mul(xf, chain->m_vertices[i]);
            const b2Vec2 b = b2Mul(xf, chain->m_vertices[i + 1]);
            const b2Vec2 ab = b - a;
            const float len2 = b2Dot(ab, ab);
            float t = 0.f;
            if (len2 > 1e-12f) t = b2Clamp(b2Dot(p - a, ab) / len2, 0.f, 1.f);
            const b2Vec2 q = a + t * ab;
            const b2Vec2 delta = p - q;
            const float d = delta.Length();
            if (d < best) {
                best = d;
                bestN = d > 1e-6f
                            ? (1.f / d) * delta
                            : (len2 > 1e-12f ? (1.f / std::sqrt(len2)) * b2Vec2(-ab.y, ab.x)
                                             : b2Vec2(0.f, 1.f));
            }
        }
        if (best < std::numeric_limits<float>::max()) {
            dist   = best;
            normal = bestN;
            return true;
        }
    }
    return false;
}

World::ContactEvent contactEventFrom(b2Contact *contact) {
    World::ContactEvent out;
    if (!contact) return out;
    Fixture *fa = fixtureFromRaw(contact->GetFixtureA());
    Fixture *fb = fixtureFromRaw(contact->GetFixtureB());
    if (fa) {
        out.bodyAId = fa->getBodyId();
        out.fixtureATag = fa->getTag();
    }
    if (fb) {
        out.bodyBId = fb->getBodyId();
        out.fixtureBTag = fb->getTag();
    }
    return out;
}

eve::Result<eve::SimulationStep> makeLegacyStep(float dt, eve::SimulationTick currentTick) {
    if (!std::isfinite(dt) || dt < 0.f) {
        return eve::Result<eve::SimulationStep>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "World update dt must be finite and non-negative",
            "physics.world.update.dt"));
    }
    const float normalized = std::min(dt, 0.05f);
    const auto nextTick = currentTick.incremented();
    if (!nextTick) {
        return eve::Result<eve::SimulationStep>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation,
            "World simulation tick cannot be incremented",
            "physics.world.simulationTick"));
    }
    auto duration = eve::Duration::fromSeconds(static_cast<double>(normalized));
    if (!duration) return eve::Result<eve::SimulationStep>::failure(duration.status());
    return eve::Result<eve::SimulationStep>::success(
        {*nextTick, std::move(duration).takeValue()});
}

}  // namespace

class ContactRelay : public b2ContactListener {
public:
    explicit ContactRelay(World *world) : world_(world) {}

    void BeginContact(b2Contact *contact) override { world_->onBeginContact(contact); }
    void EndContact(b2Contact *contact) override { world_->onEndContact(contact); }
    void PreSolve(b2Contact *contact, const b2Manifold *oldManifold) override {
        world_->onPreSolve(contact, oldManifold);
    }
    void PostSolve(b2Contact *contact, const b2ContactImpulse *impulse) override {
        world_->onPostSolve(contact, impulse);
    }

private:
    World *world_;
};

World::World(float gravityX, float gravityY, bool sleep, float meter) : meter_(meter) {
    if (meter_ <= 0.f) meter_ = 30.f;
    runtimeHandle_ = detail::allocatePhysicsWorldHandle();
    world_ = new b2World(b2Vec2(toMeters(gravityX), toMeters(gravityY)));
    world_->SetAllowSleeping(sleep);
    relay_ = new ContactRelay(this);
    world_->SetContactListener(relay_);
    auto selection = detail::selectSimulationBackend(
        SimulationBackendDomain::World2D,
        detail::makeBox2DSimulationBackend(world_), world_, true);
    if (!selection) {
        const eve::Status status = selection.status();
        throw eve::Exception("World: cannot select a simulation backend: %s",
                             status.describe().c_str());
    }
    backendSelectionStatus_ = selection.status();
    auto selected = std::move(selection).takeValue();
    backendFallback_ = selected.usedFallback;
    simulation_ = std::move(selected.backend);
}

World::~World() { destroy(); }

void World::destroy() {
    if (destroyed_) return;
    destroyed_ = true;

    // Copy sets 鈥?Body/Fixture destructors erase from them.
    std::vector<Body *> bodies(bodies_.begin(), bodies_.end());
    for (Body *b : bodies) {
        if (b) {
            b->invalidate();
            // Script may still hold Body*; leave object but null raw pointer.
            // If World is script-owned and Body is also script-owned, Body::~Body
            // will see null body_ and skip DestroyBody.
        }
    }
    bodies_.clear();

    std::vector<Fixture *> fixtures(fixtures_.begin(), fixtures_.end());
    for (Fixture *f : fixtures) {
        if (f) f->invalidate();
    }
    fixtures_.clear();
    clearContactEvents();

    simulation_.reset();

    if (world_) {
        world_->SetContactListener(nullptr);
        delete world_;
        world_ = nullptr;
    }
    delete relay_;
    relay_ = nullptr;
    runtimeHandle_ = PhysicsWorldHandle::invalid();
}

bool World::pointProbe(float x, float y, float radius, ClothContact *out) const {
    if (out) *out = ClothContact{};
    if (!isValid() || radius <= 0.f) return false;

    const float rM = toMeters(radius);
    const b2Vec2 centerM(toMeters(x), toMeters(y));

    struct Probe : b2QueryCallback {
        const World    *world = nullptr;
        ClothContact   *best  = nullptr;
        b2Vec2          center;
        float           radiusM = 0.f;

        bool ReportFixture(b2Fixture *fixture) override {
            if (!fixture || fixture->IsSensor()) return true;
            const b2Shape *shape = fixture->GetShape();
            if (!shape) return true;
            float dist;
            b2Vec2 normal;
            if (!shapePointDistance(shape, fixture->GetBody()->GetTransform(), center, dist,
                                    normal)) {
                return true;
            }
            const float depth = radiusM - dist;
            if (depth > 0.f && depth > best->depth) {
                best->hit   = true;
                best->depth = world->toPixels(depth);
                best->nx    = normal.x;
                best->ny    = normal.y;
                best->body  = static_cast<Body *>(fixture->GetBody()->GetUserData());
            }
            return true;
        }
    } probe;
    probe.world   = this;
    probe.best    = out;
    probe.center  = centerM;
    probe.radiusM = rM;

    b2AABB aabb;
    aabb.lowerBound = centerM - b2Vec2(rM, rM);
    aabb.upperBound = centerM + b2Vec2(rM, rM);
    world_->QueryAABB(&probe, aabb);
    return out->hit;
}

void World::update(float dt) { updateFull(dt, 8, 3); }

void World::updateFull(float dt, int velocityIterations, int positionIterations) {
    EV_PROFILE_MODULE("physics", "World::update");
    auto legacyStep = makeLegacyStep(dt, simulationTick_);
    if (!legacyStep) {
        legacyStep.ignore("legacy World::updateFull cannot return a structured error");
        return;
    }
    auto result = step(std::move(legacyStep).takeValue(),
                       SimulationSettings{velocityIterations, positionIterations, 4});
    result.ignore("legacy World::updateFull cannot return a structured result");
}

eve::Result<void> World::step(const eve::SimulationStep& stepValue,
                              const SimulationSettings& settings) {
    if (!isValid() || !simulation_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "Cannot step a destroyed or uninitialized physics world",
            "physics.world.step"));
    }
    auto valid = detail::validateSimulationStep(
        stepValue, settings, simulation_->observation());
    if (!valid) return valid;
    auto result = simulation_->step(stepValue, settings);
    if (!result) return result;
    simulationTick_ = stepValue.tick;
    return result;
}

SimulationObservation World::simulationObservation() const noexcept {
    return simulation_ ? simulation_->observation() : SimulationObservation{};
}

SimulationBackendKind World::backendKind() const noexcept {
    return simulation_ ? simulation_->kind() : SimulationBackendKind::Cpu;
}

SimulationDeterminism World::backendDeterminism() const noexcept {
    return simulation_ ? simulation_->determinism() : SimulationDeterminism::ToleranceBounded;
}

eve::Status World::backendSelectionStatus() const { return backendSelectionStatus_; }

void World::setGravity(float gx, float gy) {
    if (!world_) return;
    world_->SetGravity(b2Vec2(toMeters(gx), toMeters(gy)));
}

float World::getGravityX() const {
    if (!world_) return 0.f;
    return toPixels(world_->GetGravity().x);
}

float World::getGravityY() const {
    if (!world_) return 0.f;
    return toPixels(world_->GetGravity().y);
}

void World::setMeter(float pixelsPerMeter) {
    if (pixelsPerMeter <= 0.f)
        throw eve::Exception("World.setMeter: pixelsPerMeter must be > 0");
    meter_ = pixelsPerMeter;
}

float World::toMeters(float pixels) const { return pixels / meter_; }
float World::toPixels(float meters) const { return meters * meter_; }

int World::nextBodyId() { return nextId_++; }

PhysicsBodyHandle World::nextBodyRuntimeHandle() {
    if (nextBodyHandleIndex_ == PhysicsBodyHandle::invalidIndex)
        throw eve::Exception("World.newBody: process-local body handle space exhausted");
    return PhysicsBodyHandle(nextBodyHandleIndex_++, 1u);
}

Body *World::newBody(const std::string &bodyType, float x, float y) {
    if (!world_ || destroyed_) throw eve::Exception("World.newBody: world destroyed");

    b2BodyDef def;
    def.type     = parseBodyType(bodyType);
    def.position = b2Vec2(toMeters(x), toMeters(y));

    const PhysicsBodyHandle runtimeHandle = nextBodyRuntimeHandle();
    b2Body *raw = world_->CreateBody(&def);
    Body   *body = new Body(this, raw, nextBodyId(), runtimeHandle);
    raw->SetUserData(body);
    bodies_.insert(body);
    return body;
}

Body *World::findBody(PhysicsBodyHandle handle) const {
    if (!isValid() || handle.isInvalid()) return nullptr;
    for (Body *body : bodies_) {
        if (body && body->isValid() && body->runtimeHandle() == handle) return body;
    }
    return nullptr;
}

void World::destroyBody(Body *body) {
    if (!body) return;
    body->destroy();
}

void World::forgetBody(Body *body) {
    if (!body) return;
    const int id = body->getId();
    bodies_.erase(body);
    auto touches = [id](const ContactEvent &e) { return e.bodyAId == id || e.bodyBId == id; };
    beginContacts_.erase(std::remove_if(beginContacts_.begin(), beginContacts_.end(), touches),
                         beginContacts_.end());
    endContacts_.erase(std::remove_if(endContacts_.begin(), endContacts_.end(), touches),
                       endContacts_.end());
    impacts_.erase(std::remove_if(impacts_.begin(), impacts_.end(), touches), impacts_.end());
}
void World::forgetFixture(Fixture *fixture) {
    if (!fixture) return;
    const int bodyId = fixture->getBodyId();
    const std::string tag = fixture->getTag();
    auto touches = [&](const ContactEvent &e) {
        return (e.bodyAId == bodyId && e.fixtureATag == tag) ||
               (e.bodyBId == bodyId && e.fixtureBTag == tag);
    };
    beginContacts_.erase(std::remove_if(beginContacts_.begin(), beginContacts_.end(), touches),
                         beginContacts_.end());
    endContacts_.erase(std::remove_if(endContacts_.begin(), endContacts_.end(), touches),
                       endContacts_.end());
    impacts_.erase(std::remove_if(impacts_.begin(), impacts_.end(), touches), impacts_.end());
    b2Fixture *raw = fixture->raw();
    for (auto it = preSolve_.begin(); it != preSolve_.end();) {
        b2Contact *contact = it->first;
        if (contact && (contact->GetFixtureA() == raw || contact->GetFixtureB() == raw))
            it = preSolve_.erase(it);
        else
            ++it;
    }
    fixtures_.erase(fixture);
}

void World::onBeginContact(b2Contact *contact) {
    if (!contact || !fixtureFromRaw(contact->GetFixtureA()) ||
        !fixtureFromRaw(contact->GetFixtureB())) return;
    Body *a = bodyFromFixture(contact->GetFixtureA());
    Body *b = bodyFromFixture(contact->GetFixtureB());
    if (!a || !b) return;

    beginContacts_.push_back(contactEventFrom(contact));

    auto *ev = eve::ModuleManager::getInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    if (!ev) return;
    std::vector<eve::platform_event::Variant> args = {eve::platform_event::Variant::makeInt(a->getId()),
                                             eve::platform_event::Variant::makeInt(b->getId())};
    ev->push(new eve::platform_event::Message("begincontact", args));
}

void World::onEndContact(b2Contact *contact) {
    preSolve_.erase(contact);
    if (!contact || !fixtureFromRaw(contact->GetFixtureA()) ||
        !fixtureFromRaw(contact->GetFixtureB())) return;
    Body *a = bodyFromFixture(contact->GetFixtureA());
    Body *b = bodyFromFixture(contact->GetFixtureB());
    if (!a || !b) return;

    endContacts_.push_back(contactEventFrom(contact));

    auto *ev = eve::ModuleManager::getInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    if (!ev) return;
    std::vector<eve::platform_event::Variant> args = {eve::platform_event::Variant::makeInt(a->getId()),
                                             eve::platform_event::Variant::makeInt(b->getId())};
    ev->push(new eve::platform_event::Message("endcontact", args));
}

void World::onPreSolve(b2Contact *contact, const b2Manifold * /*oldManifold*/) {
    if (!contact || !world_) return;
    b2WorldManifold manifold;
    contact->GetWorldManifold(&manifold);
    const b2Manifold *local = contact->GetManifold();
    if (!local || local->pointCount <= 0) return;

    b2Body *a = contact->GetFixtureA()->GetBody();
    b2Body *b = contact->GetFixtureB()->GetBody();
    if (!a || !b) return;
    const b2Vec2 point = manifold.points[0];
    const b2Vec2 va = a->GetLinearVelocityFromWorldPoint(point);
    const b2Vec2 vb = b->GetLinearVelocityFromWorldPoint(point);

    PreSolveData data;
    data.pointX = toPixels(point.x);
    data.pointY = toPixels(point.y);
    data.normalX = manifold.normal.x;
    data.normalY = manifold.normal.y;
    data.relativeNormalSpeed = toPixels(std::max(0.f, b2Dot(va - vb, manifold.normal)));
    preSolve_[contact] = data;
}

void World::onPostSolve(b2Contact *contact, const b2ContactImpulse *impulse) {
    if (!contact || !impulse) return;
    auto found = preSolve_.find(contact);
    if (found == preSolve_.end()) return;

    ImpactEvent out;
    static_cast<ContactEvent &>(out) = contactEventFrom(contact);
    out.pointX = found->second.pointX;
    out.pointY = found->second.pointY;
    out.normalX = found->second.normalX;
    out.normalY = found->second.normalY;
    out.relativeNormalSpeed = found->second.relativeNormalSpeed;
    const int count = contact->GetManifold() ? contact->GetManifold()->pointCount : 0;
    for (int i = 0; i < count; ++i) {
        out.normalImpulse += toPixels(impulse->normalImpulses[i]);
        out.tangentImpulse += toPixels(std::fabs(impulse->tangentImpulses[i]));
    }
    if (out.normalImpulse > 0.f) impacts_.push_back(std::move(out));
}

namespace {
template <typename Event>
const Event *eventAt(const std::vector<Event> &events, int index) {
    return index >= 0 && index < int(events.size()) ? &events[size_t(index)] : nullptr;
}
}  // namespace

int World::getBeginContactBodyAId(int index) const {
    auto *e = eventAt(beginContacts_, index); return e ? e->bodyAId : 0;
}
int World::getBeginContactBodyBId(int index) const {
    auto *e = eventAt(beginContacts_, index); return e ? e->bodyBId : 0;
}
std::string World::getBeginContactFixtureATag(int index) const {
    auto *e = eventAt(beginContacts_, index); return e ? e->fixtureATag : std::string();
}
std::string World::getBeginContactFixtureBTag(int index) const {
    auto *e = eventAt(beginContacts_, index); return e ? e->fixtureBTag : std::string();
}
int World::getEndContactBodyAId(int index) const {
    auto *e = eventAt(endContacts_, index); return e ? e->bodyAId : 0;
}
int World::getEndContactBodyBId(int index) const {
    auto *e = eventAt(endContacts_, index); return e ? e->bodyBId : 0;
}
std::string World::getEndContactFixtureATag(int index) const {
    auto *e = eventAt(endContacts_, index); return e ? e->fixtureATag : std::string();
}
std::string World::getEndContactFixtureBTag(int index) const {
    auto *e = eventAt(endContacts_, index); return e ? e->fixtureBTag : std::string();
}
int World::getImpactBodyAId(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->bodyAId : 0;
}
int World::getImpactBodyBId(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->bodyBId : 0;
}
std::string World::getImpactFixtureATag(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->fixtureATag : std::string();
}
std::string World::getImpactFixtureBTag(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->fixtureBTag : std::string();
}
float World::getImpactPointX(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->pointX : 0.f;
}
float World::getImpactPointY(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->pointY : 0.f;
}
float World::getImpactNormalX(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->normalX : 0.f;
}
float World::getImpactNormalY(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->normalY : 0.f;
}
float World::getImpactRelativeNormalSpeed(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->relativeNormalSpeed : 0.f;
}
float World::getImpactNormalImpulse(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->normalImpulse : 0.f;
}
float World::getImpactTangentImpulse(int index) const {
    auto *e = eventAt(impacts_, index); return e ? e->tangentImpulse : 0.f;
}

void World::clearContactEvents() {
    beginContacts_.clear();
    endContacts_.clear();
    impacts_.clear();
    preSolve_.clear();
}

int World::rayCast(float x1, float y1, float x2, float y2) {
    rayHitBodyId_   = -1;
    rayHitX_        = 0.f;
    rayHitY_        = 0.f;
    rayHitNormalX_  = 0.f;
    rayHitNormalY_  = 0.f;
    rayHitFraction_ = 0.f;
    if (!world_ || destroyed_) return -1;

    struct Closest : public b2RayCastCallback {
        World *world = nullptr;
        float  best  = 1.f;
        Body  *hit   = nullptr;
        b2Vec2 point{};
        b2Vec2 normal{};

        float32 ReportFixture(b2Fixture *fixture, const b2Vec2 &pointIn, const b2Vec2 &normalIn,
                              float32 fraction) override {
            Body *b = bodyFromFixture(fixture);
            if (!b) return -1.f;
            if (fraction < best) {
                best   = fraction;
                hit    = b;
                point  = pointIn;
                normal = normalIn;
            }
            return fraction;
        }
    } cb;
    cb.world = this;

    b2Vec2 p1(toMeters(x1), toMeters(y1));
    b2Vec2 p2(toMeters(x2), toMeters(y2));
    world_->RayCast(&cb, p1, p2);

    if (!cb.hit) return -1;
    rayHitBodyId_   = cb.hit->getId();
    rayHitX_        = toPixels(cb.point.x);
    rayHitY_        = toPixels(cb.point.y);
    rayHitNormalX_  = cb.normal.x;
    rayHitNormalY_  = cb.normal.y;
    rayHitFraction_ = cb.best;
    return rayHitBodyId_;
}

int World::queryAABB(float x, float y, float w, float h) {
    queryBodyIds_.clear();
    if (!world_ || destroyed_) return 0;

    struct Collector : public b2QueryCallback {
        World             *world = nullptr;
        std::vector<int>  *ids   = nullptr;
        std::unordered_set<int> seen;

        bool ReportFixture(b2Fixture *fixture) override {
            Body *b = bodyFromFixture(fixture);
            if (!b) return true;
            int id = b->getId();
            if (seen.insert(id).second) ids->push_back(id);
            return true;
        }
    } cb;
    cb.world = this;
    cb.ids   = &queryBodyIds_;

    b2AABB aabb;
    float x0 = toMeters(x);
    float y0 = toMeters(y);
    float x1 = toMeters(x + w);
    float y1 = toMeters(y + h);
    aabb.lowerBound = b2Vec2(std::min(x0, x1), std::min(y0, y1));
    aabb.upperBound = b2Vec2(std::max(x0, x1), std::max(y0, y1));
    world_->QueryAABB(&cb, aabb);
    return static_cast<int>(queryBodyIds_.size());
}

int World::getQueryBodyId(int index) const {
    if (index < 0 || index >= static_cast<int>(queryBodyIds_.size()))
        throw eve::Exception("World.getQueryBodyId: index out of range");
    return queryBodyIds_[static_cast<size_t>(index)];
}

}  // namespace eve::physics
