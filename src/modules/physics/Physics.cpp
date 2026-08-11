#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/Cloth.h"
#include "physics/Fluid.h"

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

Cloth *Physics::newCloth(int cols, int rows, float spacing, float originX, float originY) {
    return new Cloth(cols, rows, spacing, originX, originY);
}

Fluid *Physics::newFluid(int capacity) { return new Fluid(capacity); }

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
    world.addFunc("rayCast", &World::rayCast);
    world.addFunc("hasRayHit", &World::hasRayHit);
    world.addFunc("getRayHitBodyId", &World::getRayHitBodyId);
    world.addFunc("getRayHitX", &World::getRayHitX);
    world.addFunc("getRayHitY", &World::getRayHitY);
    world.addFunc("getRayHitNormalX", &World::getRayHitNormalX);
    world.addFunc("getRayHitNormalY", &World::getRayHitNormalY);
    world.addFunc("getRayHitFraction", &World::getRayHitFraction);
    world.addFunc("queryAABB", &World::queryAABB);
    world.addFunc("getQueryCount", &World::getQueryCount);
    world.addFunc("getQueryBodyId", &World::getQueryBodyId);

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
    fixture.addFunc("testPoint", &Fixture::testPoint);
    fixture.addFunc("destroy", &Fixture::destroy);

    auto cloth = table.addClass<Cloth>(
        "Cloth", std::function<Cloth *()>([]() -> Cloth * { return nullptr; }), true);
    cloth.addFunc("update", &Cloth::update);
    cloth.addFunc("setGravity", &Cloth::setGravity);
    cloth.addFunc("getGravityX", &Cloth::getGravityX);
    cloth.addFunc("getGravityY", &Cloth::getGravityY);
    cloth.addFunc("setStiffness", &Cloth::setStiffness);
    cloth.addFunc("getStiffness", &Cloth::getStiffness);
    cloth.addFunc("setIterations", &Cloth::setIterations);
    cloth.addFunc("getIterations", &Cloth::getIterations);
    cloth.addFunc("setDamping", &Cloth::setDamping);
    cloth.addFunc("getDamping", &Cloth::getDamping);
    cloth.addFunc("setBounds", &Cloth::setBounds);
    cloth.addFunc("clearBounds", &Cloth::clearBounds);
    cloth.addFunc("pin", &Cloth::pin);
    cloth.addFunc("unpin", &Cloth::unpin);
    cloth.addFunc("pinTopRow", &Cloth::pinTopRow);
    cloth.addFunc("isPinned", &Cloth::isPinned);
    cloth.addFunc("grabAt", &Cloth::grabAt);
    cloth.addFunc("moveGrab", &Cloth::moveGrab);
    cloth.addFunc("releaseGrab", &Cloth::releaseGrab);
    cloth.addFunc("isGrabbing", &Cloth::isGrabbing);
    cloth.addFunc("getGrabIndex", &Cloth::getGrabIndex);
    cloth.addFunc("applyForce", &Cloth::applyForce);
    cloth.addFunc("setColor", &Cloth::setColor);
    cloth.addFunc("draw", &Cloth::draw);
    cloth.addFunc("getCols", &Cloth::getCols);
    cloth.addFunc("getRows", &Cloth::getRows);
    cloth.addFunc("getParticleCount", &Cloth::getParticleCount);
    cloth.addFunc("getParticleX", &Cloth::getParticleX);
    cloth.addFunc("getParticleY", &Cloth::getParticleY);
    cloth.addFunc("setParticlePosition", &Cloth::setParticlePosition);
    cloth.addFunc("getSpacing", &Cloth::getSpacing);
    cloth.addFunc("destroy", &Cloth::destroy);

    auto fluid = table.addClass<Fluid>(
        "Fluid", std::function<Fluid *()>([]() -> Fluid * { return nullptr; }), true);
    fluid.addFunc("update", &Fluid::update);
    fluid.addFunc("setGravity", &Fluid::setGravity);
    fluid.addFunc("getGravityX", &Fluid::getGravityX);
    fluid.addFunc("getGravityY", &Fluid::getGravityY);
    fluid.addFunc("setSmoothingRadius", &Fluid::setSmoothingRadius);
    fluid.addFunc("getSmoothingRadius", &Fluid::getSmoothingRadius);
    fluid.addFunc("setRestDensity", &Fluid::setRestDensity);
    fluid.addFunc("getRestDensity", &Fluid::getRestDensity);
    fluid.addFunc("setPressureStiffness", &Fluid::setPressureStiffness);
    fluid.addFunc("getPressureStiffness", &Fluid::getPressureStiffness);
    fluid.addFunc("setNearPressureStiffness", &Fluid::setNearPressureStiffness);
    fluid.addFunc("getNearPressureStiffness", &Fluid::getNearPressureStiffness);
    fluid.addFunc("setViscosity", &Fluid::setViscosity);
    fluid.addFunc("getViscosity", &Fluid::getViscosity);
    fluid.addFunc("setIterations", &Fluid::setIterations);
    fluid.addFunc("getIterations", &Fluid::getIterations);
    fluid.addFunc("setBounds", &Fluid::setBounds);
    fluid.addFunc("clearBounds", &Fluid::clearBounds);
    fluid.addFunc("emit", &Fluid::emit);
    fluid.addFunc("clear", &Fluid::clear);
    fluid.addFunc("interactAt", &Fluid::interactAt);
    fluid.addFunc("setColor", &Fluid::setColor);
    fluid.addFunc("setParticleSize", &Fluid::setParticleSize);
    fluid.addFunc("getParticleSize", &Fluid::getParticleSize);
    fluid.addFunc("draw", &Fluid::draw);
    fluid.addFunc("getCapacity", &Fluid::getCapacity);
    fluid.addFunc("getParticleCount", &Fluid::getParticleCount);
    fluid.addFunc("getParticleX", &Fluid::getParticleX);
    fluid.addFunc("getParticleY", &Fluid::getParticleY);
    fluid.addFunc("getParticleVx", &Fluid::getParticleVx);
    fluid.addFunc("getParticleVy", &Fluid::getParticleVy);
    fluid.addFunc("destroy", &Fluid::destroy);
}

void Physics::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Physics::getName);
    cls.addFunc("setMeter", &Physics::setMeter);
    cls.addFunc("getMeter", &Physics::getMeter);
    cls.addFunc("newWorld", &Physics::newWorld);
    cls.addFunc("newCloth", &Physics::newCloth);
    cls.addFunc("newFluid", &Physics::newFluid);
}

}  // namespace eve::physics

