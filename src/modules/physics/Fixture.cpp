#include "physics/Fixture.h"
#include "physics/Body.h"
#include "physics/World.h"

#include <Box2D/Box2D.h>

namespace eve::physics {

Fixture::Fixture(World *world, Body *body, b2Fixture *fixture)
    : world_(world), body_(body), fixture_(fixture) {}

Fixture::~Fixture() {
    if (fixture_ && body_ && body_->raw()) {
        fixture_->SetUserData(nullptr);
        body_->raw()->DestroyFixture(fixture_);
        if (world_) world_->forgetFixture(this);
    }
    fixture_ = nullptr;
    body_    = nullptr;
    world_   = nullptr;
}

void Fixture::invalidate() {
    if (fixture_) fixture_->SetUserData(nullptr);
    fixture_ = nullptr;
    body_    = nullptr;
    world_   = nullptr;
}

void Fixture::destroy() {
    if (!fixture_ || !body_ || !body_->raw()) {
        invalidate();
        return;
    }
    fixture_->SetUserData(nullptr);
    body_->raw()->DestroyFixture(fixture_);
    if (world_) world_->forgetFixture(this);
    fixture_ = nullptr;
    body_    = nullptr;
    world_   = nullptr;
}

void Fixture::setSensor(bool sensor) {
    if (!fixture_) return;
    fixture_->SetSensor(sensor);
}

bool Fixture::isSensor() const { return fixture_ ? fixture_->IsSensor() : false; }

void Fixture::setFriction(float friction) {
    if (!fixture_) return;
    fixture_->SetFriction(friction);
}

float Fixture::getFriction() const { return fixture_ ? fixture_->GetFriction() : 0.f; }

void Fixture::setRestitution(float restitution) {
    if (!fixture_) return;
    fixture_->SetRestitution(restitution);
}

float Fixture::getRestitution() const { return fixture_ ? fixture_->GetRestitution() : 0.f; }

void Fixture::setDensity(float density) {
    if (!fixture_) return;
    fixture_->SetDensity(density);
    if (body_ && body_->raw()) body_->raw()->ResetMassData();
}

float Fixture::getDensity() const { return fixture_ ? fixture_->GetDensity() : 0.f; }

bool Fixture::testPoint(float x, float y) const {
    if (!fixture_ || !world_) return false;
    return fixture_->TestPoint(b2Vec2(world_->toMeters(x), world_->toMeters(y)));
}

}  // namespace eve::physics
