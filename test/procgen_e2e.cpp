#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/Snapshot.h"
#include "graphics/ArtifactProvider.h"
#include "map/ArtifactProvider.h"
#include "physics/ArtifactProvider.h"
#include "procgen/ArtifactPublish.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/Params.h"
#include "scene/ArtifactProvider.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

eve::PersistentId persistentId(const char* text) {
    const auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::procgen::ArtifactId artifactId(const char* text) {
    const auto parsed = eve::procgen::ArtifactId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::PersistentId persistentId(eve::procgen::ArtifactId id) {
    return eve::PersistentId::fromUuid(id);
}

eve::procgen::Params hexParams() {
    eve::procgen::Params params;
    params.setSeed(424242u);
    params.setInt("width", 8);
    params.setInt("height", 7);
    params.setFloat("radius", 1.f);
    return params;
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        std::array<std::uint8_t, 16> bytes{};
        for (std::size_t i = 0; i < 8; ++i) {
            bytes[i] = static_cast<std::uint8_t>((hash >> (i * 8u)) & 0xffu);
            bytes[8 + i] = static_cast<std::uint8_t>(((hash ^ 0x9e3779b97f4a7c15ull) >> (i * 8u)) &
                                                      0xffu);
        }
        const auto id = eve::ContentId::fromBytes(std::span<const std::uint8_t>(bytes));
        if (!id)
            return eve::Result<eve::ContentId>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "test digest construction failed"));
        return eve::Result<eve::ContentId>::success(*id);
    };
}

void resetProviders() {
    eve::cap::detail::clearAllRaw();
    eve::scene::sceneArtifactProvider().clear();
    eve::map::mapArtifactProvider().clear();
    eve::graphics::graphicsArtifactProvider().clear();
    eve::physics::physicsArtifactProvider().clear();
    eve::scene::registerSceneArtifactProvider();
    eve::map::registerMapArtifactProvider();
    eve::graphics::registerGraphicsArtifactProvider();
    eve::physics::registerPhysicsArtifactProvider();
}

eve::procgen::ArtifactPublishOptions allProviders() {
    eve::procgen::ArtifactPublishOptions options;
    options.scene = true;
    options.graphics = true;
    options.physics = true;
    options.map = true;
    return options;
}

}  // namespace

TEST_CASE("procgen.e2e.hexProvidersCommitQueryableObjects") {
    resetProviders();
    const auto id = artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0101");
    eve::procgen::ArtifactStore store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto generated = eve::procgen::generateHexTerrainArtifact(hexParams(), id);
    REQUIRE(generated.ok());
    REQUIRE(generated.value().metadata.contains("algorithm"));
    REQUIRE(generated.value().metadata.contains("determinism"));
    auto published = publisher.publish(std::move(generated).takeValue(), allProviders());
    REQUIRE(published.ok());
    std::move(published).takeValue();

    auto& scene = eve::scene::sceneArtifactProvider();
    auto& map = eve::map::mapArtifactProvider();
    auto& graphics = eve::graphics::graphicsArtifactProvider();
    auto& physics = eve::physics::physicsArtifactProvider();
    CHECK_EQ(store.size(), std::size_t(1));
    CHECK_EQ(store.partCount(), std::size_t(4));
    CHECK_EQ(scene.size(), std::size_t(1));
    CHECK_EQ(map.size(), std::size_t(1));
    CHECK_EQ(graphics.size(), std::size_t(1));
    CHECK_EQ(physics.size(), std::size_t(1));
    REQUIRE(map.find(persistentId(id)) != nullptr);
    CHECK_EQ(map.find(persistentId(id))->width, 8);
    CHECK_EQ(map.find(persistentId(id))->height, 7);
    REQUIRE(graphics.find(persistentId(id)) != nullptr);
    CHECK_GT(graphics.find(persistentId(id))->positions.size(), std::size_t(0));
    REQUIRE(physics.find(persistentId(id)) != nullptr);
    const auto bounds = physics.find(persistentId(id))->bounds;
    const auto hit = physics.rayCast(persistentId(id), (bounds.minX + bounds.maxX) * .5f,
                                     bounds.maxY + 10.f, (bounds.minZ + bounds.maxZ) * .5f,
                                     0.f, -((bounds.maxY - bounds.minY) + 20.f), 0.f);
    CHECK(hit.hit);
    CHECK(hit.fraction >= 0.f);
    CHECK(hit.fraction <= 1.f);
}

TEST_CASE("procgen.e2e.providerFailureLeavesNoHalfState") {
    for (int failure = 0; failure < 4; ++failure) {
        resetProviders();
        auto& scene = eve::scene::sceneArtifactProvider();
        auto& map = eve::map::mapArtifactProvider();
        auto& graphics = eve::graphics::graphicsArtifactProvider();
        auto& physics = eve::physics::physicsArtifactProvider();
        if (failure == 0) scene.setPrepareFailure(true);
        if (failure == 1) map.setPrepareFailure(true);
        if (failure == 2) graphics.setPrepareFailure(true);
        if (failure == 3) physics.setPrepareFailure(true);

        eve::procgen::ArtifactStore store;
        eve::procgen::ArtifactPublisher publisher(store);
        auto generated = eve::procgen::generateHexTerrainArtifact(
            hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0102"));
        REQUIRE(generated.ok());
        auto result = publisher.publish(std::move(generated).takeValue(), allProviders());
        CHECK(!result.ok());
        CHECK_EQ(store.size(), std::size_t(0));
        CHECK_EQ(store.partCount(), std::size_t(0));
        CHECK_EQ(scene.size(), std::size_t(0));
        CHECK_EQ(map.size(), std::size_t(0));
        CHECK_EQ(graphics.size(), std::size_t(0));
        CHECK_EQ(physics.size(), std::size_t(0));
    }
}

TEST_CASE("procgen.e2e.absentCapabilityIsExplicit") {
    resetProviders();
    eve::cap::revoke<eve::artifact::IPhysicsArtifactAdapter>(
        &eve::physics::physicsArtifactProvider());
    eve::procgen::ArtifactStore store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto generated = eve::procgen::generateHexTerrainArtifact(
        hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0103"));
    REQUIRE(generated.ok());
    auto result = publisher.publish(std::move(generated).takeValue(), allProviders());
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::Unsupported));
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(eve::physics::physicsArtifactProvider().size(), std::size_t(0));
}

TEST_CASE("procgen.e2e.snapshotRoundTripPreservesIdentityBuildKeyAndQueries") {
    resetProviders();
    const auto id = artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0104");
    eve::procgen::ArtifactStore store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto generated = eve::procgen::generateHexTerrainArtifact(hexParams(), id);
    REQUIRE(generated.ok());
    const auto expectedKey = generated.value().buildKey;
    auto published = publisher.publish(std::move(generated).takeValue(), allProviders());
    REQUIRE(published.ok());
    std::move(published).takeValue();
    const auto* beforeCollider = eve::physics::physicsArtifactProvider().find(persistentId(id));
    REQUIRE(beforeCollider != nullptr);
    const auto bounds = beforeCollider->bounds;
    const float rayX = (bounds.minX + bounds.maxX) * .5f;
    const float rayZ = (bounds.minZ + bounds.maxZ) * .5f;
    const float rayOriginY = bounds.maxY + 10.f;
    const float rayDirectionY = -((bounds.maxY - bounds.minY) + 20.f);
    const auto beforeHit = eve::physics::physicsArtifactProvider().rayCast(
        persistentId(id), rayX, rayOriginY, rayZ, 0.f, rayDirectionY, 0.f);
    const auto beforeChecksum = eve::graphics::graphicsArtifactProvider().checksum(persistentId(id));
    const auto beforeCell = eve::map::mapArtifactProvider().cell(persistentId(id), 2, 3);

    eve::procgen::ArtifactSnapshotContext context;
    context.instanceId = persistentId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0105");
    context.revision = eve::Revision(9);
    context.tick = eve::SimulationTick(17);
    context.hashProvider = testHash();
    auto snapshot = publisher.snapshot(context);
    REQUIRE(snapshot.ok());
    auto serialized = eve::serializeSnapshotEnvelope(snapshot.value());
    REQUIRE(serialized.ok());
    auto parsed = eve::parseSnapshotEnvelope(serialized.value(), context.hashProvider);
    REQUIRE(parsed.ok());

    store.clear();
    eve::scene::sceneArtifactProvider().clear();
    eve::map::mapArtifactProvider().clear();
    eve::graphics::graphicsArtifactProvider().clear();
    eve::physics::physicsArtifactProvider().clear();
    auto restored = publisher.restore(std::move(parsed).takeValue(), context.hashProvider);
    REQUIRE(restored.ok());
    restored.value();

    CHECK_EQ(store.size(), std::size_t(1));
    CHECK_EQ(store.typeOf(id), std::optional<eve::procgen::ArtifactType>(eve::procgen::ArtifactType::Composite));
    const auto matching = store.findByBuildKey(expectedKey);
    REQUIRE_EQ(matching.size(), std::size_t(1));
    CHECK_EQ(matching.front(), id);
    CHECK_EQ(eve::scene::sceneArtifactProvider().size(), std::size_t(1));
    CHECK_EQ(eve::map::mapArtifactProvider().cell(persistentId(id), 2, 3), beforeCell);
    CHECK_EQ(eve::graphics::graphicsArtifactProvider().checksum(persistentId(id)), beforeChecksum);
    const auto afterHit = eve::physics::physicsArtifactProvider().rayCast(
        persistentId(id), rayX, rayOriginY, rayZ, 0.f, rayDirectionY, 0.f);
    CHECK_EQ(afterHit.hit, beforeHit.hit);
    CHECK_EQ(afterHit.fraction, beforeHit.fraction);
}

TEST_CASE("procgen.e2e.fixedSeedBuildKeyAndPayloadAreStable") {
    const auto first = eve::procgen::generateHexTerrainArtifact(
        hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0106"));
    const auto second = eve::procgen::generateHexTerrainArtifact(
        hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0107"));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().buildKey, second.value().buildKey);
    REQUIRE(first.value().metadata.contains("determinism"));
    const auto* determinism = first.value().metadata.at("determinism").getIf<std::string>();
    REQUIRE(determinism != nullptr);
    CHECK_EQ(*determinism, std::string("seeded_cpu"));
    const auto& firstComposite = std::get<eve::procgen::CompositeArtifact>(first.value().payload);
    const auto& secondComposite = std::get<eve::procgen::CompositeArtifact>(second.value().payload);
    REQUIRE(firstComposite.find("mesh") != nullptr);
    REQUIRE(secondComposite.find("mesh") != nullptr);
    CHECK_EQ(std::get<eve::procgen::MeshData>(firstComposite.find("mesh")->payload).positions(),
             std::get<eve::procgen::MeshData>(secondComposite.find("mesh")->payload).positions());
    CHECK_EQ(std::get<eve::procgen::Grid2D>(firstComposite.find("topology")->payload).cells(),
             std::get<eve::procgen::Grid2D>(secondComposite.find("topology")->payload).cells());
}
