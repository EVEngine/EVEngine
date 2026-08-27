#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "physics/ArtifactProvider.h"
#include "procgen/ArtifactPublish.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/Params.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>

namespace {

eve::PersistentId id(const char* text) {
    const auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

struct ColliderPublication {
    std::array<float, 12> vertices{-1.f, 0.f, -1.f, 1.f, 0.f, -1.f,
                                    1.f, 0.f, 1.f,  -1.f, 0.f, 1.f};
    std::array<std::uint32_t, 6> indices{0, 2, 1, 0, 3, 2};
    eve::artifact::PartView part;
    eve::artifact::PublicationView publication;

    explicit ColliderPublication(eve::PersistentId artifactId) {
        part.id = artifactId;
        part.role = "collider";
        part.kind = eve::artifact::PartKind::Collider;
        part.schemaVersion = 1;
        part.buildKey = "test.box3d.collider.v1";
        part.bounds = {-1.f, 0.f, -1.f, 1.f, 0.f, 1.f, true};
        part.positions = std::span<const float>(vertices);
        part.indices = std::span<const std::uint32_t>(indices);

        publication.id = artifactId;
        publication.schemaVersion = 1;
        publication.buildKey = part.buildKey;
        publication.bounds = part.bounds;
        publication.parts = std::span<const eve::artifact::PartView>(&part, 1);
    }
};

}  // namespace

TEST_CASE("physics.artifact.stageUsesRealBox3DAndRayQuery") {
    eve::physics::PhysicsArtifactProvider provider;
    const auto artifactId = id("018f0b7e-6e50-7a10-8c22-2c8f8e3e0201");
    ColliderPublication input(artifactId);

    auto prepared = provider.prepare(input.publication);
    REQUIRE(prepared.ok());
    auto stage = std::move(prepared).takeValue();
    REQUIRE(static_cast<bool>(stage));
    CHECK_EQ(provider.size(), std::size_t(0));
    stage->commit();
    stage.reset();

    const auto* collider = provider.find(artifactId);
    REQUIRE(collider != nullptr);
    CHECK_EQ(provider.size(), std::size_t(1));
    CHECK_EQ(provider.backendName(artifactId), std::string("box3d"));
    CHECK(provider.isBox3DBacked(artifactId));
    CHECK(provider.isHandleLive(collider->handle));

    const auto hit = provider.rayCast(artifactId, 0.f, 2.f, 0.f, 0.f, -4.f, 0.f);
    CHECK(hit.hit);
    CHECK(hit.fraction >= 0.f);
    CHECK(hit.fraction <= 1.f);
    CHECK(std::fabs(hit.y) < 0.001f);
}

TEST_CASE("physics.artifact.rollbackAndValidationLeaveNoCollider") {
    eve::physics::PhysicsArtifactProvider provider;
    const auto artifactId = id("018f0b7e-6e50-7a10-8c22-2c8f8e3e0202");
    ColliderPublication input(artifactId);

    auto prepared = provider.prepare(input.publication);
    REQUIRE(prepared.ok());
    auto stage = std::move(prepared).takeValue();
    stage->rollback();
    stage.reset();
    CHECK(provider.emptyState());

    provider.setPrepareFailure(true);
    auto injected = provider.prepare(input.publication);
    CHECK(!injected.ok());
    CHECK(provider.emptyState());

    input.indices[0] = 99;
    provider.setPrepareFailure(false);
    auto invalid = provider.prepare(input.publication);
    CHECK(!invalid.ok());
    CHECK(provider.emptyState());
}

TEST_CASE("physics.artifact.restoreRebuildsBox3DAndInvalidatesOldHandle") {
    eve::physics::PhysicsArtifactProvider provider;
    const auto artifactId = id("018f0b7e-6e50-7a10-8c22-2c8f8e3e0203");
    ColliderPublication input(artifactId);

    auto prepared = provider.prepare(input.publication);
    REQUIRE(prepared.ok());
    auto stage = std::move(prepared).takeValue();
    stage->commit();
    stage.reset();
    const auto oldHandle = provider.find(artifactId)->handle;
    auto snapshot = provider.snapshotState();
    REQUIRE(snapshot.ok());
    auto encoded = std::move(snapshot).takeValue();

    // A failed candidate build must leave the live Box3D world untouched.
    auto malformed = encoded;
    auto* malformedObject = malformed.getIf<eve::Value::Object>();
    REQUIRE(malformedObject != nullptr);
    auto* encodedColliders = malformedObject->at("colliders").getIf<eve::Value::Array>();
    REQUIRE(encodedColliders != nullptr);
    auto* malformedCollider = encodedColliders->front().getIf<eve::Value::Object>();
    REQUIRE(malformedCollider != nullptr);
    auto* malformedIndices = malformedCollider->at("indices").getIf<eve::Value::Array>();
    REQUIRE(malformedIndices != nullptr);
    malformedIndices->front() = eve::Value(std::int64_t(99));
    auto rejected = provider.restoreState(malformed);
    CHECK(!rejected.ok());
    CHECK_EQ(provider.size(), std::size_t(1));
    CHECK(provider.isHandleLive(oldHandle));
    CHECK(provider.rayCast(artifactId, 0.f, 2.f, 0.f, 0.f, -4.f, 0.f).hit);

    provider.clear();
    CHECK(!provider.isHandleLive(oldHandle));
    auto restored = provider.restoreState(encoded);
    REQUIRE(restored.ok());
    CHECK_EQ(provider.size(), std::size_t(1));
    const auto* rebuilt = provider.find(artifactId);
    REQUIRE(rebuilt != nullptr);
    CHECK(rebuilt->handle != oldHandle);
    CHECK(!provider.isHandleLive(oldHandle));
    CHECK(provider.isHandleLive(rebuilt->handle));
    CHECK(provider.isBox3DBacked(artifactId));
    CHECK(provider.rayCast(artifactId, 0.f, 2.f, 0.f, 0.f, -4.f, 0.f).hit);
}

TEST_CASE("physics.artifact.compositeColliderUsesPublicProcgenPublication") {
    eve::cap::detail::clearAllRaw();
    auto& provider = eve::physics::physicsArtifactProvider();
    provider.clear();
    eve::physics::registerPhysicsArtifactProvider();

    eve::procgen::Params params;
    params.setSeed(424242u);
    params.setInt("width", 8);
    params.setInt("height", 7);
    params.setFloat("radius", 1.f);
    const auto artifactId = eve::procgen::ArtifactId::parse(
        "018f0b7e-6e50-7a10-8c22-2c8f8e3e0204");
    REQUIRE(artifactId.has_value());

    auto generated = eve::procgen::generateHexTerrainArtifact(params, *artifactId);
    REQUIRE(generated.ok());
    eve::procgen::ArtifactStore store;
    eve::procgen::ArtifactPublisher publisher(store);
    eve::procgen::ArtifactPublishOptions options;
    options.physics = true;
    auto published = publisher.publish(std::move(generated).takeValue(), options);
    REQUIRE(published.ok());

    const auto persistentId = eve::PersistentId::fromUuid(*artifactId);
    CHECK_EQ(provider.size(), std::size_t(1));
    CHECK(provider.find(persistentId) != nullptr);
    CHECK(provider.isBox3DBacked(persistentId));
    const auto* collider = provider.find(persistentId);
    REQUIRE(collider != nullptr);
    const auto hit = provider.rayCast(
        persistentId, (collider->bounds.minX + collider->bounds.maxX) * .5f,
        collider->bounds.maxY + 10.f, (collider->bounds.minZ + collider->bounds.maxZ) * .5f,
        0.f, -((collider->bounds.maxY - collider->bounds.minY) + 20.f), 0.f);
    CHECK(hit.hit);
}
