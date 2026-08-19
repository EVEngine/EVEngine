#include "physics/Physics.h"
#include "physics/PhysicsCapabilities.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/Cloth.h"
#include "physics/Fluid.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::physics {

Physics::Physics() {
    registerPhysicsCapabilities();
}

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

World3D *Physics::newWorld3D(float gravityX, float gravityY, float gravityZ, bool sleep) {
    return new World3D(gravityX, gravityY, gravityZ, sleep);
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
    world.addFunc("getBeginContactCount", &World::getBeginContactCount);
    world.addFunc("getBeginContactBodyAId", &World::getBeginContactBodyAId);
    world.addFunc("getBeginContactBodyBId", &World::getBeginContactBodyBId);
    world.addFunc("getBeginContactFixtureATag", &World::getBeginContactFixtureATag);
    world.addFunc("getBeginContactFixtureBTag", &World::getBeginContactFixtureBTag);
    world.addFunc("getEndContactCount", &World::getEndContactCount);
    world.addFunc("getEndContactBodyAId", &World::getEndContactBodyAId);
    world.addFunc("getEndContactBodyBId", &World::getEndContactBodyBId);
    world.addFunc("getEndContactFixtureATag", &World::getEndContactFixtureATag);
    world.addFunc("getEndContactFixtureBTag", &World::getEndContactFixtureBTag);
    world.addFunc("getImpactCount", &World::getImpactCount);
    world.addFunc("getImpactBodyAId", &World::getImpactBodyAId);
    world.addFunc("getImpactBodyBId", &World::getImpactBodyBId);
    world.addFunc("getImpactFixtureATag", &World::getImpactFixtureATag);
    world.addFunc("getImpactFixtureBTag", &World::getImpactFixtureBTag);
    world.addFunc("getImpactPointX", &World::getImpactPointX);
    world.addFunc("getImpactPointY", &World::getImpactPointY);
    world.addFunc("getImpactNormalX", &World::getImpactNormalX);
    world.addFunc("getImpactNormalY", &World::getImpactNormalY);
    world.addFunc("getImpactRelativeNormalSpeed", &World::getImpactRelativeNormalSpeed);
    world.addFunc("getImpactNormalImpulse", &World::getImpactNormalImpulse);
    world.addFunc("getImpactTangentImpulse", &World::getImpactTangentImpulse);
    world.addFunc("clearContactEvents", &World::clearContactEvents);

    auto world3 = table.addClass<World3D>(
        "World3D", std::function<World3D *()>([]() -> World3D * { return nullptr; }), true);
    world3.addFunc("update", &World3D::update);
    world3.addFunc("updateFull", &World3D::updateFull);
    world3.addFunc("setGravity", &World3D::setGravity);
    world3.addFunc("getGravityX", &World3D::getGravityX);
    world3.addFunc("getGravityY", &World3D::getGravityY);
    world3.addFunc("getGravityZ", &World3D::getGravityZ);
    world3.addFunc("newBody", &World3D::newBody);
    world3.addFunc("destroyBody", &World3D::destroyBody);
    world3.addFunc("destroy", &World3D::destroy);
    world3.addFunc("rayCast", &World3D::rayCast);
    world3.addFunc("hasRayHit", &World3D::hasRayHit);
    world3.addFunc("getRayHitBodyId", &World3D::getRayHitBodyId);
    world3.addFunc("getRayHitX", &World3D::getRayHitX);
    world3.addFunc("getRayHitY", &World3D::getRayHitY);
    world3.addFunc("getRayHitZ", &World3D::getRayHitZ);
    world3.addFunc("getRayHitNormalX", &World3D::getRayHitNormalX);
    world3.addFunc("getRayHitNormalY", &World3D::getRayHitNormalY);
    world3.addFunc("getRayHitNormalZ", &World3D::getRayHitNormalZ);
    world3.addFunc("getRayHitFraction", &World3D::getRayHitFraction);
    world3.addFunc("queryAABB", &World3D::queryAABB);
    world3.addFunc("getQueryCount", &World3D::getQueryCount);
    world3.addFunc("getQueryBodyId", &World3D::getQueryBodyId);

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
    body.addFunc("getLinearSpeed", &Body::getLinearSpeed);
    body.addFunc("getMass", &Body::getMass);
    body.addFunc("getWorldCenterX", &Body::getWorldCenterX);
    body.addFunc("getWorldCenterY", &Body::getWorldCenterY);
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
    body.addFunc("newRectangleFixtureAt", &Body::newRectangleFixtureAt);
    body.addFunc("newCircleFixture", &Body::newCircleFixture);
    body.addFunc("destroy", &Body::destroy);

    auto body3 = table.addClass<Body3D>(
        "Body3D", std::function<Body3D *()>([]() -> Body3D * { return nullptr; }), true);
    body3.addFunc("getId", &Body3D::getId);
    body3.addFunc("setPosition", &Body3D::setPosition);
    body3.addFunc("getX", &Body3D::getX);
    body3.addFunc("getY", &Body3D::getY);
    body3.addFunc("getZ", &Body3D::getZ);
    body3.addFunc("setRotation", &Body3D::setRotation);
    body3.addFunc("getRotX", &Body3D::getRotX);
    body3.addFunc("getRotY", &Body3D::getRotY);
    body3.addFunc("getRotZ", &Body3D::getRotZ);
    body3.addFunc("getRotW", &Body3D::getRotW);
    body3.addFunc("setLinearVelocity", &Body3D::setLinearVelocity);
    body3.addFunc("getLinearVelocityX", &Body3D::getLinearVelocityX);
    body3.addFunc("getLinearVelocityY", &Body3D::getLinearVelocityY);
    body3.addFunc("getLinearVelocityZ", &Body3D::getLinearVelocityZ);
    body3.addFunc("setAngularVelocity", &Body3D::setAngularVelocity);
    body3.addFunc("getAngularVelocityX", &Body3D::getAngularVelocityX);
    body3.addFunc("getAngularVelocityY", &Body3D::getAngularVelocityY);
    body3.addFunc("getAngularVelocityZ", &Body3D::getAngularVelocityZ);
    body3.addFunc("applyForce", &Body3D::applyForce);
    body3.addFunc("applyForceAt", &Body3D::applyForceAt);
    body3.addFunc("applyLinearImpulse", &Body3D::applyLinearImpulse);
    body3.addFunc("applyAngularImpulse", &Body3D::applyAngularImpulse);
    body3.addFunc("setType", &Body3D::setType);
    body3.addFunc("getType", &Body3D::getType);
    body3.addFunc("setFixedRotation", &Body3D::setFixedRotation);
    body3.addFunc("isFixedRotation", &Body3D::isFixedRotation);
    body3.addFunc("setActive", &Body3D::setActive);
    body3.addFunc("isActive", &Body3D::isActive);
    body3.addFunc("setBullet", &Body3D::setBullet);
    body3.addFunc("isBullet", &Body3D::isBullet);
    body3.addFunc("setAwake", &Body3D::setAwake);
    body3.addFunc("isAwake", &Body3D::isAwake);
    body3.addFunc("newBoxShape", &Body3D::newBoxShape);
    body3.addFunc("newSphereShape", &Body3D::newSphereShape);
    body3.addFunc("newCapsuleShape", &Body3D::newCapsuleShape);
    body3.addFunc("destroy", &Body3D::destroy);

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
    fixture.addFunc("setTag", &Fixture::setTag);
    fixture.addFunc("getTag", &Fixture::getTag);
    fixture.addFunc("setCategoryBits", &Fixture::setCategoryBits);
    fixture.addFunc("getCategoryBits", &Fixture::getCategoryBits);
    fixture.addFunc("setMaskBits", &Fixture::setMaskBits);
    fixture.addFunc("getMaskBits", &Fixture::getMaskBits);
    fixture.addFunc("setGroupIndex", &Fixture::setGroupIndex);
    fixture.addFunc("getGroupIndex", &Fixture::getGroupIndex);
    fixture.addFunc("getBodyId", &Fixture::getBodyId);
    fixture.addFunc("getBody", &Fixture::getBody);
    fixture.addFunc("testPoint", &Fixture::testPoint);
    fixture.addFunc("destroy", &Fixture::destroy);

    auto shape3 = table.addClass<Shape3D>(
        "Shape3D", std::function<Shape3D *()>([]() -> Shape3D * { return nullptr; }), true);
    shape3.addFunc("setSensor", &Shape3D::setSensor);
    shape3.addFunc("isSensor", &Shape3D::isSensor);
    shape3.addFunc("setFriction", &Shape3D::setFriction);
    shape3.addFunc("getFriction", &Shape3D::getFriction);
    shape3.addFunc("setRestitution", &Shape3D::setRestitution);
    shape3.addFunc("getRestitution", &Shape3D::getRestitution);
    shape3.addFunc("setDensity", &Shape3D::setDensity);
    shape3.addFunc("getDensity", &Shape3D::getDensity);
    shape3.addFunc("getBody", &Shape3D::getBody);
    shape3.addFunc("testPoint", &Shape3D::testPoint);
    shape3.addFunc("destroy", &Shape3D::destroy);

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
    cls.addFunc("newWorld3D", &Physics::newWorld3D);
    cls.addFunc("newCloth", &Physics::newCloth);
    cls.addFunc("newFluid", &Physics::newFluid);
}

}  // namespace eve::physics
