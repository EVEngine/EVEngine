#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Snapshot.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

eve::SnapshotHashProvider topologyHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char byte : input) hash = (hash ^ byte) * 1099511628211ull;
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("physics.snapshot_v2_persists_2d_fixture_geometry_and_rejects_unknown_fields_atomically") {
    const auto hash = topologyHash();
    eve::physics::World world(0.f, 9.f, false, 32.f);
    auto* body = world.newBody("dynamic", 2.f, 3.f);
    REQUIRE(body->newRectangleFixtureAt(4.f, 6.f, 1.f, -2.f) != nullptr);
    const auto handle = body->runtimeHandle();
    auto captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    const eve::Value* bodies = captured.value().payload.find("bodies");
    REQUIRE(bodies != nullptr);
    REQUIRE_EQ(bodies->arraySize(), std::size_t{1});
    const eve::Value* fixtures = bodies->at(0).find("fixtures");
    REQUIRE(fixtures != nullptr);
    CHECK_EQ(fixtures->arraySize(), std::size_t{1});

    eve::Value invalidPayload = captured.value().payload;
    invalidPayload.set("unknownFutureField", eve::Value(true));
    auto invalid = eve::makeSnapshotEnvelope(captured.value().type, captured.value().schema,
                                              captured.value().schemaVersion, captured.value().instanceId,
                                              captured.value().revision, captured.value().tick,
                                              std::move(invalidPayload), hash);
    REQUIRE(invalid.ok());
    body->setPosition(11.f, 12.f);
    auto rejected = world.restore(invalid.value(), hash);
    CHECK(!rejected.ok());
    CHECK(world.findBody(handle) == body);
    CHECK_EQ(body->getX(), 11.f);
    CHECK_EQ(world.getGravityY(), 9.f);
}

TEST_CASE("physics.snapshot_v2_rebuilds_mesh_heightfield_and_joint_sources_from_detached_state") {
    const auto hash = topologyHash();
    eve::physics::World3D world(0.f, -9.f, 0.f, false);
    auto* meshBody = world.newBody("static", 0.f, 0.f, 0.f);
    auto* dynamicBody = world.newBody("dynamic", 0.f, 2.f, 0.f);
    const std::vector<float> vertices{-2.f, 0.f, -2.f, 2.f, 0.f, -2.f, 0.f, 0.f, 2.f};
    const std::vector<std::int32_t> indices{0, 1, 2};
    auto* mesh = meshBody->newTriangleMeshShape(vertices, indices);
    REQUIRE(mesh != nullptr);
    const std::vector<float> heights(9, 0.f);
    REQUIRE(meshBody->newHeightFieldShape(3, 3, 1.f, 1.f, heights, -1.f, 1.f) != nullptr);
    REQUIRE(world.newDistanceJoint(meshBody, dynamicBody, 0.f, 0.f, 0.f, 0.f, 2.f, 0.f, 2.f) != nullptr);

    auto captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    const eve::Value* shapes = captured.value().payload.find("shapes");
    const eve::Value* joints = captured.value().payload.find("joints");
    REQUIRE(shapes != nullptr);
    REQUIRE(joints != nullptr);
    CHECK_EQ(shapes->arraySize(), std::size_t{2});
    CHECK_EQ(joints->arraySize(), std::size_t{1});

    const std::vector<float> changed{-3.f, 0.f, -2.f, 3.f, 0.f, -2.f, 0.f, 0.f, 3.f};
    mesh->setTriangleMeshData(changed, indices);
    dynamicBody->setPosition(0.f, 8.f, 0.f);
    const auto handle = dynamicBody->runtimeHandle();
    auto restored = world.restore(captured.value(), hash);
    REQUIRE(restored.ok(), "{}", restored.status().describe());
    CHECK(world.findBody(handle) == nullptr);
    CHECK(!dynamicBody->isValid());
    CHECK_EQ(world.getGravityY(), -9.f);
    auto roundTrip = world.snapshot(hash);
    REQUIRE(roundTrip.ok());
    CHECK_EQ(roundTrip.value().payload.find("shapes")->arraySize(), std::size_t{2});
    CHECK_EQ(roundTrip.value().payload.find("joints")->arraySize(), std::size_t{1});
}
