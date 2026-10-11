#include "asset/procgen/TerrainDetailCard.h"
#include "asset/procgen/TerrainMaterialAtlas.h"
#include "asset/procgen/TerrainVegetationGpu.h"
#include "asset/procgen/TerrainVegetationRuntime.h"

#include "procgen/PointGraph.h"
#include "procgen/SpatialData.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

using namespace eve::asset_procgen;

namespace {
class Resolver final : public ITerrainVegetationGpuResolver {
public:
    eve::Result<TerrainVegetationGpuPrototype> resolve(const TerrainVegetationGpuPrototypeRequest& request) override {
        ++calls;
        seenMetadata = seenMetadata || request.detail != nullptr;
        if (request.prototype == "missing")
            return eve::Result<TerrainVegetationGpuPrototype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing prototype"));
        if (request.prototype == "tree") {
            TerrainVegetationGpuPrototype result;
            std::array<float, 16> first{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            auto second = first;
            second[12] = 2.f;
            result.parts.push_back({first, 20, 30, 7u, 3u});
            result.parts.push_back({second, 21, 31, 7u, 3u});
            return eve::Result<TerrainVegetationGpuPrototype>::success(std::move(result));
        }
        return eve::Result<TerrainVegetationGpuPrototype>::success({calls, calls + 10, 7u, 3u});
    }
    eve::Result<void> drawShadow(const TerrainVegetationGpuPrototypeRequest&,
                                 const std::array<float, 16>&,
                                 const std::array<float, 16>&) override {
        ++shadowCalls;
        return eve::Result<void>::success();
    }
    std::uint32_t calls        = 0;
    std::uint32_t shadowCalls  = 0;
    bool          seenMetadata = false;
};
}  // namespace

TEST_CASE("asset.procgen.terrainDetailCardsPreserveBillboardAndCrossedGrassSemantics") {
    RuntimeInstancePrototype billboard{"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "GrassBillboard", false,
                                       true};
    auto                     billboardGeometry = buildTerrainDetailCard(billboard);
    REQUIRE(billboardGeometry.ok());
    CHECK(billboardGeometry.value().cameraFacing);
    CHECK_EQ(billboardGeometry.value().positions.size(), std::size_t(12));
    CHECK_EQ(billboardGeometry.value().indices.size(), std::size_t(6));
    CHECK_EQ(billboardGeometry.value().positions[1], 0.f);
    CHECK_EQ(billboardGeometry.value().positions[7], 1.f);

    RuntimeInstancePrototype grass = billboard;
    grass.renderMode               = "Grass";
    auto crossed                   = buildTerrainDetailCard(grass);
    REQUIRE(crossed.ok());
    CHECK(!crossed.value().cameraFacing);
    CHECK_EQ(crossed.value().positions.size(), std::size_t(24));
    CHECK_EQ(crossed.value().indices.size(), std::size_t(12));

    RuntimeInstancePrototype mesh{"unity-guid:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "VertexLit", true, true};
    auto                     unsupported = buildTerrainDetailCard(mesh);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(unsupported.error()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("asset.procgen.terrainVegetationRealizesGraphAndBakedInstancesIntoBuckets") {
    eve::procgen::Heightmap heightmap(3, 3);
    auto                    spatial    = eve::procgen::SpatialData::heightfield(heightmap, 0.f, 0.f, 1.f, 1.f);
    auto                    graphAsset = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(graphAsset.ok());
    LoadedPointGraph loaded{std::move(graphAsset).takeValue(), std::make_unique<eve::procgen::PointGraph>(), "tag", {}};
    REQUIRE(loaded.graph->addNode("sample", "spatial.sample"));
    REQUIRE(loaded.graph->setNodeSpatial("sample", &spatial));
    REQUIRE(loaded.graph->setNodeFloat("sample", "spacing", 1.f));
    REQUIRE(loaded.graph->addNode("tag", "attribute.set.string"));
    REQUIRE(loaded.graph->connect("sample", "tag", 0));
    REQUIRE(loaded.graph->setNodeString("tag", "attribute", "prototype"));
    REQUIRE(loaded.graph->setNodeString("tag", "value", "grass-card"));

    auto instanceAsset = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440001");
    REQUIRE(instanceAsset.ok());
    LoadedInstanceSet baked{std::move(instanceAsset).takeValue(), {}, {}, {}};
    baked.wavingGrassAmount = .4f;
    baked.wavingGrassSpeed = .6f;
    baked.wavingGrassStrength = .8f;
    baked.wavingGrassTint = {.3f, .7f, .4f, 1.f};
    baked.prototypes.push_back({"oak", "VertexLit", true, true});
    baked.instances.push_back({"oak", {3.f, 2.f, 1.f}, {0.f, 0.f, 0.f, 1.f}, {2.f, 3.f, 2.f}});
    auto realized = realizeTerrainVegetation(&loaded, &baked);
    REQUIRE(realized.ok());
    CHECK(realized.value().generatedCount > 0);
    CHECK_EQ(realized.value().explicitCount, 1u);
    CHECK_EQ(realized.value().wavingGrassAmount, .4f);
    CHECK_EQ(realized.value().wavingGrassSpeed, .6f);
    CHECK_EQ(realized.value().wavingGrassStrength, .8f);
    CHECK_EQ(realized.value().wavingGrassTint[1], .7f);
    REQUIRE_EQ(realized.value().prototypes.size(), std::size_t(1));
    REQUIRE_EQ(realized.value().buckets.size(), std::size_t(2));
    CHECK_EQ(realized.value().buckets[0].prototype, std::string("grass-card"));
    CHECK_EQ(realized.value().buckets[1].prototype, std::string("oak"));
    CHECK(realized.value().graphNodesEvaluated > 0);
    CHECK(realized.value().graphMilliseconds >= 0.0);
    for (const auto& instance : realized.value().instances) CHECK(instance.id != 0);
}

TEST_CASE("asset.procgen.terrainVegetationSupportsOptionalProvidersAndRejectsBudgets") {
    auto instanceAsset = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440001");
    REQUIRE(instanceAsset.ok());
    LoadedInstanceSet baked{std::move(instanceAsset).takeValue(), {}, {}, {}};
    baked.instances.push_back({"tree", {}, {0.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}});
    auto explicitOnly = realizeTerrainVegetation(nullptr, &baked);
    REQUIRE(explicitOnly.ok());
    CHECK_EQ(explicitOnly.value().explicitCount, 1u);

    TerrainVegetationLimits limits;
    limits.maximumTotalInstances = 1;
    baked.instances.push_back({"tree", {}, {0.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}});
    auto overBudget = realizeTerrainVegetation(nullptr, &baked, limits);
    REQUIRE(!overBudget.ok());
    CHECK_EQ(overBudget.error()->code(), eve::DiagnosticCode::InvalidArgument);

    auto absent = realizeTerrainVegetation(nullptr, nullptr);
    REQUIRE(!absent.ok());
    CHECK_EQ(absent.error()->code(), eve::DiagnosticCode::NotFound);
}

TEST_CASE("asset.procgen.terrainVegetationGpuPlanResolvesBucketsAtomically") {
    TerrainVegetationRealization realization;
    realization.instances = {{1, "grass", {}, {1.f, 2.f, 3.f}, {0.f, 0.f, 0.f, 1.f}, {2.f, 2.f, 2.f}},
                             {2, "tree", {}, {4.f, 5.f, 6.f}, {0.f, 0.f, 0.f, 1.f}, {1.f, 2.f, 1.f}}};
    realization.buckets   = {{"grass", 0, 1}, {"tree", 1, 1}};
    realization.prototypes.push_back({"grass", "GrassBillboard", false, true});
    realization.prototypes.back().useInstancing = false;
    realization.prototypes.back().minHeight = 1.f;
    realization.prototypes.back().maxHeight = 3.f;
    realization.prototypes.back().dryColor = {0.6f, 0.4f, 0.2f, 1.f};
    realization.prototypes.back().healthyColor = {0.2f, 0.8f, 0.4f, 1.f};
    realization.wavingGrassAmount              = .4f;
    realization.wavingGrassSpeed               = .6f;
    realization.wavingGrassStrength            = .8f;
    realization.wavingGrassTint                = {.3f, .7f, .4f, 1.f};
    Resolver resolver;
    auto     plan = buildTerrainVegetationGpuPlan(realization, resolver, {2.5});
    REQUIRE(plan.ok());
    CHECK(resolver.seenMetadata);
    CHECK_EQ(resolver.calls, 2u);
    REQUIRE_EQ(plan.value().instances.size(), std::size_t(3));
    CHECK_EQ(plan.value().instances[0].meshId, 1u);
    CHECK_EQ(plan.value().instances[1].materialId, 30u);
    CHECK_EQ(plan.value().instances[2].materialId, 31u);
    CHECK(std::abs(plan.value().instances[2].model[3][0] - 6.f) < 0.0001f);
    CHECK(std::abs(plan.value().instances[0].model[3][0] - 1.f) < 0.0001f);
    CHECK(std::abs(plan.value().instances[0].color.r - .4f) < 0.0001f);
    CHECK(std::abs(plan.value().instances[0].color.g - .6f) < 0.0001f);
    CHECK(std::abs(plan.value().instances[0].terrainWave.x - 1.5f) < 0.0001f);
    CHECK_EQ(plan.value().instances[0].terrainWave.y, .4f);
    CHECK_EQ(plan.value().instances[0].terrainWave.z, .8f);
    CHECK_EQ(plan.value().instances[0].terrainWave.w, 1.f);
    CHECK_EQ(plan.value().instances[0].terrainWaveTint.g, .7f);
    CHECK_EQ(plan.value().instances[1].terrainWave.w, 0.f);
    CHECK_EQ(plan.value().instances[2].terrainWave.w, 0.f);

    auto invalidFrame =
        buildTerrainVegetationGpuPlan(realization, resolver, {std::numeric_limits<double>::quiet_NaN()});
    REQUIRE(!invalidFrame.ok());
    CHECK_EQ(invalidFrame.error()->code(), eve::DiagnosticCode::InvalidArgument);

    realization.instances[1].prototype = "missing";
    auto rejected                      = buildTerrainVegetationGpuPlan(realization, resolver);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::Conflict);
}

TEST_CASE("asset.procgen.terrainVegetationRendererOwnsFrameStateAndRejectsInvalidTime") {
    TerrainVegetationRealization realization;
    Resolver                     resolver;
    TerrainVegetationRenderer   renderer(std::move(realization), resolver);

    CHECK(renderer.lastSubmissionError() == nullptr);
    CHECK(renderer.setFrame({12.5}).ok());
    auto invalid = renderer.setFrame({std::numeric_limits<double>::infinity()});
    REQUIRE(!invalid.ok());
    CHECK_EQ(invalid.error()->code(), eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE("asset.procgen.terrainVegetationControlWeightsCullDeterministicallyAndFlipV") {
    TerrainVegetationRealization source;
    source.generatedCount = 3;
    source.explicitCount  = 1;
    source.instances      = {{1, "grass", "Grass", {0.f, 0.f, 0.f}},
                             {2, "grass", "Grass", {1.f, 0.f, 1.f}},
                             {3, "flower", "Flower", {0.f, 0.f, 1.f}},
                             {4, "tree", {}, {1.f, 0.f, 0.f}}};
    source.buckets        = {{"flower", 0, 1}, {"grass", 1, 2}, {"tree", 3, 1}};

    auto materialAsset = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440002");
    REQUIRE(materialAsset.ok());
    LoadedTerrainMaterial material{std::move(materialAsset).takeValue()};
    material.layers.resize(2);
    material.layers[0].name = "Grass";
    material.layers[1].name = "Flower";
    TerrainMaterialAtlases    atlases;
    TerrainMaterialAtlasGroup group;
    group.firstLayer = 0;
    group.layerCount = 2;
    group.control    = {2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0}};
    atlases.groups.push_back(std::move(group));

    auto normal = filterTerrainVegetationByLayerWeights(source, material, atlases, {0, 0, 1, 1, false});
    REQUIRE(normal.ok());
    CHECK_EQ(normal.value().instances.size(), std::size_t(3));
    CHECK_EQ(normal.value().weightCulledCount, 1u);
    CHECK_EQ(normal.value().explicitCount, 1u);
    auto flipped = filterTerrainVegetationByLayerWeights(source, material, atlases, {0, 0, 1, 1, true});
    REQUIRE(flipped.ok());
    CHECK_EQ(flipped.value().instances.size(), std::size_t(2));
    CHECK_EQ(flipped.value().weightCulledCount, 2u);
}
