#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/Body.h"
#include "physics/Fixture.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::physics {

Module_IMPL(Physics, new Physics());

void Physics::setMeter(float pixelsPerMeter) {
    if (pixelsPerMeter <= 0.f)
        throw Exception("Physics.setMeter: pixelsPerMeter must be > 0");
    meter_ = pixelsPerMeter;
}

float Physics::getMeter() const { return meter_; }

World *Physics::newWorld(float gravityX, float gravityY, bool sleep) {
    return new World(gravityX, gravityY, sleep, meter_);
}

void Physics::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Physics::create, false);
    expose(cls);

    auto world = table.addClass<World>(
        "World", std::function<World *()>([]() -> World * { return nullptr; }), true);
    world.addFunc("update", &World::update);
    world.addFunc("updateFull", &World::updateFull);
    world.addFunc("setGravity", &World::setGravity);
    world.addFunc("getGravityX", &World::getGravityX);
    world.addFunc("getGravityY", &World::getGravityY);
    world.addFunc("setMeter", &World::setMeter);
    world.addFunc("getMeter", &World::getMeter);
    world.addFunc("newBody", &World::newBody);
    world.addFunc("destroyBody", &World::destroyBody);
    world.addFunc("destroy", &World::destroy);
    world.addFunc("drawDebug", &World::drawDebug);

    auto body = table.addClass<Body>(
        "Body", std::function<Body *()>([]() -> Body * { return nullptr; }), true);
    body.addFunc("getId", &Body::getId);
    body.addFunc("setPosition", &Body::setPosition);
    body.addFunc("getX", &Body::getX);
    body.addFunc("getY", &Body::getY);
    body.addFunc("setAngle", &Body::setAngle);
    body.addFunc("getAngle", &Body::getAngle);
    body.addFunc("setLinearVelocity", &Body::setLinearVelocity);
    body.addFunc("getLinearVelocityX", &Body::getLinearVelocityX);
    body.addFunc("getLinearVelocityY", &Body::getLinearVelocityY);
    body.addFunc("setAngularVelocity", &Body::setAngularVelocity);
    body.addFunc("getAngularVelocity", &Body::getAngularVelocity);
    body.addFunc("applyForce", &Body::applyForce);
    body.addFunc("applyForceAt", &Body::applyForceAt);
    body.addFunc("applyLinearImpulse", &Body::applyLinearImpulse);
    body.addFunc("applyAngularImpulse", &Body::applyAngularImpulse);
    body.addFunc("setType", &Body::setType);
    body.addFunc("getType", &Body::getType);
    body.addFunc("setFixedRotation", &Body::setFixedRotation);
    body.addFunc("isFixedRotation", &Body::isFixedRotation);
    body.addFunc("setActive", &Body::setActive);
    body.addFunc("isActive", &Body::isActive);
    body.addFunc("setBullet", &Body::setBullet);
    body.addFunc("isBullet", &Body::isBullet);
    body.addFunc("setAwake", &Body::setAwake);
    body.addFunc("isAwake", &Body::isAwake);
    body.addFunc("newRectangleFixture", &Body::newRectangleFixture);
    body.addFunc("newCircleFixture", &Body::newCircleFixture);
    body.addFunc("destroy", &Body::destroy);

    auto fixture = table.addClass<Fixture>(
        "Fixture", std::function<Fixture *()>([]() -> Fixture * { return nullptr; }), true);
    fixture.addFunc("setSensor", &Fixture::setSensor);
    fixture.addFunc("isSensor", &Fixture::isSensor);
    fixture.addFunc("setFriction", &Fixture::setFriction);
    fixture.addFunc("getFriction", &Fixture::getFriction);
    fixture.addFunc("setRestitution", &Fixture::setRestitution);
    fixture.addFunc("getRestitution", &Fixture::getRestitution);
    fixture.addFunc("setDensity", &Fixture::setDensity);
    fixture.addFunc("getDensity", &Fixture::getDensity);
    fixture.addFunc("getBody", &Fixture::getBody);
    fixture.addFunc("destroy", &Fixture::destroy);
}

void Physics::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Physics::getName);
    cls.addFunc("setMeter", &Physics::setMeter);
    cls.addFunc("getMeter", &Physics::getMeter);
    cls.addFunc("newWorld", &Physics::newWorld);
}

}  // namespace eve::physics
