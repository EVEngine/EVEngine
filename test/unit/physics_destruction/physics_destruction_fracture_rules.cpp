// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "physics_destruction_p3.cpp",
    "physics_destruction.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#include <memory>
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"
#include "physics/destruction/Destruction.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/GeometryCollectionSnapshot.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// physics_destruction_p3.cpp
namespace {
namespace physics_destruction_p3 {
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

[[maybe_unused]] DestructionField strainAt(float x, float y, float z, float radius, float magnitude) {
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

[[maybe_unused]] eve::SimulationStep simStep(std::uint64_t tick, double seconds = 1.0 / 60.0) {
    return {eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("destruction dt")};
}

[[maybe_unused]] eve::Result<GeometryCollectionAsset> makeThreeBoneChain() {
    GeometryCollectionAsset asset;
    GeometryCollectionBone  a;
    a.halfExtentX            = 0.4f;
    a.halfExtentY            = 0.4f;
    a.halfExtentZ            = 0.4f;
    a.localX                 = -1.0f;
    a.localY                 = 1.0f;
    a.clusterId              = 0;
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
    const auto*       bonesValue = v1["bones"].getIf<eve::Value::Array>();
    REQUIRE(bonesValue != nullptr);
    for (const auto& boneValue : *bonesValue) {
        const auto* boneObject = boneValue.getIf<eve::Value::Object>();
        REQUIRE(boneObject != nullptr);
        eve::Value::Object bone = *boneObject;
        bone.erase("clusterId");
        bone.erase("fractureLevel");
        bones.push_back(eve::Value(std::move(bone)));
    }
    v1["bones"]   = eve::Value(std::move(bones));
    auto migrated = GeometryCollectionAsset::fromValue(eve::Value(std::move(v1)));
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().bones[0].clusterId, 0);
    REQUIRE_EQ(migrated.value().bones[1].clusterId, 0);
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
    auto decoded                 = GeometryCollectionInstanceSnapshot::fromValue(eve::Value(std::move(object)));
    REQUIRE(!decoded.ok());
}
}  // namespace physics_destruction_p3
}  // namespace

// physics_destruction.cpp
namespace {
namespace physics_destruction {
using eve::physics::BoneRuntimeState;
using eve::physics::DestructionField;
using eve::physics::DestructionFieldFalloff;
using eve::physics::DestructionFieldKind;
using eve::physics::GeometryCollectionAsset;
using eve::physics::GeometryCollectionInstance;
using eve::physics::Physics;
using eve::physics::PhysicsLink;
using eve::physics::World3D;

namespace {

[[maybe_unused]] DestructionField strainAt(float x, float y, float z, float radius, float magnitude) {
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

[[maybe_unused]] eve::SimulationStep simStep(std::uint64_t tick, double seconds = 1.0 / 60.0) {
    return {eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("destruction dt")};
}

}  // namespace

TEST_CASE("physics_destruction.assetRoundTripAndSchema") {
    auto fixture = GeometryCollectionAsset::makeWeldedBoxesFixture(2.5f);
    REQUIRE(fixture.ok());
    auto encoded = fixture.value().toValue();
    REQUIRE(encoded.ok());
    auto decoded = GeometryCollectionAsset::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().bones.size(), 2u);
    REQUIRE_EQ(decoded.value().edges.size(), 1u);
    REQUIRE_EQ(decoded.value().edges[0].strainThreshold, 2.5f);
    auto registered = GeometryCollectionAsset::ensureSchemaRegistered();
    REQUIRE(registered.ok());
    const auto* schema = eve::schema::SchemaRegistry::resolve(std::string(GeometryCollectionAsset::SchemaId),
                                                              static_cast<int>(GeometryCollectionAsset::SchemaVersion));
    REQUIRE(schema != nullptr);
}

TEST_CASE("physics_destruction.assetRejectsUnknownFields") {
    eve::Value::Object object;
    object["schema"]        = std::string(GeometryCollectionAsset::SchemaId);
    object["schemaVersion"] = static_cast<std::int64_t>(1);
    object["bones"]         = eve::Value(eve::Value::Array{});
    object["edges"]         = eve::Value(eve::Value::Array{});
    object["extra"]         = true;
    auto decoded            = GeometryCollectionAsset::fromValue(eve::Value(std::move(object)));
    REQUIRE(!decoded.ok());
}
}  // namespace physics_destruction
}  // namespace
