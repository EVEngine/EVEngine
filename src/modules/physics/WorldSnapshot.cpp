#include "physics/World.h"


#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/backend/SimulationBackend.h"
#include "physics/World3D.h"

#include <Box2D/Box2D.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "physics/snapshot/WorldSnapshotAccess.h"
#include "physics/snapshot/WorldSnapshotCodec.h"

namespace eve::physics {
using namespace snapshot_detail;


eve::Result<eve::SnapshotEnvelope> World::snapshot(const eve::SnapshotHashProvider& hashProvider) const {
    if (pendingForceStep_)
        return Result<SnapshotEnvelope>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                   "Complete the submitted force step before snapshot",
                                                                   "physics.forces.snapshot"));
    if (!isValid() || !simulation_)
        return eve::Result<eve::SnapshotEnvelope>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot snapshot a destroyed or uninitialized physics world",
            "physics.world.snapshot"));
    if (raw()->IsLocked() || simulationStepActive_)
        return Result<SnapshotEnvelope>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Cannot snapshot while stepping"));
    auto joints = WorldSnapshotAccess::captureJoints2D(*this);
    if (!joints) return Result<SnapshotEnvelope>::failure(joints.status());
    auto schema = snapshotSchema(kWorld2DSchema);
    if (!schema) return eve::Result<eve::SnapshotEnvelope>::failure(schema.status());
    const auto         observation = simulationObservation();
    std::vector<Body*> bodies(bodies_.begin(), bodies_.end());
    auto               payload = make2DPayload(*this, bodies);
    payload.set("joints", std::move(joints).takeValue());
    return eve::makeSnapshotEnvelope(
        std::string(kWorld2DType), std::move(schema).takeValue(), eve::SchemaVersion(kSnapshot2DVersion), instanceId_,
        eve::Revision(observation.stepCount), simulationTick(), std::move(payload), hashProvider);
}

eve::Result<void> World::restore(const eve::SnapshotEnvelope&     snapshotValue,
                                 const eve::SnapshotHashProvider& hashProvider) {
    if (!isValid() || !simulation_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot restore a destroyed or uninitialized physics world",
            "physics.world.restore"));
    auto verified = eve::verifySnapshotEnvelope(snapshotValue, hashProvider);
    if (!verified) return verified;
    auto envelope = checkEnvelope(snapshotValue, kWorld2DType, kWorld2DSchema);
    if (!envelope) return envelope;
    if (raw()->IsLocked() || simulationStepActive_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Cannot restore while stepping"));
    if (snapshotValue.instanceId.isNil() || snapshotValue.instanceId != instanceId_)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "physics snapshot belongs to a different world instance", "snapshot.instanceId"));
    eve::Value migratedPayload = snapshotValue.payload;
    if (snapshotValue.schemaVersion.value() == 2) {
        auto migration = migratePlanarV2(migratedPayload);
        if (!migration) return migration;
    }
    const auto* object = migratedPayload.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(*object, {"bodies", "joints", "allowSleep", "gravityX", "gravityY", "meter", "tick",
                                             "revision", "stepCount", "simulatedDurationNs", "lastDeltaSeconds"}))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "2D physics snapshot payload has unknown or missing fields", "payload"));
    auto observation = parseObservation(*object, snapshotValue);
    if (!observation) return eve::Result<void>::failure(observation.status());
    auto bodies = parseBodies2D(*object);
    if (!bodies) return eve::Result<void>::failure(bodies.status());
    auto gravityX = readFloat(*object, "gravityX");
    if (!gravityX) return eve::Result<void>::failure(gravityX.status());
    auto gravityY = readFloat(*object, "gravityY");
    if (!gravityY) return eve::Result<void>::failure(gravityY.status());
    auto meter = readFloat(*object, "meter");
    if (!meter) return eve::Result<void>::failure(meter.status());
    if (meter.value() <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "physics snapshot meter must be positive", "payload.meter"));

    std::set<int> snapshotIds;
    auto          matched = matchBodyIds(bodies.value(), snapshotIds);
    if (!matched) return matched;
    auto handleRefresh = prepareRuntimeHandleRefresh(bodies.value().size());
    if (!handleRefresh) return handleRefresh;
    auto prepared = prepareWorld2D(bodies.value(), gravityX.value(), gravityY.value(), meter.value(),
                                   observation.value().value, instanceId_);
    if (!prepared) return eve::Result<void>::failure(prepared.status());
    const auto* jointValues = field(*object, "joints")->getIf<Value::Array>();
    if (!jointValues)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Planar joints must be an array"));
    auto sleeping = readBool(*object, "allowSleep");
    if (!sleeping) return Result<void>::failure(sleeping.status());
    prepared.value()->raw()->SetAllowSleeping(sleeping.value());
    auto jointRestore = WorldSnapshotAccess::restoreJoints2D(*prepared.value(), *jointValues, nextJointHandleIndex_);
    if (!jointRestore) return jointRestore;
    WorldSnapshotAccess::adopt(*this, *prepared.value());
    refreshRuntimeHandlesAfterRestore();
    clearContactEvents();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<eve::SnapshotEnvelope> World3D::snapshot(const eve::SnapshotHashProvider& hashProvider) const {
    if (rigidBackendKind_ != RigidBackend3D::Box3D) return snapshotPortable(hashProvider);
    if (pendingForceStep_)
        return Result<SnapshotEnvelope>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                   "Complete the submitted force step before snapshot",
                                                                   "physics.forces.snapshot"));
    if (couplingStepActive_)
        return Result<SnapshotEnvelope>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Cannot snapshot while stepping", "physics.world3d.snapshot"));
    if (!isValid() || !simulation_)
        return eve::Result<eve::SnapshotEnvelope>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot snapshot a destroyed or uninitialized 3D physics world",
            "physics.world3d.snapshot"));
    auto schema = snapshotSchema(kWorld3DSchema);
    if (!schema) return eve::Result<eve::SnapshotEnvelope>::failure(schema.status());
    const auto           observation = simulationObservation();
    std::vector<Body3D*> bodies(bodies_.begin(), bodies_.end());
    auto                 couplings = WorldSnapshotAccess::captureCouplings3D(*this);
    if (!couplings) return Result<SnapshotEnvelope>::failure(couplings.status());
    auto payload = make3DPayload(*this, bodies, std::vector<Shape3D*>(shapes_.begin(), shapes_.end()),
                                 std::vector<Joint3D*>(joints_.begin(), joints_.end()));
    payload.set("couplings", std::move(couplings).takeValue());
    return eve::makeSnapshotEnvelope(
        std::string(kWorld3DType), std::move(schema).takeValue(), eve::SchemaVersion(kSnapshotVersion), instanceId_,
        eve::Revision(observation.stepCount), simulationTick(), std::move(payload), hashProvider);
}

eve::Result<void> World3D::restore(const eve::SnapshotEnvelope&     snapshotValue,
                                   const eve::SnapshotHashProvider& hashProvider) {
    if (rigidBackendKind_ != RigidBackend3D::Box3D) return restorePortable(snapshotValue, hashProvider);
    if (couplingStepActive_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Cannot restore while stepping", "physics.world3d.restore"));
    if (!isValid() || !simulation_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot restore a destroyed or uninitialized 3D physics world",
            "physics.world3d.restore"));
    auto verified = eve::verifySnapshotEnvelope(snapshotValue, hashProvider);
    if (!verified) return verified;
    auto envelope = checkEnvelope(snapshotValue, kWorld3DType, kWorld3DSchema);
    if (!envelope) return envelope;
    const bool legacyV1 = snapshotValue.schemaVersion.value() == 1;
    if (!legacyV1 && (snapshotValue.instanceId.isNil() || snapshotValue.instanceId != instanceId_))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                   "3D physics snapshot belongs to a different world instance", "snapshot.instanceId"));
    eve::Value migratedPayload = snapshotValue.payload;
    if (legacyV1) {
        auto* migrated = migratedPayload.getIf<eve::Value::Object>();
        if (!migrated)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                     "v1 3D physics payload is malformed", "payload"));
        auto legacyShapes = shapesTopologyValue(std::vector<Shape3D*>(shapes_.begin(), shapes_.end()));
        for (auto& shape : *legacyShapes.getIf<Value::Array>()) {
            shape.getIf<Value::Object>()->erase("localTransform");
            shape.getIf<Value::Object>()->erase("contact");
        }
        migrated->emplace("shapes", std::move(legacyShapes));
        auto legacyJoints = jointsTopologyValue(std::vector<Joint3D*>(joints_.begin(), joints_.end()));
        for (auto& joint : *legacyJoints.getIf<Value::Array>()) joint.getIf<Value::Object>()->erase("parameters");
        migrated->emplace("joints", std::move(legacyJoints));
        const eve::Value* bodyValues = field(*migrated, "bodies");
        if (!bodyValues || bodyValues->arraySize() != bodies_.size())
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                       "v1 snapshot requires topology-compatible live bodies", "payload.bodies"));
    }
    if (snapshotValue.schemaVersion.value() < 3) {
        auto* legacy = migratedPayload.getIf<Value::Object>();
        if (!legacy || legacy->contains("couplings"))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "Legacy snapshot has unexpected transmission fields"));
        if (legacy->contains("allowSleep"))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "Legacy world has unexpected dynamics"));
        legacy->emplace("allowSleep", Value(false));
        auto* bodyValues = migratedPayload.find("bodies");
        auto* bodyArray  = bodyValues ? bodyValues->getIf<Value::Array>() : nullptr;
        if (!bodyArray)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "Legacy bodies must be an array"));
        const auto defaults = b3DefaultBodyDef();
        for (auto& body : *bodyArray) {
            auto* bodyObject = body.getIf<Value::Object>();
            if (!bodyObject || bodyObject->contains("dynamics"))
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "Legacy body has unexpected dynamics"));
            bodyObject->emplace("dynamics", Value(Value::Object{{"linearDamping", Value(defaults.linearDamping)},
                                                                {"angularDamping", Value(defaults.angularDamping)},
                                                                {"gravityScale", Value(defaults.gravityScale)},
                                                                {"sleepThreshold", Value(defaults.sleepThreshold)},
                                                                {"allowSleep", Value(defaults.enableSleep)}}));
        }
        legacy->emplace("couplings", Value(Value::Array{}));
    }
    const auto* object = migratedPayload.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(
                       *object, {"bodies", "couplings", "allowSleep", "gravityX", "gravityY", "gravityZ", "joints",
                                 "shapes", "tick", "revision", "stepCount", "simulatedDurationNs", "lastDeltaSeconds"}))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "3D physics snapshot payload has unknown or missing fields", "payload"));
    auto observation = parseObservation(*object, snapshotValue);
    if (!observation) return eve::Result<void>::failure(observation.status());
    auto bodies = parseBodies3D(*object);
    if (!bodies) return eve::Result<void>::failure(bodies.status());
    auto gravityX = readFloat(*object, "gravityX");
    if (!gravityX) return eve::Result<void>::failure(gravityX.status());
    auto gravityY = readFloat(*object, "gravityY");
    if (!gravityY) return eve::Result<void>::failure(gravityY.status());
    auto gravityZ = readFloat(*object, "gravityZ");
    if (!gravityZ) return eve::Result<void>::failure(gravityZ.status());
    const eve::Value* snapshotShapes = field(*object, "shapes");
    const eve::Value* snapshotJoints = field(*object, "joints");
    if (!snapshotShapes || !snapshotShapes->isArray() || !snapshotJoints || !snapshotJoints->isArray())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "3D physics snapshot topology fields must be arrays", "payload.shapes"));
    std::set<int> snapshotIds;
    auto          matched = matchBodyIds(bodies.value(), snapshotIds);
    if (!matched) return matched;
    auto handleRefresh =
        prepareRuntimeHandleRefresh(bodies.value().size(), snapshotShapes->arraySize(), snapshotJoints->arraySize());
    if (!handleRefresh) return handleRefresh;
    auto allowSleep = readBool(*object, "allowSleep");
    if (!allowSleep) return Result<void>::failure(allowSleep.status());
    auto prepared = prepareWorld3D(bodies.value(), *snapshotShapes->getIf<eve::Value::Array>(),
                                   *snapshotJoints->getIf<eve::Value::Array>(), gravityX.value(), gravityY.value(),
                                   gravityZ.value(), observation.value().value, instanceId_,
                                   snapshotValue.schemaVersion.value() < 3, allowSleep.value());
    if (!prepared) return eve::Result<void>::failure(prepared.status());
    auto preparedLinks = WorldSnapshotAccess::prepareRuntimeLinks(*this, *prepared.value());
    if (!preparedLinks) return preparedLinks;
    const auto* couplings = field(*object, "couplings")->getIf<Value::Array>();
    if (!couplings)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Couplings must be an array"));
    auto preparedCouplings = WorldSnapshotAccess::restoreCouplings3D(*prepared.value(), *couplings);
    if (!preparedCouplings) return preparedCouplings;
    auto restoredObservation = simulation_->restoreObservation(observation.value().value);
    if (!restoredObservation) return restoredObservation;
    WorldSnapshotAccess::adopt(*this, *prepared.value());
    ++queryEpoch_;
    clearContactEvents();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> World::prepareRuntimeHandleRefresh(std::size_t incomingBodyCount) const {
    const auto available =
        static_cast<std::uint64_t>(PhysicsBodyHandle::invalidIndex) - nextBodyHandleIndex_;
    if (incomingBodyCount > available)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "2D physics body handle space cannot represent restored links",
            "physics.world.restore.handles"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void World::refreshRuntimeHandlesAfterRestore() {
    for (Body* body : bodies_)
        if (body && body->isValid()) body->runtimeHandle_ = nextBodyRuntimeHandle();
}

eve::Result<void> World3D::prepareRuntimeHandleRefresh(std::size_t bodyCount, std::size_t shapeCount,
                                                       std::size_t jointCount) const {
    const auto bodyAvailable = static_cast<std::uint64_t>(PhysicsBodyHandle::invalidIndex) - nextBodyHandleIndex_;
    const auto shapeAvailable = static_cast<std::uint64_t>(PhysicsShapeHandle::invalidIndex) - nextShapeHandleIndex_;
    const auto jointAvailable = static_cast<std::uint64_t>(PhysicsJointHandle::invalidIndex) - nextJointHandleIndex_;
    if (queryEpoch_ == UINT64_MAX || bodyCount > bodyAvailable || shapeCount > shapeAvailable ||
        jointCount > jointAvailable)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "3D physics handle space cannot represent restored links",
            "physics.world3d.restore.handles"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void World3D::refreshRuntimeHandlesAfterRestore() {
    ++queryEpoch_;
    shapeRecords_.clear();
    shapeHandles_.clear();
    shapeRawHandles_.clear();
    jointHandles_.clear();
    for (Body3D* body : bodies_)
        if (body && body->isValid()) body->runtimeHandle_ = nextBodyRuntimeHandle();
    for (Shape3D* shape : shapes_) {
        if (!shape || !shape->isValid()) continue;
        shape->runtimeHandle_ = nextShapeRuntimeHandle();
        indexShapeHandle(shape);
    }
    for (Joint3D* joint : joints_) {
        if (!joint || !joint->isValid()) continue;
        joint->runtimeHandle_ = nextJointRuntimeHandle();
        jointHandles_[joint->runtimeHandle_] = joint;
    }
}

}  // namespace eve::physics
