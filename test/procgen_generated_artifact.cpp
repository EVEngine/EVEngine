#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/GeneratedArtifact.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    points.setYaw(point, .25f);
    points.setScale(point, 2.f, 3.f, 4.f);
    points.setDensity(point, .75f);
    points.setPointSeed(point, 123u);
    points.setFloatAttribute(point, "weight", 1.5f);
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
}
