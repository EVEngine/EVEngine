#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"
#include "physics/destruction/Destruction.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "schema/SchemaRegistry.h"

#include <memory>

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

DestructionField strainAt(float x, float y, float z, float radius, float magnitude) {
    DestructionField field;
    field.kind = DestructionFieldKind::Strain;
    field.falloff = DestructionFieldFalloff::Linear;
    field.centerX = x;
    field.centerY = y;
    field.centerZ = z;
    field.radius = radius;
    field.magnitude = magnitude;
    return field;
}

eve::SimulationStep simStep(std::uint64_t tick, double seconds = 1.0 / 60.0) {
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
    const auto* schema =
        eve::schema::SchemaRegistry::resolve(std::string(GeometryCollectionAsset::SchemaId),
                                             static_cast<int>(GeometryCollectionAsset::SchemaVersion));
    REQUIRE(schema != nullptr);
}

TEST_CASE("physics_destruction.assetRejectsUnknownFields") {
    eve::Value::Object object;
    object["schema"] = std::string(GeometryCollectionAsset::SchemaId);
    object["schemaVersion"] = static_cast<std::int64_t>(1);
    object["bones"] = eve::Value(eve::Value::Array{});
    object["edges"] = eve::Value(eve::Value::Array{});
    object["extra"] = true;
    auto decoded = GeometryCollectionAsset::fromValue(eve::Value(std::move(object)));
    REQUIRE(!decoded.ok());
}

TEST_CASE("physics_destruction.strainFieldBreaksEdgeAndDetachesBones") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());
    REQUIRE_EQ(instance.value()->boneCount(), 2);
    REQUIRE_EQ(instance.value()->edgeCount(), 1);
    REQUIRE(!instance.value()->isEdgeBroken(0));

    auto applied = instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 3.f, 1.5f));
    REQUIRE(applied.ok());
    REQUIRE(applied.value().edgesAffected >= 1);
    REQUIRE(instance.value()->edgeStrain(0) >= 1.f);

    auto stepped = instance.value()->step(simStep(1));
    REQUIRE(stepped.ok());
    REQUIRE(instance.value()->isEdgeBroken(0));
    REQUIRE_EQ(instance.value()->detachEventCount(), 1);
    REQUIRE(instance.value()->boneState(0) == BoneRuntimeState::Detached);
    REQUIRE(instance.value()->boneState(1) == BoneRuntimeState::Detached);

    auto link0 = instance.value()->boneLink(0);
    REQUIRE(link0.ok());
    auto body0 = link0.value().resolve(*world);
    REQUIRE(body0.ok());
    REQUIRE_EQ(body0.value()->getType(), std::string("dynamic"));
}

TEST_CASE("physics_destruction.instanceDestroyReleasesBodiesBeforeWorld") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    auto created = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 2.f, 0.f);
    REQUIRE(created.ok());
    auto instance = std::move(created.value());
    auto link = instance->boneLink(0);
    REQUIRE(link.ok());
    instance.reset();
    auto resolved = link.value().resolve(*world);
    REQUIRE(!resolved.ok());
    REQUIRE_EQ(resolved.status().code(), eve::StatusCode::Rejected);
}

TEST_CASE("physics_destruction.worldDestroyFirstLeavesStaleLinks") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    auto created = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 2.f, 0.f);
    REQUIRE(created.ok());
    auto instance = std::move(created.value());
    auto link = instance->boneLink(0);
    REQUIRE(link.ok());

    world->destroy();
    world.reset();

    REQUIRE(!instance->hasLiveWorld());
    auto stepped = instance->step(simStep(1));
    REQUIRE(!stepped.ok());
    REQUIRE_EQ(stepped.status().code(), eve::StatusCode::Rejected);

    // Resolving against a fresh world must still report stale owner identity.
    std::unique_ptr<World3D> other(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto resolved = link.value().resolve(*other);
    REQUIRE(!resolved.ok());
    instance.reset();
}

TEST_CASE("physics_destruction.anchorAndSleepFields") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto asset = GeometryCollectionAsset::makeWeldedBoxesFixture(0.5f);
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());

    DestructionField anchor;
    anchor.kind = DestructionFieldKind::Anchor;
    anchor.falloff = DestructionFieldFalloff::None;
    anchor.centerX = -0.55f;
    anchor.centerY = 1.f;
    anchor.centerZ = 0.f;
    anchor.radius = 0.25f;
    auto anchored = instance.value()->applyField(anchor);
    REQUIRE(anchored.ok());
    REQUIRE(anchored.value().bonesAffected >= 1);

    REQUIRE(instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 3.f, 2.f)).ok());
    REQUIRE(instance.value()->step(simStep(1)).ok());
    REQUIRE(instance.value()->isEdgeBroken(0));
    // Bone 0 was anchored so it stays Attached; bone 1 detaches.
    REQUIRE(instance.value()->boneState(0) == BoneRuntimeState::Attached);
    REQUIRE(instance.value()->boneState(1) == BoneRuntimeState::Detached);

    auto body1 = instance.value()->boneLink(1).value().resolve(*world);
    REQUIRE(body1.ok());
    body1.value()->setLinearVelocity(0.f, 0.f, 0.f);

    DestructionField sleep;
    sleep.kind = DestructionFieldKind::Sleep;
    sleep.falloff = DestructionFieldFalloff::None;
    sleep.centerX = 0.55f;
    sleep.centerY = 1.f;
    sleep.centerZ = 0.f;
    sleep.radius = 0.5f;
    auto slept = instance.value()->applyField(sleep);
    REQUIRE(slept.ok());
    REQUIRE(slept.value().bonesAffected >= 1);
    REQUIRE(instance.value()->boneState(1) == BoneRuntimeState::Sleeping);
    REQUIRE_EQ(body1.value()->getType(), std::string("static"));
}
