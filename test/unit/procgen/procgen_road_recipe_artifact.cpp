#include "common/Capability.h"
#include "common/ProcgenWorldQuery.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
struct ArtifactTerrainQuery final : eve::IProcgenWorldQuery {
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float, float, std::uint64_t) override {
        eve::ProcgenSurfaceHit hit;
        hit.hit = true;
        hit.x   = x;
        hit.z   = z;
        hit.y   = -20.f;
        return eve::Result<eve::ProcgenSurfaceHit>::success(hit);
    }
};
struct ArtifactTerrainRegistration {
    eve::IProcgenWorldQuery* prior = eve::cap::query<eve::IProcgenWorldQuery>();
    explicit ArtifactTerrainRegistration(eve::IProcgenWorldQuery* provider) {
        eve::cap::provide<eve::IProcgenWorldQuery>(provider);
    }
    ~ArtifactTerrainRegistration() { eve::cap::provide<eve::IProcgenWorldQuery>(prior); }
};
}  // namespace

TEST_CASE("procgen.road.authoring.recipeArtifactPublishesAuthorGeometryAndAllLODProducts") {
    ArtifactTerrainQuery        terrainQuery;
    ArtifactTerrainRegistration terrainRegistration(&terrainQuery);
    auto                        source = RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(source.ok());
    auto network                   = std::move(source).takeValue();
    auto style                     = network.edges()[0].style;
    style.laneWidth                = 4.2f;
    style.sideTerrain.mode         = RoadSideTerrainMode::Average;
    style.samplingSpacingMeters    = 1.f;
    style.samplingPolicy           = RoadSamplingPolicy::ParametricDistanceTurn;
    style.crossSectionSubdivisions = 3;
    style.structures               = {{.1f, .3f, RoadStructureKind::Bridge}, {.6f, .9f, RoadStructureKind::Tunnel}};
    REQUIRE(network.setEdgeStyle(1, style).ok());
    auto points = network.edges()[0].controlPoints;
    for (auto& point : points) {
        point.rollDegrees      = 12.f;
        point.rollCenterOffset = -.3f;
    }
    points.front().rollDegrees = 270.f;
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    Params params;
    params.setString("placementWorldRevision", "test-terrain/v1");
    params.setString("scene", "must-not-use-scene");
    params.setInt("pathSegments", 32);
    params.setInt("lodCount", 3);
    params.setFloat("chunkSize", 16.f);
    params.setString("algorithmRevision", "caller-cannot-select-producer-revision");
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    auto id = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd191");
    REQUIRE(id.has_value());
    auto generated = generateRoadNetworkArtifact(params, *id);
    REQUIRE(generated.ok());
    auto originalKey = generated.value().buildKey;
    auto lookupKey   = BuildKey::forRecipe("mesh.roadNetwork", params);
    REQUIRE(lookupKey.has_value());
    REQUIRE(*lookupKey == originalKey);
    Params changedWorld = params;
    changedWorld.setString("placementWorldRevision", "test-terrain/v2");
    REQUIRE(BuildKey::forRecipe("mesh.roadNetwork", changedWorld).has_value());
    REQUIRE(*BuildKey::forRecipe("mesh.roadNetwork", changedWorld) != originalKey);
    REQUIRE(originalKey.format().starts_with("procgen/v2/mesh.roadNetwork/"));
    const auto revision = generated.value().metadata.at("algorithmRevision").asString();
    REQUIRE(!revision.empty());
    REQUIRE(revision != params.getString("algorithmRevision", ""));
    REQUIRE(originalKey.format().find('/' + revision + '/') != std::string::npos);
    auto* composite = std::get_if<CompositeArtifact>(&generated.value().payload);
    REQUIRE(composite != nullptr);
    for (const auto& part : composite->children) {
        if (part.role.starts_with("render/chunk/"))
            REQUIRE(part.buildKey.format().starts_with("eve.procgen.road.chunk.v2/" + revision + '/'));
        else
            REQUIRE(part.buildKey.format().starts_with(originalKey.format() + '/'));
    }
    const auto* mesh = composite->find("mesh");
    REQUIRE(mesh != nullptr);
    auto bake = bakeRoadNetworkRecipe(params);
    REQUIRE(bake.ok());
    REQUIRE(std::get<MeshData>(mesh->payload).positions() == bake.value().mesh.positions());
    REQUIRE(std::get<MeshData>(mesh->payload).indices() == bake.value().mesh.indices());
    REQUIRE(composite->find("collider") != nullptr);
    REQUIRE(composite->find("collider/drivable") != nullptr);
    REQUIRE(composite->find("topology") != nullptr);
    REQUIRE(composite->find("terrain/surface-weight") != nullptr);
    REQUIRE(composite->find("navigation/lane/0") != nullptr);
    REQUIRE(composite->find("placements") != nullptr);
    const auto placementData = std::get<PointSet>(composite->find("placements")->payload);
    bool       foundSnapped  = false;
    for (const auto& point : placementData.points()) {
        if (point.y == -20.f) foundSnapped = true;
    }
    REQUIRE(foundSnapped);
    for (int lod = 0; lod < 3; ++lod) {
        Params level = params;
        level.setInt("pathSegments", 32 >> lod);
        level.setInt("turnSamples", std::max(4, 12 >> lod));
        level.setFloat("samplingScale", static_cast<float>(1 << lod));
        level.setFloat("junctionChordError", .1f * static_cast<float>(1 << lod));
        if (lod > 0) {
            level.setBool("navigation", false);
            level.setBool("placements", false);
        }
        auto expected = bakeRoadNetworkRecipe(level);
        REQUIRE(expected.ok());
        std::size_t indices = 0, chunks = 0;
        const auto  suffix = "/lod/" + std::to_string(lod);
        for (const auto& part : composite->children) {
            if (part.role.starts_with("render/chunk/") && part.role.ends_with(suffix)) {
                REQUIRE(part.type == ArtifactType::MeshData);
                indices += std::get<MeshData>(part.payload).indices().size();
                ++chunks;
            }
        }
        REQUIRE(chunks > 0);
        REQUIRE(indices == expected.value().mesh.indices().size());
    }
    Params diagnosticChange = params;
    diagnosticChange.setString("authorLabel", "same-geometry-different-input");
    auto sameGeometry = generateRoadNetworkArtifact(diagnosticChange, *id);
    REQUIRE(sameGeometry.ok());
    REQUIRE(sameGeometry.value().buildKey != originalKey);
    const auto& equivalentComposite = std::get<CompositeArtifact>(sameGeometry.value().payload);
    for (const auto& part : composite->children) {
        if (!part.role.starts_with("render/chunk/")) continue;
        const auto* equivalent = equivalentComposite.find(part.role);
        REQUIRE(equivalent != nullptr);
        REQUIRE(equivalent->buildKey == part.buildKey);
    }
    ArtifactStore store;
    REQUIRE(store.publish(std::move(generated).takeValue()).ok());
    REQUIRE(store.find(*id) != nullptr);
    REQUIRE(store.findByBuildKey(originalKey).size() == 1u);
    REQUIRE(store.findPart(id->child("collider/drivable")) != nullptr);
    auto state = store.snapshotState();
    REQUIRE(state.ok());
    auto serialized = state.value().toJson();
    REQUIRE(serialized.ok());
    auto parsed = eve::Value::fromJson(serialized.value());
    REQUIRE(parsed.ok());
    ArtifactStore restored;
    REQUIRE(restored.restoreState(parsed.value()).ok());
    REQUIRE(restored.partCount() == store.partCount());
    REQUIRE(restored.find(*id)->buildKey == originalKey);
    REQUIRE(restored.find(*id)->metadata.at("algorithmRevision").asString() == revision);
    const auto* restoredPlacements = restored.findPart(id->child("placements"));
    REQUIRE(restoredPlacements != nullptr);
    const auto& savedPoints = std::get<PointSet>(restoredPlacements->payload);
    REQUIRE(savedPoints.getCount() == placementData.getCount());
    for (int i = 0; i < savedPoints.getCount(); ++i) REQUIRE(savedPoints.getY(i) == placementData.getY(i));
    auto legacyKey = BuildKey::fromCanonical("procgen/v1/mesh.roadNetwork/0123456789abcdef");
    REQUIRE(legacyKey.has_value());
    REQUIRE(restored.findByBuildKey(*legacyKey).empty());
    auto legacyState = state.value();
    auto legacyText  = legacyState.toJson();
    REQUIRE(legacyText.ok());
    auto text = legacyText.value();
    for (std::size_t at = 0; (at = text.find(originalKey.format(), at)) != std::string::npos;) {
        text.replace(at, originalKey.format().size(), legacyKey->format());
        at += legacyKey->format().size();
    }
    auto legacyParsed = eve::Value::fromJson(text);
    REQUIRE(legacyParsed.ok());
    ArtifactStore legacyStore;
    REQUIRE(legacyStore.restoreState(legacyParsed.value()).ok());
    REQUIRE(legacyStore.findByBuildKey(*legacyKey).size() == 1u);
    REQUIRE(legacyStore.findByBuildKey(originalKey).empty());
    REQUIRE(restored.findPart(id->child("terrain/surface-weight")) != nullptr);
    const auto* restoredMesh = restored.findPart(id->child("mesh"));
    REQUIRE(restoredMesh != nullptr);
    REQUIRE(std::get<MeshData>(restoredMesh->payload).positions() == bake.value().mesh.positions());
    style.laneWidth = 5.f;
    REQUIRE(network.setEdgeStyle(1, style).ok());
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    auto changed = generateRoadNetworkArtifact(params, *id);
    REQUIRE(changed.ok());
    REQUIRE(changed.value().buildKey != originalKey);
    auto count = store.partCount();
    REQUIRE(!store.publish(std::move(changed).takeValue()).ok());
    REQUIRE(store.partCount() == count);
    REQUIRE(store.find(*id)->buildKey == originalKey);
    params.setString("networkSnapshot", "{}");
    REQUIRE(!generateRoadNetworkArtifact(params, *id).ok());
    REQUIRE(store.find(*id)->buildKey == originalKey);
}
