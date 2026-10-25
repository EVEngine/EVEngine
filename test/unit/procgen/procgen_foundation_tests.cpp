// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "TextureSampler.cpp",
    "procgen_result_contract.cpp",
    "procgen_rng_contract.cpp",
    "procgen.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_TextureSampler)
#include <cmath>
#include <cstdint>
#include <string>
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/TextureSampler.h"
#include "zeroerr/unittest.h"

namespace test_TextureSampler_unit {
using namespace eve::graphics;

TEST_CASE("graphics.textureSampler.sizePresetsAndParsingContract") {
    SUB_CASE("mip count includes the base level and uses the longest dimension") {
        REQUIRE_EQ(mipmapCountForSize(0, 0), 1);
        CHECK_EQ(mipmapCountForSize(1, 1), 1);
        CHECK_EQ(mipmapCountForSize(2, 2), 2);
        CHECK_EQ(mipmapCountForSize(256, 128), 9);
        CHECK_EQ(mipmapCountForSize(64, 64), 7);
    };
    SUB_CASE("presets expose their backend independent requested state") {
        const auto nearest = TextureSampler::nearest();
        CHECK(nearest.min == FilterMode::Nearest);
        CHECK(nearest.mag == FilterMode::Nearest);
        CHECK(nearest.mipmap == MipmapMode::Disabled);
        const auto trilinear = TextureSampler::linearMipmap();
        CHECK(trilinear.min == FilterMode::Linear);
        CHECK(trilinear.mag == FilterMode::Linear);
        CHECK(trilinear.mipmap == MipmapMode::Linear);
        CHECK_EQ(TextureSampler::anisotropic(8.f).maxAnisotropy, 8.f);
        CHECK_EQ(TextureSampler::linear().maxAnisotropy, 1.f);
        CHECK_EQ(std::string(TextureSampler::filterName(FilterMode::Nearest)), "nearest");
        CHECK_EQ(std::string(TextureSampler::mipmapName(MipmapMode::Disabled)), "none");
        CHECK_EQ(std::string(TextureSampler::mipmapName(MipmapMode::Linear)), "linear");
    };
    SUB_CASE("known spellings parse; unknown values use the documented fallback") {
        CHECK(TextureSampler::parseFilter("nearest") == FilterMode::Nearest);
        CHECK(TextureSampler::parseFilter("NEAREST") == FilterMode::Nearest);
        CHECK(TextureSampler::parseFilter("unknown") == FilterMode::Linear);
        CHECK(TextureSampler::parseMipmap("") == MipmapMode::Disabled);
        CHECK(TextureSampler::parseMipmap("none") == MipmapMode::Disabled);
        CHECK(TextureSampler::parseMipmap("Nearest") == MipmapMode::Nearest);
        CHECK(TextureSampler::parseMipmap("linear") == MipmapMode::Linear);
        CHECK(TextureSampler::parseMipmap("unknown") == MipmapMode::Linear);
    };
}

TEST_CASE("graphics.renderable.meshLod_distanceFadeAndClearContract") {
    auto* renderable = Renderable3D::create();
    REQUIRE(renderable != nullptr);
    // These inert addresses are identity tokens; the selector never dereferences them.
    auto* high = reinterpret_cast<Mesh*>(uintptr_t(0x1000));
    auto* mid  = reinterpret_cast<Mesh*>(uintptr_t(0x2000));
    auto* low  = reinterpret_cast<Mesh*>(uintptr_t(0x3000));
    renderable->setMeshLod(0, high);
    renderable->setMeshLod(1, mid, 20.f);
    renderable->setMeshLod(2, low, 50.f);

    SUB_CASE("distance interval [0,20) selects high; [20,50) mid; [50,cull) low") {
        const struct {
            float distance;
            int   level;
            Mesh* mesh;
        } samples[] = {
            {0.f, 0, high}, {19.9f, 0, high}, {20.f, 1, mid}, {49.9f, 1, mid}, {50.f, 2, low}, {99.9f, 2, low},
        };
        renderable->setMeshLodCullDistance(100.f);
        for (const auto& sample : samples) {
            CHECK_EQ(renderable->getMeshLodLevelAtDistance(sample.distance), sample.level);
            CHECK(renderable->meshRenderer()->meshForDistance(sample.distance) == sample.mesh);
        }
        CHECK_EQ(renderable->getMeshLodLevelAtDistance(100.f), -1);
        CHECK(renderable->meshRenderer()->meshForDistance(100.f) == nullptr);
    };

    SUB_CASE("fade policy produces a second level and continuous weight at boundary") {
        REQUIRE(renderable->setMeshLodFadeWidth(0, .25f).ok());
        CHECK_EQ(renderable->getMeshLodSecondaryLevelAtDistance(17.5f), 1);
        CHECK(std::abs(renderable->getMeshLodSecondaryWeightAtDistance(17.5f) - .5f) < .0001f);
        CHECK(!renderable->setMeshLodFadeWidth(0, 1.1f).ok());
        CHECK(!renderable->setMeshLodFadePolicy(0, true, .5f).ok());
    };

    SUB_CASE("temporal transition reaches target level and rejects negative distance") {
        REQUIRE(renderable->setMeshLodFadePolicy(2, true, 1.f).ok());
        REQUIRE(renderable->advanceMeshLodTransition(5.f, 0.f).ok());
        REQUIRE(renderable->advanceMeshLodTransition(25.f, 0.f).ok());
        CHECK_EQ(renderable->getMeshLodSecondaryLevelAtDistance(25.f), 1);
        REQUIRE(renderable->advanceMeshLodTransition(25.f, .25f).ok());
        CHECK(std::abs(renderable->getMeshLodSecondaryWeightAtDistance(25.f) - .25f) < .0001f);
        REQUIRE(renderable->advanceMeshLodTransition(25.f, .75f).ok());
        CHECK_EQ(renderable->getMeshLodSecondaryLevelAtDistance(25.f), -1);
        CHECK_EQ(renderable->getMeshLodLevelAtDistance(25.f), 1);
        CHECK(!renderable->advanceMeshLodTransition(-1.f, .1f).ok());
    };

    SUB_CASE("clear removes LODs but preserves the primary mesh") {
        renderable->clearMeshLod();
        CHECK_EQ(renderable->getMeshLodCount(), 0);
        CHECK(renderable->meshRenderer()->meshForDistance(80.f) == high);
    };
}
}  // namespace test_TextureSampler_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_result_contract)
#include <memory>
#include "procgen/PointGraph.h"
#include "zeroerr/unittest.h"

namespace test_procgen_result_contract_unit {
using namespace eve;
using namespace eve::procgen;

namespace {
void configure(PointGraph& graph) {
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.addNode("out", "transform"));
    REQUIRE(graph.connect("source", "out"));
    REQUIRE(graph.setComputePolicy("cpu"));
    PointSet points;
    points.add(1.f, 0.f, 2.f);
    REQUIRE(graph.setNodePoints("source", &points));
}
template <class T>
void requireFailure(const Result<T>& result, DiagnosticCode code, const std::string& path) {
    REQUIRE(!result.ok());
    const auto* diagnostic = result.status().primaryDiagnostic();
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->code() == code);
    REQUIRE_EQ(diagnostic->path(), path);
}
}  // namespace

TEST_CASE("procgen.pointGraph.executeResult_returnsOwningOutputAndPreservesPriorDiagnostic") {
    PointGraph graph;
    configure(graph);
    auto missing = graph.executeResult("missing");
    requireFailure(missing, DiagnosticCode::NotFound, "missing");
    auto output = graph.executeResult("out");
    REQUIRE(output.ok());
    auto points = std::move(output).takeValue();
    REQUIRE_EQ(points.getCount(), 1);
    REQUIRE(graph.removeNode("source"));
    auto invalid = graph.executeResult("out");
    requireFailure(invalid, DiagnosticCode::InvalidArgument, "out");
    requireFailure(missing, DiagnosticCode::NotFound, "missing");
    REQUIRE_EQ(points.getX(0), 1.f);
    std::unique_ptr<PointSet> legacy(graph.execute("out"));
    REQUIRE(!legacy);
    REQUIRE(!graph.getError().empty());
}

TEST_CASE("procgen.pointGraph.cancellationAndNodeBudget_reportCancelledWithoutCorruptingGraph") {
    PointGraph graph;
    configure(graph);
    auto valid = graph.validateResult();
    REQUIRE(valid.ok());
    graph.requestCancel();
    auto cancelled = graph.executeResult("out");
    requireFailure(cancelled, DiagnosticCode::Cancelled, "out");
    graph.resetCancellation();
    graph.setExecutionNodeBudget(1);
    auto budget = graph.executeResult("out");
    requireFailure(budget, DiagnosticCode::Cancelled, "out");
    graph.setExecutionNodeBudget(0);
    REQUIRE(graph.executeResult("out").ok());
}

TEST_CASE("procgen.pointGraph.cycles_areRejectedByValidationAndExecution") {
    PointGraph graph;
    configure(graph);
    REQUIRE(graph.addNode("loop", "transform"));
    REQUIRE(graph.connect("out", "loop"));
    REQUIRE(graph.connect("loop", "out"));
    auto cycle = graph.executeResult("out");
    requireFailure(cycle, DiagnosticCode::Conflict, "out");
    auto invalid = graph.validateResult();
    requireFailure(invalid, DiagnosticCode::Conflict, "out");
}

TEST_CASE("procgen.pointGraph.unsetInputAndPointBudget_reportSourcePathThenRecover") {
    PointGraph graph;
    REQUIRE(graph.addNode("source", "input"));
    auto unset = graph.executeResult("source");
    requireFailure(unset, DiagnosticCode::InvalidArgument, "source");
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    points.add(2.f, 0.f, 0.f);
    REQUIRE(graph.setNodePoints("source", &points));
    graph.setMaxNodeOutputPoints(1);
    auto budget = graph.executeResult("source");
    requireFailure(budget, DiagnosticCode::InvalidArgument, "source");
    graph.setMaxNodeOutputPoints(0);
    auto recovered = graph.executeResult("source");
    REQUIRE(recovered.ok());
    REQUIRE_EQ(recovered.value().getCount(), 2);
}


TEST_CASE("procgen.pointGraph.nestedSubgraphFailure_preservesFullNodePath") {
    PointGraph nested;
    configure(nested);
    REQUIRE(nested.addNode("math", "density.remap"));
    REQUIRE(nested.connect("out", "math"));
    REQUIRE(nested.setNodeFloat("math", "inputMax", 0.f));
    PointGraph graph;
    configure(graph);
    REQUIRE(graph.addNode("nested", "subgraph"));
    REQUIRE(graph.connect("out", "nested"));
    REQUIRE(graph.setNodeSubgraph("nested", &nested, "source", "math"));
    auto failed = graph.executeResult("nested");
    requireFailure(failed, DiagnosticCode::InvalidArgument, "nested/math");
}
}  // namespace test_procgen_result_contract_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_rng_contract)
#include <DTL/Shape/CellularAutomatonIsland.hpp>
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "zeroerr/unittest.h"

namespace test_procgen_rng_contract_unit {
using namespace eve::procgen;

namespace {
bool sameCells(const Grid2D& a, const Grid2D& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getCell(x, y) != b.getCell(x, y)) return false;
    return true;
}
}  // namespace

TEST_CASE("procgen.rng.cellularSeed_matchesSpecifiedLegacyCellsAcrossFillBoundaries") {
    auto& registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    for (uint32_t seed : {0u, 1u, 42u, 1234567u}) {
        for (float fill : {0.f, 0.25f, 0.45f, 1.f}) {
            Params params;
            params.setSeed(seed);
            params.setSize(31, 24);
            params.setInt("loops", 5);
            params.setFloat("fill", fill);
            std::vector<std::vector<std::uint_fast8_t>> matrix(24, std::vector<std::uint_fast8_t>(31));
            // Test-only legacy oracle; production must not touch this RNG.
            DTL_RANDOM_ENGINE.seed(params.getSeed());
            DTL_RANDOM_ENGINE.clear();
            dtl::shape::CellularAutomatonIsland<std::uint_fast8_t> legacy(1, 0, 5, double(fill));
            REQUIRE(legacy.draw(matrix));
            Grid2D expected;
            expected.resize(31, 24);
            for (int y = 0; y < 24; ++y)
                for (int x = 0; x < 31; ++x)
                    expected.setCell(x, y, matrix[size_t(y)][size_t(x)] == 1 ? Semantic::Floor : Semantic::Wall);
            Grid2D      actual;
            std::string error;
            REQUIRE(registry.generate("cave.cellular", params, actual, error));
            CHECK(sameCells(expected, actual));
        }
    }
}

TEST_CASE("procgen.rng.cellularGeneration_usesOnlyExplicitSeed") {
    SUB_CASE("does not consume global implicit stream") {
        auto& registry = GeneratorRegistry::instance();
        registry.registerBuiltins();
        DTL_RANDOM_ENGINE.seed(77u);
        DTL_RANDOM_ENGINE.clear();
        const auto expected = DTL_RANDOM_ENGINE.get();
        DTL_RANDOM_ENGINE.seed(77u);
        DTL_RANDOM_ENGINE.clear();
        Params params;
        params.setSeed(42);
        params.setSize(32, 24);
        Grid2D      actual;
        std::string error;
        REQUIRE(registry.generate("cave.cellular", params, actual, error));
        CHECK(DTL_RANDOM_ENGINE.get() == expected);
    };
    SUB_CASE("same seed replays independent of intervening generation") {
        auto& registry = GeneratorRegistry::instance();
        registry.registerBuiltins();
        Params params;
        params.setSize(31, 24);
        params.setSeed(17);
        Grid2D      first;
        Grid2D      intervening;
        Grid2D      replay;
        std::string error;
        REQUIRE(registry.generate("cave.cellular", params, first, error));
        params.setSeed(29);
        REQUIRE(registry.generate("cave.cellular", params, intervening, error));
        params.setSeed(17);
        REQUIRE(registry.generate("cave.cellular", params, replay, error));
        CHECK(sameCells(first, replay));
    };
}
}  // namespace test_procgen_rng_contract_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen)
#include "image/ImageData.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/JsonExport.h"
#include "procgen/MeshBuild.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/CastleMesh.h"
#include "procgen/algorithms/HexTerrain.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainPipeline.h"
#include "procgen/heightmap/TerrainStreaming.h"
#include "procgen/texture/ColorRamp.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace test_procgen_unit {
using namespace eve::procgen;

namespace {
bool sameCells(const Grid2D& lhs, const Grid2D& rhs) {
    return lhs.getWidth() == rhs.getWidth() && lhs.getHeight() == rhs.getHeight() && lhs.cells() == rhs.cells() &&
           lhs.detail() == rhs.detail();
}

int countCells(const Grid2D& grid, uint32_t kind) {
    return static_cast<int>(std::count(grid.cells().begin(), grid.cells().end(), kind));
}

bool meshIndicesInRange(const MeshBuild& mesh) {
    for (int index = 0; index < mesh.getIndexCount(); ++index) {
        if (mesh.getIndex(index) >= mesh.getVertexCount()) return false;
    }
    return true;
}
}  // namespace

TEST_CASE("procgen.params.dimensionTypedGetterAndCanonicalKeyContract") {
    SUB_CASE("integer dimension keys own seed and size") {
        Params params;
        REQUIRE_EQ(params.getSeed(), 1u);
        CHECK_EQ(params.getWidth(), 32);
        CHECK_EQ(params.getHeight(), 32);
        params.setSize(0, -3);
        CHECK_EQ(params.getWidth(), 1);
        CHECK_EQ(params.getHeight(), 1);
        params.setInt("width", 48);
        params.setInt("height", 20);
        params.setInt("seed", 7);
        CHECK_EQ(params.getWidth(), 48);
        CHECK_EQ(params.getHeight(), 20);
        CHECK_EQ(params.getSeed(), 7u);
        params.setFloat("width", 2.5f);
        params.setString("seed", "19");
        CHECK_EQ(params.getWidth(), 48);
        CHECK_EQ(params.getSeed(), 7u);
        CHECK_EQ(params.getFloat("width", -1.f), 2.5f);
        CHECK_EQ(params.getString("seed", ""), "19");
    };
    SUB_CASE("typed getters do not parse text or fractional values") {
        Params params;
        params.setBool("enabled", true);
        params.setFloat("fraction", 1.5f);
        params.setString("numericText", "12");
        CHECK_EQ(params.getInt("enabled", -1), 1);
        CHECK_EQ(params.getFloat("enabled", -1.f), 1.f);
        CHECK(params.getBool("enabled", false));
        CHECK_EQ(params.getInt("fraction", -1), -1);
        CHECK_EQ(params.getInt("numericText", -1), -1);
        CHECK_EQ(params.getFloat("numericText", -1.f), -1.f);
        CHECK_EQ(params.getString("enabled", "fallback"), "fallback");
        CHECK_EQ(params.getString("numericText", "fallback"), "12");
        CHECK_EQ(params.getInt("missing", 17), 17);
    };
    SUB_CASE("canonical key ignores insertion order but preserves Value type") {
        Params first;
        first.setBool("a", true);
        first.setInt("b", 1);
        Params reordered;
        reordered.setInt("b", 1);
        reordered.setBool("a", true);
        CHECK_EQ(first.canonicalString(), reordered.canonicalString());
        reordered.setString("b", "1");
        CHECK(first.canonicalString() != reordered.canonicalString());
    };
}

TEST_CASE("procgen.grid.semanticAndDetailCells_areIndependentOfObjectMetadata") {
    Grid2D grid;
    grid.resize(3, 2);
    grid.fill(static_cast<int>(Semantic::Wall));
    grid.setCell(1, 0, static_cast<int>(Semantic::Floor));
    grid.setDetail(1, 0, 12);
    grid.setMeta("biome", "cave");
    grid.addObjectAt("spawn", "unit", 1.f, 0.f);
    CHECK_EQ(grid.getWidth(), 3);
    CHECK_EQ(grid.getHeight(), 2);
    REQUIRE_EQ(countCells(grid, Semantic::Wall), 5);
    CHECK_EQ(grid.getCell(1, 0), static_cast<int>(Semantic::Floor));
    CHECK_EQ(grid.getDetail(1, 0), 12);
    CHECK_EQ(grid.getMeta("biome", ""), "cave");
    CHECK_EQ(grid.getObjectCount(), 1);
    CHECK_EQ(grid.getObjectName(0), "spawn");
    grid.clearObjects();
    CHECK_EQ(grid.getObjectCount(), 0);
    CHECK_EQ(grid.getCell(1, 0), static_cast<int>(Semantic::Floor));
}

TEST_CASE("procgen.semantic.nameLookup_roundTripsKnownIdsAndDefaultsUnknown") {
    REQUIRE_EQ(std::string(semanticName(Semantic::Wall)), "wall");
    CHECK_EQ(semanticId("floor"), Semantic::Floor);
    CHECK_EQ(semanticId("unregistered"), Semantic::Empty);
}

TEST_CASE("procgen.registry.builtins_haveSchemasAndSortedIds") {
    auto& registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    registry.registerBuiltins();
    const auto ids = registry.list();
    REQUIRE(!ids.empty());
    CHECK(std::is_sorted(ids.begin(), ids.end()));
    for (const auto& id : ids) {
        const auto* schema = registry.descriptor(id);
        REQUIRE(schema != nullptr);
        CHECK_EQ(schema->id, id);
        CHECK(!schema->displayName.empty());
        CHECK(!schema->category.empty());
    }
    CHECK(registry.descriptor("not.registered") == nullptr);
    Params params;
    CHECK(!registry.applyDefaults("not.registered", params));
    REQUIRE(registry.applyDefaults("cave.cellular", params));
    CHECK(std::abs(params.getFloat("fill", 0.f) - .45f) < .0001f);
}

TEST_CASE("procgen.registry.gridRecipes_replaySameSeedWithExpectedShapeAndSemantics") {
    auto& registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    const struct {
        const char* id;
        int         width;
        int         height;
        bool        expectFloor;
    } recipes[] = {
        {"dungeon.bsp", 48, 36, true},    {"cave.cellular", 32, 24, true},  {"cave.drunkard", 40, 30, true},
        {"maze.backtrack", 21, 15, true}, {"noise.terrain", 32, 32, false},
    };
    for (const auto& recipe : recipes) {
        Params params;
        params.setSeed(42);
        params.setSize(recipe.width, recipe.height);
        Grid2D      first;
        Grid2D      replay;
        std::string error;
        REQUIRE(registry.generate(recipe.id, params, first, error));
        REQUIRE(registry.generate(recipe.id, params, replay, error));
        CHECK(sameCells(first, replay));
        CHECK_EQ(first.getWidth(), recipe.width);
        CHECK_EQ(first.getHeight(), recipe.height);
        if (recipe.expectFloor) CHECK(countCells(first, Semantic::Floor) + countCells(first, Semantic::Corridor) > 0);
    }
}

TEST_CASE("procgen.wfc.presetAndInvalidInputContract") {
    SUB_CASE("built in presets replay their seed and expose metadata") {
        auto& registry = GeneratorRegistry::instance();
        registry.registerBuiltins();
        for (const char* preset : {"dungeon", "cave", "terrain"}) {
            Params params;
            params.setSeed(17);
            params.setSize(24, 18);
            params.setString("preset", preset);
            params.setInt("maxAttempts", 64);
            Grid2D      first;
            Grid2D      replay;
            std::string error;
            REQUIRE(registry.generate("wfc.simple", params, first, error));
            REQUIRE(registry.generate("wfc.simple", params, replay, error));
            CHECK(sameCells(first, replay));
            CHECK_EQ(first.getMeta("algorithm", ""), "wfc.simple");
            CHECK_EQ(first.getMeta("preset", ""), preset);
            if (std::string(preset) != "terrain") {
                for (int x = 0; x < first.getWidth(); ++x) {
                    CHECK_EQ(first.getCell(x, 0), static_cast<int>(Semantic::Wall));
                    CHECK_EQ(first.getCell(x, first.getHeight() - 1), static_cast<int>(Semantic::Wall));
                }
            }
        }
    };
    SUB_CASE("invalid dimensions and preset return explanatory errors") {
        auto& registry = GeneratorRegistry::instance();
        registry.registerBuiltins();
        const struct {
            int         width;
            int         height;
            const char* preset;
            const char* errorToken;
        } samples[] = {
            {2, 2, "cave", "4x4"},
            {300, 16, "cave", "256"},
            {12, 12, "unknown", "preset"},
        };
        for (const auto& sample : samples) {
            Params params;
            params.setSize(sample.width, sample.height);
            params.setString("preset", sample.preset);
            Grid2D      grid;
            std::string error;
            CHECK(!registry.generate("wfc.simple", params, grid, error));
            CHECK(error.find(sample.errorToken) != std::string::npos);
        }
    };
}

TEST_CASE("procgen.meshBuild.verticesIndicesMetadataAndClear_areObservable") {
    MeshBuild mesh;
    CHECK(mesh.empty());
    mesh.addVertex(1.f, 2.f, 3.f, 0.f, 1.f, 0.f, .25f, .75f);
    mesh.addVertex(4.f, 5.f, 6.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(7.f, 8.f, 9.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    REQUIRE_EQ(mesh.getVertexCount(), 3);
    CHECK_EQ(mesh.getIndexCount(), 3);
    CHECK_EQ(mesh.getPositionX(0), 1.f);
    CHECK_EQ(mesh.getPositionY(1), 5.f);
    CHECK_EQ(mesh.getPositionZ(2), 9.f);
    CHECK_EQ(mesh.getNormalY(0), 1.f);
    CHECK_EQ(mesh.getUvU(0), .25f);
    CHECK_EQ(mesh.getUvV(0), .75f);
    CHECK_EQ(mesh.getIndex(2), 2);
    CHECK_EQ(mesh.getPositionX(99), 0.f);
    CHECK_EQ(mesh.getIndex(99), 0);
    mesh.setMeta("algorithm", "fixture");
    CHECK_EQ(mesh.getMeta("algorithm", ""), "fixture");
    mesh.clear();
    CHECK(mesh.empty());
}

TEST_CASE("procgen.marchingCubes.sphereRecipe_replaysMeshAndKeepsTriangleIndicesInRange") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    Params params;
    params.setSeed(1);
    params.setInt("resolution", 20);
    params.setString("field", "sphere");
    params.setFloat("radius", .7f);
    MeshBuild   first;
    MeshBuild   replay;
    std::string error;
    REQUIRE(registry.generate("mesh.marchingcubes", params, first, error));
    REQUIRE(registry.generate("mesh.marchingcubes", params, replay, error));
    CHECK(first.getVertexCount() > 100);
    CHECK_EQ(first.getIndexCount() % 3, 0);
    CHECK(meshIndicesInRange(first));
    CHECK_EQ(first.positions(), replay.positions());
    CHECK_EQ(first.indices(), replay.indices());
}

TEST_CASE("procgen.marchingCubes.rawDensity_uniformFieldsAreEmptyAndPlaneCutsSurface") {
    constexpr int      n = 4;
    std::vector<float> density(static_cast<size_t>(n * n * n), 1.f);
    MeshBuild          mesh;
    std::string        error;
    REQUIRE(marchingCubes(density.data(), n, n, n, 0.f, mesh, &error));
    CHECK(mesh.empty());
    std::fill(density.begin(), density.end(), -1.f);
    REQUIRE(marchingCubes(density.data(), n, n, n, 0.f, mesh, &error));
    CHECK(mesh.empty());
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                density[static_cast<size_t>(x + y * n + z * n * n)] = static_cast<float>(y) / (n - 1) - .5f;
    REQUIRE(marchingCubes(density.data(), n, n, n, 0.f, mesh, &error));
    CHECK(mesh.getVertexCount() > 0);
    CHECK_EQ(mesh.getIndexCount() % 3, 0);
    CHECK(meshIndicesInRange(mesh));
    for (int i = 0; i < mesh.getVertexCount(); ++i) CHECK(std::abs(mesh.getPositionY(i)) < .35f);
}

TEST_CASE("procgen.marchingCubes.invalidBufferShapeAndField_haveExplicitErrors") {
    MeshBuild   mesh;
    std::string error;
    CHECK(!marchingCubes(nullptr, 4, 4, 4, 0.f, mesh, &error));
    CHECK(error.find("null") != std::string::npos);
    float one = 0.f;
    CHECK(!marchingCubes(&one, 1, 1, 1, 0.f, mesh, &error));
    CHECK(error.find("2x2x2") != std::string::npos);

    std::vector<float> density;
    int                nx = 0, ny = 0, nz = 0;
    Params             valid;
    valid.setString("field", "sphere");
    valid.setInt("resolution", 8);
    REQUIRE(fillDensityField(valid, density, nx, ny, nz, error));
    CHECK_EQ(nx, 8);
    CHECK_EQ(ny, 8);
    CHECK_EQ(nz, 8);
    CHECK_EQ(density.size(), static_cast<size_t>(8 * 8 * 8));

    SUB_CASE("unknown field") {
        valid.setString("field", "unknown");
        CHECK(!fillDensityField(valid, density, nx, ny, nz, error));
        CHECK(error.find("field") != std::string::npos);
    };
    SUB_CASE("resolution below minimum") {
        valid.setInt("resolution", 1);
        CHECK(!fillDensityField(valid, density, nx, ny, nz, error));
    };
    SUB_CASE("resolution above maximum") {
        valid.setInt("resolution", 200);
        CHECK(!fillDensityField(valid, density, nx, ny, nz, error));
        CHECK(error.find("128") != std::string::npos);
    };
}

TEST_CASE("procgen.meshRecipes.shapeReplayParameterAndValidationContract") {
    SUB_CASE("registered shapes replay and produce valid triangle topology") {
        auto& registry = MeshRecipeRegistry::instance();
        registry.registerBuiltins();
        const char* ids[] = {"mesh.rock", "mesh.tree", "mesh.skyscraper", "mesh.flower", "mesh.bush"};
        for (const char* id : ids) {
            REQUIRE(registry.has(id));
            Params params;
            params.setSeed(23);
            MeshBuild   first;
            MeshBuild   replay;
            std::string error;
            REQUIRE(registry.generate(id, params, first, error));
            REQUIRE(registry.generate(id, params, replay, error));
            CHECK(first.getVertexCount() > 0);
            CHECK_EQ(first.getIndexCount() % 3, 0);
            CHECK(meshIndicesInRange(first));
            CHECK_EQ(first.positions(), replay.positions());
            CHECK_EQ(first.indices(), replay.indices());
        }
    };
    SUB_CASE("rock subdivisions and flattening change expected geometry") {
        auto& registry = MeshRecipeRegistry::instance();
        registry.registerBuiltins();
        Params params;
        params.setSeed(1847);
        params.setInt("subdivisions", 3);
        params.setFloat("flattening", .3f);
        MeshBuild   baseline;
        MeshBuild   variant;
        std::string error;
        REQUIRE(registry.generate("mesh.rock", params, baseline, error));
        CHECK_EQ(baseline.getVertexCount(), 642);
        CHECK_EQ(baseline.getIndexCount() / 3, 1280);
        params.setFloat("flattening", .58f);
        REQUIRE(registry.generate("mesh.rock", params, variant, error));
        CHECK(baseline.positions() != variant.positions());
        params.setInt("subdivisions", 2);
        REQUIRE(registry.generate("mesh.rock", params, variant, error));
        CHECK_EQ(variant.getVertexCount(), 162);
        params.setString("baseShape", "unknown");
        CHECK(!registry.generate("mesh.rock", params, variant, error));
    };
    SUB_CASE("tree invalid options identify the rejected field") {
        auto& registry = MeshRecipeRegistry::instance();
        registry.registerBuiltins();
        Params      params;
        MeshBuild   mesh;
        std::string error;
        const struct {
            const char* key;
            const char* value;
        } bad[] = {
            {"style", "watercolor"},
            {"branchAlgorithm", "crystalGrowth"},
            {"leafMode", "confetti"},
        };
        for (const auto& sample : bad) {
            params = Params{};
            params.setString(sample.key, sample.value);
            CHECK(!registry.generate("mesh.tree", params, mesh, error));
            CHECK(error.find(sample.key) != std::string::npos);
        }
    };
}

TEST_CASE("procgen.json.gridExport_containsShapeCellsAndObjects") {
    auto& registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    Params params;
    params.setSeed(3);
    params.setSize(12, 10);
    Grid2D      grid;
    std::string error;
    REQUIRE(registry.generate("dungeon.bsp", params, grid, error));
    const std::string json = gridToJson(grid);
    CHECK(json.find("\"width\":12") != std::string::npos);
    CHECK(json.find("\"height\":10") != std::string::npos);
    CHECK(json.find("\"cells\":") != std::string::npos);
    CHECK(json.find("spawn") != std::string::npos);
}

TEST_CASE("procgen.texture.noisePeriodAndRamp_samplesAreDeterministic") {
    NoiseField first{42, 8, 8};
    NoiseField replay{42, 8, 8};
    REQUIRE_EQ(first.fbm(1.25f, 3.5f, 4), replay.fbm(1.25f, 3.5f, 4));
    CHECK_EQ(first.hash01(0, 0), first.hash01(8, 0));
    CHECK_EQ(first.hash01(0, 0), first.hash01(0, 8));
    ColorRamp ramp;
    ramp.add(0.f, 0, 0, 0);
    ramp.add(1.f, 255, 255, 255);
    CHECK(ramp.sampleBanded(.1f, 3).r < ramp.sampleBanded(.9f, 3).r);
}

TEST_CASE("procgen.texture.registryImages_matchDimensionsFormatAndBytesOnReplay") {
    auto& registry = TextureRecipeRegistry::instance();
    registry.registerBuiltins();
    for (const char* id : {"tex.soil", "tex.stone", "tex.marble", "tex.water", "tex.sky_cloud"}) {
        Params params;
        params.setSeed(11);
        params.setSize(32, 32);
        params.setInt("colors", 5);
        std::string error;
        auto        first  = registry.generate(id, params, error);
        auto        replay = registry.generate(id, params, error);
        REQUIRE(static_cast<bool>(first));
        REQUIRE(static_cast<bool>(replay));
        CHECK_EQ(first->getWidth(), 32);
        CHECK_EQ(first->getHeight(), 32);
        CHECK_EQ(first->getFormat(), std::string("RGBA8"));
        CHECK_EQ(first->getSize(), replay->getSize());
        CHECK(std::memcmp(first->getData(), replay->getData(), first->getSize()) == 0);
    }
}

TEST_CASE("procgen.terrain.pipeline_deterministicallyBuildsDrainageAndClimateAtInputResolution") {
    Heightmap terrain(16, 12);
    for (int y = 0; y < terrain.getHeight(); ++y)
        for (int x = 0; x < terrain.getWidth(); ++x) terrain.setHeight(x, y, .8f - static_cast<float>(y) * .04f);
    const auto drainage = TerrainPipeline::buildHydrology(terrain, .02f, .2f);
    const auto climate  = TerrainPipeline::buildClimate(terrain, drainage, .2f, .35f);
    REQUIRE_EQ(drainage.width, 16);
    CHECK_EQ(drainage.height, 12);
    CHECK_EQ(drainage.flowDirection.size(), static_cast<size_t>(16 * 12));
    CHECK_EQ(drainage.flowAccumulation.size(), static_cast<size_t>(16 * 12));
    CHECK_EQ(climate.temperature.size(), static_cast<size_t>(16 * 12));
    CHECK_EQ(climate.biomes.size(), static_cast<size_t>(16 * 12));
    const auto replay = TerrainPipeline::buildHydrology(terrain, .02f, .2f);
    CHECK_EQ(drainage.flowDirection, replay.flowDirection);
    CHECK_EQ(drainage.rivers, replay.rivers);
}

TEST_CASE("procgen.terrain.archive_bakeOpenEdgeChunkAndCorruptionContract") {
    Heightmap terrain(9, 7);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x) terrain.setHeight(x, y, 10.f + x * .25f + y * .5f);
    const auto           drainage = TerrainPipeline::buildHydrology(terrain, 5.f, 10.f);
    const auto           climate  = TerrainPipeline::buildClimate(terrain, drainage, 10.f, .6f);
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> replay;
    std::string          error;
    REQUIRE(TerrainAsset::bake(terrain, drainage, climate, 4, bytes, &error));
    REQUIRE(TerrainAsset::bake(terrain, drainage, climate, 4, replay, &error));
    CHECK_EQ(bytes, replay);
    TerrainAsset asset;
    REQUIRE(asset.open(bytes.data(), bytes.size(), &error));
    CHECK_EQ(asset.getWidth(), 9);
    CHECK_EQ(asset.getHeight(), 7);
    CHECK_EQ(asset.getChunkSize(), 4);
    CHECK_EQ(asset.chunks().size(), static_cast<size_t>(6));
    TerrainChunkData edge;
    REQUIRE(asset.loadChunk(2, 1, edge, &error));
    CHECK_EQ(edge.width, 1);
    CHECK_EQ(edge.height, 3);
    CHECK(std::abs(edge.heights.height(0, 2) - terrain.height(8, 6)) < .001f);
    CHECK_EQ(edge.biomes.size(), static_cast<size_t>(3));

    const auto entry = asset.chunks().front();
    bytes[static_cast<size_t>(entry.offset) + entry.storedSize / 2] ^= 0x5a;
    TerrainAsset corrupt;
    REQUIRE(corrupt.open(bytes.data(), bytes.size(), &error));
    TerrainChunkData rejected;
    CHECK(!corrupt.loadChunk(entry.chunkX, entry.chunkY, rejected, &error));
}

TEST_CASE("procgen.terrain.streamingBudget_limitsLoadsAndEvictsDistantChunks") {
    Heightmap terrain(24, 24);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) terrain.setHeight(x, y, static_cast<float>(x + y * 2));
    const auto           drainage = TerrainPipeline::buildHydrology(terrain, 5.f, -1.f);
    const auto           climate  = TerrainPipeline::buildClimate(terrain, drainage, -1.f, .5f);
    std::vector<uint8_t> bytes;
    std::string          error;
    REQUIRE(TerrainAsset::bake(terrain, drainage, climate, 8, bytes, &error));
    TerrainStreamingCache cache;
    REQUIRE(cache.open(bytes.data(), bytes.size(), &error));
    const auto first = cache.streamAround(12, 12, 1, 2, &error);
    CHECK_EQ(first.loaded, 2);
    CHECK_EQ(first.pending, 3);
    CHECK_EQ(first.resident, 2);
    const auto complete = cache.streamAround(12, 12, 1, 0, &error);
    CHECK_EQ(complete.loaded, 3);
    CHECK_EQ(complete.resident, 5);
    CHECK_EQ(cache.streamAround(12, 12, 1, 0, &error).loaded, 0);
    float height = 0.f;
    REQUIRE(cache.sampleHeight(7.5f, 12.f, height));
    CHECK(std::abs(height - 31.5f) < .002f);
    TerrainSample sample;
    REQUIRE(cache.sampleCell(12, 12, sample));
    CHECK(std::abs(sample.height - 36.f) < .002f);
    const auto moved = cache.streamAround(23, 23, 0, 0, &error);
    CHECK_EQ(moved.evicted, 5);
    CHECK_EQ(moved.resident, 1);
    CHECK(!cache.sampleCell(12, 12, sample));
    REQUIRE(cache.sampleCell(23, 23, sample));
    CHECK(std::abs(sample.height - 69.f) < .002f);
    CHECK_EQ(cache.streamAround(0, 0, -1, 0, &error).resident, 1);
}

TEST_CASE("procgen.cloud.field_seedTimeAndPeriodDetermineCoverage") {
    CloudField::Params params;
    params.seed       = 99;
    params.worldScale = 64.f;
    params.coverage   = .5f;
    CloudField  first(params);
    CloudField  replay(params);
    const float atStart = first.coverageAt(3.f, 4.f, 0.f);
    REQUIRE(std::abs(atStart - replay.coverageAt(3.f, 4.f, 0.f)) < 1e-5f);
    CHECK(std::abs(atStart - first.coverageAt(67.f, 4.f, 0.f)) < 1e-4f);
    CHECK(std::abs(atStart - first.coverageAt(3.f, 68.f, 0.f)) < 1e-4f);
    CHECK(atStart >= 0.f);
    CHECK(atStart <= 1.f);
    const float atFive        = first.coverageAt(3.f, 4.f, 5.f);
    const float atTen         = first.coverageAt(3.f, 4.f, 10.f);
    const bool  shadowChanged = atStart != atFive || atFive != atTen;
    CHECK(shadowChanged);
}

TEST_CASE("procgen.cloud.shadow_overheadAngledAndBelowHorizonProjection") {
    CloudField::Params fieldParams;
    fieldParams.seed       = 7;
    fieldParams.worldScale = 48.f;
    CloudField          field(fieldParams);
    CloudShadow::Params params;
    params.field         = field;
    params.sunDirX       = 0.f;
    params.sunDirY       = 1.f;
    params.sunDirZ       = 0.f;
    params.cloudAltitude = 50.f;
    CloudShadow overhead(params);
    REQUIRE(std::abs(overhead.coverageAt(5.f, 6.f, 0.f) - field.coverageAt(5.f, 6.f, 0.f)) < 1e-5f);
    params.sunDirX = .5f;
    CloudShadow angled(params);
    float       offsetX = 0.f, offsetZ = 0.f;
    angled.cloudOffset(offsetX, offsetZ);
    CHECK(std::abs(angled.coverageAt(5.f, 6.f, 0.f) - field.coverageAt(5.f + offsetX, 6.f + offsetZ, 0.f)) < 1e-5f);
    params.sunDirY = -1.f;
    CloudShadow below(params);
    CHECK_EQ(below.coverageAt(5.f, 6.f, 0.f), 0.f);
    CHECK_EQ(below.shadowFactorAt(5.f, 6.f, 0.f), 1.f);
}

TEST_CASE("procgen.pbr.materialMapSetAndChannelRangeContract") {
    SUB_CASE("registry recipes produce six maps with replayable albedo") {
        auto& registry = PbrRecipeRegistry::instance();
        registry.registerPbrBuiltins();
        for (const char* id : {"pbr.soil", "pbr.rock", "pbr.marble", "pbr.water", "pbr.wood", "pbr.cloth"}) {
            Params params;
            params.setSeed(7);
            params.setSize(24, 24);
            std::string error;
            auto        first  = registry.generate(id, params, error);
            auto        replay = registry.generate(id, params, error);
            REQUIRE(static_cast<bool>(first));
            REQUIRE(static_cast<bool>(replay));
            REQUIRE(first->albedo != nullptr);
            REQUIRE(first->normal != nullptr);
            REQUIRE(first->roughness != nullptr);
            REQUIRE(first->metallic != nullptr);
            REQUIRE(first->height != nullptr);
            REQUIRE(first->ao != nullptr);
            CHECK_EQ(first->albedo->getWidth(), 24);
            CHECK_EQ(first->albedo->getHeight(), 24);
            CHECK(std::memcmp(first->albedo->getData(), replay->albedo->getData(), first->albedo->getSize()) == 0);
        }
    };
    SUB_CASE("metallic and roughness inputs constrain pixel channels") {
        auto& registry = PbrRecipeRegistry::instance();
        registry.registerPbrBuiltins();
        Params params;
        params.setSeed(3);
        params.setSize(24, 24);
        params.setFloat("metallic", 1.f);
        params.setFloat("roughnessLow", .1f);
        params.setFloat("roughnessHigh", .2f);
        std::string error;
        auto        material = registry.generate("pbr.marble", params, error);
        REQUIRE(static_cast<bool>(material));
        const auto* metallic  = static_cast<const uint8_t*>(material->metallic->getData());
        const auto* roughness = static_cast<const uint8_t*>(material->roughness->getData());
        for (size_t index = 0; index < material->metallic->getSize(); index += 4) {
            CHECK_EQ(metallic[index], 255);
            CHECK(roughness[index] >= 25);
            CHECK(roughness[index] <= 51);
        }
    };
}

TEST_CASE("procgen.linearStructure.recipeMetadataAndSegmentScalingContract") {
    SUB_CASE("registered recipes produce triangle geometry and metadata") {
        auto& registry = MeshRecipeRegistry::instance();
        registry.registerBuiltins();
        for (const char* id :
             {"mesh.fence", "mesh.stonewall", "mesh.bridge", "mesh.greatwall", "mesh.hedge", "mesh.chevaldefrise"}) {
            Params params;
            params.setSeed(7);
            params.setInt("segments", 4);
            params.setFloat("segLength", 1.f);
            MeshBuild   mesh;
            std::string error;
            REQUIRE(registry.generate(id, params, mesh, error));
            CHECK(mesh.getVertexCount() > 0);
            CHECK_EQ(mesh.getIndexCount() % 3, 0);
            CHECK(meshIndicesInRange(mesh));
            CHECK_EQ(mesh.getMeta("algorithm", ""), id);
            CHECK_EQ(mesh.getMeta("segments", ""), "4");
        }
    };
    SUB_CASE("segment count scales fence vertex count") {
        auto& registry = MeshRecipeRegistry::instance();
        registry.registerBuiltins();
        Params params;
        params.setInt("segments", 2);
        MeshBuild   two;
        MeshBuild   six;
        std::string error;
        REQUIRE(registry.generate("mesh.fence", params, two, error));
        params.setInt("segments", 6);
        REQUIRE(registry.generate("mesh.fence", params, six, error));
        CHECK_EQ(six.getVertexCount(), 3 * two.getVertexCount());
    };
}

TEST_CASE("procgen.castle.ringsAndFloors_determineGroupsAndStairCount") {
    Params params;
    params.setSeed(8675309);
    params.setFloat("width", 48.f);
    params.setFloat("depth", 40.f);
    params.setInt("rings", 2);
    params.setInt("keepFloors", 4);
    params.setInt("detail", 2);
    MeshBuild   mesh;
    MeshBuild   replay;
    std::string error;
    REQUIRE(generateCastleMesh(params, mesh, error));
    REQUIRE(generateCastleMesh(params, replay, error));
    CHECK_EQ(mesh.positions(), replay.positions());
    CHECK(meshIndicesInRange(mesh));
    CHECK_EQ(mesh.getMeta("rings", ""), "2");
    CHECK_EQ(mesh.getMeta("keepFloors", ""), "4");
    CHECK_EQ(mesh.getMeta("stairFlights", ""), "5");
    std::set<std::string> groups;
    for (int i = 0; i < mesh.getGroupCount(); ++i) groups.insert(mesh.getGroupName(i));
    for (const char* required : {"walls", "battlements", "towers", "gatehouses", "stairs", "keep", "courtyard"})
        CHECK(groups.count(required) == 1);
}

TEST_CASE("procgen.hexTerrain.seededMesh_replaysHydrologyAndBiomeCounts") {
    Params params;
    params.setSeed(20260825);
    params.setSize(38, 28);
    params.setInt("riverCount", 8);
    params.setFloat("radius", .62f);
    MeshBuild   first;
    MeshBuild   replay;
    std::string error;
    REQUIRE(generateHexTerrainMesh(params, first, error));
    REQUIRE(generateHexTerrainMesh(params, replay, error));
    CHECK_EQ(first.positions(), replay.positions());
    CHECK_EQ(first.indices(), replay.indices());
    CHECK(meshIndicesInRange(first));
    CHECK_EQ(first.getMeta("algorithm", ""), "mesh.hexterrain");
    CHECK(std::stoi(first.getMeta("cells.ocean", "0")) > 0);
    CHECK(std::stoi(first.getMeta("cells.grassland", "0")) > 0);
    CHECK(std::stoi(first.getMeta("cells.river", "0")) > 0);
}

}  // namespace test_procgen_unit
#endif
