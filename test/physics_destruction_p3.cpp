#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Physics.h"
#include "physics/World3D.h"
#include "physics/destruction/Destruction.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/GeometryCollectionSnapshot.h"
#include "schema/SchemaRegistry.h"

#include <memory>

using eve::physics::BoneRuntimeState;
using eve::physics::Destruction;
using eve::physics::DestructionField;
using eve::physics::DestructionFieldFalloff;
using eve::physics::DestructionFieldKind;
using eve::physics::DestructionStepBudget;
using eve::physics::GeometryCollectionAsset;
using eve::physics::GeometryCollectionBone;
using eve::physics::GeometryCollectionEdge;
using eve::physics::GeometryCollectionInstance;
using eve::physics::GeometryCollectionInstanceSnapshot;
using eve::physics::Physics;
using eve::physics::World3D;

namespace {

DestructionField strainAt(float x, float y, float z, float radius, float magnitude) {
    DestructionField field;
    field.kind      = DestructionFieldKind::Strain;
    field.falloff   = DestructionFieldFalloff::Linear;
    field.centerX   = x;
    field.centerY   = y;
    field.centerZ   = z;
    field.radius    = radius;
    field.magnitude = magnitude;
    return field;
}

eve::SimulationStep simStep(std::uint64_t tick, double seconds = 1.0 / 60.0) {
    return {eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("destruction dt")};
}

eve::Result<GeometryCollectionAsset> makeThreeBoneChain() {
    GeometryCollectionAsset asset;
    GeometryCollectionBone a;
    a.halfExtentX = 0.4f;
    a.halfExtentY = 0.4f;
    a.halfExtentZ = 0.4f;
    a.localX      = -1.0f;
    a.localY      = 1.0f;
    a.clusterId   = 0;
    GeometryCollectionBone b = a;
    b.localX                 = 0.0f;
    b.clusterId              = 0;
    GeometryCollectionBone c = a;
    c.localX                 = 1.0f;
    c.clusterId              = 1;
    asset.bones              = {a, b, c};
    GeometryCollectionEdge e0;
    e0.boneA           = 0;
    e0.boneB           = 1;
    e0.strainThreshold = 1.f;
    GeometryCollectionEdge e1;
    e1.boneA           = 1;
    e1.boneB           = 2;
    e1.strainThreshold = 1.f;
    asset.edges        = {e0, e1};
    auto valid         = asset.validate();
    if (!valid) return eve::Result<GeometryCollectionAsset>::failure(valid.status());
    return eve::Result<GeometryCollectionAsset>::success(std::move(asset));
}

}  // namespace

TEST_CASE("physics_destruction_p3.assetV2RoundTripAndV1Migrate") {
    auto fixture = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(fixture.ok());
    fixture.value().bones[0].clusterId     = 3;
    fixture.value().bones[0].fractureLevel = 0;
    fixture.value().bones[1].clusterId     = 7;
    auto encoded                           = fixture.value().toValue();
    REQUIRE(encoded.ok());
    auto decoded = GeometryCollectionAsset::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().bones[0].clusterId, 3);
    REQUIRE_EQ(decoded.value().bones[1].clusterId, 7);

    // Version-1 documents migrate with clusterId/fractureLevel = 0.
    eve::Value::Object v1 = *encoded.value().getIf<eve::Value::Object>();
    v1["schemaVersion"]   = static_cast<std::int64_t>(1);
    eve::Value::Array bones;
    const auto* bonesValue = v1["bones"].getIf<eve::Value::Array>();
    REQUIRE(bonesValue != nullptr);
    for (const auto& boneValue : *bonesValue) {
        const auto* boneObject = boneValue.getIf<eve::Value::Object>();
        REQUIRE(boneObject != nullptr);
        eve::Value::Object bone = *boneObject;
        bone.erase("clusterId");
        bone.erase("fractureLevel");
        bones.push_back(eve::Value(std::move(bone)));
    }
    v1["bones"] = eve::Value(std::move(bones));
    auto migrated = GeometryCollectionAsset::fromValue(eve::Value(std::move(v1)));
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().bones[0].clusterId, 0);
    REQUIRE_EQ(migrated.value().bones[1].clusterId, 0);
}

TEST_CASE("physics_destruction_p3.edgeBreakBudgetDefersAndDrains") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto asset = makeThreeBoneChain();
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());

    DestructionStepBudget budget;
    budget.maxEdgeBreaksPerStep = 1;
    instance.value()->setStepBudget(budget);

    REQUIRE(instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 5.f, 2.f)).ok());
    auto step1 = instance.value()->step(simStep(1));
    REQUIRE(step1.ok());
    REQUIRE_EQ(step1.value().edgesBroken, 1);
    REQUIRE_EQ(step1.value().edgesDeferred, 1);
    REQUIRE_EQ(instance.value()->pendingEdgeBreakCount(), 1);
    REQUIRE(instance.value()->isEdgeBroken(0) != instance.value()->isEdgeBroken(1));

    auto step2 = instance.value()->step(simStep(2));
    REQUIRE(step2.ok());
    REQUIRE_EQ(step2.value().edgesBroken, 1);
    REQUIRE_EQ(step2.value().edgesDeferred, 0);
    REQUIRE_EQ(instance.value()->pendingEdgeBreakCount(), 0);
    REQUIRE(instance.value()->isEdgeBroken(0));
    REQUIRE(instance.value()->isEdgeBroken(1));
}

TEST_CASE("physics_destruction_p3.clusterBreakEventsAcrossMembership") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto asset = makeThreeBoneChain();
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());
    REQUIRE_EQ(instance.value()->boneClusterId(0), 0);
    REQUIRE_EQ(instance.value()->boneClusterId(2), 1);

    REQUIRE(instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 5.f, 2.f)).ok());
    auto stepped = instance.value()->step(simStep(1));
    REQUIRE(stepped.ok());
    REQUIRE(stepped.value().clusterBreaks >= 1);
    REQUIRE(instance.value()->clusterBreakEventCount() >= 1);
    auto event = instance.value()->clusterBreakEventAt(0);
    REQUIRE((event.clusterA == 0 && event.clusterB == 1) || (event.clusterA == 1 && event.clusterB == 0));
}

TEST_CASE("physics_destruction_p3.sleepBudgetDefersRemainder") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto asset = makeThreeBoneChain();
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());

    REQUIRE(instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 5.f, 2.f)).ok());
    REQUIRE(instance.value()->step(simStep(1)).ok());
    for (int i = 0; i < 3; ++i) {
        if (instance.value()->boneState(i) != BoneRuntimeState::Detached) continue;
        auto body = instance.value()->boneLink(i).value().resolve(*world);
        REQUIRE(body.ok());
        body.value()->setLinearVelocity(0.f, 0.f, 0.f);
    }

    DestructionStepBudget budget;
    budget.maxSleepsPerStep = 1;
    instance.value()->setStepBudget(budget);

    DestructionField sleep;
    sleep.kind    = DestructionFieldKind::Sleep;
    sleep.falloff = DestructionFieldFalloff::None;
    sleep.centerX = 0.f;
    sleep.centerY = 1.f;
    sleep.centerZ = 0.f;
    sleep.radius  = 5.f;
    auto slept    = instance.value()->applyField(sleep);
    REQUIRE(slept.ok());
    REQUIRE_EQ(slept.value().bonesAffected, 1);
    REQUIRE(slept.value().sleepsDeferred >= 1);
}

TEST_CASE("physics_destruction_p3.instanceSnapshotRoundTripRestore") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    asset.value().bones[0].clusterId = 2;
    asset.value().bones[1].clusterId = 4;
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 1.f, 2.f, 3.f);
    REQUIRE(instance.ok());

    REQUIRE(instance.value()->applyField(strainAt(1.f, 3.f, 3.f, 5.f, 2.f)).ok());
    REQUIRE(instance.value()->step(simStep(5)).ok());
    REQUIRE(instance.value()->isEdgeBroken(0));

    auto capture = instance.value()->captureSnapshot();
    REQUIRE(capture.ok());
    auto encoded = capture.value().toValue();
    REQUIRE(encoded.ok());
    auto decoded = GeometryCollectionInstanceSnapshot::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().bones[0].clusterId, 2);
    REQUIRE_EQ(decoded.value().bones[1].clusterId, 4);
    REQUIRE(decoded.value().edges[0].broken);

    // Mutate live state then restore.
    DestructionField sleep;
    sleep.kind    = DestructionFieldKind::Sleep;
    sleep.falloff = DestructionFieldFalloff::None;
    sleep.centerX = 1.f;
    sleep.centerY = 3.f;
    sleep.centerZ = 3.f;
    sleep.radius  = 5.f;
    for (int i = 0; i < 2; ++i) {
        auto body = instance.value()->boneLink(i).value().resolve(*world);
        REQUIRE(body.ok());
        body.value()->setLinearVelocity(0.f, 0.f, 0.f);
    }
    REQUIRE(instance.value()->applyField(sleep).ok());

    auto restored = instance.value()->restoreSnapshot(decoded.value());
    REQUIRE(restored.ok());
    REQUIRE(instance.value()->isEdgeBroken(0));
    REQUIRE(instance.value()->boneState(0) == BoneRuntimeState::Detached ||
            instance.value()->boneState(0) == BoneRuntimeState::Attached);
    REQUIRE_EQ(instance.value()->boneClusterId(0), 2);
    REQUIRE_EQ(instance.value()->boneClusterId(1), 4);
    REQUIRE_EQ(instance.value()->pendingEdgeBreakCount(), 0);

    auto registered = GeometryCollectionInstanceSnapshot::ensureSchemaRegistered();
    REQUIRE(registered.ok());
    REQUIRE(eve::schema::SchemaRegistry::resolve(
                std::string(GeometryCollectionInstanceSnapshot::SchemaId),
                static_cast<int>(GeometryCollectionInstanceSnapshot::SchemaVersion)) != nullptr);
    auto schemas = Destruction::create()->registerGeometryCollectionSchema();
    REQUIRE(schemas.ok());
}

TEST_CASE("physics_destruction_p3.snapshotRejectsUnknownFields") {
    eve::Value::Object object;
    object["schema"]             = std::string(GeometryCollectionInstanceSnapshot::SchemaId);
    object["schemaVersion"]      = static_cast<std::int64_t>(1);
    object["originX"]            = 0.0;
    object["originY"]            = 0.0;
    object["originZ"]            = 0.0;
    object["lastTick"]           = static_cast<std::int64_t>(0);
    object["sleepBatchRevision"] = static_cast<std::int64_t>(0);
    object["bones"]              = eve::Value(eve::Value::Array{});
    object["edges"]              = eve::Value(eve::Value::Array{});
    object["extra"]              = true;
    auto decoded = GeometryCollectionInstanceSnapshot::fromValue(eve::Value(std::move(object)));
    REQUIRE(!decoded.ok());
}
