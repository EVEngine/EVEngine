#include "physics/Physics.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/DistanceField3D.h"
#include "physics/Cloth.h"
#include "physics/Cloth3D.h"
#include "physics/ClothGPU.h"
#include "physics/Fixture.h"
#include "physics/Fluid.h"
#include "physics/PhysicsCapabilities.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"

#include "common/Exception.h"
#include "gpgpu/Gpgpu.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <vector>
#include <algorithm>

namespace eve::physics {

Physics::Physics() { registerPhysicsCapabilities(); }

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

DistanceField3D *Physics::newDistanceField3D(int width, int height, int depth, float cellSize,
                                             float originX, float originY, float originZ,
                                             float outsideDistance) {
    return new DistanceField3D(width, height, depth, cellSize, originX, originY, originZ,
                               outsideDistance);
}

Cloth *Physics::newCloth(int cols, int rows, float spacing, float originX, float originY) {
    return new Cloth(cols, rows, spacing, originX, originY);
}

Cloth3D *Physics::newCloth3D(int cols, int rows, float spacing, float originX, float originY,
                             float originZ) {
    return new Cloth3D(cols, rows, spacing, originX, originY, originZ);
}

ClothGPU *Physics::newClothGPU(int cols, int rows, float spacing, float originX, float originY) {
    auto *gpgpu = eve::ModuleManager::getInstance<eve::gpgpu::Gpgpu>("Gpgpu");
    if (!gpgpu) gpgpu = eve::gpgpu::Gpgpu::create();
    return new ClothGPU(gpgpu, cols, rows, spacing, originX, originY);
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
    world3.addFunc("setContinuousCollisionEnabled", &World3D::setContinuousCollisionEnabled);
    world3.addFunc("isContinuousCollisionEnabled", &World3D::isContinuousCollisionEnabled);
    world3.addFunc("setRestitutionThreshold", &World3D::setRestitutionThreshold);
    world3.addFunc("getRestitutionThreshold", &World3D::getRestitutionThreshold);
    world3.addFunc("setHitEventThreshold", &World3D::setHitEventThreshold);
    world3.addFunc("getHitEventThreshold", &World3D::getHitEventThreshold);
    world3.addFunc("setBodyPairCollisionEnabled", &World3D::setBodyPairCollisionEnabled);
    world3.addFunc("isBodyPairCollisionEnabled", &World3D::isBodyPairCollisionEnabled);
    world3.addFunc("setShapePairCollisionEnabled", &World3D::setShapePairCollisionEnabled);
    world3.addFunc("isShapePairCollisionEnabled", &World3D::isShapePairCollisionEnabled);
    world3.addFunc("setContactTuning", &World3D::setContactTuning);
    world3.addFunc("getContactHertz", &World3D::getContactHertz);
    world3.addFunc("getContactDampingRatio", &World3D::getContactDampingRatio);
    world3.addFunc("getContactPushOutSpeed", &World3D::getContactPushOutSpeed);
    world3.addFunc("setContactRecycleDistance", &World3D::setContactRecycleDistance);
    world3.addFunc("getContactRecycleDistance", &World3D::getContactRecycleDistance);
    world3.addFunc("setMaximumLinearSpeed", &World3D::setMaximumLinearSpeed);
    world3.addFunc("getMaximumLinearSpeed", &World3D::getMaximumLinearSpeed);
    world3.addFunc("setWarmStartingEnabled", &World3D::setWarmStartingEnabled);
    world3.addFunc("isWarmStartingEnabled", &World3D::isWarmStartingEnabled);
    world3.addFunc("explode", &World3D::explode);
    world3.addFunc("getExplosionResultCount", &World3D::getExplosionResultCount);
    world3.addFunc("getExplosionResultBodyId", &World3D::getExplosionResultBodyId);
    world3.addFunc("getExplosionResultDeltaVelocityX",
                   &World3D::getExplosionResultDeltaVelocityX);
    world3.addFunc("getExplosionResultDeltaVelocityY",
                   &World3D::getExplosionResultDeltaVelocityY);
    world3.addFunc("getExplosionResultDeltaVelocityZ",
                   &World3D::getExplosionResultDeltaVelocityZ);
    world3.addFunc("getExplosionResultDeltaAngularVelocityX",
                   &World3D::getExplosionResultDeltaAngularVelocityX);
    world3.addFunc("getExplosionResultDeltaAngularVelocityY",
                   &World3D::getExplosionResultDeltaAngularVelocityY);
    world3.addFunc("getExplosionResultDeltaAngularVelocityZ",
                   &World3D::getExplosionResultDeltaAngularVelocityZ);
    world3.addFunc("getBoundsMinX", &World3D::getBoundsMinX);
    world3.addFunc("getBoundsMinY", &World3D::getBoundsMinY);
    world3.addFunc("getBoundsMinZ", &World3D::getBoundsMinZ);
    world3.addFunc("getBoundsMaxX", &World3D::getBoundsMaxX);
    world3.addFunc("getBoundsMaxY", &World3D::getBoundsMaxY);
    world3.addFunc("getBoundsMaxZ", &World3D::getBoundsMaxZ);
    world3.addFunc("getBodyCount", &World3D::getBodyCount);
    world3.addFunc("getShapeCount", &World3D::getShapeCount);
    world3.addFunc("getContactCount", &World3D::getContactCount);
    world3.addFunc("getJointCount", &World3D::getJointCount);
    world3.addFunc("getIslandCount", &World3D::getIslandCount);
    world3.addFunc("getAwakeBodyCount", &World3D::getAwakeBodyCount);
    world3.addFunc("getAwakeContactCount", &World3D::getAwakeContactCount);
    world3.addFunc("getRecycledContactCount", &World3D::getRecycledContactCount);
    world3.addFunc("getStaticTreeHeight", &World3D::getStaticTreeHeight);
    world3.addFunc("getDynamicTreeHeight", &World3D::getDynamicTreeHeight);
    world3.addFunc("getMemoryByteCount", &World3D::getMemoryByteCount);
    world3.addFunc("getProfileStepMs", &World3D::getProfileStepMs);
    world3.addFunc("getProfilePairsMs", &World3D::getProfilePairsMs);
    world3.addFunc("getProfileCollideMs", &World3D::getProfileCollideMs);
    world3.addFunc("getProfileSolveMs", &World3D::getProfileSolveMs);
    world3.addFunc("getProfileBulletsMs", &World3D::getProfileBulletsMs);
    world3.addFunc("getProfileSensorsMs", &World3D::getProfileSensorsMs);
    world3.addFunc("newBody", &World3D::newBody);
    world3.addFunc("newDistanceJoint", &World3D::newDistanceJoint);
    world3.addFunc("newRevoluteJoint", &World3D::newRevoluteJoint);
    world3.addFunc("newPrismaticJoint", &World3D::newPrismaticJoint);
    world3.addFunc("newSphericalJoint", &World3D::newSphericalJoint);
    world3.addFunc("newWheelJoint", &World3D::newWheelJoint);
    world3.addFunc("destroyBody", &World3D::destroyBody);
    world3.addFunc("destroy", &World3D::destroy);
    world3.addFunc("rayCast", &World3D::rayCast);
    world3.addFunc("rayCastAll", &World3D::rayCastAll);
    world3.addFunc("hasRayHit", &World3D::hasRayHit);
    world3.addFunc("getRayHitBodyId", &World3D::getRayHitBodyId);
    world3.addFunc("getRayHitShapeId", &World3D::getRayHitShapeId);
    world3.addFunc("getRayHitShapeTag", &World3D::getRayHitShapeTag);
    world3.addFunc("getRayHitMaterialId", &World3D::getRayHitMaterialId);
    world3.addFunc("getRayHitTriangleIndex", &World3D::getRayHitTriangleIndex);
    world3.addFunc("getRayHitX", &World3D::getRayHitX);
    world3.addFunc("getRayHitY", &World3D::getRayHitY);
    world3.addFunc("getRayHitZ", &World3D::getRayHitZ);
    world3.addFunc("getRayHitNormalX", &World3D::getRayHitNormalX);
    world3.addFunc("getRayHitNormalY", &World3D::getRayHitNormalY);
    world3.addFunc("getRayHitNormalZ", &World3D::getRayHitNormalZ);
    world3.addFunc("getRayHitFraction", &World3D::getRayHitFraction);
    world3.addFunc("getRayResultCount", &World3D::getRayResultCount);
    world3.addFunc("getRayResultBodyId", &World3D::getRayResultBodyId);
    world3.addFunc("getRayResultShapeId", &World3D::getRayResultShapeId);
    world3.addFunc("getRayResultShapeTag", &World3D::getRayResultShapeTag);
    world3.addFunc("getRayResultMaterialId", &World3D::getRayResultMaterialId);
    world3.addFunc("getRayResultTriangleIndex", &World3D::getRayResultTriangleIndex);
    world3.addFunc("getRayResultX", &World3D::getRayResultX);
    world3.addFunc("getRayResultY", &World3D::getRayResultY);
    world3.addFunc("getRayResultZ", &World3D::getRayResultZ);
    world3.addFunc("getRayResultNormalX", &World3D::getRayResultNormalX);
    world3.addFunc("getRayResultNormalY", &World3D::getRayResultNormalY);
    world3.addFunc("getRayResultNormalZ", &World3D::getRayResultNormalZ);
    world3.addFunc("getRayResultFraction", &World3D::getRayResultFraction);
    world3.addFunc("queryAABB", &World3D::queryAABB);
    world3.addFunc("querySphere", &World3D::querySphere);
    world3.addFunc("queryCapsule", &World3D::queryCapsule);
    world3.addFunc("queryBox", &World3D::queryBox);
    world3.addFunc("castSphere", &World3D::castSphere);
    world3.addFunc("castSphereAll", &World3D::castSphereAll);
    world3.addFunc("castCapsule", &World3D::castCapsule);
    world3.addFunc("castCapsuleAll", &World3D::castCapsuleAll);
    world3.addFunc("castBox", &World3D::castBox);
    world3.addFunc("castBoxAll", &World3D::castBoxAll);
    world3.addFunc("closestPoint", &World3D::closestPoint);
    world3.addFunc("hasClosestPoint", &World3D::hasClosestPoint);
    world3.addFunc("getClosestBodyId", &World3D::getClosestBodyId);
    world3.addFunc("getClosestShapeId", &World3D::getClosestShapeId);
    world3.addFunc("getClosestShapeTag", &World3D::getClosestShapeTag);
    world3.addFunc("getClosestX", &World3D::getClosestX);
    world3.addFunc("getClosestY", &World3D::getClosestY);
    world3.addFunc("getClosestZ", &World3D::getClosestZ);
    world3.addFunc("getClosestNormalX", &World3D::getClosestNormalX);
    world3.addFunc("getClosestNormalY", &World3D::getClosestNormalY);
    world3.addFunc("getClosestNormalZ", &World3D::getClosestNormalZ);
    world3.addFunc("getClosestDistance", &World3D::getClosestDistance);
    world3.addFunc("hasShapeCastHit", &World3D::hasShapeCastHit);
    world3.addFunc("getShapeCastBodyId", &World3D::getShapeCastBodyId);
    world3.addFunc("getShapeCastShapeId", &World3D::getShapeCastShapeId);
    world3.addFunc("getShapeCastShapeTag", &World3D::getShapeCastShapeTag);
    world3.addFunc("getShapeCastMaterialId", &World3D::getShapeCastMaterialId);
    world3.addFunc("getShapeCastTriangleIndex", &World3D::getShapeCastTriangleIndex);
    world3.addFunc("getShapeCastX", &World3D::getShapeCastX);
    world3.addFunc("getShapeCastY", &World3D::getShapeCastY);
    world3.addFunc("getShapeCastZ", &World3D::getShapeCastZ);
    world3.addFunc("getShapeCastNormalX", &World3D::getShapeCastNormalX);
    world3.addFunc("getShapeCastNormalY", &World3D::getShapeCastNormalY);
    world3.addFunc("getShapeCastNormalZ", &World3D::getShapeCastNormalZ);
    world3.addFunc("getShapeCastFraction", &World3D::getShapeCastFraction);
    world3.addFunc("getShapeCastResultCount", &World3D::getShapeCastResultCount);
    world3.addFunc("getShapeCastResultBodyId", &World3D::getShapeCastResultBodyId);
    world3.addFunc("getShapeCastResultShapeId", &World3D::getShapeCastResultShapeId);
    world3.addFunc("getShapeCastResultShapeTag", &World3D::getShapeCastResultShapeTag);
    world3.addFunc("getShapeCastResultMaterialId", &World3D::getShapeCastResultMaterialId);
    world3.addFunc("getShapeCastResultTriangleIndex",
                   &World3D::getShapeCastResultTriangleIndex);
    world3.addFunc("getShapeCastResultX", &World3D::getShapeCastResultX);
    world3.addFunc("getShapeCastResultY", &World3D::getShapeCastResultY);
    world3.addFunc("getShapeCastResultZ", &World3D::getShapeCastResultZ);
    world3.addFunc("getShapeCastResultNormalX", &World3D::getShapeCastResultNormalX);
    world3.addFunc("getShapeCastResultNormalY", &World3D::getShapeCastResultNormalY);
    world3.addFunc("getShapeCastResultNormalZ", &World3D::getShapeCastResultNormalZ);
    world3.addFunc("getShapeCastResultFraction", &World3D::getShapeCastResultFraction);
    world3.addFunc("moveCapsule", &World3D::moveCapsule);
    world3.addFunc("getMoverDeltaX", &World3D::getMoverDeltaX);
    world3.addFunc("getMoverDeltaY", &World3D::getMoverDeltaY);
    world3.addFunc("getMoverDeltaZ", &World3D::getMoverDeltaZ);
    world3.addFunc("getMoverNormalX", &World3D::getMoverNormalX);
    world3.addFunc("getMoverNormalY", &World3D::getMoverNormalY);
    world3.addFunc("getMoverNormalZ", &World3D::getMoverNormalZ);
    world3.addFunc("getMoverPlaneCount", &World3D::getMoverPlaneCount);
    world3.addFunc("getMoverIterations", &World3D::getMoverIterations);
    world3.addFunc("setMoverUp", &World3D::setMoverUp);
    world3.addFunc("setMoverSlopeLimit", &World3D::setMoverSlopeLimit);
    world3.addFunc("isMoverGrounded", &World3D::isMoverGrounded);
    world3.addFunc("getMoverGroundDot", &World3D::getMoverGroundDot);
    world3.addFunc("getQueryCount", &World3D::getQueryCount);
    world3.addFunc("getQueryBodyId", &World3D::getQueryBodyId);
    world3.addFunc("getQueryShapeCount", &World3D::getQueryShapeCount);
    world3.addFunc("getQueryShapeId", &World3D::getQueryShapeId);
    world3.addFunc("getQueryShapeTag", &World3D::getQueryShapeTag);
    world3.addFunc("getBeginContactCount", &World3D::getBeginContactCount);
    world3.addFunc("getBeginContactBodyAId", &World3D::getBeginContactBodyAId);
    world3.addFunc("getBeginContactBodyBId", &World3D::getBeginContactBodyBId);
    world3.addFunc("getBeginContactShapeAId", &World3D::getBeginContactShapeAId);
    world3.addFunc("getBeginContactShapeBId", &World3D::getBeginContactShapeBId);
    world3.addFunc("getBeginContactShapeATag", &World3D::getBeginContactShapeATag);
    world3.addFunc("getBeginContactShapeBTag", &World3D::getBeginContactShapeBTag);
    world3.addFunc("getEndContactCount", &World3D::getEndContactCount);
    world3.addFunc("getEndContactBodyAId", &World3D::getEndContactBodyAId);
    world3.addFunc("getEndContactBodyBId", &World3D::getEndContactBodyBId);
    world3.addFunc("getEndContactShapeAId", &World3D::getEndContactShapeAId);
    world3.addFunc("getEndContactShapeBId", &World3D::getEndContactShapeBId);
    world3.addFunc("getEndContactShapeATag", &World3D::getEndContactShapeATag);
    world3.addFunc("getEndContactShapeBTag", &World3D::getEndContactShapeBTag);
    world3.addFunc("getBeginTriggerCount", &World3D::getBeginTriggerCount);
    world3.addFunc("getBeginTriggerSensorBodyId", &World3D::getBeginTriggerSensorBodyId);
    world3.addFunc("getBeginTriggerVisitorBodyId", &World3D::getBeginTriggerVisitorBodyId);
    world3.addFunc("getBeginTriggerSensorShapeId", &World3D::getBeginTriggerSensorShapeId);
    world3.addFunc("getBeginTriggerVisitorShapeId", &World3D::getBeginTriggerVisitorShapeId);
    world3.addFunc("getBeginTriggerSensorShapeTag", &World3D::getBeginTriggerSensorShapeTag);
    world3.addFunc("getBeginTriggerVisitorShapeTag", &World3D::getBeginTriggerVisitorShapeTag);
    world3.addFunc("getEndTriggerCount", &World3D::getEndTriggerCount);
    world3.addFunc("getEndTriggerSensorBodyId", &World3D::getEndTriggerSensorBodyId);
    world3.addFunc("getEndTriggerVisitorBodyId", &World3D::getEndTriggerVisitorBodyId);
    world3.addFunc("getEndTriggerSensorShapeId", &World3D::getEndTriggerSensorShapeId);
    world3.addFunc("getEndTriggerVisitorShapeId", &World3D::getEndTriggerVisitorShapeId);
    world3.addFunc("getEndTriggerSensorShapeTag", &World3D::getEndTriggerSensorShapeTag);
    world3.addFunc("getEndTriggerVisitorShapeTag", &World3D::getEndTriggerVisitorShapeTag);
    world3.addFunc("clearContactEvents", &World3D::clearContactEvents);
    world3.addFunc("queryBodyContacts", &World3D::queryBodyContacts);
    world3.addFunc("getContactPointCount", &World3D::getContactPointCount);
    world3.addFunc("getContactPointShapeId", &World3D::getContactPointShapeId);
    world3.addFunc("getContactPointShapeTag", &World3D::getContactPointShapeTag);
    world3.addFunc("getContactPointOtherBodyId", &World3D::getContactPointOtherBodyId);
    world3.addFunc("getContactPointOtherShapeId", &World3D::getContactPointOtherShapeId);
    world3.addFunc("getContactPointOtherShapeTag", &World3D::getContactPointOtherShapeTag);
    world3.addFunc("getContactPointX", &World3D::getContactPointX);
    world3.addFunc("getContactPointY", &World3D::getContactPointY);
    world3.addFunc("getContactPointZ", &World3D::getContactPointZ);
    world3.addFunc("getContactPointNormalX", &World3D::getContactPointNormalX);
    world3.addFunc("getContactPointNormalY", &World3D::getContactPointNormalY);
    world3.addFunc("getContactPointNormalZ", &World3D::getContactPointNormalZ);
    world3.addFunc("getContactPointSeparation", &World3D::getContactPointSeparation);
    world3.addFunc("getContactPointNormalImpulse", &World3D::getContactPointNormalImpulse);
    world3.addFunc("getContactPointTotalNormalImpulse", &World3D::getContactPointTotalNormalImpulse);
    world3.addFunc("getContactPointNormalVelocity", &World3D::getContactPointNormalVelocity);
    world3.addFunc("isContactPointPersisted", &World3D::isContactPointPersisted);
    world3.addFunc("getHitCount", &World3D::getHitCount);
    world3.addFunc("getHitBodyAId", &World3D::getHitBodyAId);
    world3.addFunc("getHitBodyBId", &World3D::getHitBodyBId);
    world3.addFunc("getHitShapeAId", &World3D::getHitShapeAId);
    world3.addFunc("getHitShapeBId", &World3D::getHitShapeBId);
    world3.addFunc("getHitShapeATag", &World3D::getHitShapeATag);
    world3.addFunc("getHitShapeBTag", &World3D::getHitShapeBTag);
    world3.addFunc("getHitPointX", &World3D::getHitPointX);
    world3.addFunc("getHitPointY", &World3D::getHitPointY);
    world3.addFunc("getHitPointZ", &World3D::getHitPointZ);
    world3.addFunc("getHitNormalX", &World3D::getHitNormalX);
    world3.addFunc("getHitNormalY", &World3D::getHitNormalY);
    world3.addFunc("getHitNormalZ", &World3D::getHitNormalZ);
    world3.addFunc("getHitApproachSpeed", &World3D::getHitApproachSpeed);
    world3.addFunc("getHitNormalImpulse", &World3D::getHitNormalImpulse);
    world3.addFunc("getJointStressCount", &World3D::getJointStressCount);
    world3.addFunc("getJointStressJointId", &World3D::getJointStressJointId);
    world3.addFunc("getJointStressBodyAId", &World3D::getJointStressBodyAId);
    world3.addFunc("getJointStressBodyBId", &World3D::getJointStressBodyBId);
    world3.addFunc("getJointStressKind", &World3D::getJointStressKind);
    world3.addFunc("getJointStressForceX", &World3D::getJointStressForceX);
    world3.addFunc("getJointStressForceY", &World3D::getJointStressForceY);
    world3.addFunc("getJointStressForceZ", &World3D::getJointStressForceZ);
    world3.addFunc("getJointStressTorqueX", &World3D::getJointStressTorqueX);
    world3.addFunc("getJointStressTorqueY", &World3D::getJointStressTorqueY);
    world3.addFunc("getJointStressTorqueZ", &World3D::getJointStressTorqueZ);
    world3.addFunc("setQueryFilter", &World3D::setQueryFilter);
    world3.addFunc("resetQueryFilter", &World3D::resetQueryFilter);
    world3.addFunc("getQueryCategoryBits", &World3D::getQueryCategoryBits);
    world3.addFunc("getQueryMaskBits", &World3D::getQueryMaskBits);
    world3.addFunc("setQueryIgnoredBodyId", &World3D::setQueryIgnoredBodyId);
    world3.addFunc("setQueryIgnoredShapeId", &World3D::setQueryIgnoredShapeId);
    world3.addFunc("getQueryIgnoredBodyId", &World3D::getQueryIgnoredBodyId);
    world3.addFunc("getQueryIgnoredShapeId", &World3D::getQueryIgnoredShapeId);
    world3.addFunc("clearQueryIgnores", &World3D::clearQueryIgnores);

    auto sdf = table.addClass<DistanceField3D>(
        "DistanceField3D", std::function<DistanceField3D *()>([]() -> DistanceField3D * {
            return nullptr;
        }), true);
    sdf.addFunc("setDistances", [](DistanceField3D *field, ssq::Array values) {
        std::vector<float> distances;
        distances.reserve(values.size());
        const size_t count = values.size();
        for (size_t i = 0; i < count; ++i) distances.push_back(values.get<float>(i));
        field->setDistances(distances);
    });
    sdf.addFunc("setDistanceRegion",
                [](DistanceField3D *field, int x, int y, int z, int width, int height, int depth,
                   ssq::Array values) {
                    std::vector<float> distances;
                    const size_t count = values.size();
                    distances.reserve(count);
                    for (size_t i = 0; i < count; ++i)
                        distances.push_back(values.get<float>(i));
                    field->setDistanceRegion(x, y, z, width, height, depth, distances);
                });
    sdf.addFunc("fill", &DistanceField3D::fill);
    sdf.addFunc("fillRegion", &DistanceField3D::fillRegion);
    sdf.addFunc("setDistance", &DistanceField3D::setDistance);
    sdf.addFunc("getDistance", &DistanceField3D::getDistance);
    sdf.addFunc("sample", &DistanceField3D::sample);
    sdf.addFunc("getWidth", &DistanceField3D::getWidth);
    sdf.addFunc("getHeight", &DistanceField3D::getHeight);
    sdf.addFunc("getDepth", &DistanceField3D::getDepth);
    sdf.addFunc("getSampleCount", &DistanceField3D::getSampleCount);
    sdf.addFunc("getCellSize", &DistanceField3D::getCellSize);
    sdf.addFunc("getOriginX", &DistanceField3D::getOriginX);
    sdf.addFunc("getOriginY", &DistanceField3D::getOriginY);
    sdf.addFunc("getOriginZ", &DistanceField3D::getOriginZ);
    sdf.addFunc("getOutsideDistance", &DistanceField3D::getOutsideDistance);
    sdf.addFunc("getRevision", &DistanceField3D::getRevision);
    sdf.addFunc("sampleNormal", &DistanceField3D::sampleNormal);
    sdf.addFunc("getNormalX", &DistanceField3D::getNormalX);
    sdf.addFunc("getNormalY", &DistanceField3D::getNormalY);
    sdf.addFunc("getNormalZ", &DistanceField3D::getNormalZ);
    sdf.addFunc("checkSphere", &DistanceField3D::checkSphere);
    sdf.addFunc("checkCapsule", &DistanceField3D::checkCapsule);
    sdf.addFunc("castSphere", &DistanceField3D::castSphere);
    sdf.addFunc("castCapsule", &DistanceField3D::castCapsule);
    sdf.addFunc("moveCapsule", &DistanceField3D::moveCapsule);
    sdf.addFunc("setMoverUp", &DistanceField3D::setMoverUp);
    sdf.addFunc("setMoverSlopeLimit", &DistanceField3D::setMoverSlopeLimit);
    sdf.addFunc("setMoverSkinWidth", &DistanceField3D::setMoverSkinWidth);
    sdf.addFunc("getMoverSkinWidth", &DistanceField3D::getMoverSkinWidth);
    sdf.addFunc("setMoverGroundSnap", &DistanceField3D::setMoverGroundSnap);
    sdf.addFunc("getMoverGroundSnap", &DistanceField3D::getMoverGroundSnap);
    sdf.addFunc("setMoverStepHeight", &DistanceField3D::setMoverStepHeight);
    sdf.addFunc("getMoverStepHeight", &DistanceField3D::getMoverStepHeight);
    sdf.addFunc("getCollisionDistance", &DistanceField3D::getCollisionDistance);
    sdf.addFunc("getCollisionX", &DistanceField3D::getCollisionX);
    sdf.addFunc("getCollisionY", &DistanceField3D::getCollisionY);
    sdf.addFunc("getCollisionZ", &DistanceField3D::getCollisionZ);
    sdf.addFunc("getSurfaceX", &DistanceField3D::getSurfaceX);
    sdf.addFunc("getSurfaceY", &DistanceField3D::getSurfaceY);
    sdf.addFunc("getSurfaceZ", &DistanceField3D::getSurfaceZ);
    sdf.addFunc("getShapeContactX", &DistanceField3D::getShapeContactX);
    sdf.addFunc("getShapeContactY", &DistanceField3D::getShapeContactY);
    sdf.addFunc("getShapeContactZ", &DistanceField3D::getShapeContactZ);
    sdf.addFunc("getPenetrationDepth", &DistanceField3D::getPenetrationDepth);
    sdf.addFunc("getCastFraction", &DistanceField3D::getCastFraction);
    sdf.addFunc("getCastDistance", &DistanceField3D::getCastDistance);
    sdf.addFunc("didCastStartInside", &DistanceField3D::didCastStartInside);
    sdf.addFunc("getMoverDeltaX", &DistanceField3D::getMoverDeltaX);
    sdf.addFunc("getMoverDeltaY", &DistanceField3D::getMoverDeltaY);
    sdf.addFunc("getMoverDeltaZ", &DistanceField3D::getMoverDeltaZ);
    sdf.addFunc("getMoverNormalX", &DistanceField3D::getMoverNormalX);
    sdf.addFunc("getMoverNormalY", &DistanceField3D::getMoverNormalY);
    sdf.addFunc("getMoverNormalZ", &DistanceField3D::getMoverNormalZ);
    sdf.addFunc("getMoverIterations", &DistanceField3D::getMoverIterations);
    sdf.addFunc("isMoverGrounded", &DistanceField3D::isMoverGrounded);
    sdf.addFunc("getMoverGroundDot", &DistanceField3D::getMoverGroundDot);
    sdf.addFunc("didMoverHitWall", &DistanceField3D::didMoverHitWall);

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
    body3.addFunc("getMass", &Body3D::getMass);
    body3.addFunc("setAngularVelocity", &Body3D::setAngularVelocity);
    body3.addFunc("getAngularVelocityX", &Body3D::getAngularVelocityX);
    body3.addFunc("getAngularVelocityY", &Body3D::getAngularVelocityY);
    body3.addFunc("getAngularVelocityZ", &Body3D::getAngularVelocityZ);
    body3.addFunc("localToWorldPoint", &Body3D::localToWorldPoint);
    body3.addFunc("worldToLocalPoint", &Body3D::worldToLocalPoint);
    body3.addFunc("localToWorldVector", &Body3D::localToWorldVector);
    body3.addFunc("worldToLocalVector", &Body3D::worldToLocalVector);
    body3.addFunc("getLocalPointVelocity", &Body3D::getLocalPointVelocity);
    body3.addFunc("getWorldPointVelocity", &Body3D::getWorldPointVelocity);
    body3.addFunc("applyForce", &Body3D::applyForce);
    body3.addFunc("applyForceAt", &Body3D::applyForceAt);
    body3.addFunc("applyTorque", &Body3D::applyTorque);
    body3.addFunc("applyLinearImpulse", &Body3D::applyLinearImpulse);
    body3.addFunc("applyLinearImpulseAt", &Body3D::applyLinearImpulseAt);
    body3.addFunc("applyAngularImpulse", &Body3D::applyAngularImpulse);
    body3.addFunc("applyLocalForce", &Body3D::applyLocalForce);
    body3.addFunc("applyLocalForceToCenter", &Body3D::applyLocalForceToCenter);
    body3.addFunc("applyLocalTorque", &Body3D::applyLocalTorque);
    body3.addFunc("applyLocalLinearImpulse", &Body3D::applyLocalLinearImpulse);
    body3.addFunc("applyLocalLinearImpulseToCenter", &Body3D::applyLocalLinearImpulseToCenter);
    body3.addFunc("applyLocalAngularImpulse", &Body3D::applyLocalAngularImpulse);
    body3.addFunc("setTargetTransform", &Body3D::setTargetTransform);
    body3.addFunc("setLinearDamping", &Body3D::setLinearDamping);
    body3.addFunc("getLinearDamping", &Body3D::getLinearDamping);
    body3.addFunc("setAngularDamping", &Body3D::setAngularDamping);
    body3.addFunc("getAngularDamping", &Body3D::getAngularDamping);
    body3.addFunc("setGravityScale", &Body3D::setGravityScale);
    body3.addFunc("getGravityScale", &Body3D::getGravityScale);
    body3.addFunc("setSleepEnabled", &Body3D::setSleepEnabled);
    body3.addFunc("isSleepEnabled", &Body3D::isSleepEnabled);
    body3.addFunc("setSleepThreshold", &Body3D::setSleepThreshold);
    body3.addFunc("getSleepThreshold", &Body3D::getSleepThreshold);
    body3.addFunc("setMotionLocks", &Body3D::setMotionLocks);
    body3.addFunc("isLinearXLocked", &Body3D::isLinearXLocked);
    body3.addFunc("isLinearYLocked", &Body3D::isLinearYLocked);
    body3.addFunc("isLinearZLocked", &Body3D::isLinearZLocked);
    body3.addFunc("isAngularXLocked", &Body3D::isAngularXLocked);
    body3.addFunc("isAngularYLocked", &Body3D::isAngularYLocked);
    body3.addFunc("isAngularZLocked", &Body3D::isAngularZLocked);
    body3.addFunc("getMass", &Body3D::getMass);
    body3.addFunc("setMassProperties", &Body3D::setMassProperties);
    body3.addFunc("resetMassProperties", &Body3D::resetMassProperties);
    body3.addFunc("getInertiaXX", &Body3D::getInertiaXX);
    body3.addFunc("getInertiaYY", &Body3D::getInertiaYY);
    body3.addFunc("getInertiaZZ", &Body3D::getInertiaZZ);
    body3.addFunc("getInertiaXY", &Body3D::getInertiaXY);
    body3.addFunc("getInertiaXZ", &Body3D::getInertiaXZ);
    body3.addFunc("getInertiaYZ", &Body3D::getInertiaYZ);
    body3.addFunc("getLocalCenterX", &Body3D::getLocalCenterX);
    body3.addFunc("getLocalCenterY", &Body3D::getLocalCenterY);
    body3.addFunc("getLocalCenterZ", &Body3D::getLocalCenterZ);
    body3.addFunc("getWorldCenterX", &Body3D::getWorldCenterX);
    body3.addFunc("getWorldCenterY", &Body3D::getWorldCenterY);
    body3.addFunc("getWorldCenterZ", &Body3D::getWorldCenterZ);
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
    body3.addFunc("newConvexHullShape", [](Body3D *body, ssq::Array values, int maxVertices) {
        std::vector<float> vertices;
        vertices.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i) vertices.push_back(values.get<float>(i));
        return body->newConvexHullShape(vertices, maxVertices);
    });
    body3.addFunc("newTriangleMeshShape",
                  [](Body3D *body, ssq::Array vertexValues, ssq::Array indexValues) {
                      std::vector<float> vertices;
                      std::vector<int32_t> indices;
                      vertices.reserve(vertexValues.size());
                      indices.reserve(indexValues.size());
                      for (size_t i = 0; i < vertexValues.size(); ++i)
                          vertices.push_back(vertexValues.get<float>(i));
                      for (size_t i = 0; i < indexValues.size(); ++i)
                          indices.push_back(indexValues.get<int32_t>(i));
                      return body->newTriangleMeshShape(vertices, indices);
                  });
    body3.addFunc(
        "newTriangleMeshShapeFull",
        [](Body3D *body, ssq::Array vertexValues, ssq::Array indexValues, bool weldVertices,
           float weldTolerance, bool identifyEdges, bool useMedianSplit) {
            std::vector<float> vertices;
            std::vector<int32_t> indices;
            vertices.reserve(vertexValues.size());
            indices.reserve(indexValues.size());
            for (size_t i = 0; i < vertexValues.size(); ++i)
                vertices.push_back(vertexValues.get<float>(i));
            for (size_t i = 0; i < indexValues.size(); ++i)
                indices.push_back(indexValues.get<int32_t>(i));
            return body->newTriangleMeshShape(vertices, indices, weldVertices, weldTolerance,
                                              identifyEdges, useMedianSplit);
        });
    body3.addFunc("newHeightFieldShape",
                  [](Body3D *body, int countX, int countZ, float cellSizeX, float cellSizeZ,
                     ssq::Array values) {
                      std::vector<float> heights;
                      heights.reserve(values.size());
                      for (size_t i = 0; i < values.size(); ++i)
                          heights.push_back(values.get<float>(i));
                      if (heights.empty())
                          throw eve::Exception("Body3D.newHeightFieldShape: heights is empty");
                      const auto range = std::minmax_element(heights.begin(), heights.end());
                      return body->newHeightFieldShape(countX, countZ, cellSizeX, cellSizeZ,
                                                       heights, *range.first, *range.second);
                  });
    body3.addFunc(
        "newHeightFieldShapeFull",
        [](Body3D *body, int countX, int countZ, float cellSizeX, float cellSizeZ,
           ssq::Array values, float globalMin, float globalMax, bool clockwiseWinding) {
            std::vector<float> heights;
            heights.reserve(values.size());
            for (size_t i = 0; i < values.size(); ++i)
                heights.push_back(values.get<float>(i));
            return body->newHeightFieldShape(countX, countZ, cellSizeX, cellSizeZ, heights,
                                             globalMin, globalMax, clockwiseWinding);
        });
    body3.addFunc("destroy", &Body3D::destroy);

    auto joint3 = table.addClass<Joint3D>(
        "Joint3D", std::function<Joint3D *()>([]() -> Joint3D * { return nullptr; }), true);
    joint3.addFunc("getId", &Joint3D::getId);
    joint3.addFunc("getKind", &Joint3D::getKind);
    joint3.addFunc("getBodyAId", &Joint3D::getBodyAId);
    joint3.addFunc("getBodyBId", &Joint3D::getBodyBId);
    joint3.addFunc("setCollideConnected", &Joint3D::setCollideConnected);
    joint3.addFunc("getCollideConnected", &Joint3D::getCollideConnected);
    joint3.addFunc("setForceThreshold", &Joint3D::setForceThreshold);
    joint3.addFunc("getForceThreshold", &Joint3D::getForceThreshold);
    joint3.addFunc("setTorqueThreshold", &Joint3D::setTorqueThreshold);
    joint3.addFunc("getTorqueThreshold", &Joint3D::getTorqueThreshold);
    joint3.addFunc("getConstraintForceX", &Joint3D::getConstraintForceX);
    joint3.addFunc("getConstraintForceY", &Joint3D::getConstraintForceY);
    joint3.addFunc("getConstraintForceZ", &Joint3D::getConstraintForceZ);
    joint3.addFunc("getConstraintTorqueX", &Joint3D::getConstraintTorqueX);
    joint3.addFunc("getConstraintTorqueY", &Joint3D::getConstraintTorqueY);
    joint3.addFunc("getConstraintTorqueZ", &Joint3D::getConstraintTorqueZ);
    joint3.addFunc("getLinearSeparation", &Joint3D::getLinearSeparation);
    joint3.addFunc("getAngularSeparation", &Joint3D::getAngularSeparation);
    joint3.addFunc("setDistanceLength", &Joint3D::setDistanceLength);
    joint3.addFunc("getDistanceLength", &Joint3D::getDistanceLength);
    joint3.addFunc("getDistanceCurrentLength", &Joint3D::getDistanceCurrentLength);
    joint3.addFunc("setDistanceSpring", &Joint3D::setDistanceSpring);
    joint3.addFunc("setDistanceLimits", &Joint3D::setDistanceLimits);
    joint3.addFunc("setDistanceMotor", &Joint3D::setDistanceMotor);
    joint3.addFunc("setRevoluteSpring", &Joint3D::setRevoluteSpring);
    joint3.addFunc("setRevoluteLimits", &Joint3D::setRevoluteLimits);
    joint3.addFunc("setRevoluteMotor", &Joint3D::setRevoluteMotor);
    joint3.addFunc("getRevoluteAngle", &Joint3D::getRevoluteAngle);
    joint3.addFunc("getRevoluteMotorTorque", &Joint3D::getRevoluteMotorTorque);
    joint3.addFunc("setPrismaticSpring", &Joint3D::setPrismaticSpring);
    joint3.addFunc("setPrismaticLimits", &Joint3D::setPrismaticLimits);
    joint3.addFunc("setPrismaticMotor", &Joint3D::setPrismaticMotor);
    joint3.addFunc("getPrismaticTranslation", &Joint3D::getPrismaticTranslation);
    joint3.addFunc("getPrismaticSpeed", &Joint3D::getPrismaticSpeed);
    joint3.addFunc("getPrismaticMotorForce", &Joint3D::getPrismaticMotorForce);
    joint3.addFunc("setSphericalConeLimit", &Joint3D::setSphericalConeLimit);
    joint3.addFunc("setSphericalTwistLimits", &Joint3D::setSphericalTwistLimits);
    joint3.addFunc("setSphericalMotor", &Joint3D::setSphericalMotor);
    joint3.addFunc("getSphericalConeAngle", &Joint3D::getSphericalConeAngle);
    joint3.addFunc("getSphericalTwistAngle", &Joint3D::getSphericalTwistAngle);
    joint3.addFunc("setWheelSuspension", &Joint3D::setWheelSuspension);
    joint3.addFunc("setWheelSuspensionLimits", &Joint3D::setWheelSuspensionLimits);
    joint3.addFunc("setWheelSpinMotor", &Joint3D::setWheelSpinMotor);
    joint3.addFunc("setWheelSteering", &Joint3D::setWheelSteering);
    joint3.addFunc("setWheelSteeringLimits", &Joint3D::setWheelSteeringLimits);
    joint3.addFunc("getWheelSpinSpeed", &Joint3D::getWheelSpinSpeed);
    joint3.addFunc("getWheelSpinTorque", &Joint3D::getWheelSpinTorque);
    joint3.addFunc("getWheelSteeringAngle", &Joint3D::getWheelSteeringAngle);
    joint3.addFunc("getWheelSteeringTorque", &Joint3D::getWheelSteeringTorque);
    joint3.addFunc("destroy", &Joint3D::destroy);

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
    shape3.addFunc("getId", &Shape3D::getId);
    shape3.addFunc("setTag", &Shape3D::setTag);
    shape3.addFunc("getTag", &Shape3D::getTag);
    shape3.addFunc("setHitEventsEnabled", &Shape3D::setHitEventsEnabled);
    shape3.addFunc("areHitEventsEnabled", &Shape3D::areHitEventsEnabled);
    shape3.addFunc("setOneWay", &Shape3D::setOneWay);
    shape3.addFunc("disableOneWay", &Shape3D::disableOneWay);
    shape3.addFunc("isOneWay", &Shape3D::isOneWay);
    shape3.addFunc("getKind", &Shape3D::getKind);
    shape3.addFunc("setBoxSize", &Shape3D::setBoxSize);
    shape3.addFunc("setSphereRadius", &Shape3D::setSphereRadius);
    shape3.addFunc("setCapsuleSize", &Shape3D::setCapsuleSize);
    shape3.addFunc("setConvexHullVertices",
                   [](Shape3D *shape, ssq::Array values, int maxVertices) {
                       std::vector<float> vertices;
                       vertices.reserve(values.size());
                       for (size_t i = 0; i < values.size(); ++i)
                           vertices.push_back(values.get<float>(i));
                       shape->setConvexHullVertices(vertices, maxVertices);
                   });
    shape3.addFunc("getConvexHullPointCount", &Shape3D::getConvexHullPointCount);
    shape3.addFunc("getConvexHullMaxVertices", &Shape3D::getConvexHullMaxVertices);
    shape3.addFunc("setTriangleMeshData",
                   [](Shape3D *shape, ssq::Array vertexValues, ssq::Array indexValues) {
                       std::vector<float> vertices;
                       std::vector<int32_t> indices;
                       vertices.reserve(vertexValues.size());
                       indices.reserve(indexValues.size());
                       for (size_t i = 0; i < vertexValues.size(); ++i)
                           vertices.push_back(vertexValues.get<float>(i));
                       for (size_t i = 0; i < indexValues.size(); ++i)
                           indices.push_back(indexValues.get<int32_t>(i));
                       shape->setTriangleMeshData(vertices, indices);
                   });
    shape3.addFunc(
        "setTriangleMeshDataFull",
        [](Shape3D *shape, ssq::Array vertexValues, ssq::Array indexValues, bool weldVertices,
           float weldTolerance, bool identifyEdges, bool useMedianSplit) {
            std::vector<float> vertices;
            std::vector<int32_t> indices;
            vertices.reserve(vertexValues.size());
            indices.reserve(indexValues.size());
            for (size_t i = 0; i < vertexValues.size(); ++i)
                vertices.push_back(vertexValues.get<float>(i));
            for (size_t i = 0; i < indexValues.size(); ++i)
                indices.push_back(indexValues.get<int32_t>(i));
            shape->setTriangleMeshData(vertices, indices, weldVertices, weldTolerance,
                                       identifyEdges, useMedianSplit);
        });
    shape3.addFunc("getTriangleMeshVertexCount", &Shape3D::getTriangleMeshVertexCount);
    shape3.addFunc("getTriangleMeshTriangleCount", &Shape3D::getTriangleMeshTriangleCount);
    shape3.addFunc("setTriangleMeshMaterialIndices", [](Shape3D *shape, ssq::Array values) {
        std::vector<int32_t> indices;
        indices.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i)
            indices.push_back(values.get<int32_t>(i));
        shape->setTriangleMeshMaterialIndices(indices);
    });
    shape3.addFunc("getTriangleMeshMaterialIndex",
                   &Shape3D::getTriangleMeshMaterialIndex);
    shape3.addFunc("getTriangleMeshMaterialCount",
                   &Shape3D::getTriangleMeshMaterialCount);
    shape3.addFunc("setTriangleMeshMaterial", &Shape3D::setTriangleMeshMaterial);
    shape3.addFunc("getTriangleMeshMaterialFriction",
                   &Shape3D::getTriangleMeshMaterialFriction);
    shape3.addFunc("getTriangleMeshMaterialRestitution",
                   &Shape3D::getTriangleMeshMaterialRestitution);
    shape3.addFunc("getTriangleMeshMaterialRollingResistance",
                   &Shape3D::getTriangleMeshMaterialRollingResistance);
    shape3.addFunc("getTriangleMeshMaterialId", &Shape3D::getTriangleMeshMaterialId);
    shape3.addFunc("setHeightFieldHeights", [](Shape3D *shape, ssq::Array values) {
        std::vector<float> heights;
        heights.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i)
            heights.push_back(values.get<float>(i));
        shape->setHeightFieldHeights(heights);
    });
    shape3.addFunc("setHeightFieldRegion",
                   [](Shape3D *shape, int x, int z, int width, int depth,
                      ssq::Array values) {
                       std::vector<float> heights;
                       heights.reserve(values.size());
                       for (size_t i = 0; i < values.size(); ++i)
                           heights.push_back(values.get<float>(i));
                       shape->setHeightFieldRegion(x, z, width, depth, heights);
                   });
    shape3.addFunc("getHeightFieldHeight", &Shape3D::getHeightFieldHeight);
    shape3.addFunc("getHeightFieldCountX", &Shape3D::getHeightFieldCountX);
    shape3.addFunc("getHeightFieldCountZ", &Shape3D::getHeightFieldCountZ);
    shape3.addFunc("getHeightFieldCellSizeX", &Shape3D::getHeightFieldCellSizeX);
    shape3.addFunc("getHeightFieldCellSizeZ", &Shape3D::getHeightFieldCellSizeZ);
    shape3.addFunc("getHeightFieldGlobalMin", &Shape3D::getHeightFieldGlobalMin);
    shape3.addFunc("getHeightFieldGlobalMax", &Shape3D::getHeightFieldGlobalMax);
    shape3.addFunc("getBoxWidth", &Shape3D::getBoxWidth);
    shape3.addFunc("getBoxHeight", &Shape3D::getBoxHeight);
    shape3.addFunc("getBoxDepth", &Shape3D::getBoxDepth);
    shape3.addFunc("getRadius", &Shape3D::getRadius);
    shape3.addFunc("getCapsuleHeight", &Shape3D::getCapsuleHeight);
    shape3.addFunc("setLocalTransform", &Shape3D::setLocalTransform);
    shape3.addFunc("setLocalPosition", &Shape3D::setLocalPosition);
    shape3.addFunc("setLocalRotation", &Shape3D::setLocalRotation);
    shape3.addFunc("getLocalX", &Shape3D::getLocalX);
    shape3.addFunc("getLocalY", &Shape3D::getLocalY);
    shape3.addFunc("getLocalZ", &Shape3D::getLocalZ);
    shape3.addFunc("getLocalRotX", &Shape3D::getLocalRotX);
    shape3.addFunc("getLocalRotY", &Shape3D::getLocalRotY);
    shape3.addFunc("getLocalRotZ", &Shape3D::getLocalRotZ);
    shape3.addFunc("getLocalRotW", &Shape3D::getLocalRotW);
    shape3.addFunc("setSensor", &Shape3D::setSensor);
    shape3.addFunc("isSensor", &Shape3D::isSensor);
    shape3.addFunc("setFriction", &Shape3D::setFriction);
    shape3.addFunc("getFriction", &Shape3D::getFriction);
    shape3.addFunc("setRestitution", &Shape3D::setRestitution);
    shape3.addFunc("getRestitution", &Shape3D::getRestitution);
    shape3.addFunc("setRollingResistance", &Shape3D::setRollingResistance);
    shape3.addFunc("getRollingResistance", &Shape3D::getRollingResistance);
    shape3.addFunc("setTangentVelocity", &Shape3D::setTangentVelocity);
    shape3.addFunc("getTangentVelocityX", &Shape3D::getTangentVelocityX);
    shape3.addFunc("getTangentVelocityY", &Shape3D::getTangentVelocityY);
    shape3.addFunc("getTangentVelocityZ", &Shape3D::getTangentVelocityZ);
    shape3.addFunc("setMaterialId", &Shape3D::setMaterialId);
    shape3.addFunc("getMaterialId", &Shape3D::getMaterialId);
    shape3.addFunc("setExplosionScale", &Shape3D::setExplosionScale);
    shape3.addFunc("getExplosionScale", &Shape3D::getExplosionScale);
    shape3.addFunc("setFrictionCombineMode", &Shape3D::setFrictionCombineMode);
    shape3.addFunc("getFrictionCombineMode", &Shape3D::getFrictionCombineMode);
    shape3.addFunc("setRestitutionCombineMode", &Shape3D::setRestitutionCombineMode);
    shape3.addFunc("getRestitutionCombineMode", &Shape3D::getRestitutionCombineMode);
    shape3.addFunc("setDensity", &Shape3D::setDensity);
    shape3.addFunc("getDensity", &Shape3D::getDensity);
    shape3.addFunc("setCategoryBits", [](Shape3D *shape, int64_t bits) {
        shape->setCategoryBits(static_cast<uint64_t>(bits));
    });
    shape3.addFunc("getCategoryBits", [](Shape3D *shape) {
        return static_cast<int64_t>(shape->getCategoryBits());
    });
    shape3.addFunc("setMaskBits", [](Shape3D *shape, int64_t bits) {
        shape->setMaskBits(static_cast<uint64_t>(bits));
    });
    shape3.addFunc("getMaskBits", [](Shape3D *shape) {
        return static_cast<int64_t>(shape->getMaskBits());
    });
    shape3.addFunc("setGroupIndex", &Shape3D::setGroupIndex);
    shape3.addFunc("getGroupIndex", &Shape3D::getGroupIndex);
    shape3.addFunc("getBody", [](Shape3D *shape) { return shape->getBody(); });
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
    cloth.addFunc("setParticleSize", &Cloth::setParticleSize);
    cloth.addFunc("getParticleSize", &Cloth::getParticleSize);
    cloth.addFunc("setParticleMass", &Cloth::setParticleMass);
    cloth.addFunc("getParticleMass", &Cloth::getParticleMass);
    cloth.addFunc("setSelfCollision", &Cloth::setSelfCollision);
    cloth.addFunc("getSelfCollision", &Cloth::getSelfCollision);
    cloth.addFunc("setFoldStiffness", &Cloth::setFoldStiffness);
    cloth.addFunc("getFoldStiffness", &Cloth::getFoldStiffness);
    cloth.addFunc("setMaxFoldAngle", &Cloth::setMaxFoldAngle);
    cloth.addFunc("getMaxFoldAngle", &Cloth::getMaxFoldAngle);
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
    cloth.addFunc("interactAt", &Cloth::interactAt);
    cloth.addFunc("setCollideWorld", &Cloth::setCollideWorld);
    cloth.addFunc("getCollideWorld", &Cloth::getCollideWorld);
    cloth.addFunc("reset", &Cloth::reset);
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

    auto cloth3 = table.addClass<Cloth3D>(
        "Cloth3D", std::function<Cloth3D *()>([]() -> Cloth3D * { return nullptr; }), true);
    cloth3.addFunc("update", &Cloth3D::update);
    cloth3.addFunc("setGravity", &Cloth3D::setGravity);
    cloth3.addFunc("getGravityX", &Cloth3D::getGravityX);
    cloth3.addFunc("getGravityY", &Cloth3D::getGravityY);
    cloth3.addFunc("getGravityZ", &Cloth3D::getGravityZ);
    cloth3.addFunc("setStiffness", &Cloth3D::setStiffness);
    cloth3.addFunc("getStiffness", &Cloth3D::getStiffness);
    cloth3.addFunc("setIterations", &Cloth3D::setIterations);
    cloth3.addFunc("getIterations", &Cloth3D::getIterations);
    cloth3.addFunc("setDamping", &Cloth3D::setDamping);
    cloth3.addFunc("getDamping", &Cloth3D::getDamping);
    cloth3.addFunc("setParticleSize", &Cloth3D::setParticleSize);
    cloth3.addFunc("getParticleSize", &Cloth3D::getParticleSize);
    cloth3.addFunc("setParticleMass", &Cloth3D::setParticleMass);
    cloth3.addFunc("getParticleMass", &Cloth3D::getParticleMass);
    cloth3.addFunc("setSelfCollision", &Cloth3D::setSelfCollision);
    cloth3.addFunc("getSelfCollision", &Cloth3D::getSelfCollision);
    cloth3.addFunc("setFoldStiffness", &Cloth3D::setFoldStiffness);
    cloth3.addFunc("getFoldStiffness", &Cloth3D::getFoldStiffness);
    cloth3.addFunc("setMaxFoldAngle", &Cloth3D::setMaxFoldAngle);
    cloth3.addFunc("getMaxFoldAngle", &Cloth3D::getMaxFoldAngle);
    cloth3.addFunc("setBounds", &Cloth3D::setBounds);
    cloth3.addFunc("clearBounds", &Cloth3D::clearBounds);
    cloth3.addFunc("pin", &Cloth3D::pin);
    cloth3.addFunc("unpin", &Cloth3D::unpin);
    cloth3.addFunc("pinTopRow", &Cloth3D::pinTopRow);
    cloth3.addFunc("isPinned", &Cloth3D::isPinned);
    cloth3.addFunc("grabAt", &Cloth3D::grabAt);
    cloth3.addFunc("moveGrab", &Cloth3D::moveGrab);
    cloth3.addFunc("releaseGrab", &Cloth3D::releaseGrab);
    cloth3.addFunc("isGrabbing", &Cloth3D::isGrabbing);
    cloth3.addFunc("getGrabIndex", &Cloth3D::getGrabIndex);
    cloth3.addFunc("applyForce", &Cloth3D::applyForce);
    cloth3.addFunc("interactAt", &Cloth3D::interactAt);
    cloth3.addFunc("setCollideWorld", &Cloth3D::setCollideWorld);
    cloth3.addFunc("getCollideWorld", &Cloth3D::getCollideWorld);
    cloth3.addFunc("reset", &Cloth3D::reset);
    cloth3.addFunc("setColor", &Cloth3D::setColor);
    cloth3.addFunc("draw", &Cloth3D::draw);
    cloth3.addFunc("getCols", &Cloth3D::getCols);
    cloth3.addFunc("getRows", &Cloth3D::getRows);
    cloth3.addFunc("getParticleCount", &Cloth3D::getParticleCount);
    cloth3.addFunc("getParticleX", &Cloth3D::getParticleX);
    cloth3.addFunc("getParticleY", &Cloth3D::getParticleY);
    cloth3.addFunc("getParticleZ", &Cloth3D::getParticleZ);
    cloth3.addFunc("setParticlePosition", &Cloth3D::setParticlePosition);
    cloth3.addFunc("getSpacing", &Cloth3D::getSpacing);
    cloth3.addFunc("getOriginX", &Cloth3D::getOriginX);
    cloth3.addFunc("getOriginY", &Cloth3D::getOriginY);
    cloth3.addFunc("getOriginZ", &Cloth3D::getOriginZ);
    cloth3.addFunc("destroy", &Cloth3D::destroy);

    auto clothGpu = table.addClass<ClothGPU>(
        "ClothGPU", std::function<ClothGPU *()>([]() -> ClothGPU * { return nullptr; }), true);
    clothGpu.addFunc("update", &ClothGPU::update);
    clothGpu.addFunc("setGravity", &ClothGPU::setGravity);
    clothGpu.addFunc("getGravityX", &ClothGPU::getGravityX);
    clothGpu.addFunc("getGravityY", &ClothGPU::getGravityY);
    clothGpu.addFunc("setStiffness", &ClothGPU::setStiffness);
    clothGpu.addFunc("getStiffness", &ClothGPU::getStiffness);
    clothGpu.addFunc("setIterations", &ClothGPU::setIterations);
    clothGpu.addFunc("getIterations", &ClothGPU::getIterations);
    clothGpu.addFunc("setDamping", &ClothGPU::setDamping);
    clothGpu.addFunc("getDamping", &ClothGPU::getDamping);
    clothGpu.addFunc("setParticleSize", &ClothGPU::setParticleSize);
    clothGpu.addFunc("getParticleSize", &ClothGPU::getParticleSize);
    clothGpu.addFunc("setSelfCollision", &ClothGPU::setSelfCollision);
    clothGpu.addFunc("getSelfCollision", &ClothGPU::getSelfCollision);
    clothGpu.addFunc("setBounds", &ClothGPU::setBounds);
    clothGpu.addFunc("clearBounds", &ClothGPU::clearBounds);
    clothGpu.addFunc("pin", &ClothGPU::pin);
    clothGpu.addFunc("unpin", &ClothGPU::unpin);
    clothGpu.addFunc("pinTopRow", &ClothGPU::pinTopRow);
    clothGpu.addFunc("isPinned", &ClothGPU::isPinned);
    clothGpu.addFunc("applyForce", &ClothGPU::applyForce);
    clothGpu.addFunc("interactAt", &ClothGPU::interactAt);
    clothGpu.addFunc("setColor", &ClothGPU::setColor);
    clothGpu.addFunc("draw", &ClothGPU::draw);
    clothGpu.addFunc("getCols", &ClothGPU::getCols);
    clothGpu.addFunc("getRows", &ClothGPU::getRows);
    clothGpu.addFunc("getParticleCount", &ClothGPU::getParticleCount);
    clothGpu.addFunc("getParticleX", &ClothGPU::getParticleX);
    clothGpu.addFunc("getParticleY", &ClothGPU::getParticleY);
    clothGpu.addFunc("getSpacing", &ClothGPU::getSpacing);
    clothGpu.addFunc("getOriginX", &ClothGPU::getOriginX);
    clothGpu.addFunc("getOriginY", &ClothGPU::getOriginY);
    clothGpu.addFunc("reset", &ClothGPU::reset);
    clothGpu.addFunc("destroy", &ClothGPU::destroy);

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
    cls.addFunc("newDistanceField3D", &Physics::newDistanceField3D);
    cls.addFunc("newCloth", &Physics::newCloth);
    cls.addFunc("newCloth3D", &Physics::newCloth3D);
    cls.addFunc("newClothGPU", &Physics::newClothGPU);
    cls.addFunc("newFluid", &Physics::newFluid);
}

}  // namespace eve::physics
