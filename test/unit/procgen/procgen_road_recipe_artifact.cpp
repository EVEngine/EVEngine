#include "common/Capability.h"
#include "common/ProcgenWorldQuery.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
class TerrainProbe final : public eve::IProcgenWorldQuery {
public:
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float, float, std::uint64_t) override {
        eve::ProcgenSurfaceHit hit;
        hit.hit = true;
        hit.x   = x;
        hit.z   = z;
        hit.y   = -20.f;
        return eve::Result<eve::ProcgenSurfaceHit>::success(hit);
    }
};

struct ScopedTerrain {
    explicit ScopedTerrain(TerrainProbe& probe) : probe(probe), previous(eve::cap::query<eve::IProcgenWorldQuery>()) {
        eve::cap::provide<eve::IProcgenWorldQuery>(&probe);
    }
    ~ScopedTerrain() { eve::cap::provide<eve::IProcgenWorldQuery>(previous); }
    TerrainProbe&            probe;
    eve::IProcgenWorldQuery* previous;
};

Params authoredRecipe() {
    auto made = RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(made.ok());
    auto       network             = std::move(made).takeValue();
    const auto edgeId              = network.edges().front().id;
    auto       style               = network.edges().front().style;
    style.laneWidth                = 4.2f;
    style.sideTerrain.mode         = RoadSideTerrainMode::Average;
    style.samplingSpacingMeters    = 1.f;
    style.samplingPolicy           = RoadSamplingPolicy::ParametricDistanceTurn;
    style.crossSectionSubdivisions = 3;
    style.structures               = {{.1f, .3f, RoadStructureKind::Bridge}, {.6f, .9f, RoadStructureKind::Tunnel}};
    REQUIRE(network.setEdgeStyle(edgeId, style).ok());
    auto markers = network.edges().front().controlPoints;
    for (auto& marker : markers) {
        marker.rollDegrees      = 12.f;
        marker.rollCenterOffset = -.3f;
    }
    markers.front().rollDegrees = 270.f;
    REQUIRE(network.setEdgeControlPoints(edgeId, markers).ok());
    Params params;
    params.setString("scene", "ignored-when-networkSnapshot-is-set");
    params.setString("placementWorldRevision", "terrain/v1");
    params.setString("algorithmRevision", "caller-value-must-not-win");
    params.setInt("pathSegments", 32);
    params.setInt("lodCount", 3);
    params.setFloat("chunkSize", 16.f);
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    return params;
}

ArtifactId recipeId() {
    auto parsed = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd191");
    REQUIRE(parsed.has_value());
    return *parsed;
}
}  // namespace

TEST_CASE("procgen.road.artifact.spec.generation") {
    TerrainProbe  probe;
    ScopedTerrain registration(probe);
    auto          params    = authoredRecipe();
    auto          id        = recipeId();
    auto          generated = generateRoadNetworkArtifact(params, id);
    REQUIRE(generated.ok());
    auto* generatedView = &generated.value();
    auto* parts         = std::get_if<CompositeArtifact>(&generated.value().payload);
    REQUIRE(parts != nullptr);

    SUB_CASE("parent key is canonical, revision is producer-owned, and required roles exist") {
        auto expected = BuildKey::forRecipe("mesh.roadNetwork", params);
        REQUIRE(expected.has_value());
        REQUIRE(generatedView->buildKey == *expected);
        REQUIRE(expected->format().starts_with("procgen/v2/mesh.roadNetwork/"));
        auto revision = generatedView->metadata.at("algorithmRevision").asString();
        REQUIRE(!revision.empty());
        REQUIRE(revision != params.getString("algorithmRevision", ""));
        REQUIRE(expected->format().find('/' + revision + '/') != std::string::npos);
        for (const char* role : {"mesh", "collider", "collider/drivable", "topology", "terrain/surface-weight",
                                 "navigation/lane/0", "placements"})
            REQUIRE(parts->find(role) != nullptr);
        for (const auto& part : parts->children) {
            if (part.role.starts_with("render/chunk/"))
                REQUIRE(part.buildKey.format().starts_with("eve.procgen.road.chunk.v2/" + revision + '/'));
            else
                REQUIRE(part.buildKey.format().starts_with(expected->format() + '/'));
        }
    };
    SUB_CASE("base mesh and each LOD chunk represent the same recipe geometry") {
        auto base = bakeRoadNetworkRecipe(params);
        REQUIRE(base.ok());
        const auto* mesh = parts->find("mesh");
        REQUIRE(mesh);
        REQUIRE(std::get<MeshData>(mesh->payload).positions() == base.value().mesh.positions());
        REQUIRE(std::get<MeshData>(mesh->payload).indices() == base.value().mesh.indices());
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
            for (const auto& part : parts->children)
                if (part.role.starts_with("render/chunk/") && part.role.ends_with(suffix)) {
                    REQUIRE(part.type == ArtifactType::MeshData);
                    indices += std::get<MeshData>(part.payload).indices().size();
                    ++chunks;
                }
            REQUIRE(chunks > 0);
            REQUIRE(indices == expected.value().mesh.indices().size());
        }
    };
    SUB_CASE("placements use the world provider and key dependencies are precise") {
        const auto* placements = parts->find("placements");
        REQUIRE(placements);
        const auto& points  = std::get<PointSet>(placements->payload);
        bool        snapped = false;
        for (const auto& point : points.points())
            if (point.y == -20.f) snapped = true;
        REQUIRE(snapped);
        Params changedWorld = params;
        changedWorld.setString("placementWorldRevision", "terrain/v2");
        auto newKey = BuildKey::forRecipe("mesh.roadNetwork", changedWorld);
        REQUIRE(newKey.has_value());
        REQUIRE(*newKey != generatedView->buildKey);
        Params diagnosticChange = params;
        diagnosticChange.setString("authorLabel", "same-geometry");
        auto sameGeometry = generateRoadNetworkArtifact(diagnosticChange, id);
        REQUIRE(sameGeometry.ok());
        REQUIRE(sameGeometry.value().buildKey != generatedView->buildKey);
        const auto& equivalent = std::get<CompositeArtifact>(sameGeometry.value().payload);
        for (const auto& part : parts->children) {
            if (!part.role.starts_with("render/chunk/")) continue;
            auto* other = equivalent.find(part.role);
            REQUIRE(other);
            REQUIRE(other->buildKey == part.buildKey);
        }
    };
}

TEST_CASE("procgen.road.artifact.spec.storeAndInvalidInput") {
    TerrainProbe  probe;
    ScopedTerrain registration(probe);
    auto          params    = authoredRecipe();
    auto          id        = recipeId();
    auto          generated = generateRoadNetworkArtifact(params, id);
    REQUIRE(generated.ok());
    const auto    originalKey = generated.value().buildKey;
    ArtifactStore store;
    auto*         storeView = &store;
    REQUIRE(storeView->publish(std::move(generated).takeValue()).ok());
    REQUIRE(storeView->find(id) != nullptr);
    REQUIRE(storeView->findByBuildKey(originalKey).size() == 1);
    REQUIRE(storeView->findPart(id.child("collider/drivable")) != nullptr);

    SUB_CASE("snapshot round trip preserves parts, revision, and point positions") {
        auto state = storeView->snapshotState();
        REQUIRE(state.ok());
        auto text = state.value().toJson();
        REQUIRE(text.ok());
        auto parsed = eve::Value::fromJson(text.value());
        REQUIRE(parsed.ok());
        ArtifactStore restored;
        REQUIRE(restored.restoreState(parsed.value()).ok());
        REQUIRE(restored.partCount() == storeView->partCount());
        REQUIRE(restored.find(id)->buildKey == originalKey);
        const auto* before = storeView->findPart(id.child("placements"));
        const auto* after  = restored.findPart(id.child("placements"));
        REQUIRE(before != nullptr);
        REQUIRE(after != nullptr);
        const auto& first  = std::get<PointSet>(before->payload);
        const auto& second = std::get<PointSet>(after->payload);
        REQUIRE(first.getCount() == second.getCount());
        for (int i = 0; i < first.getCount(); ++i) REQUIRE(first.getY(i) == second.getY(i));
    };
    SUB_CASE("duplicate id publication fails without replacing an existing artifact") {
        auto changed = params;
        changed.setString("authorLabel", "new-parent-key");
        auto replacement = generateRoadNetworkArtifact(changed, id);
        REQUIRE(replacement.ok());
        REQUIRE(replacement.value().buildKey != originalKey);
        const auto count = storeView->partCount();
        REQUIRE(!storeView->publish(std::move(replacement).takeValue()).ok());
        REQUIRE(storeView->partCount() == count);
        REQUIRE(storeView->find(id)->buildKey == originalKey);
    };
    SUB_CASE("bad embedded network snapshot fails without altering the published store") {
        params.setString("networkSnapshot", "{}");
        REQUIRE(!generateRoadNetworkArtifact(params, id).ok());
        REQUIRE(storeView->find(id)->buildKey == originalKey);
    };
}
