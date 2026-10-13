#include <zeroerr/unittest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/Snapshot.h"
#include "graphics/ArtifactProvider.h"
#include "image/ImageData.h"
#include "map/ArtifactProvider.h"
#include "physics/ArtifactProvider.h"
#include "procgen/ArtifactPublish.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/core/ProcgenCore.h"
#include "procgen/mesh/DynamicMeshUvPaintSession.h"
#include "procgen/mesh/GeometryStroke.h"
#include "procgen/mesh/MeshUvProjection.h"
#include "procgen/texture/FloorTextures.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"
#include "scene/ArtifactProvider.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// procgen_core_boundary.cpp
namespace {

static_assert(std::is_default_constructible_v<eve::procgen::Params>);
static_assert(std::is_default_constructible_v<eve::procgen::Grid2D>);
static_assert(std::is_default_constructible_v<eve::procgen::PointSet>);
static_assert(std::is_default_constructible_v<eve::procgen::MeshBuild>);

}  // namespace

TEST_CASE("procgen.core.cpuSurfaceIsBackendNeutral") {
    using namespace eve::procgen;

    Params params;
    params.setSeed(42);
    params.setSize(4, 3);
    params.setBool("decorations", true);

    const RecipeDescriptor recipe = RecipeDescriptor::grid("core.grid", "Core Grid", "test", 1, 1, 32, 32);
    recipe.applyDefaults(params);
    CHECK_EQ(params.getSeed(), uint32_t(42));
    CHECK_EQ(params.getWidth(), 4);
    CHECK_EQ(params.getHeight(), 3);

    Grid2D grid;
    grid.resize(params.getWidth(), params.getHeight());
    grid.fill(int(Semantic::Floor));
    grid.setMeta("buildKey", "core-grid:v1:42:4x3");
    CHECK_EQ(grid.getCell(2, 1), int(Semantic::Floor));
    CHECK_EQ(grid.getMeta("buildKey", ""), std::string("core-grid:v1:42:4x3"));

    PointSet points;
    points.add(1.f, 0.f, 2.f);
    const PointSet jittered = jitterPointPositions(points, params.getSeed(), 0.25f, 0.25f);
    CHECK_EQ(jittered.getCount(), 1);
    CHECK_EQ(deriveSeed(params.getSeed(), "terrain"), deriveSeed(params.getSeed(), "terrain"));
    CHECK(deriveSeed(params.getSeed(), "terrain") != deriveSeed(params.getSeed(), "props"));

    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    CHECK_EQ(mesh.getVertexCount(), 3);
    CHECK_EQ(mesh.getIndexCount(), 3);

    ProcgenContext context("core-system", params.getSeed(), "core-grid:v1:42:4x3");
    CHECK_EQ(context.getBuildKey(), std::string("core-grid:v1:42:4x3"));
    CHECK_EQ(context.seedFor("terrain"), deriveSeed(params.getSeed(), "terrain"));
}

TEST_CASE("procgen.params.typedStorageConversionsAndCanonicalEncoding") {
    using namespace eve::procgen;

    Params params;
    params.setInt("count", 7);
    params.setFloat("ratio", 1.5f);
    params.setBool("enabled", true);
    params.setString("label", "7");

    // Numeric conversion is explicit and does not go through text.
    CHECK_EQ(params.getInt("count", -1), 7);
    CHECK_EQ(params.getFloat("count", -1.f), 7.f);
    CHECK_EQ(params.getFloat("ratio", -1.f), 1.5f);
    CHECK_EQ(params.getInt("ratio", -1), -1);  // fractional Double is not truncated
    CHECK(params.getBool("enabled", false));
    CHECK_EQ(params.getInt("enabled", -1), 1);  // Bool has only 0/1 numeric conversion
    CHECK_EQ(params.getString("count", "wrong-kind"), "wrong-kind");
    CHECK_EQ(params.getInt("label", -1), -1);  // String is never parsed
    CHECK(params.getBool("label", true));      // invalid kind returns the caller default

    const std::string canonical = params.canonicalString();
    CHECK(canonical.find("int:7") != std::string::npos);
    CHECK(canonical.find("double:") != std::string::npos);
    CHECK(canonical.find("bool:1") != std::string::npos);
    CHECK(canonical.find("string:1:7") != std::string::npos);

    Params sameValues;
    sameValues.setString("label", "7");
    sameValues.setBool("enabled", true);
    sameValues.setFloat("ratio", 1.5f);
    sameValues.setInt("count", 7);
    CHECK_EQ(sameValues.canonicalString(), canonical);

    Params differentKind;
    differentKind.setInt("count", 7);
    differentKind.setFloat("ratio", 1.5f);
    differentKind.setBool("enabled", true);
    differentKind.setInt("label", 7);
    CHECK(differentKind.canonicalString() != canonical);
}

TEST_CASE("procgen.params.dimensionsHaveDedicatedSemantics") {
    using namespace eve::procgen;

    Params params;
    params.setSize(0, -4);
    CHECK_EQ(params.getWidth(), 1);
    CHECK_EQ(params.getHeight(), 1);
    params.setInt("width", 64);
    params.setInt("height", 48);
    params.setInt("seed", 123);
    CHECK_EQ(params.getWidth(), 64);
    CHECK_EQ(params.getHeight(), 48);
    CHECK_EQ(params.getSeed(), uint32_t(123));
    CHECK_EQ(params.getInt("width", -1), 64);
    CHECK_EQ(params.getInt("height", -1), 48);
    CHECK_EQ(params.getInt("seed", -1), 123);

    // Mesh recipes also use width/height as floating algorithm parameters;
    // those values remain separate from the grid dimensions.
    params.setFloat("width", 2.5f);
    params.setString("height", "building-height");
    CHECK_EQ(params.getWidth(), 64);
    CHECK_EQ(params.getHeight(), 48);
    CHECK_EQ(params.getFloat("width", -1.f), 2.5f);
    CHECK_EQ(params.getString("height", "missing"), "building-height");

    Params reordered;
    reordered.setString("height", "building-height");
    reordered.setFloat("width", 2.5f);
    reordered.setSeed(123);
    reordered.setSize(64, 48);
    CHECK_EQ(reordered.canonicalString(), params.canonicalString());
}

// procgen_dynamic_mesh_uv_paint.cpp
using namespace eve;

namespace {
procgen::MeshBuild paintTriangle(float lift = 0.f) {
    procgen::MeshBuild mesh;
    mesh.addVertex(0.f, lift, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, lift, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, lift, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}
}  // namespace

TEST_CASE("procgen.dynamicMeshUvPaint.reusesCanonicalPainterAcrossMeshUpdates") {
    image::ImageData                   pixels(16, 16, "RGBA8");
    procgen::DynamicMeshUvPaintSession session;
    REQUIRE(session.initializeResult(paintTriangle(), pixels).ok());
    REQUIRE(session.paintSurfacePointResult(0, 0.25f, 0.f, 0.25f, 2.f, 1.f, 0.f, 0.f, 1.f).ok());
    const auto paintRevision = session.paintRevision();
    REQUIRE(session.updateMeshResult(paintTriangle(1.f)).ok());
    CHECK_EQ(session.paintRevision(), paintRevision);
    REQUIRE(session.paintSurfacePointResult(0, 0.25f, 1.f, 0.25f, 2.f, 0.f, 1.f, 0.f, 1.f).ok());
    CHECK(session.paintRevision() > paintRevision);
    REQUIRE(session.undoResult().ok());
    REQUIRE(session.currentImageResult().ok());
}

TEST_CASE("procgen.dynamicMeshUvPaint.moduleHandleDetectsRelease") {
    auto* procgen = procgen::Procgen::create();
    auto  created = procgen->newDynamicMeshUvPaintSessionHandle();
    REQUIRE(created.ok());
    const auto handle = created.value();
    CHECK(procgen->resolveDynamicMeshUvPaintSession(handle).isBound());
    REQUIRE(procgen->release(handle).ok());
    CHECK(procgen->isStale(handle));
    CHECK(!procgen->resolveDynamicMeshUvPaintSession(handle).isBound());
}

// procgen_e2e.cpp
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

eve::PersistentId persistentId(eve::procgen::ArtifactId id) { return eve::PersistentId::fromUuid(id); }

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
            bytes[i]     = static_cast<std::uint8_t>((hash >> (i * 8u)) & 0xffu);
            bytes[8 + i] = static_cast<std::uint8_t>(((hash ^ 0x9e3779b97f4a7c15ull) >> (i * 8u)) & 0xffu);
        }
        const auto id = eve::ContentId::fromBytes(std::span<const std::uint8_t>(bytes));
        if (!id)
            return eve::Result<eve::ContentId>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "test digest construction failed"));
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
    options.scene    = true;
    options.graphics = true;
    options.physics  = true;
    options.map      = true;
    return options;
}

}  // namespace

TEST_CASE("procgen.e2e.hexProvidersCommitQueryableObjects") {
    resetProviders();
    const auto                      id = artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0101");
    eve::procgen::ArtifactStore     store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto                            generated = eve::procgen::generateHexTerrainArtifact(hexParams(), id);
    REQUIRE(generated.ok());
    REQUIRE(generated.value().metadata.contains("algorithm"));
    REQUIRE(generated.value().metadata.contains("determinism"));
    auto published = publisher.publish(std::move(generated).takeValue(), allProviders());
    REQUIRE(published.ok());
    std::move(published).takeValue();

    auto& scene    = eve::scene::sceneArtifactProvider();
    auto& map      = eve::map::mapArtifactProvider();
    auto& graphics = eve::graphics::graphicsArtifactProvider();
    auto& physics  = eve::physics::physicsArtifactProvider();
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
    const auto* collider = physics.find(persistentId(id));
    REQUIRE_GE(collider->indices.size(), std::size_t(3));
    const auto vertex = [collider](std::size_t corner, std::size_t axis) {
        return collider->vertices[std::size_t(collider->indices[corner]) * 3u + axis];
    };
    const float rayX   = (vertex(0, 0) + vertex(1, 0) + vertex(2, 0)) / 3.f;
    const float rayZ   = (vertex(0, 2) + vertex(1, 2) + vertex(2, 2)) / 3.f;
    const auto  bounds = collider->bounds;
    const auto  hit    = physics.rayCast(persistentId(id), rayX, bounds.maxY + 10.f, rayZ, 0.f,
                                         -((bounds.maxY - bounds.minY) + 20.f), 0.f);
    CHECK(hit.hit);
    CHECK(hit.fraction >= 0.f);
    CHECK(hit.fraction <= 1.f);
}

TEST_CASE("procgen.e2e.providerFailureLeavesNoHalfState") {
    for (int failure = 0; failure < 4; ++failure) {
        resetProviders();
        auto& scene    = eve::scene::sceneArtifactProvider();
        auto& map      = eve::map::mapArtifactProvider();
        auto& graphics = eve::graphics::graphicsArtifactProvider();
        auto& physics  = eve::physics::physicsArtifactProvider();
        if (failure == 0) scene.setPrepareFailure(true);
        if (failure == 1) map.setPrepareFailure(true);
        if (failure == 2) graphics.setPrepareFailure(true);
        if (failure == 3) physics.setPrepareFailure(true);

        eve::procgen::ArtifactStore     store;
        eve::procgen::ArtifactPublisher publisher(store);
        auto                            generated =
            eve::procgen::generateHexTerrainArtifact(hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0102"));
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
    eve::cap::revoke<eve::artifact::IPhysicsArtifactAdapter>(&eve::physics::physicsArtifactProvider());
    eve::procgen::ArtifactStore     store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto                            generated =
        eve::procgen::generateHexTerrainArtifact(hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0103"));
    REQUIRE(generated.ok());
    auto result = publisher.publish(std::move(generated).takeValue(), allProviders());
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::Unsupported));
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(eve::physics::physicsArtifactProvider().size(), std::size_t(0));
}

TEST_CASE("procgen.e2e.snapshotRoundTripPreservesIdentityBuildKeyAndQueries") {
    resetProviders();
    const auto                      id = artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0104");
    eve::procgen::ArtifactStore     store;
    eve::procgen::ArtifactPublisher publisher(store);
    auto                            generated = eve::procgen::generateHexTerrainArtifact(hexParams(), id);
    REQUIRE(generated.ok());
    const auto expectedKey = generated.value().buildKey;
    auto       published   = publisher.publish(std::move(generated).takeValue(), allProviders());
    REQUIRE(published.ok());
    std::move(published).takeValue();
    const auto* beforeCollider = eve::physics::physicsArtifactProvider().find(persistentId(id));
    REQUIRE(beforeCollider != nullptr);
    const auto  bounds        = beforeCollider->bounds;
    const float rayX          = (bounds.minX + bounds.maxX) * .5f;
    const float rayZ          = (bounds.minZ + bounds.maxZ) * .5f;
    const float rayOriginY    = bounds.maxY + 10.f;
    const float rayDirectionY = -((bounds.maxY - bounds.minY) + 20.f);
    const auto  beforeHit = eve::physics::physicsArtifactProvider().rayCast(persistentId(id), rayX, rayOriginY, rayZ,
                                                                            0.f, rayDirectionY, 0.f);
    const auto  beforeChecksum = eve::graphics::graphicsArtifactProvider().checksum(persistentId(id));
    const auto  beforeCell     = eve::map::mapArtifactProvider().cell(persistentId(id), 2, 3);

    eve::procgen::ArtifactSnapshotContext context;
    context.instanceId   = persistentId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0105");
    context.revision     = eve::Revision(9);
    context.tick         = eve::SimulationTick(17);
    context.hashProvider = testHash();
    auto snapshot        = publisher.snapshot(context);
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
    const auto afterHit = eve::physics::physicsArtifactProvider().rayCast(persistentId(id), rayX, rayOriginY, rayZ, 0.f,
                                                                          rayDirectionY, 0.f);
    CHECK_EQ(afterHit.hit, beforeHit.hit);
    CHECK_EQ(afterHit.fraction, beforeHit.fraction);
}

TEST_CASE("procgen.e2e.fixedSeedBuildKeyAndPayloadAreStable") {
    const auto first =
        eve::procgen::generateHexTerrainArtifact(hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0106"));
    const auto second =
        eve::procgen::generateHexTerrainArtifact(hexParams(), artifactId("018f0b7e-6e50-7a10-8c22-2c8f8e3e0107"));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().buildKey, second.value().buildKey);
    REQUIRE(first.value().metadata.contains("determinism"));
    const auto* determinism = first.value().metadata.at("determinism").getIf<std::string>();
    REQUIRE(determinism != nullptr);
    CHECK_EQ(*determinism, std::string("seeded_cpu"));
    const auto& firstComposite  = std::get<eve::procgen::CompositeArtifact>(first.value().payload);
    const auto& secondComposite = std::get<eve::procgen::CompositeArtifact>(second.value().payload);
    REQUIRE(firstComposite.find("mesh") != nullptr);
    REQUIRE(secondComposite.find("mesh") != nullptr);
    CHECK_EQ(std::get<eve::procgen::MeshData>(firstComposite.find("mesh")->payload).positions(),
             std::get<eve::procgen::MeshData>(secondComposite.find("mesh")->payload).positions());
    CHECK_EQ(std::get<eve::procgen::Grid2D>(firstComposite.find("topology")->payload).cells(),
             std::get<eve::procgen::Grid2D>(secondComposite.find("topology")->payload).cells());
}

// procgen_floor_textures.cpp
// Dedicated floor-texture recipe coverage lives in this TU so the large
// procgen.cpp suite stays process-isolated and focused.


using namespace eve::procgen;

TEST_CASE("procgen.floorTextures.wood.reproducibleLayouts") {
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(TextureRecipeRegistry::instance().has("tex.floor.wood"));

    const char* layouts[] = {"planks", "staggered", "herringbone", "chevron", "parquet",
                             "basket", "diagonal",  "ladder",      "finger",  "versailles"};
    for (const char* layout : layouts) {
        Params p;
        p.setSeed(42);
        p.setSize(64, 64);
        p.setString("layout", layout);
        p.setString("tone", "oak");
        p.setInt("rows", 5);
        p.setInt("cols", 4);
        p.setFloat("gap", 0.05f);
        p.setInt("seamless", 1);

        std::string err;
        auto        a = TextureRecipeRegistry::instance().generate("tex.floor.wood", p, err);
        auto        b = TextureRecipeRegistry::instance().generate("tex.floor.wood", p, err);
        REQUIRE(static_cast<bool>(a));
        REQUIRE(static_cast<bool>(b));
        CHECK_EQ(a->getWidth(), 64);
        CHECK_EQ(a->getHeight(), 64);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.floorTextures.tile.reproduciblePatterns") {
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(TextureRecipeRegistry::instance().has("tex.floor.tile"));

    const char* patterns[] = {"square", "checker",   "diamond",     "hex",     "subway",    "brick",    "stack",
                              "mosaic", "basket",    "herringbone", "octagon", "fishscale", "pinwheel", "star",
                              "cobble", "arabesque", "terrazzo",    "scallop", "windmill",  "moroccan"};
    for (const char* pattern : patterns) {
        Params p;
        p.setSeed(17);
        p.setSize(48, 48);
        p.setString("pattern", pattern);
        p.setString("palette", "ceramic");
        p.setInt("tilesX", 4);
        p.setInt("tilesY", 4);
        p.setFloat("grout", 0.07f);
        p.setInt("seamless", 1);

        std::string err;
        auto        a = TextureRecipeRegistry::instance().generate("tex.floor.tile", p, err);
        auto        b = TextureRecipeRegistry::instance().generate("tex.floor.tile", p, err);
        REQUIRE(static_cast<bool>(a));
        REQUIRE(static_cast<bool>(b));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.floorTextures.paramsChangeOutput") {
    TextureRecipeRegistry::instance().registerBuiltins();

    Params oak;
    oak.setSeed(9);
    oak.setSize(32, 32);
    oak.setString("layout", "planks");
    oak.setString("tone", "oak");

    Params walnut = oak;
    walnut.setString("tone", "walnut");

    std::string err;
    auto        a = TextureRecipeRegistry::instance().generate("tex.floor.wood", oak, err);
    auto        b = TextureRecipeRegistry::instance().generate("tex.floor.wood", walnut, err);
    REQUIRE(static_cast<bool>(a));
    REQUIRE(static_cast<bool>(b));
    CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) != 0);

    Params square;
    square.setSeed(3);
    square.setSize(32, 32);
    square.setString("pattern", "square");
    square.setString("palette", "ceramic");

    Params checker = square;
    checker.setString("pattern", "checker");

    auto c = TextureRecipeRegistry::instance().generate("tex.floor.tile", square, err);
    auto d = TextureRecipeRegistry::instance().generate("tex.floor.tile", checker, err);
    REQUIRE(static_cast<bool>(c));
    REQUIRE(static_cast<bool>(d));
    CHECK(std::memcmp(c->getData(), d->getData(), c->getSize()) != 0);
}

TEST_CASE("procgen.floorTextures.resultApiAndDefaults") {
    Params p;
    p.setSeed(1);
    p.setSize(40, 40);
    auto wood = generateWoodFloorTexture(p);
    REQUIRE(wood.ok());
    REQUIRE(static_cast<bool>(wood.value()));
    CHECK_EQ(wood.value()->getWidth(), 40);

    auto tile = generateTileFloorTexture(p);
    REQUIRE(tile.ok());
    REQUIRE(static_cast<bool>(tile.value()));

    TextureRecipeRegistry::instance().registerBuiltins();
    Params filled;
    filled.setSize(16, 16);
    REQUIRE(TextureRecipeRegistry::instance().applyDefaults("tex.floor.wood", filled));
    CHECK_EQ(filled.getString("layout", ""), std::string("planks"));
    CHECK_EQ(filled.getString("tone", ""), std::string("oak"));
    REQUIRE(TextureRecipeRegistry::instance().applyDefaults("tex.floor.tile", filled));
    CHECK_EQ(filled.getString("pattern", ""), std::string("square"));
    CHECK_EQ(filled.getString("palette", ""), std::string("ceramic"));
}

TEST_CASE("procgen.floorTextures.pbr.fullMapSet") {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    REQUIRE(PbrRecipeRegistry::instance().has("pbr.floor.wood"));
    REQUIRE(PbrRecipeRegistry::instance().has("pbr.floor.tile"));

    Params wood;
    wood.setSeed(11);
    wood.setSize(48, 48);
    wood.setString("layout", "herringbone");
    wood.setString("tone", "walnut");
    wood.setInt("rows", 6);
    wood.setInt("cols", 6);
    wood.setInt("seamless", 1);

    std::string err;
    auto        woodSet = PbrRecipeRegistry::instance().generate("pbr.floor.wood", wood, err);
    REQUIRE(static_cast<bool>(woodSet));
    REQUIRE(woodSet->albedo != nullptr);
    REQUIRE(woodSet->normal != nullptr);
    REQUIRE(woodSet->roughness != nullptr);
    REQUIRE(woodSet->metallic != nullptr);
    REQUIRE(woodSet->height != nullptr);
    REQUIRE(woodSet->ao != nullptr);
    CHECK_EQ(woodSet->albedo->getWidth(), 48);
    CHECK_EQ(woodSet->normal->getWidth(), 48);
    CHECK_EQ(woodSet->height->getFormat(), std::string("RGBA8"));

    auto woodResult = generateWoodFloorPbr(wood);
    REQUIRE(woodResult.ok());
    REQUIRE(static_cast<bool>(woodResult.value()));
    CHECK(woodResult.value()->albedo != nullptr);
    CHECK(woodResult.value()->ao != nullptr);

    Params tile;
    tile.setSeed(22);
    tile.setSize(40, 40);
    tile.setString("pattern", "octagon");
    tile.setString("palette", "encaustic");
    tile.setFloat("glaze", 0.55f);
    tile.setInt("seamless", 1);

    auto tileSet = PbrRecipeRegistry::instance().generate("pbr.floor.tile", tile, err);
    REQUIRE(static_cast<bool>(tileSet));
    REQUIRE(tileSet->albedo != nullptr);
    REQUIRE(tileSet->normal != nullptr);
    REQUIRE(tileSet->roughness != nullptr);
    REQUIRE(tileSet->metallic != nullptr);
    REQUIRE(tileSet->height != nullptr);
    REQUIRE(tileSet->ao != nullptr);

    Params defaults;
    defaults.setSize(16, 16);
    REQUIRE(PbrRecipeRegistry::instance().applyDefaults("pbr.floor.wood", defaults));
    CHECK_EQ(defaults.getString("layout", ""), std::string("planks"));
    CHECK(defaults.getFloat("normalStrength", 0.f) > 0.f);
    REQUIRE(PbrRecipeRegistry::instance().applyDefaults("pbr.floor.tile", defaults));
    CHECK_EQ(defaults.getString("pattern", ""), std::string("square"));
}

// procgen_generated_artifact.cpp
using namespace eve::procgen;

namespace {

ArtifactId testId() {
    const auto parsed = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd001");
    REQUIRE(parsed.has_value());
    return *parsed;
}

}  // namespace

TEST_CASE("procgen.generatedArtifact.buildKeyIsCanonicalAndIdentityIsSeparate") {
    Params first;
    first.setSeed(17);
    first.setInt("width", 48);
    first.setFloat("radius", .75f);
    first.setBool("decorations", true);

    Params second;
    second.setBool("decorations", true);
    second.setFloat("radius", .75f);
    second.setInt("width", 48);
    second.setSeed(17);

    const auto firstKey  = BuildKey::forRecipe("mesh.hexterrain", first);
    const auto secondKey = BuildKey::forRecipe("mesh.hexterrain", second);
    REQUIRE(firstKey.has_value());
    REQUIRE(secondKey.has_value());
    CHECK_EQ(firstKey->format(), secondKey->format());

    second.setSeed(18);
    const auto changedKey = BuildKey::forRecipe("mesh.hexterrain", second);
    REQUIRE(changedKey.has_value());
    CHECK_NE(firstKey->format(), changedKey->format());

    const ArtifactId root = testId();
    CHECK(!root.isNil());
    CHECK(root.child("mesh") != root);
    CHECK(root.child("mesh") != root.child("collider"));
    CHECK(root.child("").isNil());
}

TEST_CASE("procgen.generatedArtifact.hexTerrainPublishesCompositeProducts") {
    Params params;
    params.setInt("width", 8);
    params.setInt("height", 7);
    params.setInt("riverCount", 2);
    params.setInt("seed", 91);

    auto result = generateHexTerrainArtifact(params, testId());
    REQUIRE(result.ok());
    GeneratedArtifact artifact = std::move(result).takeValue();
    CHECK_EQ(static_cast<int>(artifact.type), static_cast<int>(ArtifactType::Composite));
    CHECK(!artifact.buildKey.empty());
    CHECK(artifact.bounds.isValid());

    const auto& composite = std::get<CompositeArtifact>(artifact.payload);
    REQUIRE_EQ(composite.children.size(), std::size_t(4));
    const ArtifactPart* mesh     = composite.find("mesh");
    const ArtifactPart* collider = composite.find("collider");
    const ArtifactPart* topology = composite.find("topology");
    const ArtifactPart* anchors  = composite.find("anchors");
    REQUIRE(mesh != nullptr);
    REQUIRE(collider != nullptr);
    REQUIRE(topology != nullptr);
    REQUIRE(anchors != nullptr);
    CHECK_EQ(static_cast<int>(mesh->type), static_cast<int>(ArtifactType::MeshData));
    CHECK(std::get<MeshData>(mesh->payload).getVertexCount() > 0);
    CHECK(std::get<Collider>(collider->payload).isValid());
    CHECK_EQ(std::get<Grid2D>(topology->payload).getWidth(), 8);
    CHECK_EQ(std::get<PointSet>(anchors->payload).getCount(), 4);
}

TEST_CASE("procgen.generatedArtifact.castlePublishesCompositeProductsAndStoreProtectsIdentity") {
    Params params;
    params.setInt("rings", 2);
    params.setInt("keepFloors", 2);
    params.setInt("detail", 1);
    params.setInt("seed", 12);

    const ArtifactId id     = testId();
    auto             result = generateCastleArtifact(params, id);
    REQUIRE(result.ok());
    GeneratedArtifact artifact  = std::move(result).takeValue();
    const BuildKey    key       = artifact.buildKey;
    const auto&       composite = std::get<CompositeArtifact>(artifact.payload);
    REQUIRE(composite.has("mesh"));
    REQUIRE(composite.has("collider"));
    REQUIRE(composite.has("topology"));
    REQUIRE(composite.has("anchors"));
    CHECK(std::get<MeshData>(composite.find("mesh")->payload).getIndexCount() > 0);
    CHECK(std::get<Collider>(composite.find("collider")->payload).isValid());
    CHECK_EQ(std::get<Grid2D>(composite.find("topology")->payload).getWidth(), 2);
    CHECK_EQ(std::get<PointSet>(composite.find("anchors")->payload).getCount(), 5);

    ArtifactStore store;
    auto          published = store.publish(std::move(artifact));
    REQUIRE(published.ok());
    CHECK_EQ(std::move(published).takeValue(), id);
    CHECK_EQ(store.size(), std::size_t(1));
    CHECK(store.find(id) != nullptr);
    CHECK_EQ(store.findByBuildKey(key).size(), std::size_t(1));

    Params sameParams = params;
    auto   duplicate  = generateCastleArtifact(sameParams, id);
    REQUIRE(duplicate.ok());
    auto conflict = store.publish(std::move(duplicate).takeValue());
    CHECK(!conflict.ok());
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(eve::StatusCode::Conflict));

    auto removed = store.remove(id);
    CHECK(removed.ok());
    CHECK(store.find(id) == nullptr);
    auto missing = store.remove(id);
    CHECK(!missing.ok());
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(eve::StatusCode::NotFound));
}

TEST_CASE("procgen.generatedArtifact.rejectsInvalidContracts") {
    const ArtifactId id  = testId();
    auto             key = BuildKey::fromCanonical("procgen/v1/test/0000000000000001");
    REQUIRE(key.has_value());
    Bounds bounds;
    bounds.include(0.f, 0.f, 0.f);
    Grid2D grid;
    grid.resize(1, 1);

    auto nil = makeArtifact(ArtifactId::nil(), ArtifactType::Grid, eve::SchemaVersion(1), *key, bounds, {}, {},
                            GeneratedArtifact::Payload(grid));
    CHECK(!nil.ok());
    CHECK_EQ(static_cast<int>(nil.code()), static_cast<int>(eve::StatusCode::Rejected));

    auto mismatch = makeArtifact(id, ArtifactType::MeshData, eve::SchemaVersion(1), *key, bounds, {}, {},
                                 GeneratedArtifact::Payload(grid));
    CHECK(!mismatch.ok());
    CHECK_EQ(static_cast<int>(mismatch.code()), static_cast<int>(eve::StatusCode::Rejected));
}

TEST_CASE("procgen.meshBuild.checkedGroupRestoreIsAtomic") {
    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);

    auto valid = mesh.restoreGroupData({"main"}, {0}, 0);
    REQUIRE(valid.ok());
    CHECK_EQ(mesh.getGroupCount(), 1);
    CHECK_EQ(mesh.activeGroup(), 0);

    auto invalid = mesh.restoreGroupData({""}, {0}, 0);
    REQUIRE(!invalid.ok());
    CHECK_EQ(static_cast<int>(invalid.error()->code()), static_cast<int>(eve::DiagnosticCode::ProcgenGroupDataInvalid));
    CHECK_EQ(mesh.getGroupName(0), std::string("main"));
    CHECK_EQ(mesh.getTriangleGroup(0), 0);
}

TEST_CASE("procgen.generatedArtifact.denseSnapshotRoundTripsEveryPayloadAndRebuildsIndexes") {
    const ArtifactId root = testId();
    const auto       key  = BuildKey::fromCanonical("procgen/v1/roundtrip/0000000000000001");
    REQUIRE(key.has_value());

    Bounds rootBounds;
    rootBounds.include(0.f, 0.f, 0.f);
    rootBounds.include(4.f, 3.f, 2.f);

    Grid2D grid;
    grid.resize(2, 1);
    grid.setCell(0, 0, 7);
    grid.setCell(1, 0, 9);
    grid.setDetail(1, 0, 3);
    grid.setMeta("theme", "test");
    grid.addObject("spawn", "unit", 1.f, 2.f, 3.f, 4.f, 5);
    Bounds gridBounds;
    gridBounds.include(0.f, 0.f, 0.f);
    gridBounds.include(2.f, 0.f, 1.f);

    PointSet  points;
    const int point = points.add(1.f, 2.f, 3.f);
    points.setNormal(point, .1f, .9f, .2f);
    points.setRotation(point, .15f, .25f, .35f);
    points.setScale(point, 2.f, 3.f, 4.f);
    points.setBounds(point, -1.f, -2.f, -3.f, 1.f, 2.f, 3.f);
    points.setColor(point, .1f, .2f, .3f, .4f);
    points.setSteepness(point, .65f);
    points.setDensity(point, .75f);
    points.setPointSeed(point, 123u);
    points.trySetPointId(point, 18446744073709551614ull).expect("artifact snapshot point id");
    points.setFloatAttribute(point, "weight", 1.5f);
    points.setIntAttribute(point, "variant", 4);
    points.setBoolAttribute(point, "enabled", true);
    points.setVectorAttribute(point, "wind", 5.f, 6.f, 7.f);
    points.setStringAttribute(point, "tag", "anchor");
    Bounds pointBounds;
    pointBounds.include(1.f, 2.f, 3.f);

    MeshData mesh;
    mesh.setActiveGroup("main");
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    mesh.setMeta("material", "debug");
    Bounds meshBounds;
    meshBounds.include(0.f, 0.f, 0.f);
    meshBounds.include(1.f, 1.f, 0.f);

    ImageData image;
    image.width    = 2;
    image.height   = 1;
    image.channels = 4;
    image.format   = "rgba8";
    image.pixels   = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    Bounds imageBounds;
    imageBounds.include(0.f, 0.f, 0.f);
    imageBounds.include(2.f, 1.f, 0.f);

    Collider collider;
    collider.vertices = mesh.positions();
    collider.indices  = mesh.indices();
    collider.bounds   = meshBounds;
    collider.shape    = "triangle_mesh";

    auto makePart = [&](std::string role, ArtifactId id, ArtifactType type, Bounds bounds, ArtifactLeafPayload payload,
                        std::vector<ArtifactId> dependencies = {}) {
        return makeArtifactPart(std::move(role), id, type, eve::SchemaVersion(1), *key, bounds, std::move(dependencies),
                                {}, std::move(payload));
    };
    std::vector<ArtifactPart> children;
    auto                      addPart = [&](auto result) {
        REQUIRE(result.ok());
        children.emplace_back(std::move(result).takeValue());
    };
    addPart(makePart("grid", root.child("grid"), ArtifactType::Grid, gridBounds, std::move(grid)));
    addPart(makePart("points", root.child("points"), ArtifactType::PointSet, pointBounds, std::move(points)));
    addPart(makePart("mesh", root.child("mesh"), ArtifactType::MeshData, meshBounds, std::move(mesh)));
    addPart(makePart("image", root.child("image"), ArtifactType::ImageData, imageBounds, std::move(image)));
    addPart(makePart("collider", root.child("collider"), ArtifactType::Collider, meshBounds, std::move(collider),
                     {root.child("mesh")}));

    CompositeArtifact composite;
    composite.children = std::move(children);
    auto generated     = makeArtifact(root, ArtifactType::Composite, eve::SchemaVersion(1), *key, rootBounds, {}, {},
                                      std::move(composite));
    REQUIRE(generated.ok());

    ArtifactStore source;
    REQUIRE(source.publish(std::move(generated).takeValue()).ok());
    auto snapshot = source.snapshotState();
    REQUIRE(snapshot.ok());

    ArtifactStore restored;
    REQUIRE(restored.restoreState(snapshot.value()).ok());
    auto restoredSnapshot = restored.snapshotState();
    REQUIRE(restoredSnapshot.ok());
    CHECK_EQ(restoredSnapshot.value(), snapshot.value());
    CHECK(restored.find(root) != nullptr);
    CHECK_EQ(restored.partCount(), std::size_t(5));
    CHECK(restored.findPart(root.child("grid")) != nullptr);
    CHECK(restored.findPart(root.child("points")) != nullptr);
    CHECK(restored.findPart(root.child("mesh")) != nullptr);
    CHECK(restored.findPart(root.child("image")) != nullptr);
    CHECK(restored.findPart(root.child("collider")) != nullptr);
    REQUIRE(restored.typeOf(root.child("mesh")).has_value());
    CHECK_EQ(static_cast<int>(*restored.typeOf(root.child("mesh"))), static_cast<int>(ArtifactType::MeshData));
    const auto* restoredMesh = restored.findPart(root.child("mesh"));
    REQUIRE(restoredMesh != nullptr);
    CHECK_EQ(std::get<MeshData>(restoredMesh->payload).groupNames(), std::vector<std::string>{"main"});
    const auto* restoredPointsPart = restored.findPart(root.child("points"));
    REQUIRE(restoredPointsPart != nullptr);
    const auto& restoredPoints = std::get<PointSet>(restoredPointsPart->payload);
    CHECK_EQ(restoredPoints.getPitch(0), .15f);
    CHECK_EQ(restoredPoints.getRoll(0), .35f);
    CHECK_EQ(restoredPoints.getBoundsMinY(0), -2.f);
    CHECK_EQ(restoredPoints.getColorA(0), .4f);
    CHECK_EQ(restoredPoints.getSteepness(0), .65f);
    CHECK_EQ(restoredPoints.getPointId(0), 18446744073709551614ull);
    CHECK_EQ(restoredPoints.getIntAttribute(0, "variant", -1), 4);
    CHECK(restoredPoints.getBoolAttribute(0, "enabled", false));
    CHECK_EQ(restoredPoints.getVectorAttributeZ(0, "wind", -1.f), 7.f);
}

// procgen_geometry_stroke.cpp
using namespace eve::procgen;

namespace {

template <typename T>
void requireOk(const eve::Result<T>& result) {
    REQUIRE(result.ok());
}

}  // namespace

TEST_CASE("procgen.geometryStroke.buildsAllOfficialCrossSections") {
    struct Expected {
        const char* shape;
        int         totalIndices;
    };
    for (const Expected expected : {Expected{"quad", 24}, Expected{"triangularPrism", 42}, Expected{"cube", 60}}) {
        GeometryStroke stroke;
        requireOk(stroke.setShapeResult(expected.shape));
        requireOk(stroke.setSizeResult(0.5f, 0.3f));
        requireOk(stroke.addPointResult(0.f, 0.f, 0.f));
        requireOk(stroke.addPointResult(1.f, 0.f, 0.f));
        requireOk(stroke.addPointResult(1.f, 1.f, 0.f));
        auto mesh = stroke.buildMeshResult();
        requireOk(mesh);
        CHECK_EQ(mesh.value().getIndexCount(), expected.totalIndices);
        CHECK_EQ(mesh.value().getMeta("shape", ""), expected.shape);
        CHECK_EQ(mesh.value().getMeta("pointCount", ""), "3");
        CHECK_EQ(mesh.value().getGroupCount(), 1);
    }
}

TEST_CASE("procgen.geometryStroke.supportsPlanarSpacingUndoAndAtomicValidation") {
    GeometryStroke stroke;
    requireOk(stroke.setInputSpaceResult("planar", 2.f));
    requireOk(stroke.setMinimumSpacingResult(0.5f));
    auto first = stroke.addPointResult(0.f, 99.f, 0.f);
    auto close = stroke.addPointResult(0.1f, -5.f, 0.f);
    auto next  = stroke.addPointResult(1.f, -5.f, 0.f);
    requireOk(first);
    requireOk(close);
    requireOk(next);
    CHECK(first.value());
    CHECK(!close.value());
    CHECK(next.value());
    CHECK_EQ(stroke.pointCount(), 2);

    const auto revision = stroke.revision();
    CHECK(!stroke.setShapeResult("sphere").ok());
    CHECK_EQ(stroke.revision(), revision);
    auto mesh = stroke.buildMeshResult();
    requireOk(mesh);
    for (int index = 0; index < mesh.value().getVertexCount(); ++index)
        CHECK(std::abs(mesh.value().getPositionY(index) - 2.f) < 1e-6f);

    requireOk(stroke.undoResult());
    CHECK_EQ(stroke.pointCount(), 1);
    CHECK(!stroke.buildMeshResult().ok());
    requireOk(stroke.undoResult());
    CHECK(!stroke.undoResult().ok());
}
