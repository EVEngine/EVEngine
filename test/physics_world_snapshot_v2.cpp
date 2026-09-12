#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Snapshot.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"

#include <cstdint>
#include <string_view>

namespace {

eve::PersistentId worldId(std::string_view text) {
    const auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SnapshotHashProvider testHashProvider() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left = 14695981039346656037ull, right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("physics.snapshot_v2_has_stable_identity_and_rejects_foreign_world_atomically") {
    const auto idA = worldId("01234567-89ab-4cde-8fab-0123456789ab");
    const auto idB = worldId("11234567-89ab-4cde-8fab-0123456789ab");
    const auto hash = testHashProvider();
    eve::physics::World source(0.f, 10.f, false, 30.f, idA);
    eve::physics::World foreign(0.f, 20.f, false, 30.f, idB);
    source.newBody("dynamic", 3.f, 4.f);
    foreign.newBody("dynamic", 8.f, 9.f);

    auto snapshot = source.snapshot(hash);
    REQUIRE(snapshot.ok());
    CHECK(!snapshot.value().instanceId.isNil());
    CHECK(snapshot.value().instanceId == idA);
    CHECK_EQ(snapshot.value().schemaVersion.value(), std::uint64_t{2});
    const float beforeGravity = foreign.getGravityY();
    auto rejected = foreign.restore(snapshot.value(), hash);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK_EQ(foreign.getGravityY(), beforeGravity);
}

TEST_CASE("physics.snapshot_v2_explicit_identity_survives_world_recreation") {
    const auto id = worldId("51234567-89ab-4cde-8fab-0123456789ab");
    const auto hash = testHashProvider();
    eve::physics::World source(0.f, 10.f, false, 30.f, id);
    eve::physics::World replacement(0.f, 20.f, false, 30.f, id);
    source.newBody("dynamic", 3.f, 4.f);
    auto* replacementBody = replacement.newBody("dynamic", 8.f, 9.f);

    auto snapshot = source.snapshot(hash);
    REQUIRE(snapshot.ok());
    REQUIRE(replacement.restore(snapshot.value(), hash).ok());
    CHECK_EQ(replacement.persistentId(), id);
    CHECK_EQ(replacementBody->getX(), 3.f);
    CHECK_EQ(replacementBody->getY(), 4.f);
}

TEST_CASE("physics.snapshot_v2_auto_identity_is_non_nil_and_world_local") {
    const auto hash = testHashProvider();
    eve::physics::World first(0.f, 10.f, false, 30.f);
    eve::physics::World second(0.f, 10.f, false, 30.f);
    first.newBody("dynamic", 0.f, 0.f);
    second.newBody("dynamic", 0.f, 0.f);

    auto firstSnapshot = first.snapshot(hash);
    auto secondSnapshot = second.snapshot(hash);
    REQUIRE(firstSnapshot.ok());
    REQUIRE(secondSnapshot.ok());
    CHECK(!firstSnapshot.value().instanceId.isNil());
    CHECK(!secondSnapshot.value().instanceId.isNil());
    CHECK(firstSnapshot.value().instanceId != secondSnapshot.value().instanceId);
}

TEST_CASE("physics.snapshot_v2_success_invalidates_2d_body_handles") {
    const auto hash = testHashProvider();
    eve::physics::World world(0.f, 10.f, false, 30.f,
                              worldId("21234567-89ab-4cde-8fab-0123456789ab"));
    auto* body = world.newBody("dynamic", 3.f, 4.f);
    const auto oldHandle = body->runtimeHandle();
    auto snapshot = world.snapshot(hash);
    REQUIRE(snapshot.ok());
    body->setPosition(30.f, 40.f);
    auto restored = world.restore(snapshot.value(), hash);
    REQUIRE(restored.ok(), "{}", restored.status().describe());
    CHECK(world.findBody(oldHandle) == nullptr);
    CHECK(world.findBody(body->runtimeHandle()) == body);
    CHECK(body->runtimeHandle() != oldHandle);
    CHECK_EQ(body->getX(), 3.f);
}

TEST_CASE("physics.snapshot_v2_success_invalidates_3d_body_shape_and_joint_handles") {
    const auto hash = testHashProvider();
    eve::physics::World3D world(0.f, -10.f, 0.f, false,
                                worldId("31234567-89ab-4cde-8fab-0123456789ab"));
    auto* bodyA = world.newBody("dynamic", 0.f, 0.f, 0.f);
    auto* bodyB = world.newBody("dynamic", 2.f, 0.f, 0.f);
    auto* shape = bodyA->newBoxShape(1.f, 1.f, 1.f);
    auto* joint = world.newDistanceJoint(bodyA, bodyB, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 2.f);
    REQUIRE(shape != nullptr);
    REQUIRE(joint != nullptr);
    const auto oldBody = bodyA->runtimeHandle();
    const auto oldShape = shape->runtimeHandle();
    const auto oldJoint = joint->runtimeHandle();
    auto snapshot = world.snapshot(hash);
    REQUIRE(snapshot.ok());
    REQUIRE(world.restore(snapshot.value(), hash).ok());
    CHECK(world.findBody(oldBody) == nullptr);
    CHECK(world.findShape(oldShape) == nullptr);
    CHECK(world.findJoint(oldJoint) == nullptr);
    CHECK(world.findBody(bodyA->runtimeHandle()) == bodyA);
    CHECK(world.findShape(shape->runtimeHandle()) == shape);
    CHECK(world.findJoint(joint->runtimeHandle()) == joint);
}

TEST_CASE("physics.snapshot_v2_rejects_foreign_3d_world_without_invalidating_handles") {
    const auto hash = testHashProvider();
    eve::physics::World3D source(0.f, -10.f, 0.f, false,
                                 worldId("61234567-89ab-4cde-8fab-0123456789ab"));
    eve::physics::World3D foreign(0.f, -20.f, 0.f, false,
                                  worldId("71234567-89ab-4cde-8fab-0123456789ab"));
    source.newBody("dynamic", 0.f, 1.f, 0.f);
    auto* body = foreign.newBody("dynamic", 0.f, 9.f, 0.f);
    const auto handle = body->runtimeHandle();

    auto snapshot = source.snapshot(hash);
    REQUIRE(snapshot.ok());
    auto rejected = foreign.restore(snapshot.value(), hash);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK(foreign.findBody(handle) == body);
    CHECK_EQ(body->getY(), 9.f);
    CHECK_EQ(foreign.getGravityY(), -20.f);
}

TEST_CASE("physics.snapshot_v1_migrates_only_against_topology_compatible_live_world") {
    const auto hash = testHashProvider();
    eve::physics::World world(0.f, 10.f, false, 30.f,
                              worldId("41234567-89ab-4cde-8fab-0123456789ab"));
    auto* body = world.newBody("dynamic", 1.f, 2.f);
    auto current = world.snapshot(hash);
    REQUIRE(current.ok());
    auto legacy = eve::makeSnapshotEnvelope(current.value().type, current.value().schema, eve::SchemaVersion(1),
                                             eve::PersistentId::nil(), current.value().revision,
                                             current.value().tick, current.value().payload, hash);
    REQUIRE(legacy.ok());
    const auto handle = body->runtimeHandle();
    auto restored = world.restore(legacy.value(), hash);
    REQUIRE(restored.ok());
    CHECK(world.findBody(handle) == nullptr);
    CHECK(!body->isValid());
}
