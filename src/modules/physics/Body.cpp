#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/World.h"

#include "common/Exception.h"

#include <Box2D/Box2D.h>

#include <cstring>

namespace eve::physics {
namespace {

b2BodyType parseBodyType(const std::string &type) {
    if (type == "static") return b2_staticBody;
    if (type == "kinematic") return b2_kinematicBody;
    if (type == "dynamic") return b2_dynamicBody;
    throw eve::Exception("Body.setType: unknown body type '%s'", type.c_str());
}

const char *bodyTypeName(b2BodyType t) {
    switch (t) {
        case b2_staticBody: return "static";
        case b2_kinematicBody: return "kinematic";
        case b2_dynamicBody: return "dynamic";
        default: return "static";
    }
}

}  // namespace

Body::Body(World *world, b2Body *body, int id, PhysicsBodyHandle runtimeHandle)
    : world_(world), body_(body), id_(id), runtimeHandle_(runtimeHandle) {}

Body::~Body() {
    if (body_ && world_ && world_->raw()) {
        // Invalidate fixture wrappers before DestroyBody frees b2Fixtures.
        b2Fixture *f = body_->GetFixtureList();
        while (f) {
            b2Fixture *next = f->GetNext();
            auto *wrap = static_cast<Fixture *>(f->GetUserData());
            if (wrap) {
                world_->forgetFixture(wrap);
                wrap->invalidate();
            }
            f = next;
        }
        body_->SetUserData(nullptr);
        world_->raw()->DestroyBody(body_);
        world_->forgetBody(this);
    }
    body_  = nullptr;
    world_ = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
}

void Body::invalidate() {
    if (body_) body_->SetUserData(nullptr);
    body_  = nullptr;
    world_ = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
}

void Body::destroy() {
    if (!body_ || !world_ || !world_->raw()) {
        invalidate();
        return;
    }
    b2Fixture *f = body_->GetFixtureList();
    while (f) {
        b2Fixture *next = f->GetNext();
        auto *wrap = static_cast<Fixture *>(f->GetUserData());
        if (wrap) {
            world_->forgetFixture(wrap);
            wrap->invalidate();
        }
        f = next;
    }
    body_->SetUserData(nullptr);
    world_->raw()->DestroyBody(body_);
    world_->forgetBody(this);
    body_  = nullptr;
    world_ = nullptr;
    runtimeHandle_ = PhysicsBodyHandle::invalid();
}

void Body::setPosition(float x, float y) {
    if (!body_ || !world_) return;
    body_->SetTransform(b2Vec2(world_->toMeters(x), world_->toMeters(y)), body_->GetAngle());
}

float Body::getX() const {
    if (!body_ || !world_) return 0.f;
    return world_->toPixels(body_->GetPosition().x);
}

float Body::getY() const {
    if (!body_ || !world_) return 0.f;
    return world_->toPixels(body_->GetPosition().y);
}

void Body::setAngle(float radians) {
    if (!body_) return;
    body_->SetTransform(body_->GetPosition(), radians);
}

float Body::getAngle() const {
    if (!body_) return 0.f;
    return body_->GetAngle();
}

void Body::setLinearVelocity(float vx, float vy) {
    if (!body_ || !world_) return;
    body_->SetLinearVelocity(b2Vec2(world_->toMeters(vx), world_->toMeters(vy)));
}

float Body::getLinearVelocityX() const {
    if (!body_ || !world_) return 0.f;
    return world_->toPixels(body_->GetLinearVelocity().x);
}

float Body::getLinearVelocityY() const {
    if (!body_ || !world_) return 0.f;
    return world_->toPixels(body_->GetLinearVelocity().y);
}

float Body::getLinearSpeed() const {
    if (!body_ || !world_) return 0.f;
    return world_->toPixels(body_->GetLinearVelocity().Length());
}

float Body::getMass() const { return body_ ? body_->GetMass() : 0.f; }

float Body::getWorldCenterX() const {
    if (!body_ || !world_) return 0.f;
    return world_->toPixels(body_->GetWorldCenter().x);
}

float Body::getWorldCenterY() const {
    if (!body_ || !world_) return 0.f;
    return world_->toPixels(body_->GetWorldCenter().y);
}

void Body::setAngularVelocity(float omega) {
    if (!body_) return;
    body_->SetAngularVelocity(omega);
}

float Body::getAngularVelocity() const {
    if (!body_) return 0.f;
    return body_->GetAngularVelocity();
}

void Body::applyForce(float fx, float fy) {
    if (!body_ || !world_) return;
    // Force in Newtons ≈ (pixels/s² * mass) with mass in kg; convert like LÖVE.
    body_->ApplyForceToCenter(b2Vec2(world_->toMeters(fx), world_->toMeters(fy)), true);
}

void Body::applyForceAt(float fx, float fy, float x, float y) {
    if (!body_ || !world_) return;
    body_->ApplyForce(b2Vec2(world_->toMeters(fx), world_->toMeters(fy)),
                      b2Vec2(world_->toMeters(x), world_->toMeters(y)), true);
}

void Body::applyLinearImpulse(float ix, float iy) {
    if (!body_ || !world_) return;
    body_->ApplyLinearImpulse(b2Vec2(world_->toMeters(ix), world_->toMeters(iy)),
                              body_->GetWorldCenter(), true);
}

void Body::applyAngularImpulse(float impulse) {
    if (!body_) return;
    body_->ApplyAngularImpulse(impulse, true);
}

void Body::setType(const std::string &bodyType) {
    if (!body_) return;
    body_->SetType(parseBodyType(bodyType));
}

std::string Body::getType() const {
    if (!body_) return "static";
    return bodyTypeName(body_->GetType());
}

void Body::setFixedRotation(bool fixed) {
    if (!body_) return;
    body_->SetFixedRotation(fixed);
}

bool Body::isFixedRotation() const { return body_ ? body_->IsFixedRotation() : false; }

void Body::setActive(bool active) {
    if (!body_) return;
    body_->SetActive(active);
}

bool Body::isActive() const { return body_ ? body_->IsActive() : false; }

void Body::setBullet(bool bullet) {
    if (!body_) return;
    body_->SetBullet(bullet);
}

bool Body::isBullet() const { return body_ ? body_->IsBullet() : false; }

void Body::setAwake(bool awake) {
    if (!body_) return;
    body_->SetAwake(awake);
}

bool Body::isAwake() const { return body_ ? body_->IsAwake() : false; }

Fixture *Body::newRectangleFixture(float width, float height, float density, float friction,
                                   float restitution) {
    return newRectangleFixtureAt(width, height, 0.f, 0.f, density, friction, restitution);
}

Fixture *Body::newRectangleFixtureAt(float width, float height, float offsetX, float offsetY,
                                     float density, float friction, float restitution) {
    if (!body_ || !world_) throw eve::Exception("Body.newRectangleFixture: body destroyed");
    if (width <= 0.f || height <= 0.f)
        throw eve::Exception("Body.newRectangleFixture: width/height must be > 0");

    b2PolygonShape shape;
    shape.SetAsBox(world_->toMeters(width) * 0.5f, world_->toMeters(height) * 0.5f,
                   b2Vec2(world_->toMeters(offsetX), world_->toMeters(offsetY)), 0.f);

    b2FixtureDef def;
    def.shape       = &shape;
    def.density     = density;
    def.friction    = friction;
    def.restitution = restitution;

    b2Fixture *raw = body_->CreateFixture(&def);
    auto *fx = new Fixture(world_, this, raw);
    raw->SetUserData(fx);
    world_->fixtures_.insert(fx);
    return fx;
}

Fixture *Body::newCircleFixture(float radius, float density, float friction, float restitution) {
    if (!body_ || !world_) throw eve::Exception("Body.newCircleFixture: body destroyed");
    if (radius <= 0.f) throw eve::Exception("Body.newCircleFixture: radius must be > 0");

    b2CircleShape shape;
    shape.m_radius = world_->toMeters(radius);

    b2FixtureDef def;
    def.shape       = &shape;
    def.density     = density;
    def.friction    = friction;
    def.restitution = restitution;

    b2Fixture *raw = body_->CreateFixture(&def);
    auto *fx = new Fixture(world_, this, raw);
    raw->SetUserData(fx);
    world_->fixtures_.insert(fx);
    return fx;
}

}  // namespace eve::physics
