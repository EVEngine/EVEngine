#include "physics/World.h"
#include "physics/Body.h"
#include "physics/Fixture.h"

#include "common/Exception.h"
#include "event/Event.h"
#include "graphics/Graphics.h"
#include "graphics/Canvas.h"

#include <Box2D/Box2D.h>

#include <algorithm>
#include <cmath>
#include <cstring>

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

}  // namespace

class ContactRelay : public b2ContactListener {
public:
    explicit ContactRelay(World *world) : world_(world) {}

    void BeginContact(b2Contact *contact) override { world_->onBeginContact(contact); }
    void EndContact(b2Contact *contact) override { world_->onEndContact(contact); }

private:
    World *world_;
};

class DebugDraw : public b2Draw {
public:
    DebugDraw() { SetFlags(e_shapeBit | e_jointBit | e_aabbBit); }

    void begin(graphics::Graphics *gfx, float meter) {
        gfx_   = gfx;
        meter_ = meter;
    }
    void end() { gfx_ = nullptr; }

    void DrawPolygon(const b2Vec2 *vertices, int32 vertexCount, const b2Color &color) override {
        drawPoly(vertices, vertexCount, color, false);
    }
    void DrawSolidPolygon(const b2Vec2 *vertices, int32 vertexCount, const b2Color &color) override {
        drawPoly(vertices, vertexCount, color, true);
    }
    void DrawCircle(const b2Vec2 &center, float32 radius, const b2Color &color) override {
        drawCircle(center, radius, color, false);
    }
    void DrawSolidCircle(const b2Vec2 &center, float32 radius, const b2Vec2 & /*axis*/,
                         const b2Color &color) override {
        drawCircle(center, radius, color, true);
    }
    void DrawSegment(const b2Vec2 &p1, const b2Vec2 &p2, const b2Color &color) override {
        if (!gfx_) return;
        float x1 = p1.x * meter_, y1 = p1.y * meter_;
        float x2 = p2.x * meter_, y2 = p2.y * meter_;
        float minx = std::min(x1, x2), miny = std::min(y1, y2);
        float w = std::max(1.f, std::fabs(x2 - x1));
        float h = std::max(1.f, std::fabs(y2 - y1));
        gfx_->drawSolidRect(minx, miny, w, h, Color(color.r, color.g, color.b, color.a * 0.8f));
    }
    void DrawTransform(const b2Transform &xf) override {
        DrawSegment(xf.p, xf.p + 0.5f * xf.q.GetXAxis(), b2Color(1, 0, 0));
        DrawSegment(xf.p, xf.p + 0.5f * xf.q.GetYAxis(), b2Color(0, 1, 0));
    }

private:
    void drawPoly(const b2Vec2 *vertices, int32 vertexCount, const b2Color &color, bool solid) {
        if (!gfx_ || vertexCount < 2) return;
        for (int32 i = 0; i < vertexCount; ++i) {
            const b2Vec2 &a = vertices[i];
            const b2Vec2 &b = vertices[(i + 1) % vertexCount];
            DrawSegment(a, b, color);
        }
        if (solid && vertexCount >= 3) {
            float minx = vertices[0].x, maxx = vertices[0].x;
            float miny = vertices[0].y, maxy = vertices[0].y;
            for (int32 i = 1; i < vertexCount; ++i) {
                minx = std::min(minx, vertices[i].x);
                maxx = std::max(maxx, vertices[i].x);
                miny = std::min(miny, vertices[i].y);
                maxy = std::max(maxy, vertices[i].y);
            }
            gfx_->drawSolidRect(minx * meter_, miny * meter_, (maxx - minx) * meter_,
                                (maxy - miny) * meter_,
                                Color(color.r, color.g, color.b, color.a * 0.25f));
        }
    }
    void drawCircle(const b2Vec2 &center, float32 radius, const b2Color &color, bool solid) {
        if (!gfx_) return;
        float px = (center.x - radius) * meter_;
        float py = (center.y - radius) * meter_;
        float d  = radius * 2.f * meter_;
        float a  = solid ? color.a * 0.35f : color.a * 0.7f;
        gfx_->drawSolidRect(px, py, d, d, Color(color.r, color.g, color.b, a));
    }

    graphics::Graphics *gfx_   = nullptr;
    float               meter_ = 30.f;
};

World::World(float gravityX, float gravityY, bool sleep, float meter) : meter_(meter) {
    if (meter_ <= 0.f) meter_ = 30.f;
    world_ = new b2World(b2Vec2(toMeters(gravityX), toMeters(gravityY)));
    world_->SetAllowSleeping(sleep);
    relay_ = new ContactRelay(this);
    world_->SetContactListener(relay_);
    draw_ = new DebugDraw();
    world_->SetDebugDraw(draw_);
}

World::~World() { destroy(); }

void World::destroy() {
    if (destroyed_) return;
    destroyed_ = true;

    // Copy sets — Body/Fixture destructors erase from them.
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

    if (world_) {
        world_->SetContactListener(nullptr);
        world_->SetDebugDraw(nullptr);
        delete world_;
        world_ = nullptr;
    }
    delete relay_;
    relay_ = nullptr;
    delete draw_;
    draw_ = nullptr;
}

void World::update(float dt) { updateFull(dt, 8, 3); }

void World::updateFull(float dt, int velocityIterations, int positionIterations) {
    if (!world_ || destroyed_) return;
    if (dt < 0.f) dt = 0.f;
    // Cap to avoid spiral-of-death on hitch frames.
    if (dt > 0.05f) dt = 0.05f;
    world_->Step(dt, velocityIterations, positionIterations);
}

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

Body *World::newBody(const std::string &bodyType, float x, float y) {
    if (!world_ || destroyed_) throw eve::Exception("World.newBody: world destroyed");

    b2BodyDef def;
    def.type     = parseBodyType(bodyType);
    def.position = b2Vec2(toMeters(x), toMeters(y));

    b2Body *raw = world_->CreateBody(&def);
    Body   *body = new Body(this, raw, nextBodyId());
    raw->SetUserData(body);
    bodies_.insert(body);
    return body;
}

void World::destroyBody(Body *body) {
    if (!body) return;
    body->destroy();
}

void World::forgetBody(Body *body) { bodies_.erase(body); }
void World::forgetFixture(Fixture *fixture) { fixtures_.erase(fixture); }

void World::onBeginContact(b2Contact *contact) {
    Body *a = bodyFromFixture(contact->GetFixtureA());
    Body *b = bodyFromFixture(contact->GetFixtureB());
    if (!a || !b) return;

    auto *ev = eve::ModuleManager::getInstance<eve::event::Event>("Event");
    if (!ev) return;
    std::vector<eve::event::Variant> args = {eve::event::Variant::makeInt(a->getId()),
                                             eve::event::Variant::makeInt(b->getId())};
    ev->push(new eve::event::Message("begincontact", args));
}

void World::onEndContact(b2Contact *contact) {
    Body *a = bodyFromFixture(contact->GetFixtureA());
    Body *b = bodyFromFixture(contact->GetFixtureB());
    if (!a || !b) return;

    auto *ev = eve::ModuleManager::getInstance<eve::event::Event>("Event");
    if (!ev) return;
    std::vector<eve::event::Variant> args = {eve::event::Variant::makeInt(a->getId()),
                                             eve::event::Variant::makeInt(b->getId())};
    ev->push(new eve::event::Message("endcontact", args));
}

void World::drawDebug(graphics::Graphics *gfx) {
    if (!world_ || !draw_ || !gfx) return;
    draw_->begin(gfx, meter_);
    world_->DrawDebugData();
    draw_->end();
}

}  // namespace eve::physics
