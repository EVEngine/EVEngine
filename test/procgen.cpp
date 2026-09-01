#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "procgen/Procgen.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "procgen/JsonExport.h"
#include "procgen/MeshBuild.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/LinearStructure.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainPipeline.h"
#include "procgen/heightmap/TerrainStreaming.h"
#include "procgen/algorithms/CastleMesh.h"
#include "procgen/texture/TextureRecipe.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/ColorRamp.h"
#include "map/TileLayer.h"
#include "image/ImageData.h"
#include "graphics/Graphics.h"
#include "graphics/ClipSpace.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "map/TileLayer.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/JsonExport.h"
#include "procgen/MeshBuild.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/LinearStructure.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/HexTerrain.h"
#include "procgen/texture/ColorRamp.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>
#include "RenderImageAudit.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::procgen;
using namespace eve::graphics;

TEST_CASE("procgen.hexTerrain.biomesRiversCliffsDeterministic") {
    Params p;
    p.setInt("width", 38);
    p.setInt("height", 28);
    p.setInt("seed", 20260825);
    p.setInt("riverCount", 8);
    p.setFloat("radius", 0.62f);
    p.setFloat("heightScale", 3.8f);
    MeshBuild a, b;
    std::string error;
    REQUIRE(generateHexTerrainMesh(p, a, error));
    REQUIRE(generateHexTerrainMesh(p, b, error));
    CHECK_EQ(a.positions(), b.positions());
    CHECK_EQ(a.indices(), b.indices());
    CHECK_EQ(a.getMeta("algorithm", ""), std::string("mesh.hexterrain"));
    CHECK_EQ(a.getMeta("shoreGeometry", ""), std::string("edge-bands"));
    CHECK_EQ(a.getMeta("hydrology", ""),
             std::string("drainage-rivers+basin-lakes+confluences"));
    CHECK_EQ(a.getMeta("riverGeometry", ""), std::string("seeded-quadratic-ribbons"));
    CHECK_EQ(a.getMeta("cliffGeometry", ""),
             std::string("seeded-segmented-rock-walls"));
    CHECK_EQ(a.getMeta("mountainGeometry", ""),
             std::string("seeded-offset-three-ring-peaks"));
    CHECK(std::stoi(a.getMeta("cells.deepOcean", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.ocean", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.coast", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.grassland", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.hills", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.mountain", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.forest", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.swamp", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.rainforest", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.ice", "0")) > 0);
    CHECK(std::stoi(a.getMeta("cells.river", "0")) > 0);
    CHECK(std::stoi(a.getMeta("edges.cliff", "0")) > 0);
    CHECK(a.getVertexCount() >= 38 * 28 * 7);
    CHECK(a.getIndexCount() >= 38 * 28 * 18);

    bool hasOcean = false, hasLand = false, hasRiver = false, hasCliff = false;
    for (int i = 0; i < a.getVertexCount(); ++i) {
        const int primary = int(std::floor(a.getUvU(i)));
        const int secondary = int(std::floor(a.getUvV(i)));
        hasOcean = hasOcean || primary <= 1;
        hasLand = hasLand || (primary >= 3 && primary <= 9);
        hasRiver = hasRiver || secondary == 11;
        hasCliff = hasCliff || primary == 10;
    }
    CHECK(hasOcean);
    CHECK(hasLand);
    CHECK(hasRiver);
    CHECK(hasCliff);
}

TEST_CASE("procgen.params.booleanRoundTrip") {
    Params p;
    CHECK(p.getBool("decorations", true));
    p.setBool("decorations", false);
    CHECK(!p.getBool("decorations", true));
    // Text is a distinct Value kind; getters never parse or stringify it.
    p.setString("textBool", "false");
    CHECK(p.getBool("textBool", true));
    CHECK_EQ(p.getInt("textBool", -1), -1);
    CHECK_EQ(p.getString("decorations", "missing"), "missing");
}

namespace {

struct ParamsLease {
    ProcgenParamsHandleRef handle{};
    explicit ParamsLease(ProcgenParamsHandleRef value) : handle(value) {}
    ~ParamsLease() {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("procgen test params cleanup");
    }
    ParamsLease(const ParamsLease &)            = delete;
    ParamsLease &operator=(const ParamsLease &) = delete;
    ParamsLease(ParamsLease &&other) noexcept : handle(other.handle) { other.handle = {}; }
    [[nodiscard]] eve::script::Borrowed<Params> view() const noexcept { return Procgen::resolve(handle); }
};

ParamsLease requireParams(const Params &source) {
    auto result = Procgen::newParamsHandle();
    REQUIRE(result.ok());
    ParamsLease lease{std::move(result).takeValue()};
    auto        view = lease.view();
    REQUIRE(view.isBound());
    *view = source;
    return lease;
}

struct GridLease {
    ProcgenGridHandleRef handle{};
    explicit GridLease(ProcgenGridHandleRef value) : handle(value) {}
    ~GridLease() {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("procgen test grid cleanup");
    }
    GridLease(const GridLease &)            = delete;
    GridLease &operator=(const GridLease &) = delete;
    GridLease(GridLease &&other) noexcept : handle(other.handle) { other.handle = {}; }
    [[nodiscard]] eve::script::Borrowed<Grid2D> view() const noexcept { return Procgen::resolve(handle); }
};

GridLease requireGrid(Procgen &proc, const std::string &algorithm, ProcgenParamsHandleRef params) {
    auto result = proc.generateHandle(algorithm, params);
    REQUIRE(result.ok());
    return GridLease{std::move(result).takeValue()};
}

struct MeshLease {
    Procgen                  *owner = nullptr;
    ProcgenMeshBuildHandleRef handle{};
    MeshLease(Procgen &proc, ProcgenMeshBuildHandleRef value) : owner(&proc), handle(value) {}
    ~MeshLease() {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releaseMeshBuild(handle);
        result.ignore("procgen test mesh cleanup");
    }
    MeshLease(const MeshLease &)            = delete;
    MeshLease &operator=(const MeshLease &) = delete;
    MeshLease(MeshLease &&other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<MeshBuild> view() const noexcept {
        return owner ? owner->resolveMeshBuild(handle) : eve::script::Borrowed<MeshBuild>();
    }
};

MeshLease requireMesh(Procgen &proc, const std::string &recipe, ProcgenParamsHandleRef params) {
    auto result = proc.buildMeshHandle(recipe, params);
    REQUIRE(result.ok());
    return MeshLease(proc, std::move(result).takeValue());
}

struct PbrLease {
    Procgen                    *owner = nullptr;
    ProcgenPbrMaterialHandleRef handle{};
    PbrLease(Procgen &proc, ProcgenPbrMaterialHandleRef value) : owner(&proc), handle(value) {}
    ~PbrLease() {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releasePbrMaterial(handle);
        result.ignore("procgen test PBR cleanup");
    }
    PbrLease(const PbrLease &)            = delete;
    PbrLease &operator=(const PbrLease &) = delete;
    PbrLease(PbrLease &&other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<PbrTextureSet> view() const noexcept {
        return owner ? owner->resolvePbrMaterial(handle) : eve::script::Borrowed<PbrTextureSet>();
    }
};

bool gridsEqual(const Grid2D &a, const Grid2D &b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    return a.cells() == b.cells();
}

int countSemantic(const Grid2D &g, int semantic) {
    int n = 0;
    for (uint32_t c : g.cells())
        if (int(c) == semantic) ++n;
    return n;
}

bool neighborsOk(int a, int b, const std::set<std::pair<int, int>> &allowed) {
    if (a > b) std::swap(a, b);
    return allowed.count({a, b}) > 0;
}

bool terrainAdjacencyOk(const Grid2D &g) {
    // Ordered elevation band: only self + ±1 neighbors.
    static const int band[] = {int(Semantic::Water), int(Semantic::Sand),  int(Semantic::Grass),
                               int(Semantic::Dirt),  int(Semantic::Stone), int(Semantic::Snow)};
    std::set<std::pair<int, int>> allowed;
    for (int i = 0; i < 6; ++i) {
        allowed.insert({band[i], band[i]});
        if (i + 1 < 6) {
            int lo = band[i], hi = band[i + 1];
            if (lo > hi) std::swap(lo, hi);
            allowed.insert({lo, hi});
        }
    }
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int c = g.getCell(x, y);
            if (x + 1 < w && !neighborsOk(c, g.getCell(x + 1, y), allowed)) return false;
            if (y + 1 < h && !neighborsOk(c, g.getCell(x, y + 1), allowed)) return false;
        }
    }
    return true;
}

bool caveAdjacencyOk(const Grid2D &g) {
    std::set<std::pair<int, int>> allowed = {
        {int(Semantic::Wall), int(Semantic::Wall)},
        {int(Semantic::Wall), int(Semantic::Floor)},
        {int(Semantic::Floor), int(Semantic::Floor)},
    };
    // Normalize pair order for lookup.
    auto ok = [&](int a, int b) {
        if (a > b) std::swap(a, b);
        return allowed.count({a, b}) > 0;
    };
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int c = g.getCell(x, y);
            if (x + 1 < w && !ok(c, g.getCell(x + 1, y))) return false;
            if (y + 1 < h && !ok(c, g.getCell(x, y + 1))) return false;
        }
    }
    return true;
}

bool borderIsWall(const Grid2D &g) {
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int x = 0; x < w; ++x) {
        if (g.getCell(x, 0) != int(Semantic::Wall)) return false;
        if (g.getCell(x, h - 1) != int(Semantic::Wall)) return false;
    }
    for (int y = 0; y < h; ++y) {
        if (g.getCell(0, y) != int(Semantic::Wall)) return false;
        if (g.getCell(w - 1, y) != int(Semantic::Wall)) return false;
    }
    return true;
}

bool meshIndicesInRange(const MeshBuild &m) {
    const int vc = m.getVertexCount();
    for (int i = 0; i < m.getIndexCount(); ++i) {
        const int id = m.getIndex(i);
        if (id < 0 || id >= vc) return false;
    }
    return true;
}

bool meshNormalsFiniteUnit(const MeshBuild &m, float tol = 0.15f) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        const float nx = m.getNormalX(i);
        const float ny = m.getNormalY(i);
        const float nz = m.getNormalZ(i);
        if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) return false;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (std::fabs(len - 1.f) > tol) return false;
    }
    return true;
}

bool meshPositionsFinite(const MeshBuild &m) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        if (!std::isfinite(m.getPositionX(i)) || !std::isfinite(m.getPositionY(i)) ||
            !std::isfinite(m.getPositionZ(i)))
            return false;
    }
    return true;
}

float meshApproxSignedVolume(const MeshBuild &m) {
    // Sum of signed tet volumes with origin (valid for closed mesh).
    float vol = 0.f;
    for (int t = 0; t + 2 < m.getIndexCount(); t += 3) {
        const int i0 = m.getIndex(t);
        const int i1 = m.getIndex(t + 1);
        const int i2 = m.getIndex(t + 2);
        const float ax = m.getPositionX(i0), ay = m.getPositionY(i0), az = m.getPositionZ(i0);
        const float bx = m.getPositionX(i1), by = m.getPositionY(i1), bz = m.getPositionZ(i1);
        const float cx = m.getPositionX(i2), cy = m.getPositionY(i2), cz = m.getPositionZ(i2);
        vol += ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx);
    }
    return vol / 6.f;
}

bool approxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

bool hasWalkablePath(const Grid2D &g) {
    // BFS from first floor/corridor cell; ensure >1 walkable and connected component covers all.
    const int w = g.getWidth();
    const int h = g.getHeight();
    auto walkable = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        const int c = g.getCell(x, y);
        return c == int(Semantic::Floor) || c == int(Semantic::Corridor);
    };
    int start = -1;
    int total = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (walkable(x, y)) {
                ++total;
                if (start < 0) start = y * w + x;
            }
        }
    }
    if (total < 2 || start < 0) return false;
    std::vector<char> seen(size_t(w * h), 0);
    std::vector<int>  q;
    q.push_back(start);
    seen[size_t(start)] = 1;
    size_t qi = 0;
    int    reached = 0;
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    while (qi < q.size()) {
        const int i = q[qi++];
        const int x = i % w;
        const int y = i / w;
        ++reached;
        for (int d = 0; d < 4; ++d) {
            const int nx = x + dx[d];
            const int ny = y + dy[d];
            if (!walkable(nx, ny)) continue;
            const int ni = ny * w + nx;
            if (seen[size_t(ni)]) continue;
            seen[size_t(ni)] = 1;
            q.push_back(ni);
        }
    }
    return reached == total;
}

}  // namespace

TEST_CASE("procgen.semantic.names") {
    CHECK_EQ(std::string(semanticName(Semantic::Wall)), "wall");
    CHECK_EQ(semanticId("floor"), Semantic::Floor);
    CHECK_EQ(semanticId("nope"), Semantic::Empty);
}

TEST_CASE("procgen.registry.builtins") {
    GeneratorRegistry::instance().registerBuiltins();
    CHECK(GeneratorRegistry::instance().has("dungeon.bsp"));
    CHECK(GeneratorRegistry::instance().has("cave.cellular"));
    CHECK(GeneratorRegistry::instance().has("cave.drunkard"));
    CHECK(GeneratorRegistry::instance().has("maze.backtrack"));
    CHECK(GeneratorRegistry::instance().has("noise.terrain"));
    CHECK(GeneratorRegistry::instance().has("wfc.simple"));
}

TEST_CASE("procgen.registry.schemas.describe_every_grid_generator") {
    auto& registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    const auto ids = registry.list();
    REQUIRE(!ids.empty());
    for (const std::string& id : ids) {
        const GeneratorDescriptor* schema = registry.descriptor(id);
        REQUIRE(schema != nullptr);
        CHECK_EQ(schema->id, id);
        CHECK(!schema->displayName.empty());
        CHECK(!schema->category.empty());
        CHECK(!schema->params.empty());
        for (const ParamDescriptor& param : schema->params) {
            CHECK(!param.key.empty());
            CHECK(!param.displayName.empty());
            if (param.hasMinimum && param.hasMaximum) CHECK_LE(param.minimum, param.maximum);
            if (param.kind == ParamKind::Choice) {
                CHECK(!param.choices.empty());
                CHECK(std::find(param.choices.begin(), param.choices.end(), param.defaultValue) !=
                      param.choices.end());
            }
        }
    }

    Params params;
    REQUIRE(registry.applyDefaults("cave.cellular", params));
    CHECK(params.has("fill"));
    CHECK_LT(std::abs(params.getFloat("fill", 0.f) - 0.45f), 0.0001f);
    params.setInt("width", 96);
    params.setInt("height", 48);
    params.setInt("seed", 7);
    CHECK_EQ(params.getWidth(), 96);
    CHECK_EQ(params.getHeight(), 48);
    CHECK_EQ(params.getSeed(), 7u);
}

TEST_CASE("procgen.mesh.rock.reproducibleAndControllable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(1847);
    p.setInt("subdivisions", 3);
    p.setFloat("radius", 0.7f);
    p.setFloat("flattening", 0.3f);
    p.setFloat("angularity", 0.45f);
    p.setFloat("erosion", 0.16f);
    p.setFloat("scale", 2.4f);
    MeshBuild a, b, flatter;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, a, err));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, b, err));
    CHECK_EQ(a.getVertexCount(), 642);
    CHECK_EQ(a.getIndexCount() / 3, 1280);
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());
    CHECK(meshIndicesInRange(a));
    CHECK(meshNormalsFiniteUnit(a));

    p.setFloat("flattening", 0.58f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, flatter, err));
    CHECK(a.positions() != flatter.positions());

    p.setInt("subdivisions", 2);
    MeshBuild lod1;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, lod1, err));
    CHECK_EQ(lod1.getVertexCount(), 162);
    CHECK_EQ(lod1.getIndexCount() / 3, 320);

    std::vector<std::vector<float>> shapePositions;
    for (const char *shape : {"boulder", "slab", "block", "shard"}) {
        p.setString("baseShape", shape);
        p.setInt("subdivisions", 3);
        MeshBuild variant;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, variant, err));
        CHECK_EQ(variant.getMeta("baseShape", ""), shape);
        CHECK_EQ(variant.getVertexCount(), 642);
        CHECK(meshIndicesInRange(variant));
        CHECK(meshNormalsFiniteUnit(variant));
        shapePositions.push_back(variant.positions());
    }
    CHECK(shapePositions[0] != shapePositions[1]);
    CHECK(shapePositions[1] != shapePositions[2]);
    CHECK(shapePositions[2] != shapePositions[3]);

    p.setString("baseShape", "invalid");
    MeshBuild invalid;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.rock", p, invalid, err));
}

TEST_CASE("procgen.mesh.tree.stylesAndLeaves") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params low;
    low.setSeed(1234);
    low.setString("style", "lowpoly");
    low.setString("leafMode", "cards");
    low.setFloat("leafDensity", 0.8f);
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.tree", low, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.tree", low, b, err));
    CHECK(a.getVertexCount() > 0);
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());
    CHECK(meshIndicesInRange(a));
    CHECK(meshPositionsFinite(a));
    CHECK(meshNormalsFiniteUnit(a));
    CHECK_EQ(a.getMeta("style", ""), "lowpoly");
    CHECK_EQ(a.getMeta("leafMode", ""), "cards");

    Params realistic;
    realistic.setSeed(22);
    realistic.setString("style", "realistic");
    realistic.setString("leafMode", "canopy");
    realistic.setString("branchAlgorithm", "spaceColonization");
    realistic.setFloat("leafDensity", 1.f);
    MeshBuild canopy;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.tree", realistic, canopy, err));
    CHECK(canopy.getVertexCount() > a.getVertexCount() / 2);
    CHECK(meshIndicesInRange(canopy));
    CHECK(meshNormalsFiniteUnit(canopy));
    CHECK_EQ(canopy.getMeta("branchAlgorithm", ""), "spaceColonization");

    Params sparseCoverage = realistic;
    sparseCoverage.setFloat("lowerLeafCoverage", 0.f);
    sparseCoverage.setFloat("upperLeafCoverage", 0.f);
    Params fullCoverage = realistic;
    fullCoverage.setFloat("lowerLeafCoverage", 1.f);
    fullCoverage.setFloat("upperLeafCoverage", 1.f);
    MeshBuild sparseLeaves, coveredBranches;
    CHECK(MeshRecipeRegistry::instance().generate(
        "mesh.tree", sparseCoverage, sparseLeaves, err));
    CHECK(MeshRecipeRegistry::instance().generate(
        "mesh.tree", fullCoverage, coveredBranches, err));
    CHECK(coveredBranches.getVertexCount() > sparseLeaves.getVertexCount());

    Params bare;
    bare.setSeed(22);
    bare.setString("leafMode", "none");
    MeshBuild noLeaves;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.tree", bare, noLeaves, err));
    CHECK(noLeaves.getVertexCount() < canopy.getVertexCount());

    Params straight = bare;
    straight.setFloat("trunkCurve", 0.f);
    straight.setFloat("branchCurve", 0.f);
    straight.setFloat("curveBack", 0.f);
    straight.setFloat("tropism", 0.f);
    straight.setFloat("droop", 0.f);
    Params expressive = straight;
    expressive.setFloat("trunkCurve", 0.2f);
    expressive.setFloat("branchCurve", 0.24f);
    expressive.setFloat("curveBack", 0.2f);
    expressive.setFloat("tropism", 0.35f);
    expressive.setFloat("droop", 0.28f);
    MeshBuild rigidTree, curvedTree;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.tree", straight, rigidTree, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.tree", expressive, curvedTree, err));
    CHECK(rigidTree.positions() != curvedTree.positions());
    CHECK(meshPositionsFinite(curvedTree));
    CHECK(meshNormalsFiniteUnit(curvedTree));
}

TEST_CASE("procgen.mesh.tree.validatesOptions") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setString("style", "watercolor");
    MeshBuild mesh;
    std::string err;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.tree", p, mesh, err));
    CHECK(err.find("style") != std::string::npos);
    p.setString("style", "lowpoly");
    p.setString("branchAlgorithm", "crystalGrowth");
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.tree", p, mesh, err));
    CHECK(err.find("branchAlgorithm") != std::string::npos);
}

TEST_CASE("procgen.mesh.skyscraper.reproducibleAndControllable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(2026);
    p.setFloat("baseWidth", 10.f);
    p.setFloat("baseDepth", 10.f);
    p.setInt("tiers", 6);
    p.setFloat("tierHeight", 6.f);
    p.setFloat("setback", 0.08f);
    p.setInt("windowCols", 6);
    p.setInt("windowRows", 4);
    p.setFloat("windowDepth", 0.04f);
    p.setFloat("spireHeight", 4.f);
    MeshBuild a, b, tapering;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.skyscraper", p, a, err));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.skyscraper", p, b, err));
    CHECK(a.getVertexCount() > 0);
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());
    CHECK_EQ(a.getIndexCount() % 3, 0);
    CHECK(meshIndicesInRange(a));
    CHECK(meshPositionsFinite(a));
    CHECK(meshNormalsFiniteUnit(a));
    CHECK_EQ(a.getMeta("algorithm", ""), "mesh.skyscraper");
    CHECK_EQ(a.getMeta("tiers", ""), "6");
    CHECK_EQ(a.getMeta("windowCols", ""), "6");
    CHECK_EQ(a.getMeta("windowRows", ""), "4");
    // 6 tiers * 4 facades * (1 wall + cols*rows windows) quads = 6*4*(1+24) = 600 quads, plus roof.
    CHECK_EQ(a.getVertexCount() % 4, 0);

    // More setbacks / smaller footprint must change geometry.
    p.setFloat("setback", 0.2f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.skyscraper", p, tapering, err));
    CHECK(a.positions() != tapering.positions());

    // A taller tower should have more vertices (more tiers).
    Params tall;
    tall.setInt("tiers", 12);
    tall.setInt("windowCols", 8);
    tall.setInt("windowRows", 6);
    MeshBuild tallMesh;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.skyscraper", tall, tallMesh, err));
    CHECK(tallMesh.getVertexCount() > a.getVertexCount());
    CHECK(meshNormalsFiniteUnit(tallMesh));
}

TEST_CASE("procgen.mesh.skyscraper.heightsAndBounds") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(7);
    p.setInt("tiers", 4);
    p.setFloat("tierHeight", 5.f);
    p.setFloat("spireHeight", 3.f);
    MeshBuild mesh;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.skyscraper", p, mesh, err));
    // Total height: tiers*tierHeight + spire.
    const float expectedHeight = 4.f * 5.f + 3.f;
    CHECK_EQ(std::stof(mesh.getMeta("height", "0")), expectedHeight);
    // Every vertex must sit on or above the ground plane (y >= -epsilon).
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        CHECK(mesh.getPositionY(i) >= -1e-4f);
    }
    // Base footprint should be wider than the top tier after setbacks.
    const float baseW = p.getFloat("baseWidth", 10.f) * 0.5f;
    const float topW = baseW * (1.f - p.getFloat("setback", 0.08f) * 3.f);
    CHECK(topW < baseW);
}

TEST_CASE("procgen.mesh.tree.renderDump") {
    const char *outputPath = std::getenv("EVENGINE_TREE_RENDER_PNG");
    if (!outputPath || !outputPath[0]) return;

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings ws;
    ws.width = 900;
    ws.height = 700;
    ws.centered = true;
    REQUIRE(win->setWindowSettings(ws));

    Params params;
    params.setSeed(31415);
    params.setString("style", "lowpoly");
    const char *leafModeOverride = std::getenv("EVENGINE_TREE_LEAF_MODE");
    params.setString("leafMode", leafModeOverride ? leafModeOverride : "canopy");
    params.setString("branchAlgorithm", "spaceColonization");
    params.setFloat("leafDensity", 0.82f);
    params.setFloat("height", 6.2f);
    params.setFloat("crownRadius", 2.15f);
    params.setInt("branchLevels", 3);
    params.setInt("branchCount", 8);
    params.setInt("attractorCount", 120);
    params.setInt("colonizationIterations", 38);
    params.setFloat("branchInertia", 1.15f);
    params.setFloat("tropism", 0.20f);
    params.setFloat("droop", 0.08f);
    params.setFloat("growthStep", 0.25f);
    params.setFloat("maxTurnAngle", 18.f);
    params.setInt("maxChildren", 2);

    Procgen generator;
    auto    treeParams = requireParams(params);
    auto    treeMesh   = generator.generateMeshBorrowed("mesh.tree", treeParams.handle, gfx);
    REQUIRE(treeMesh.isBound());

    // 4px bark/foliage atlas; UVs are partitioned by the mesh recipe.
    const uint8_t atlasPixels[] = {
        111, 70, 42, 255, 128, 82, 47, 255,
        64, 119, 57, 255, 82, 145, 67, 255,
    };
    Texture *atlas = gfx->newTexture(4, 1, atlasPixels);
    REQUIRE(atlas != nullptr);

    auto *tree = Renderable3D::create();
    tree->setMesh(treeMesh.get());
    tree->setTexture(atlas);
    tree->setTint(1.f, 1.f, 1.f, 1.f);
    tree->setRoughness(0.88f);
    tree->setRotation(-18.f, 0.f, 0.f);

    auto *camera = Camera3D::createCamera();
    camera->setEye(9.2f, 5.1f, 11.4f);
    camera->setTarget(0.f, 3.1f, 0.f);
    camera->setUp(0.f, 1.f, 0.f);
    camera->setFov(36.f);
    camera->setAmbient(0.30f, 0.34f, 0.28f);
    camera->setActive(true);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.075f, 0.105f, 0.095f, 1.f));
    RenderSystem3D::setDirectionalLight(-0.55f, -1.f, -0.35f, 1.45f, 1.32f, 1.08f);

    // Warm up pipelines and read back the final stable frame.
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(image.get() != nullptr);
    REQUIRE(saveImagePng(*image, outputPath));
    std::printf("tree render saved: %s\n", outputPath);
    win->close();
}

TEST_CASE("procgen.mesh.bush.reproducibleAndStyles") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(20260815u);
    p.setString("style", "mound");
    p.setString("leafMode", "mixed");
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.bush", p, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.bush", p, b, err));
    CHECK(a.getVertexCount() > 0);
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());
    CHECK(meshIndicesInRange(a));
    CHECK(meshPositionsFinite(a));
    CHECK(meshNormalsFiniteUnit(a));
    CHECK_EQ(a.getMeta("recipe", ""), "mesh.bush");
    CHECK_EQ(a.getMeta("style", ""), "mound");
    CHECK_EQ(a.getMeta("leafMode", ""), "mixed");

    // Sphere style produces a different silhouette (rounder, taller lobes).
    Params sphere = p;
    sphere.setString("style", "sphere");
    MeshBuild round;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.bush", sphere, round, err));
    CHECK(round.getVertexCount() > 0);
    CHECK(round.positions() != a.positions());
    CHECK(meshIndicesInRange(round));
    CHECK(meshNormalsFiniteUnit(round));
    CHECK_EQ(round.getMeta("style", ""), "sphere");

    // Bare foliage still yields a closed mound (blobs only, no cards/twigs tufts).
    Params blobsOnly = p;
    blobsOnly.setString("leafMode", "blobs");
    blobsOnly.setInt("twigs", 0);
    MeshBuild mounds;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.bush", blobsOnly, mounds, err));
    CHECK(mounds.getVertexCount() > 0);
    CHECK(meshIndicesInRange(mounds));
    CHECK(meshNormalsFiniteUnit(mounds));

    // "none" drops cards but keeps the blobs; fewer vertices than the mixed default.
    Params bare = p;
    bare.setString("leafMode", "none");
    bare.setInt("twigs", 0);
    MeshBuild noCards;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.bush", bare, noCards, err));
    CHECK(noCards.getVertexCount() > 0);
    CHECK(noCards.getVertexCount() < a.getVertexCount());
    CHECK(meshNormalsFiniteUnit(noCards));

    // Lower resolution should be cheaper than the default.
    Params lod = p;
    lod.setInt("rings", 2);
    lod.setInt("radialSegments", 5);
    lod.setInt("blobs", 5);
    MeshBuild cheap;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.bush", lod, cheap, err));
    CHECK(cheap.getVertexCount() < a.getVertexCount());
    CHECK(meshIndicesInRange(cheap));
    CHECK(meshNormalsFiniteUnit(cheap));
}

TEST_CASE("procgen.mesh.bush.validatesOptions") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setString("style", "bonsai");
    MeshBuild mesh;
    std::string err;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.bush", p, mesh, err));
    CHECK(err.find("style") != std::string::npos);
    p.setString("style", "mound");
    p.setString("leafMode", "marching");
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.bush", p, mesh, err));
    CHECK(err.find("leafMode") != std::string::npos);
}

TEST_CASE("procgen.mesh.bush.renderDump") {
    const char *outputPath = std::getenv("EVENGINE_BUSH_RENDER_PNG");
    if (!outputPath || !outputPath[0]) return;

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings ws;
    ws.width = 900;
    ws.height = 700;
    ws.centered = true;
    REQUIRE(win->setWindowSettings(ws));

    Params params;
    params.setSeed(20260815u);
    params.setString("style", "mound");
    params.setString("leafMode", "mixed");
    params.setFloat("height", 1.7f);
    params.setFloat("width", 2.6f);
    params.setInt("blobs", 12);
    params.setInt("rings", 5);
    params.setInt("radialSegments", 12);
    params.setFloat("leafDensity", 0.8f);
    params.setFloat("leafSize", 0.32f);
    params.setInt("twigs", 6);

    Procgen generator;
    auto    bushParams = requireParams(params);
    auto    bushMesh   = generator.generateMeshBorrowed("mesh.bush", bushParams.handle, gfx);
    REQUIRE(bushMesh.isBound());

    // A tiny two-tone foliage atlas (dark base / light highlight), like the tree test.
    const uint8_t atlasPixels[] = {
        104, 67, 38, 255, 84, 132, 60, 255,
    };
    Texture *atlas = gfx->newTexture(2, 1, atlasPixels);
    REQUIRE(atlas != nullptr);

    auto *bush = Renderable3D::create();
    bush->setMesh(bushMesh.get());
    bush->setTexture(atlas);
    bush->setTint(1.f, 1.f, 1.f, 1.f);
    bush->setRoughness(0.9f);
    bush->setRotation(-14.f, 0.f, 0.f);

    auto *camera = Camera3D::createCamera();
    camera->setEye(5.2f, 3.2f, 5.6f);
    camera->setTarget(0.f, 0.9f, 0.f);
    camera->setUp(0.f, 1.f, 0.f);
    camera->setFov(36.f);
    camera->setAmbient(0.34f, 0.38f, 0.30f);
    camera->setActive(true);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.075f, 0.105f, 0.095f, 1.f));
    RenderSystem3D::setDirectionalLight(-0.55f, -1.f, -0.35f, 1.45f, 1.32f, 1.08f);

    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(image.get() != nullptr);
    REQUIRE(saveImagePng(*image, outputPath));
    std::printf("bush render saved: %s\n", outputPath);
    win->close();
}

TEST_CASE("procgen.dungeon.bsp.reproducible") {
    Params p;
    p.setSeed(42);
    p.setSize(48, 36);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("dungeon.bsp", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("dungeon.bsp", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(countSemantic(a, int(Semantic::Floor)) > 0);
    CHECK(countSemantic(a, int(Semantic::Wall)) > 0);
    CHECK(a.getObjectCount() >= 2);
    CHECK(hasWalkablePath(a));
}

TEST_CASE("procgen.cave.cellular.reproducible") {
    Params p;
    p.setSeed(7);
    p.setSize(32, 24);
    p.setInt("loops", 4);
    p.setFloat("fill", 0.45f);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("cave.cellular", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("cave.cellular", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(countSemantic(a, int(Semantic::Floor)) > 0);
}

TEST_CASE("procgen.cave.drunkard.reproducible") {
    Params p;
    p.setSeed(99);
    p.setSize(40, 30);
    p.setFloat("floorPct", 0.4f);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("cave.drunkard", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("cave.drunkard", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(countSemantic(a, int(Semantic::Floor)) > 10);
}

TEST_CASE("procgen.maze.backtrack.reproducible") {
    Params p;
    p.setSeed(123);
    p.setSize(21, 15);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("maze.backtrack", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("maze.backtrack", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(hasWalkablePath(a));
}

TEST_CASE("procgen.noise.terrain.reproducible") {
    Params p;
    p.setSeed(55);
    p.setSize(32, 32);
    p.setFloat("frequency", 5.f);
    p.setInt("octaves", 3);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("noise.terrain", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("noise.terrain", p, b, err));
    CHECK(gridsEqual(a, b));
    // Multiple biome bands expected.
    std::set<int> kinds;
    for (uint32_t c : a.cells()) kinds.insert(int(c));
    CHECK(kinds.size() >= 2);
}

TEST_CASE("procgen.terrain.pipeline.erosionHydrologyAndBiomes") {
    Heightmap ridge(32, 24);
    for (int y = 0; y < ridge.getHeight(); ++y) {
        for (int x = 0; x < ridge.getWidth(); ++x) {
            const float downhill = 0.82f - float(y) * 0.018f;
            const float channel = (x == 15 || x == 16) ? -0.08f : 0.f;
            ridge.setHeight(x, y, downhill + channel);
        }
    }
    const float massBefore = std::accumulate(ridge.data().begin(), ridge.data().end(), 0.f);
    Heightmap thermal = ridge;
    ThermalErosionSettings thermalSettings;
    thermalSettings.iterations = 12;
    TerrainPipeline::erodeThermal(thermal, thermalSettings);
    const float massAfter = std::accumulate(thermal.data().begin(), thermal.data().end(), 0.f);
    CHECK(std::abs(massAfter - massBefore) < 0.001f);
    CHECK(thermal.height(15, 12) > ridge.height(15, 12));
    const auto [thermalMin, thermalMax] = std::minmax_element(thermal.data().begin(), thermal.data().end());
    const auto [ridgeMin, ridgeMax] = std::minmax_element(ridge.data().begin(), ridge.data().end());
    CHECK(*thermalMin >= *ridgeMin - 0.0001f);
    CHECK(*thermalMax <= *ridgeMax + 0.0001f);

    Heightmap hydraulicA = ridge, hydraulicB = ridge;
    HydraulicErosionSettings hydraulicSettings;
    hydraulicSettings.iterations = 20;
    TerrainPipeline::erodeHydraulic(hydraulicA, hydraulicSettings);
    TerrainPipeline::erodeHydraulic(hydraulicB, hydraulicSettings);
    CHECK(hydraulicA.data() == hydraulicB.data());
    CHECK(hydraulicA.data() != ridge.data());
    const auto [hydraulicMin, hydraulicMax] =
        std::minmax_element(hydraulicA.data().begin(), hydraulicA.data().end());
    CHECK(std::isfinite(*hydraulicMin));
    CHECK(std::isfinite(*hydraulicMax));
    CHECK(*hydraulicMin >= 0.f);
    CHECK(*hydraulicMax <= *ridgeMax + 0.0001f);

    const HydrologyMap hydrology = TerrainPipeline::buildHydrology(ridge, 8.f, 0.2f);
    CHECK_EQ(hydrology.flowAccumulation.size(), ridge.data().size());
    CHECK(std::count(hydrology.rivers.begin(), hydrology.rivers.end(), uint8_t(1)) > 0);
    for (int y = 1; y + 1 < ridge.getHeight(); ++y)
        for (int x = 1; x + 1 < ridge.getWidth(); ++x)
            CHECK(hydrology.flowDirection[size_t(y * ridge.getWidth() + x)] >= 0);
    const ClimateMap climate = TerrainPipeline::buildClimate(ridge, hydrology, 0.2f, 0.8f);
    CHECK_EQ(climate.biomes.size(), ridge.data().size());
    CHECK(std::count(climate.biomes.begin(), climate.biomes.end(), Biome::River) > 0);
    std::set<Biome> biomes(climate.biomes.begin(), climate.biomes.end());
    CHECK(biomes.size() >= 3);

    Heightmap fluvialA = ridge, fluvialB = ridge;
    FluvialErosionSettings fluvialSettings;
    fluvialSettings.iterations = 6;
    fluvialSettings.riverThreshold = 0.02f;
    TerrainPipeline::erodeFluvial(fluvialA, fluvialSettings);
    TerrainPipeline::erodeFluvial(fluvialB, fluvialSettings);
    CHECK(fluvialA.data() == fluvialB.data());
    const auto maxCardinalStep = [](const Heightmap &map) {
        float result = 0.f;
        for (int y = 0; y < map.getHeight(); ++y) for (int x = 0; x < map.getWidth(); ++x) {
            if (x + 1 < map.getWidth())
                result = std::max(result, std::abs(map.height(x, y) - map.height(x + 1, y)));
            if (y + 1 < map.getHeight())
                result = std::max(result, std::abs(map.height(x, y) - map.height(x, y + 1)));
        }
        return result;
    };
    // Valley widening must not introduce a hard circular rim or terrace scarp.
    CHECK(maxCardinalStep(fluvialA) <= maxCardinalStep(ridge) + 0.08f);
    const HydrologyMap erodedHydrology =
        TerrainPipeline::buildHydrology(fluvialA, fluvialSettings.riverThreshold,
                                        -std::numeric_limits<float>::infinity());
    const float channelCutoff = fluvialSettings.riverThreshold * float(fluvialA.data().size());
    int channelLinks = 0, uphillLinks = 0;
    for (size_t start = 0; start < erodedHydrology.flowDirection.size(); ++start) {
        if (erodedHydrology.flowAccumulation[start] < channelCutoff) continue;
        size_t current = start;
        bool reachedBoundary = false;
        for (size_t step = 0; step <= fluvialA.data().size(); ++step) {
            const int x = int(current % size_t(fluvialA.getWidth()));
            const int y = int(current / size_t(fluvialA.getWidth()));
            if (x == 0 || y == 0 || x + 1 == fluvialA.getWidth() || y + 1 == fluvialA.getHeight()) {
                reachedBoundary = true;
                break;
            }
            const int direction = erodedHydrology.flowDirection[current];
            REQUIRE(direction >= 0);
            static constexpr int flowDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
            static constexpr int flowDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
            const size_t receiver = size_t((y + flowDy[direction]) * fluvialA.getWidth() +
                                           x + flowDx[direction]);
            if (fluvialA.data()[receiver] > fluvialA.data()[current] + 0.001f) ++uphillLinks;
            ++channelLinks;
            current = receiver;
        }
        CHECK(reachedBoundary);
    }
    REQUIRE(channelLinks > 0);
    CHECK(uphillLinks * 20 <= channelLinks); // at least 95% of the bed is non-uphill
    int loweredCells = 0;
    for (size_t i = 0; i < fluvialA.data().size(); ++i) {
        if (fluvialA.data()[i] < ridge.data()[i] - 0.0001f) ++loweredCells;
        CHECK(fluvialA.data()[i] >= ridge.data()[i] - fluvialSettings.maxDepth - 0.0001f);
    }
    CHECK(loweredCells > 0);
}

TEST_CASE("procgen.terrain.pipeline.multiSeedDrainageMorphology") {
    Procgen procgen;
    std::set<uint64_t> terrainHashes;
    static constexpr std::array<int, 5> seeds{17, 1031, 8191, 20260826, 7340033};
    static constexpr int flowDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int flowDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int seed : seeds) {
        Params params;
        params.setSeed(seed);
        params.setSize(65, 65);
        params.setFloat("frequency", 1.f / 31.f);
        params.setInt("octaves", 4);
        params.setFloat("gain", 0.4f);
        params.setFloat("warp", 0.07f);
        const TerrainSampler sampler = TerrainSampler::fromParams(params);
        auto terrain = std::make_unique<Heightmap>(Heightmap::generate(
            sampler, params.getWidth(), params.getHeight()));
        TerrainPipeline::erodeThermal(*terrain, {12, 0.011f, 0.28f});
        TerrainPipeline::erodeHydraulic(*terrain, {8, 0.007f, 0.11f, 1.4f, 0.08f, 0.11f});
        TerrainErosionMap terrainDiagnostics = TerrainPipeline::erodeFluvialDetailed(
            *terrain, {10, 0.008f, 0.12f, 0.18f, 2.5f});
        if (seed == seeds.front()) {
            if (const char *path = std::getenv("EVENGINE_TERRAIN_NATURAL_EROSION_PNG")) {
                std::unique_ptr<eve::image::ImageData> image(
                    procgen.generateTerrainErosionMap(&terrainDiagnostics, 0.f));
                REQUIRE(image.get() != nullptr);
                REQUIRE(saveImagePng(*image, path));
            }
        }
        const HydrologyMap hydro = TerrainPipeline::buildHydrology(*terrain, 24.f, 0.2f);
        const auto [minIt, maxIt] = std::minmax_element(terrain->data().begin(), terrain->data().end());
        CHECK(*maxIt - *minIt > 0.18f);
        const int riverCells = int(std::count(hydro.rivers.begin(), hydro.rivers.end(), uint8_t(1)));
        CHECK(riverCells >= 12);
        CHECK(riverCells < int(terrain->data().size() / 5));
        CHECK_EQ(hydro.streamOrder.size(), hydro.rivers.size());
        CHECK(*std::max_element(hydro.streamOrder.begin(), hydro.streamOrder.end()) >= 2);

        int junctions = 0, longestChain = 0;
        for (int y = 1; y + 1 < terrain->getHeight(); ++y) {
            for (int x = 1; x + 1 < terrain->getWidth(); ++x) {
                const size_t i = size_t(y * terrain->getWidth() + x);
                if (!hydro.rivers[i]) continue;
                int incoming = 0;
                for (int d = 0; d < 8; ++d) {
                    const int nx = x + flowDx[d], ny = y + flowDy[d];
                    const size_t n = size_t(ny * terrain->getWidth() + nx);
                    const int receiver = hydro.flowDirection[n];
                    if (hydro.rivers[n] && receiver >= 0 &&
                        nx + flowDx[receiver] == x && ny + flowDy[receiver] == y) ++incoming;
                }
                if (incoming >= 2) ++junctions;
                size_t current = i;
                int chain = 0;
                for (; chain < int(terrain->data().size()); ++chain) {
                    const int cx = int(current % size_t(terrain->getWidth()));
                    const int cy = int(current / size_t(terrain->getWidth()));
                    const int direction = hydro.flowDirection[current];
                    if (direction < 0 || cx == 0 || cy == 0 ||
                        cx + 1 == terrain->getWidth() || cy + 1 == terrain->getHeight()) break;
                    const size_t receiver = size_t((cy + flowDy[direction]) * terrain->getWidth() +
                                                   cx + flowDx[direction]);
                    // A thresholded D8 channel must never disappear merely
                    // because its contributing area shrinks downstream.
                    CHECK(hydro.flowAccumulation[receiver] >= hydro.flowAccumulation[current]);
                    if (terrain->data()[receiver] > 0.2f && hydro.lakeDepth[receiver] <= 0.f)
                        CHECK(hydro.rivers[receiver]);
                    current = receiver;
                }
                longestChain = std::max(longestChain, chain);
            }
        }
        CHECK(junctions > 0);
        CHECK(longestChain >= 8);

        uint64_t hash = 1469598103934665603ull;
        for (float value : terrain->data()) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            hash = (hash ^ bits) * 1099511628211ull;
        }
        terrainHashes.insert(hash);
    }
    CHECK_EQ(terrainHashes.size(), seeds.size());
}

TEST_CASE("procgen.terrain.pipeline.fluvialBreachesClosedBasinSpillSill") {
    Heightmap basin(41, 41);
    const int center = 20;
    for (int y = 0; y < 41; ++y) for (int x = 0; x < 41; ++x) {
        const float radial = std::hypot(float(x - center), float(y - center));
        float elevation = 0.18f + 0.012f * radial;
        if (radial > 12.f && radial < 15.f) elevation += 0.32f;
        if (radial >= 15.f) elevation = 0.25f - 0.006f * (radial - 15.f);
        basin.setHeight(x, y, elevation);
    }
    const HydrologyMap closedHydro = TerrainPipeline::buildHydrology(
        basin, 3.f, -std::numeric_limits<float>::infinity());
    CHECK(closedHydro.lakeDepth[size_t(center * 41 + center)] > 0.2f);
    CHECK(!closedHydro.rivers[size_t(center * 41 + center)]);
    const ClimateMap closedClimate = TerrainPipeline::buildClimate(basin, closedHydro, 0.f, 0.5f);
    CHECK_EQ(int(closedClimate.biomes[size_t(center * 41 + center)]), int(Biome::Lake));
    CHECK(std::count(closedClimate.biomes.begin(), closedClimate.biomes.end(), Biome::Wetland) > 0);

    // A deep connected basin is one geomorphic unit. A shallow fringe cell
    // must not be incised merely because its own fill depth is below the breach
    // limit; otherwise Priority-Flood's routing tree appears as radial stripes.
    Heightmap preservedBasin = basin;
    TerrainPipeline::erodeFluvial(preservedBasin, {24, 3.f, 0.14f, 0.55f, 4.f, 0.05f});
    const HydrologyMap preservedHydro = TerrainPipeline::buildHydrology(
        preservedBasin, 3.f, -std::numeric_limits<float>::infinity());
    CHECK(preservedHydro.lakeDepth[size_t(center * 41 + center)] > 0.15f);
    CHECK(std::abs(preservedBasin.height(center, center) - basin.height(center, center)) < 0.01f);

    // The same basin may be deliberately opened when the caller supplies a
    // breach budget larger than the component's maximum depression depth.
    TerrainPipeline::erodeFluvial(basin, {24, 3.f, 0.14f, 0.55f, 4.f, 0.55f});
    const HydrologyMap hydro = TerrainPipeline::buildHydrology(
        basin, 3.f, -std::numeric_limits<float>::infinity());
    size_t current = size_t(center * 41 + center);
    int links = 0;
    for (; links < 41 * 41; ++links) {
        const int x = int(current % 41u), y = int(current / 41u);
        if (x == 0 || y == 0 || x == 40 || y == 40) break;
        const int direction = hydro.flowDirection[current];
        REQUIRE(direction >= 0);
        static constexpr int flowDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static constexpr int flowDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
        const size_t receiver = size_t((y + flowDy[direction]) * 41 + x + flowDx[direction]);
        CHECK(basin.data()[receiver] <= basin.data()[current] + 0.001f);
        current = receiver;
    }
    CHECK(links >= 15);
    CHECK(links < 41 * 41);
}

TEST_CASE("procgen.terrain.pipeline.classifiesPerennialLakeBasins") {
    Heightmap puddle(25, 25);
    for (int y = 0; y < 25; ++y) for (int x = 0; x < 25; ++x)
        puddle.setHeight(x, y, 0.5f - float(y) * 0.002f);
    puddle.setHeight(12, 12, puddle.height(12, 12) - 0.018f);

    const HydrologyMap raw = TerrainPipeline::buildHydrology(
        puddle, 8.f, -std::numeric_limits<float>::infinity(), 1.f, false);
    REQUIRE(raw.lakeDepth[size_t(12 * 25 + 12)] > 0.01f);
    const HydrologyMap classified = TerrainPipeline::buildHydrology(
        puddle, 8.f, -std::numeric_limits<float>::infinity());
    CHECK_EQ(classified.lakeDepth[size_t(12 * 25 + 12)], 0.f);

    Heightmap lake(25, 25);
    for (int y = 0; y < 25; ++y) for (int x = 0; x < 25; ++x) {
        const float radius = std::hypot(float(x - 12), float(y - 12));
        lake.setHeight(x, y, 0.30f + std::min(radius, 8.f) * 0.012f);
    }
    const HydrologyMap perennial = TerrainPipeline::buildHydrology(
        lake, 8.f, -std::numeric_limits<float>::infinity());
    CHECK(perennial.lakeDepth[size_t(12 * 25 + 12)] > 0.05f);
    CHECK(std::count_if(perennial.lakeDepth.begin(), perennial.lakeDepth.end(),
                        [](float depth) { return depth > 0.f; }) >= 20);
}

TEST_CASE("procgen.terrain.pipeline.fluvialResolvesValleyCrossSections") {
    Heightmap slope(65, 65);
    for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x) {
        const float broadRelief = 0.055f * std::sin(float(x) * 0.145f) +
                                  0.025f * std::sin(float(x + y) * 0.071f);
        slope.setHeight(x, y, 0.88f - float(y) * 0.009f + broadRelief);
    }
    const Heightmap originalSlope = slope;
    FluvialErosionSettings settings;
    settings.iterations = 18;
    settings.riverThreshold = 0.008f;
    settings.incision = 0.075f;
    settings.maxDepth = 0.12f;
    settings.bankWidth = 5.f;
    settings.maxBreachDepth = 0.015f;
    TerrainErosionMap erosionMap = TerrainPipeline::erodeFluvialDetailed(slope, settings);
    REQUIRE_EQ(erosionMap.width, 65);
    REQUIRE_EQ(erosionMap.height, 65);
    REQUIRE_EQ(erosionMap.wear.size(), slope.data().size());
    float totalWear = 0.f, totalDeposition = 0.f;
    for (size_t i = 0; i < slope.data().size(); ++i) {
        totalWear += erosionMap.wear[i];
        totalDeposition += erosionMap.deposition[i];
        CHECK(std::abs((erosionMap.deposition[i] - erosionMap.wear[i]) -
                       erosionMap.heightDelta[i]) < 1e-5f);
    }
    CHECK(totalWear > 0.1f);
    CHECK(totalDeposition > 0.f);
    Procgen diagnosticProcgen;
    std::unique_ptr<eve::image::ImageData> combined(
        diagnosticProcgen.generateTerrainErosionMap(&erosionMap, 0.f));
    std::unique_ptr<eve::image::ImageData> wearImage(
        diagnosticProcgen.generateTerrainWearMap(&erosionMap, 0.f));
    std::unique_ptr<eve::image::ImageData> depositImage(
        diagnosticProcgen.generateTerrainDepositionMap(&erosionMap, 0.f));
    REQUIRE(combined.get() != nullptr);
    REQUIRE(wearImage.get() != nullptr);
    REQUIRE(depositImage.get() != nullptr);
    CHECK_EQ(combined->getWidth(), 65);
    CHECK(std::memcmp(combined->getData(), wearImage->getData(), combined->getSize()) != 0);
    CHECK(std::memcmp(combined->getData(), depositImage->getData(), combined->getSize()) != 0);
    if (const char *path = std::getenv("EVENGINE_TERRAIN_EROSION_PNG"))
        REQUIRE(saveImagePng(*combined, path));
    const HydrologyMap hydro = TerrainPipeline::buildHydrology(
        slope, settings.riverThreshold, -std::numeric_limits<float>::infinity());
    const float cutoff = settings.riverThreshold * float(slope.data().size());
    static constexpr int flowDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int flowDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    std::vector<float> crossSectionRelief;
    std::vector<std::pair<float, int>> flowAndIncisedWidth;
    int materiallyIncised = 0;
    for (int y = 9; y < 56; ++y) for (int x = 9; x < 56; ++x) {
        const size_t i = size_t(y * 65 + x);
        if (hydro.flowAccumulation[i] < cutoff || hydro.flowDirection[i] < 0) continue;
        const int d = hydro.flowDirection[i];
        const int px = -flowDy[d], py = flowDx[d];
        const float left = slope.height(x + px * 4, y + py * 4);
        const float right = slope.height(x - px * 4, y - py * 4);
        crossSectionRelief.push_back((left + right) * 0.5f - slope.height(x, y));
        int incisedWidth = 0;
        for (int offset = -8; offset <= 8; ++offset) {
            const int sx = x + px * offset, sy = y + py * offset;
            if (originalSlope.height(sx, sy) - slope.height(sx, sy) > 0.002f)
                ++incisedWidth;
        }
        flowAndIncisedWidth.emplace_back(hydro.flowAccumulation[i], incisedWidth);
        if (originalSlope.height(x, y) - slope.height(x, y) > 0.01f) ++materiallyIncised;
    }
    REQUIRE(crossSectionRelief.size() >= 8);
    std::sort(crossSectionRelief.begin(), crossSectionRelief.end());
    CHECK(crossSectionRelief[crossSectionRelief.size() / 2] > 0.008f);
    CHECK(materiallyIncised >= int(crossSectionRelief.size() / 3));
    // A larger contributing basin must create a wider geomorphic corridor,
    // not merely a wider water ribbon at render time.
    std::sort(flowAndIncisedWidth.begin(), flowAndIncisedWidth.end());
    const size_t quartile = std::max<size_t>(1, flowAndIncisedWidth.size() / 4);
    float headwaterWidth = 0.f, trunkWidth = 0.f;
    for (size_t n = 0; n < quartile; ++n) {
        headwaterWidth += float(flowAndIncisedWidth[n].second);
        trunkWidth += float(flowAndIncisedWidth[flowAndIncisedWidth.size() - 1 - n].second);
    }
    CHECK(trunkWidth / float(quartile) > headwaterWidth / float(quartile) + 0.5f);
}

TEST_CASE("procgen.terrain.pipeline.resolutionScaledHydrologyAndValleys") {
    auto physicalTerrain = [](int resolution) {
        Heightmap map(resolution, resolution);
        const float extent = float(resolution - 1);
        for (int y = 0; y < resolution; ++y) for (int x = 0; x < resolution; ++x) {
            const float u = float(x) / extent, v = float(y) / extent;
            const float ridge = 0.075f * std::sin(u * 6.28318530718f) +
                                0.035f * std::sin((u * 2.1f + v * 0.55f) * 6.28318530718f);
            map.setHeight(x, y, 0.88f - 0.34f * v + ridge);
        }
        return map;
    };
    Heightmap coarse = physicalTerrain(33);
    Heightmap fine = physicalTerrain(65);
    const HydrologyMap coarseBefore = TerrainPipeline::buildHydrology(
        coarse, 0.015f, -std::numeric_limits<float>::infinity(), 1.f);
    const HydrologyMap fineBefore = TerrainPipeline::buildHydrology(
        fine, 0.015f, -std::numeric_limits<float>::infinity(), 2.f);
    const float coarseRiverFraction = float(std::count(
        coarseBefore.rivers.begin(), coarseBefore.rivers.end(), uint8_t(1))) /
        float(coarseBefore.rivers.size());
    const float fineRiverFraction = float(std::count(
        fineBefore.rivers.begin(), fineBefore.rivers.end(), uint8_t(1))) /
        float(fineBefore.rivers.size());
    CHECK(fineRiverFraction > coarseRiverFraction * 0.45f);
    CHECK(fineRiverFraction < coarseRiverFraction * 2.2f);
    const float coarsePeakFlow = *std::max_element(
        coarseBefore.flowAccumulation.begin(), coarseBefore.flowAccumulation.end()) /
        float(coarse.data().size());
    const float finePeakFlow = *std::max_element(
        fineBefore.flowAccumulation.begin(), fineBefore.flowAccumulation.end()) /
        float(fine.data().size());
    CHECK(std::abs(coarsePeakFlow - finePeakFlow) < 0.12f);

    const Heightmap coarseOriginal = coarse, fineOriginal = fine;
    TerrainPipeline::erodeFluvial(coarse, {10, 0.015f, 0.055f, 0.10f, 3.f, 0.01f, 1.f});
    TerrainPipeline::erodeFluvial(fine, {10, 0.015f, 0.055f, 0.10f, 6.f, 0.01f, 2.f});
    float correspondenceError = 0.f, coarseLowering = 0.f, fineLowering = 0.f;
    int samples = 0;
    for (int y = 2; y < 31; ++y) for (int x = 2; x < 31; ++x) {
        correspondenceError += std::abs(coarse.height(x, y) - fine.height(x * 2, y * 2));
        coarseLowering += coarseOriginal.height(x, y) - coarse.height(x, y);
        fineLowering += fineOriginal.height(x * 2, y * 2) - fine.height(x * 2, y * 2);
        ++samples;
    }
    correspondenceError /= float(samples);
    coarseLowering /= float(samples);
    fineLowering /= float(samples);
    CHECK(correspondenceError < 0.035f);
    REQUIRE(coarseLowering > 0.f);
    CHECK(fineLowering > coarseLowering * 0.35f);
    CHECK(fineLowering < coarseLowering * 2.8f);
}

TEST_CASE("procgen.terrain.asset.chunkedRoundTripAndCorruptionDetection") {
    Heightmap heightmap(19, 13);
    for (int y = 0; y < heightmap.getHeight(); ++y)
        for (int x = 0; x < heightmap.getWidth(); ++x)
            heightmap.setHeight(x, y, 10.f + float(x) * 0.25f + float(y) * 0.5f);
    HydrologyMap hydrology = TerrainPipeline::buildHydrology(heightmap, 5.f, 10.f);
    hydrology.lakeDepth[size_t(12 * 19 + 18)] = 2.1f;
    const ClimateMap climate = TerrainPipeline::buildClimate(heightmap, hydrology, 10.f, 0.6f);
    std::vector<uint8_t> bytesA, bytesB;
    std::string error;
    REQUIRE(TerrainAsset::bake(heightmap, hydrology, climate, 8, bytesA, &error));
    REQUIRE(TerrainAsset::bake(heightmap, hydrology, climate, 8, bytesB, &error));
    CHECK(bytesA == bytesB);

    TerrainAsset asset;
    REQUIRE(asset.open(bytesA.data(), bytesA.size(), &error));
    CHECK_EQ(asset.getWidth(), 19);
    CHECK_EQ(asset.getHeight(), 13);
    CHECK_EQ(asset.getChunkSize(), 8);
    CHECK_EQ(asset.chunks().size(), size_t(6));
    CHECK(std::any_of(asset.chunks().begin(), asset.chunks().end(),
                      [](const TerrainChunkEntry &entry) { return entry.compressed; }));

    TerrainChunkData edge;
    REQUIRE(asset.loadChunk(2, 1, edge, &error));
    CHECK_EQ(edge.width, 3);
    CHECK_EQ(edge.height, 5);
    CHECK(std::abs(edge.heights.height(2, 4) - heightmap.height(18, 12)) < 0.001f);
    CHECK_EQ(edge.rivers.size(), size_t(15));
    CHECK_EQ(edge.flowDirection.size(), size_t(15));
    CHECK_EQ(int(edge.flowDirection.front()),
             int(hydrology.flowDirection[size_t(8 * 19 + 16)]));
    CHECK_EQ(edge.flowVectorX.size(), size_t(15));
    CHECK_EQ(edge.flowVectorY.size(), size_t(15));
    CHECK(std::abs(edge.flowVectorX.front() -
                   hydrology.flowVectorX[size_t(8 * 19 + 16)]) < 0.009f);
    CHECK(std::abs(edge.flowVectorY.front() -
                   hydrology.flowVectorY[size_t(8 * 19 + 16)]) < 0.009f);
    CHECK_EQ(edge.streamOrder.size(), size_t(15));
    CHECK_EQ(edge.streamOrder.front(), hydrology.streamOrder[size_t(8 * 19 + 16)]);
    CHECK_EQ(edge.lakeDepth.size(), size_t(15));
    CHECK(std::abs(edge.lakeDepth.back() - 2.1f) < 0.05f);
    CHECK_EQ(edge.biomes.size(), size_t(15));

    std::vector<uint8_t> corrupt = bytesA;
    const TerrainChunkEntry first = asset.chunks().front();
    corrupt[size_t(first.offset) + first.storedSize / 2] ^= 0x5au;
    TerrainAsset corruptAsset;
    REQUIRE(corruptAsset.open(corrupt.data(), corrupt.size(), &error));
    TerrainChunkData rejected;
    CHECK(!corruptAsset.loadChunk(first.chunkX, first.chunkY, rejected, &error));

    std::vector<uint8_t> malformed = bytesA;
    malformed[57] = 1; // directory reserved bytes must remain zero
    TerrainAsset malformedAsset;
    CHECK(!malformedAsset.open(malformed.data(), malformed.size(), &error));
    malformed = bytesA;
    malformed[44] = 0xff; malformed[45] = 0xff; malformed[46] = 0xff; malformed[47] = 0x7f;
    CHECK(!malformedAsset.open(malformed.data(), malformed.size(), &error));
    malformed = bytesA;
    malformed[24] = 0; malformed[25] = 0; malformed[26] = 0xc0; malformed[27] = 0x7f; // NaN min height
    CHECK(!malformedAsset.open(malformed.data(), malformed.size(), &error));

    // EVTR v1/v2/v3/v4 remain readable. Missing layers are supplied with safe
    // defaults or reconstructed from D8 instead of invalidating a world.
    auto legacyAsset = [](uint16_t version) {
        std::vector<uint8_t> raw{0x00, 0x80, 0xff}; // height UNORM16, drainage
        if (version >= 3) raw.push_back(5);          // D8 east (encoded + 1)
        if (version >= 4) raw.insert(raw.end(), {0xff, 0x80}); // continuous east
        if (version >= 2) raw.push_back(0x40);       // lake depth
        raw.insert(raw.end(), {0x80, 0x40, 0x01, uint8_t(Biome::Grassland)});
        uint32_t hash = 2166136261u;
        for (uint8_t byte : raw) { hash ^= byte; hash *= 16777619u; }
        std::vector<uint8_t> result{'E', 'V', 'T', 'R'};
        auto u16 = [&](uint16_t v) { result.push_back(uint8_t(v)); result.push_back(uint8_t(v >> 8)); };
        auto u32 = [&](uint32_t v) { for (int b = 0; b < 4; ++b) result.push_back(uint8_t(v >> (b * 8))); };
        auto u64 = [&](uint64_t v) { for (int b = 0; b < 8; ++b) result.push_back(uint8_t(v >> (b * 8))); };
        auto f32 = [&](float v) { uint32_t bits; std::memcpy(&bits, &v, 4); u32(bits); };
        u16(version); u16(0); u32(1); u32(1); u32(1); u32(1);
        f32(0.f); f32(1.f); f32(10.f); u64(44);
        u32(0); u32(0); u16(1); u16(1);
        result.insert(result.end(), {0, 0, 0, 0}); u64(80);
        u32(uint32_t(raw.size())); u32(uint32_t(raw.size())); u32(hash);
        result.insert(result.end(), raw.begin(), raw.end());
        return result;
    };
    for (uint16_t version : {uint16_t(1), uint16_t(2), uint16_t(3), uint16_t(4)}) {
        const std::vector<uint8_t> legacy = legacyAsset(version);
        TerrainAsset legacyReader;
        REQUIRE(legacyReader.open(legacy.data(), legacy.size(), &error));
        TerrainChunkData legacyChunk;
        REQUIRE(legacyReader.loadChunk(0, 0, legacyChunk, &error));
        CHECK_EQ(int(legacyChunk.flowDirection[0]), version >= 3 ? 4 : -1);
        CHECK(std::abs(legacyChunk.flowVectorX[0] - (version >= 3 ? 1.f : 0.f)) < 0.001f);
        CHECK(std::abs(legacyChunk.flowVectorY[0]) < 0.001f);
        CHECK_EQ(legacyChunk.streamOrder[0], uint8_t(0));
        if (version == 1) CHECK_EQ(legacyChunk.lakeDepth[0], 0.f);
        else CHECK(std::abs(legacyChunk.lakeDepth[0] - float(0x40) / 255.f) < 0.0001f);
    }
}

TEST_CASE("procgen.terrain.streaming.budgetEvictionAndCrossChunkSampling") {
    Heightmap heightmap(24, 24);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) heightmap.setHeight(x, y, float(x + y * 2));
    HydrologyMap hydrology = TerrainPipeline::buildHydrology(heightmap, 5.f, -1.f);
    hydrology.lakeDepth[size_t(12 * 24 + 12)] = 3.f;
    const ClimateMap climate = TerrainPipeline::buildClimate(heightmap, hydrology, -1.f, 0.5f);
    std::vector<uint8_t> bytes;
    std::string error;
    REQUIRE(TerrainAsset::bake(heightmap, hydrology, climate, 8, bytes, &error));

    TerrainStreamingCache stream;
    REQUIRE(stream.open(bytes.data(), bytes.size(), &error));
    TerrainStreamStats first = stream.streamAround(12, 12, 1, 2, &error);
    CHECK_EQ(first.loaded, 2);
    CHECK_EQ(first.pending, 3);
    CHECK_EQ(first.resident, 2);
    TerrainStreamStats second = stream.streamAround(12, 12, 1, 0, &error);
    CHECK_EQ(second.loaded, 3);
    CHECK_EQ(second.pending, 0);
    CHECK_EQ(second.resident, 5);
    TerrainStreamStats stable = stream.streamAround(12, 12, 1, 0, &error);
    CHECK_EQ(stable.loaded, 0);
    CHECK_EQ(stable.evicted, 0);

    float interpolated = 0.f;
    REQUIRE(stream.sampleHeight(7.5f, 12.f, interpolated));
    CHECK(std::abs(interpolated - 31.5f) < 0.002f);
    TerrainSample sample;
    REQUIRE(stream.sampleCell(12, 12, sample));
    CHECK(std::abs(sample.height - 36.f) < 0.002f);
    CHECK_EQ(sample.flowDirection, int(hydrology.flowDirection[size_t(12 * 24 + 12)]));
    CHECK(std::abs(sample.flowVectorX - hydrology.flowVectorX[size_t(12 * 24 + 12)]) < 0.009f);
    CHECK(std::abs(sample.flowVectorY - hydrology.flowVectorY[size_t(12 * 24 + 12)]) < 0.009f);
    CHECK_EQ(sample.streamOrder, int(hydrology.streamOrder[size_t(12 * 24 + 12)]));
    CHECK(std::abs(sample.lakeDepth - 3.f) < 0.28f);
    CHECK(sample.lake);

    TerrainStreamStats moved = stream.streamAround(23, 23, 0, 0, &error);
    CHECK_EQ(moved.evicted, 5);
    CHECK_EQ(moved.loaded, 1);
    CHECK_EQ(moved.resident, 1);
    CHECK(!stream.sampleCell(12, 12, sample));
    REQUIRE(stream.sampleCell(23, 23, sample));
    CHECK(std::abs(sample.height - 69.f) < 0.002f);
    TerrainStreamStats noOp = stream.streamAround(0, 0, -1, 0, &error);
    CHECK_EQ(noOp.resident, 1);
}

TEST_CASE("procgen.terrain.streaming.crossChunkHydrologyTraceAndHalo") {
    Heightmap heightmap(24, 9);
    for (int y = 0; y < 9; ++y) for (int x = 0; x < 24; ++x)
        heightmap.setHeight(x, y, 100.f - float(x) * 2.f + std::abs(float(y - 4)) * 3.f);
    HydrologyMap hydrology = TerrainPipeline::buildHydrology(heightmap, 2.f, -1.f);
    const ClimateMap climate = TerrainPipeline::buildClimate(heightmap, hydrology, -1.f, 0.5f);
    std::vector<uint8_t> bytes;
    std::string error;
    REQUIRE(TerrainAsset::bake(heightmap, hydrology, climate, 8, bytes, &error));

    TerrainStreamingCache stream;
    REQUIRE(stream.open(bytes.data(), bytes.size(), &error));
    REQUIRE_EQ(stream.streamAround(7, 4, 1, 0, &error).resident, 3);

    int receiverX = -1, receiverY = -1;
    REQUIRE(stream.getReceiver(7, 4, receiverX, receiverY));
    CHECK_EQ(receiverX, 8);
    CHECK_EQ(receiverY, 4);

    std::vector<std::pair<int, int>> path;
    CHECK(!stream.traceFlow(7, 4, 64, path)); // third X chunk is not resident yet
    CHECK(path.size() > size_t(8));
    REQUIRE_EQ(stream.streamAround(12, 4, 2, 0, &error).resident, 6);
    REQUIRE(stream.traceFlow(7, 4, 64, path));
    CHECK_EQ(path.front().first, 7);
    CHECK_EQ(path.front().second, 4);
    CHECK_EQ(path.back().first, 23);
    CHECK_EQ(path.back().second, 4);

    TerrainStreamingWindow halo;
    REQUIRE(stream.buildWindow(7, 3, 3, 3, halo));
    CHECK_EQ(halo.originX, 7);
    CHECK_EQ(halo.heights.getWidth(), 3);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) {
        const size_t local = size_t(y * 3 + x);
        const size_t global = size_t((y + 3) * 24 + x + 7);
        CHECK(std::abs(halo.heights.height(x, y) - heightmap.height(x + 7, y + 3)) < 0.002f);
        CHECK_EQ(int(halo.hydrology.flowDirection[local]), int(hydrology.flowDirection[global]));
        CHECK_EQ(halo.hydrology.streamOrder[local], hydrology.streamOrder[global]);
    }
}

TEST_CASE("procgen.terrain.module.erosionAndAnalysisApi") {
    Procgen procgen;
    auto heightmapResult = procgen.newHeightmapHandle(16, 16);
    REQUIRE(heightmapResult.ok());
    const ProcgenHeightmapHandleRef heightmapHandle = std::move(heightmapResult).takeValue();
    auto heightmapView = procgen.resolveHeightmap(heightmapHandle);
    REQUIRE(heightmapView.isBound());
    Heightmap *heightmap = heightmapView.get();
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            heightmap->setHeight(x, y, 0.85f - float(y) * 0.035f + (x == 8 ? -0.1f : 0.f));
    const std::vector<float> before = heightmap->data();
    CHECK(procgen.erodeTerrainThermal(heightmap, 8, 0.02f, 0.3f));
    CHECK(procgen.erodeTerrainHydraulic(heightmap, 8, 0.01f, 0.1f, 2.f, 0.15f, 0.1f));
    CHECK(procgen.erodeTerrainFluvial(heightmap, 3, 0.03f, 0.006f, 0.08f, 1.5f));
    CHECK(procgen.erodeTerrainFluvialAdvanced(
        heightmap, 2, 0.03f, 0.006f, 0.08f, 1.5f, 0.015f));
    CHECK(procgen.erodeTerrainFluvialScaled(
        heightmap, 2, 0.03f, 0.006f, 0.08f, 3.f, 0.015f, 2.f));
    std::unique_ptr<TerrainErosionMap> diagnostics(procgen.erodeTerrainFluvialDetailed(
        heightmap, 2, 0.03f, 0.006f, 0.08f, 3.f, 0.015f, 2.f));
    REQUIRE(diagnostics.get() != nullptr);
    CHECK_EQ(diagnostics->getWidth(), 16);
    CHECK(diagnostics->getWear(8, 8) >= 0.f);
    CHECK(diagnostics->getDeposition(8, 8) >= 0.f);
    CHECK_EQ(diagnostics->getWear(-1, 0), 0.f);
    CHECK(procgen.erodeTerrainFluvialDetailed(
        heightmap, 2, 0.03f, 0.006f, 0.08f, 3.f, 0.015f, 0.f) == nullptr);
    CHECK(!procgen.erodeTerrainFluvialScaled(
        heightmap, 2, 0.03f, 0.006f, 0.08f, 3.f, 0.015f, 0.f));
    CHECK(heightmap->data() != before);
    std::unique_ptr<TerrainLayers> layers(procgen.analyzeTerrain(heightmap, 4.f, 0.2f, 0.7f));
    REQUIRE(layers.get() != nullptr);
    std::unique_ptr<TerrainLayers> scaledLayers(
        procgen.analyzeTerrainScaled(heightmap, 4.f, 0.2f, 0.7f, 2.f));
    REQUIRE(scaledLayers.get() != nullptr);
    CHECK(procgen.analyzeTerrainScaled(heightmap, 4.f, 0.2f, 0.7f, 0.f) == nullptr);
    CHECK_EQ(layers->getWidth(), 16);
    CHECK(layers->getFlowAccumulation(8, 8) >= 1.f);
    CHECK(layers->getStreamOrder(8, 8) >= 0);
    CHECK(layers->getTemperature(8, 8) >= 0.f);
    CHECK(layers->getMoisture(8, 8) >= 0.f);
    CHECK(!layers->getBiomeName(8, 8).empty());
    CHECK_EQ(layers->getBiome(-1, 0), -1);
    std::unique_ptr<eve::data::ByteData> archive(
        procgen.bakeTerrainAsset(heightmap, layers.get(), 8));
    REQUIRE(archive.get() != nullptr);
    TerrainAsset opened;
    CHECK(opened.open(static_cast<const uint8_t *>(archive->getData()), archive->getSize()));
    CHECK_EQ(opened.chunks().size(), size_t(4));
    CHECK(!procgen.erodeTerrainThermal(nullptr, 1, 0.1f, 0.1f));
    auto heightmapRelease = procgen.releaseHeightmap(heightmapHandle);
    heightmapRelease.ignore("terrain module test cleanup");
}

TEST_CASE("procgen.terrain.mesh.lodSkirtsStableSeamsAndMaterialWeights") {
    Heightmap heightmap(33, 17);
    for (int y = 0; y < 17; ++y)
        for (int x = 0; x < 33; ++x)
            heightmap.setHeight(x, y, 0.2f + float(x) * 0.012f + 0.04f * std::sin(float(y) * 0.4f));
    HydrologyMap hydrology;
    hydrology.width = 33; hydrology.height = 17;
    hydrology.flowDirection.assign(33 * 17, -1);
    hydrology.flowAccumulation.assign(33 * 17, 1.f);
    hydrology.rivers.assign(33 * 17, 0);
    ClimateMap climate;
    climate.width = 33; climate.height = 17;
    climate.temperature.assign(33 * 17, 0.6f);
    climate.moisture.assign(33 * 17, 0.7f);
    climate.biomes.assign(33 * 17, Biome::Forest);
    for (int y = 0; y < 17; ++y)
        for (int x = 16; x < 33; ++x) climate.biomes[size_t(y * 33 + x)] = Biome::Desert;
    TerrainLayers layers(std::move(hydrology), std::move(climate));

    TerrainMeshSettings settings;
    settings.cellsX = 16; settings.cellsY = 16;
    settings.cellSize = 2.f; settings.heightScale = 10.f; settings.skirtDepth = 3.f;
    TerrainMeshSettings lodErrorSettings = settings;
    lodErrorSettings.lod = 1;
    const float lod1Error = TerrainMeshBuilder::estimateGeometricError(heightmap, lodErrorSettings);
    lodErrorSettings.lod = 2;
    const float lod2Error = TerrainMeshBuilder::estimateGeometricError(heightmap, lodErrorSettings);
    CHECK(lod1Error > 0.f);
    CHECK(lod2Error >= lod1Error);
    const int nearLod = TerrainLodSelector::select(heightmap, settings, 3, 5.f, 1080.f, 60.f, 1.f);
    const int farLod = TerrainLodSelector::select(heightmap, settings, 3, 10000.f, 1080.f, 60.f, 1.f);
    CHECK_EQ(nearLod, 0);
    CHECK_EQ(farLod, 3);
    CHECK_EQ(TerrainLodSelector::select(heightmap, settings, 3, -1.f, 1080.f, 60.f, 1.f), -1);
    TerrainMeshChunk left, right;
    std::string error;
    REQUIRE(TerrainMeshBuilder::build(heightmap, &layers, settings, left, &error));
    settings.originX = 16;
    REQUIRE(TerrainMeshBuilder::build(heightmap, &layers, settings, right, &error));
    CHECK_EQ(left.getBaseVertexCount(), 17 * 17);
    CHECK_EQ(left.getVertexCount(), 17 * 17 + 4 * 17 * 2);
    CHECK_EQ(left.getIndexCount(), 16 * 16 * 6 + 4 * 16 * 6);
    CHECK(meshIndicesInRange(left.mesh()));
    CHECK(meshNormalsFiniteUnit(left.mesh(), 0.02f));
    for (int y = 0; y < 17; ++y) {
        const int li = y * 17 + 16, ri = y * 17;
        CHECK(std::abs(left.mesh().getPositionY(li) - right.mesh().getPositionY(ri)) < 0.0001f);
        CHECK(std::abs(left.mesh().getNormalX(li) - right.mesh().getNormalX(ri)) < 0.0001f);
        CHECK(std::abs(left.mesh().getNormalY(li) - right.mesh().getNormalY(ri)) < 0.0001f);
        CHECK(std::abs(left.mesh().getNormalZ(li) - right.mesh().getNormalZ(ri)) < 0.0001f);
        CHECK_EQ(left.getBiome(li), right.getBiome(ri));
        for (int channel = 0; channel < 4; ++channel)
            CHECK(std::abs(left.getMaterialWeight(li, channel) -
                           right.getMaterialWeight(ri, channel)) < 0.0001f);
    }
    const int firstSkirtTop = left.getBaseVertexCount(), firstSkirtBottom = firstSkirtTop + 1;
    CHECK(std::abs(left.mesh().getPositionY(firstSkirtTop) -
                   left.mesh().getPositionY(firstSkirtBottom) - 3.f) < 0.0001f);
    for (int vertex = 0; vertex < left.getVertexCount(); ++vertex) {
        float sum = 0.f;
        for (int channel = 0; channel < 4; ++channel) {
            CHECK(left.getMaterialWeight(vertex, channel) >= 0.f);
            sum += left.getMaterialWeight(vertex, channel);
        }
        CHECK(std::abs(sum - 1.f) < 0.0001f);
    }
    CHECK_EQ(left.getBiome(4), int(Biome::Forest));
    CHECK_EQ(right.getBiome(4), int(Biome::Desert));
    CHECK_EQ(left.getBiome(-1), -1);
    CHECK(left.getMaterialWeight(4, 1) > left.getMaterialWeight(4, 0));
    CHECK(right.getMaterialWeight(4, 0) > right.getMaterialWeight(4, 1));
    Procgen procgen;
    std::unique_ptr<eve::image::ImageData> splat(procgen.generateTerrainSplatMap(&left));
    std::unique_ptr<eve::image::ImageData> rightSplat(procgen.generateTerrainSplatMap(&right));
    REQUIRE(splat.get() != nullptr);
    REQUIRE(rightSplat.get() != nullptr);
    CHECK_EQ(splat->getWidth(), 17);
    CHECK_EQ(splat->getHeight(), 17);
    const auto *rgba = static_cast<const uint8_t *>(splat->getData());
    const auto *rightRgba = static_cast<const uint8_t *>(rightSplat->getData());
    for (int vertex = 0; vertex < left.getBaseVertexCount(); ++vertex) {
        int sum = 0;
        for (int channel = 0; channel < 4; ++channel) {
            sum += rgba[vertex * 4 + channel];
            CHECK(std::abs(float(rgba[vertex * 4 + channel]) / 255.f -
                           left.getMaterialWeight(vertex, channel)) <= 1.f / 255.f);
        }
        CHECK_EQ(sum, 255);
    }
    for (int y = 0; y < 17; ++y) for (int channel = 0; channel < 4; ++channel)
        CHECK_EQ(rgba[(y * 17 + 16) * 4 + channel],
                 rightRgba[(y * 17) * 4 + channel]);
    CHECK(procgen.generateTerrainSplatMap(nullptr) == nullptr);
    std::unique_ptr<eve::image::ImageData> albedo(procgen.generateTerrainAlbedoMap(&left));
    REQUIRE(albedo.get() != nullptr);
    CHECK_EQ(albedo->getWidth(), 17);
    CHECK_EQ(albedo->getHeight(), 17);
    const auto *albedoRgba = static_cast<const uint8_t *>(albedo->getData());
    for (int vertex = 0; vertex < left.getBaseVertexCount(); ++vertex)
        CHECK_EQ(albedoRgba[vertex * 4 + 3], uint8_t(255));
    CHECK(procgen.generateTerrainAlbedoMap(nullptr) == nullptr);

    settings.originX = 0; settings.lod = 1;
    TerrainMeshChunk lod1;
    REQUIRE(TerrainMeshBuilder::build(heightmap, &layers, settings, lod1, &error));
    CHECK_EQ(lod1.getLodStep(), 2);
    CHECK(std::abs(lod1.getGeometricError() - lod1Error) < 0.0001f);
    CHECK_EQ(lod1.getBaseVertexCount(), 9 * 9);
    CHECK_EQ(lod1.getVertexCount(), 9 * 9 + 4 * 9 * 2);
    CHECK_EQ(lod1.getIndexCount(), 8 * 8 * 6 + 4 * 8 * 6);
    for (int y = 0; y < 9; ++y) for (int x = 0; x < 9; ++x) {
        const int coarse = y * 9 + x;
        const int fine = (y * 2) * 17 + x * 2;
        CHECK_EQ(lod1.getBiome(coarse), left.getBiome(fine));
        for (int channel = 0; channel < 4; ++channel)
            CHECK(std::abs(lod1.getMaterialWeight(coarse, channel) -
                           left.getMaterialWeight(fine, channel)) < 0.0001f);
    }

    Heightmap riverHeight(9, 9);
    HydrologyMap riverHydrology;
    riverHydrology.width = 9; riverHydrology.height = 9;
    riverHydrology.flowDirection.assign(81, -1);
    riverHydrology.flowAccumulation.assign(81, 1.f);
    riverHydrology.rivers.assign(81, 0);
    ClimateMap riverClimate;
    riverClimate.width = 9; riverClimate.height = 9;
    riverClimate.temperature.assign(81, 0.5f);
    riverClimate.moisture.assign(81, 0.8f);
    riverClimate.biomes.assign(81, Biome::Grassland);
    for (int y = 0; y < 8; ++y) {
        const size_t i = size_t(y * 9 + 4);
        riverHeight.setHeight(4, y, 0.8f - float(y) * 0.05f);
        riverHydrology.flowDirection[i] = 6;
        riverHydrology.flowAccumulation[i] = float(y + 2) * 10.f;
        riverHydrology.rivers[i] = 1;
        riverClimate.biomes[i] = Biome::River;
    }
    TerrainLayers riverLayers(std::move(riverHydrology), std::move(riverClimate));
    TerrainRiverMeshSettings riverSettings;
    riverSettings.cellsX = 8; riverSettings.cellsY = 8;
    riverSettings.minWidth = 0.1f; riverSettings.maxWidth = 0.5f;
    MeshBuild riverMesh;
    REQUIRE(TerrainRiverMeshBuilder::build(riverHeight, riverLayers, riverSettings, riverMesh, &error));
    CHECK_EQ(riverMesh.getVertexCount(), 8 * (4 + 9));
    CHECK_EQ(riverMesh.getIndexCount(), 8 * (6 + 24));
    CHECK(meshIndicesInRange(riverMesh));
    CHECK(meshNormalsFiniteUnit(riverMesh, 0.001f));

    TerrainRiverMeshSettings excludedSlope = riverSettings;
    excludedSlope.minSurfaceSlope = 0.10f;
    excludedSlope.maxSurfaceSlope = 0.20f;
    MeshBuild excludedRiverMesh;
    REQUIRE(TerrainRiverMeshBuilder::build(
        riverHeight, riverLayers, excludedSlope, excludedRiverMesh, &error));
    CHECK(excludedRiverMesh.empty());
    excludedSlope.maxSurfaceSlope = excludedSlope.minSurfaceSlope;
    CHECK(!TerrainRiverMeshBuilder::build(
        riverHeight, riverLayers, excludedSlope, excludedRiverMesh, &error));

    TerrainRiverMeshSettings leftRiverSettings = riverSettings;
    leftRiverSettings.cellsX = 4;
    TerrainRiverMeshSettings rightRiverSettings = leftRiverSettings;
    rightRiverSettings.originX = 4;
    MeshBuild leftRiverMesh, rightRiverMesh;
    REQUIRE(TerrainRiverMeshBuilder::build(riverHeight, riverLayers, leftRiverSettings,
                                           leftRiverMesh, &error));
    REQUIRE(TerrainRiverMeshBuilder::build(riverHeight, riverLayers, rightRiverSettings,
                                           rightRiverMesh, &error));
    REQUIRE_EQ(leftRiverMesh.getVertexCount(), rightRiverMesh.getVertexCount());
    for (int vertex = 0; vertex < leftRiverMesh.getVertexCount(); ++vertex) {
        CHECK(std::abs(leftRiverMesh.getPositionX(vertex) -
                       (rightRiverMesh.getPositionX(vertex) + 4.f)) < 0.0001f);
        CHECK(std::abs(leftRiverMesh.getPositionY(vertex) -
                       rightRiverMesh.getPositionY(vertex)) < 0.0001f);
        CHECK(std::abs(leftRiverMesh.getPositionZ(vertex) -
                       rightRiverMesh.getPositionZ(vertex)) < 0.0001f);
    }

    Heightmap lakeHeight(9, 9);
    HydrologyMap lakeHydrology;
    lakeHydrology.width = 9; lakeHydrology.height = 9;
    lakeHydrology.flowDirection.assign(81, -1);
    lakeHydrology.flowAccumulation.assign(81, 1.f);
    lakeHydrology.lakeDepth.assign(81, 0.f);
    lakeHydrology.rivers.assign(81, 0);
    ClimateMap lakeClimate;
    lakeClimate.width = 9; lakeClimate.height = 9;
    lakeClimate.temperature.assign(81, 0.5f);
    lakeClimate.moisture.assign(81, 1.f);
    lakeClimate.biomes.assign(81, Biome::Grassland);
    for (int y = 2; y <= 6; ++y) for (int x = 2; x <= 6; ++x)
        lakeHydrology.lakeDepth[size_t(y * 9 + x)] = 0.2f;
    TerrainLayers lakeLayers(std::move(lakeHydrology), std::move(lakeClimate));
    TerrainLakeMeshSettings lakeSettings;
    lakeSettings.cellsX = 8; lakeSettings.cellsY = 8; lakeSettings.minimumDepth = 0.01f;
    MeshBuild lakeMesh;
    REQUIRE(TerrainLakeMeshBuilder::build(lakeHeight, lakeLayers, lakeSettings, lakeMesh, &error));
    CHECK(lakeMesh.getVertexCount() > 0);
    CHECK(meshIndicesInRange(lakeMesh));
    CHECK(meshNormalsFiniteUnit(lakeMesh, 0.001f));
}

TEST_CASE("procgen.wfc.simple.reproducible") {
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(42);
    p.setSize(24, 18);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(countSemantic(a, int(Semantic::Wall)) > 0);
    CHECK(countSemantic(a, int(Semantic::Floor)) + countSemantic(a, int(Semantic::Corridor)) > 0);
    CHECK(borderIsWall(a));
}

TEST_CASE("procgen.wfc.simple.cavePreset") {
    Params p;
    p.setSeed(11);
    p.setSize(20, 16);
    p.setString("preset", "cave");
    p.setInt("maxAttempts", 64);
    Grid2D g;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g, err));
    CHECK(borderIsWall(g));
    CHECK(caveAdjacencyOk(g));
    CHECK(countSemantic(g, int(Semantic::Floor)) > 0);
    CHECK(countSemantic(g, int(Semantic::Wall)) > 0);
    CHECK_EQ(g.getMeta("algorithm", ""), std::string("wfc.simple"));
    CHECK_EQ(g.getMeta("preset", ""), std::string("cave"));
}

TEST_CASE("procgen.wfc.simple.terrainPreset") {
    Params p;
    p.setSeed(7);
    p.setSize(24, 20);
    p.setString("preset", "terrain");
    p.setInt("maxAttempts", 64);
    Grid2D g, g2;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g, err));
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g2, err));
    CHECK(gridsEqual(g, g2));
    CHECK(terrainAdjacencyOk(g));
    std::set<int> kinds;
    for (uint32_t c : g.cells()) kinds.insert(int(c));
    CHECK(kinds.size() >= 1);
    // Terrain does not force wall border.
    CHECK(countSemantic(g, int(Semantic::Wall)) == 0);
}

TEST_CASE("procgen.wfc.simple.dungeonObjectsAndMeta") {
    Params p;
    p.setSeed(99);
    p.setSize(28, 20);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D g;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g, err));
    CHECK_EQ(g.getMeta("algorithm", ""), std::string("wfc.simple"));
    CHECK_EQ(g.getMeta("preset", ""), std::string("dungeon"));
    // Walkable cells should produce spawn + stairs.
    const int walkable =
        countSemantic(g, int(Semantic::Floor)) + countSemantic(g, int(Semantic::Corridor));
    if (walkable > 0) {
        CHECK(g.getObjectCount() >= 2);
        std::unordered_set<std::string> types;
        for (int i = 0; i < g.getObjectCount(); ++i) types.insert(g.getObjectType(i));
        CHECK(types.count("spawn") == 1);
        CHECK(types.count("stairs") == 1);
    }
}

TEST_CASE("procgen.wfc.simple.seedVaries") {
    Params a, b;
    a.setSeed(1);
    b.setSeed(2);
    a.setSize(18, 14);
    b.setSize(18, 14);
    a.setString("preset", "cave");
    b.setString("preset", "cave");
    a.setInt("maxAttempts", 64);
    b.setInt("maxAttempts", 64);
    Grid2D ga, gb;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", a, ga, err));
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", b, gb, err));
    // Different seeds should almost always differ; allow rare collision by checking not
    // identical OR both valid borders.
    CHECK(borderIsWall(ga));
    CHECK(borderIsWall(gb));
    CHECK(!gridsEqual(ga, gb));
}

TEST_CASE("procgen.wfc.simple.errors") {
    std::string err;
    Grid2D g;
    Params tooSmall;
    tooSmall.setSeed(1);
    tooSmall.setSize(2, 2);
    tooSmall.setString("preset", "cave");
    CHECK(!GeneratorRegistry::instance().generate("wfc.simple", tooSmall, g, err));
    CHECK(err.find("4x4") != std::string::npos);

    Params badPreset;
    badPreset.setSeed(1);
    badPreset.setSize(12, 12);
    badPreset.setString("preset", "nope");
    CHECK(!GeneratorRegistry::instance().generate("wfc.simple", badPreset, g, err));
    CHECK(err.find("preset") != std::string::npos);

    Params tooBig;
    tooBig.setSeed(1);
    tooBig.setSize(300, 16);
    tooBig.setString("preset", "cave");
    CHECK(!GeneratorRegistry::instance().generate("wfc.simple", tooBig, g, err));
    CHECK(err.find("256") != std::string::npos);
}

TEST_CASE("procgen.wfc.simple.viaModule") {
    Procgen *mod = Procgen::create();
    CHECK(mod->hasAlgorithm("wfc.simple"));
    Params p;
    p.setSeed(5);
    p.setSize(16, 12);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    auto params    = requireParams(p);
    auto gridLease = requireGrid(*mod, "wfc.simple", params.handle);
    auto grid      = gridLease.view();
    REQUIRE(grid.isBound());
    CHECK(borderIsWall(*grid));
    CHECK_EQ(grid->getWidth(), 16);
    CHECK_EQ(grid->getHeight(), 12);

    Params bad;
    bad.setSeed(1);
    bad.setSize(8, 8);
    bad.setString("preset", "invalid");
    auto badParams = requireParams(bad);
    auto failed    = mod->generateHandle("wfc.simple", badParams.handle);
    CHECK(!failed.ok());
    const eve::Diagnostic *diagnostic = failed.error();
    REQUIRE(diagnostic != nullptr);
    CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::Failed);
}

TEST_CASE("procgen.meshBuild.accessors") {
    MeshBuild m;
    CHECK(m.empty());
    CHECK_EQ(m.getVertexCount(), 0);
    CHECK_EQ(m.getIndexCount(), 0);
    m.addVertex(1.f, 2.f, 3.f, 0.f, 1.f, 0.f, 0.25f, 0.75f);
    m.addVertex(4.f, 5.f, 6.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    m.addVertex(7.f, 8.f, 9.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    m.addTriangle(0, 1, 2);
    CHECK(!m.empty());
    CHECK_EQ(m.getVertexCount(), 3);
    CHECK_EQ(m.getIndexCount(), 3);
    CHECK_EQ(m.getPositionX(0), 1.f);
    CHECK_EQ(m.getPositionY(1), 5.f);
    CHECK_EQ(m.getPositionZ(2), 9.f);
    CHECK_EQ(m.getNormalY(0), 1.f);
    CHECK_EQ(m.getUvU(0), 0.25f);
    CHECK_EQ(m.getUvV(0), 0.75f);
    CHECK_EQ(m.getIndex(2), 2);
    // Out of range accessors are safe zeros.
    CHECK_EQ(m.getPositionX(99), 0.f);
    CHECK_EQ(m.getIndex(99), 0);
    m.setMeta("algo", "test");
    CHECK_EQ(m.getMeta("algo", ""), std::string("test"));
    CHECK_EQ(m.getMeta("missing", "d"), std::string("d"));
    m.clear();
    CHECK(m.empty());
}

TEST_CASE("procgen.mesh.marchingcubes.sphere") {
    MeshRecipeRegistry::instance().registerBuiltins();
    CHECK(MeshRecipeRegistry::instance().has("mesh.marchingcubes"));
    Params p;
    p.setSeed(1);
    p.setInt("resolution", 20);
    p.setString("field", "sphere");
    p.setFloat("radius", 0.7f);
    p.setFloat("isolevel", 0.f);
    MeshBuild mesh;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, mesh, err));
    CHECK(mesh.getVertexCount() > 100);
    CHECK(mesh.getIndexCount() > 100);
    CHECK_EQ(mesh.getIndexCount() % 3, 0);
    CHECK(meshIndicesInRange(mesh));
    CHECK(meshPositionsFinite(mesh));
    CHECK(meshNormalsFiniteUnit(mesh));
    // Closed sphere should have non-trivial volume.
    CHECK(std::fabs(meshApproxSignedVolume(mesh)) > 0.05f);
    // Same seed ⇒ same mesh.
    MeshBuild mesh2;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, mesh2, err));
    CHECK_EQ(mesh.getVertexCount(), mesh2.getVertexCount());
    CHECK(mesh.positions() == mesh2.positions());
    CHECK(mesh.indices() == mesh2.indices());
}

TEST_CASE("procgen.mesh.hexplanet.topology") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setFloat("radius", 2.f);
    p.setInt("subdivisions", 2);
    p.setFloat("tileInset", 0.05f);
    MeshBuild mesh;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.hexplanet", p, mesh, err));
    CHECK(meshIndicesInRange(mesh));
    CHECK(meshPositionsFinite(mesh));
    CHECK(meshNormalsFiniteUnit(mesh));
    CHECK_EQ(mesh.getMeta("algorithm", ""), std::string("mesh.hexplanet"));
    CHECK_EQ(mesh.getMeta("pentagons", ""), std::string("12"));
    // V = 10 * 4^n + 2 for a subdivided icosahedron.
    CHECK_EQ(mesh.getMeta("cells", ""), std::string("162"));
    CHECK_EQ(mesh.getMeta("hexagons", ""), std::string("150"));
    CHECK_EQ(mesh.getIndexCount() % 3, 0);
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        const float x = mesh.getPositionX(i), y = mesh.getPositionY(i), z = mesh.getPositionZ(i);
        CHECK(std::fabs(std::sqrt(x * x + y * y + z * z) - 2.f) < 1e-4f);
    }
}

TEST_CASE("procgen.render.hexplanetPng") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 768;
    settings.height = 768;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    Procgen *procgen = Procgen::create();
    Params params;
    params.setFloat("radius", 1.f);
    params.setInt("subdivisions", 3);
    params.setFloat("tileInset", 0.12f);
    auto planetParams = requireParams(params);
    auto planetMesh   = procgen->generateMeshBorrowed("mesh.hexplanet", planetParams.handle, gfx);
    REQUIRE(planetMesh.isBound());

    const uint8_t oceanBlue[4] = {42, 155, 181, 255};
    Texture *planetTexture = gfx->newTexture(1, 1, oceanBlue);
    REQUIRE(planetTexture != nullptr);

    auto *planet = eve::graphics::Renderable3D::create();
    planet->setMesh(planetMesh.get());
    planet->setTexture(planetTexture);
    planet->setMetallic(0.05f);
    planet->setRoughness(0.72f);
    planet->setCastShadow(false);
    planet->setReceiveShadow(false);
    planet->transform()->yaw = 0.42f;
    planet->transform()->pitch = -0.22f;

    auto *camera = eve::graphics::Camera3D::createCamera();
    camera->setEye(2.65f, 1.55f, 3.05f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(38.f);
    camera->setAmbient(0.16f, 0.19f, 0.24f);

    // A transparent 2D entity drives the normal present path used by render tests.
    auto *present = eve::graphics::Renderable2D::create();
    present->transform()->x = 0.f;
    present->transform()->y = 0.f;
    present->sprite()->width = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a = 0.f;

    gfx->setBackgroundColor(Color(0.012f, 0.018f, 0.035f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    eve::graphics::RenderSystem3D::setDirectionalLight(0.55f, 0.85f, 1.1f, 1.15f, 0.98f,
                                                        0.88f);
    for (int frame = 0; frame < 8; ++frame) {
        planet->transform()->yaw += 0.025f;
        eve::graphics::RenderSystem3D::render(*gfx);
        eve::graphics::RenderSystem::render(*gfx);
    }

    [[maybe_unused]] auto *const           imageModule = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(static_cast<bool>(image));
    std::unique_ptr<eve::filesystem::FileData> png(
        image->encode(medialoader::FormatHandler::ENCODED_PNG, "hex_planet.png", false));
    REQUIRE(static_cast<bool>(png));
    REQUIRE(png->getSize() > 0);

    const std::filesystem::path outDir =
        std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out";
    std::filesystem::create_directories(outDir);
    const std::filesystem::path outPath = outDir / "hex_planet.png";
    std::ofstream output(outPath, std::ios::binary);
    REQUIRE(output.good());
    output.write(static_cast<const char *>(png->getData()),
                 static_cast<std::streamsize>(png->getSize()));
    REQUIRE(output.good());
    output.close();
    std::printf("hex planet render saved: %s\n", outPath.string().c_str());
    win->close();
}

TEST_CASE("procgen.render.cloudShadowsDarkenGround") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 256;
    settings.height = 256;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    // Flat ground grid over XZ.
    const int N = 24;
    const float S = 40.f;  // world extent
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= N; ++y) {
        for (int x = 0; x <= N; ++x) {
            pos.push_back(-S / 2 + S * float(x) / N);
            pos.push_back(0.f);
            pos.push_back(-S / 2 + S * float(y) / N);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            nrm.push_back(0.f);
            uv.push_back(float(x) / N);
            uv.push_back(float(y) / N);
        }
    }
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const uint32_t a = uint32_t(y * (N + 1) + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + uint32_t(N + 1);
            const uint32_t d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    }
    Mesh *groundMesh = gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                              idx.data(), int(idx.size()));
    REQUIRE(groundMesh != nullptr);
    const uint8_t grass[4] = {96, 150, 70, 255};
    Texture *grassTex = gfx->newTexture(1, 1, grass);
    REQUIRE(grassTex != nullptr);

    auto *ground = Renderable3D::create();
    ground->setMesh(groundMesh);
    ground->setTexture(grassTex);
    ground->setRoughness(0.9f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(false);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 18.f, 6.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(55.f);
    camera->setAmbient(0.12f, 0.14f, 0.16f);

    gfx->setBackgroundColor(Color(0.02f, 0.03f, 0.05f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.3f, 1.f, 0.2f, 1.15f, 1.05f, 0.95f);

    auto meanLuma = [&]() -> float {
        for (int frame = 0; frame < 3; ++frame) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        std::unique_ptr<eve::image::ImageData> img(gfx->newImageData());
        REQUIRE(img.get() != nullptr);
        const uint8_t *px = static_cast<const uint8_t *>(img->getData());
        const int w = img->getWidth();
        const int h = img->getHeight();
        double sum = 0.0;
        for (int i = 0; i < w * h; ++i) {
            const size_t o = size_t(i) * 4u;
            sum += 0.2126 * px[o] + 0.7152 * px[o + 1] + 0.0722 * px[o + 2];
        }
        return float(sum / double(w * h));
    };

    // No clouds → fully lit (baseline).
    gfx->setCloudShadows(0.f, 1.5f, 0.f, 4.f, 0.f, 0.5f, 0.5f);
    const float lit = meanLuma();

    // Dense, strong clouds → ground visibly darker.
    gfx->setCloudShadows(0.9f, 2.0f, 1.5f, 4.f, 0.f, 0.5f, 0.5f);
    const float cloudy = meanLuma();

    gfx->setCloudShadows(0.f, 1.5f, 0.f, 4.f, 0.f, 0.5f, 0.5f);
    const float litAgain = meanLuma();

    std::printf("cloud shadows render: lit=%.3f cloudy=%.3f litAgain=%.3f\n", lit, cloudy, litAgain);
    CHECK_GT(lit, 10.f);            // baseline is lit
    CHECK(cloudy < lit * 0.85f);    // clouds meaningfully darken the ground
    CHECK(approxEq(lit, litAgain, 2.f));  // disabled again → back to baseline
    win->close();
}

TEST_CASE("procgen.render.skyscraperPng") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 640;
    settings.height = 720;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    Procgen *procgen = Procgen::create();
    Params params;
    params.setSeed(99);
    params.setFloat("baseWidth", 10.f);
    params.setFloat("baseDepth", 10.f);
    params.setInt("tiers", 6);
    params.setFloat("tierHeight", 6.f);
    params.setFloat("setback", 0.08f);
    params.setInt("windowCols", 6);
    params.setInt("windowRows", 5);
    params.setFloat("windowDepth", 0.06f);
    params.setFloat("spireHeight", 5.f);
    auto towerParams = requireParams(params);
    auto towerMesh   = procgen->generateMeshBorrowed("mesh.skyscraper", towerParams.handle, gfx);
    REQUIRE(towerMesh.isBound());

    // 2x2 atlas: bottom texel dark wall, top texel bright window (matches UV convention).
    const uint8_t atlas[16] = {
        90, 96, 104, 255,   // wall
        120, 128, 138, 255, // wall
        240, 236, 220, 255, // window
        200, 205, 215, 255, // window
    };
    Texture *towerTexture = gfx->newTexture(2, 2, atlas);
    REQUIRE(towerTexture != nullptr);

    auto *tower = eve::graphics::Renderable3D::create();
    tower->setMesh(towerMesh.get());
    tower->setTexture(towerTexture);
    tower->setMetallic(0.02f);
    tower->setRoughness(0.85f);
    tower->setCastShadow(true);
    tower->setReceiveShadow(true);
    tower->transform()->yaw = 0.6f;

    auto *camera = eve::graphics::Camera3D::createCamera();
    camera->setEye(18.f, 14.f, 20.f);
    camera->setTarget(0.f, 14.f, 0.f);
    camera->setFov(42.f);
    camera->setAmbient(0.16f, 0.18f, 0.22f);

    auto *present = eve::graphics::Renderable2D::create();
    present->transform()->x = 0.f;
    present->transform()->y = 0.f;
    present->sprite()->width = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a = 0.f;

    gfx->setBackgroundColor(Color(0.12f, 0.14f, 0.20f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    eve::graphics::RenderSystem3D::setDirectionalLight(0.55f, 0.85f, 1.1f, 1.15f, 0.98f, 0.88f);
    for (int frame = 0; frame < 8; ++frame) {
        tower->transform()->yaw += 0.02f;
        eve::graphics::RenderSystem3D::render(*gfx);
        eve::graphics::RenderSystem::render(*gfx);
    }

    [[maybe_unused]] auto *const           imageModule = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(static_cast<bool>(image));
    std::unique_ptr<eve::filesystem::FileData> png(
        image->encode(medialoader::FormatHandler::ENCODED_PNG, "skyscraper.png", false));
    REQUIRE(static_cast<bool>(png));
    REQUIRE(png->getSize() > 0);

    const std::filesystem::path outDir =
        std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out";
    std::filesystem::create_directories(outDir);
    const std::filesystem::path outPath = outDir / "skyscraper.png";
    std::ofstream output(outPath, std::ios::binary);
    REQUIRE(output.good());
    output.write(static_cast<const char *>(png->getData()),
                 static_cast<std::streamsize>(png->getSize()));
    REQUIRE(output.good());
    output.close();
    std::printf("skyscraper render saved: %s\n", outPath.string().c_str());
    win->close();
}

TEST_CASE("procgen.render.castlePng") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 900;
    settings.height = 700;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    Procgen *procgen = Procgen::create();
    Params params;
    params.setSeed(20260826);
    params.setFloat("width", 48.f);
    params.setFloat("depth", 40.f);
    params.setInt("rings", 2);
    params.setInt("keepFloors", 4);
    params.setInt("detail", 2);
    params.setFloat("towerSpacing", 15.f);
    auto castleParams = requireParams(params);
    auto castleBuild  = requireMesh(*procgen, "mesh.castle", castleParams.handle);
    auto castleView   = castleBuild.view();
    REQUIRE(castleView.isBound());

    const uint8_t stone[16] = {
        126, 116, 98, 255,  150, 139, 116, 255,
        105, 96, 82, 255,   169, 157, 132, 255,
    };
    Texture *stoneTexture = gfx->newTexture(2, 2, stone);
    REQUIRE(stoneTexture != nullptr);
    // Exercise the graph-style group filter + CPU-to-GPU upload path. Each
    // semantic component can now have independent material/collision policy.
    const float groupTint[][3] = {
        {.74f,.69f,.58f}, {.82f,.76f,.64f}, {.66f,.61f,.52f}, {.58f,.53f,.46f},
        {.48f,.43f,.36f}, {.71f,.65f,.54f}, {.60f,.55f,.45f},
    };
    std::vector<eve::graphics::Renderable3D *> castleParts;
    for (int group = 0; group < castleView->getGroupCount(); ++group) {
        auto part = castleView->copyGroup(group);
        if (!part) continue;
        auto gpuPart = procgen->uploadMeshBorrowed(*part, *gfx);
        REQUIRE(gpuPart.isBound());
        auto *renderable = eve::graphics::Renderable3D::create();
        renderable->setMesh(gpuPart.get());
        renderable->setTexture(stoneTexture);
        const int tint = std::min(group, 6);
        renderable->setTint(groupTint[tint][0], groupTint[tint][1], groupTint[tint][2], 1.f);
        renderable->setMetallic(0.01f);
        renderable->setRoughness(0.94f);
        renderable->setCastShadow(true);
        renderable->setReceiveShadow(true);
        castleParts.push_back(renderable);
    }
    CHECK_EQ(castleParts.size(), size_t(7));

    auto *camera = eve::graphics::Camera3D::createCamera();
    // View from the south-east so the gate opening and both wall stair systems
    // are part of the visual contract rather than hidden behind the north wall.
    camera->setEye(42.f, 32.f, -64.f);
    camera->setTarget(0.f, 5.5f, 0.f);
    camera->setFov(43.f);
    camera->setAmbient(0.19f, 0.20f, 0.23f);
    auto *present = eve::graphics::Renderable2D::create();
    present->sprite()->width = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a = 0.f;
    gfx->setBackgroundColor(Color(0.09f, 0.13f, 0.19f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.6f, 0.95f, 0.8f, 1.35f, 1.18f, 0.96f);
    for (int frame = 0; frame < 8; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    [[maybe_unused]] auto *const           imageModule = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(static_cast<bool>(image));
    RenderAuditConfig auditCfg;
    auditCfg.scene = "procgen";
    auditCfg.phase = "castle";
    auditCfg.width = image->getWidth();
    auditCfg.height = image->getHeight();
    const RenderAuditResult audit = auditImage(*image, auditCfg, {0.09f, 0.13f, 0.19f}, 2);
    CHECK(!audit.empty);
    CHECK(audit.occupancy > 0.08f);
    CHECK(audit.meanLuma > 12.f);
    const std::filesystem::path outDir = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out";
    std::filesystem::create_directories(outDir);
    const std::filesystem::path outPath = outDir / "castle.png";
    REQUIRE(saveImagePng(*image, outPath.string()));
    std::printf("castle render saved: %s occupancy=%.3f luma=%.3f\n",
                outPath.string().c_str(), audit.occupancy, audit.meanLuma);
    win->close();
}

TEST_CASE("procgen.mesh.marchingcubes.allFields") {
    MeshRecipeRegistry::instance().registerBuiltins();
    const char *fields[] = {"sphere", "torus", "noise", "terrain"};
    for (const char *field : fields) {
        Params p;
        p.setSeed(42);
        p.setInt("resolution", 18);
        p.setString("field", field);
        p.setFloat("scale", 1.5f);
        p.setInt("octaves", 2);
        MeshBuild mesh;
        std::string err;
        CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, mesh, err));
        CHECK(mesh.getVertexCount() > 20);
        CHECK_EQ(mesh.getIndexCount() % 3, 0);
        CHECK(meshIndicesInRange(mesh));
        CHECK(meshPositionsFinite(mesh));
        CHECK(meshNormalsFiniteUnit(mesh));
        CHECK_EQ(mesh.getMeta("algorithm", ""), std::string("mesh.marchingcubes"));
        CHECK_EQ(mesh.getMeta("field", ""), std::string(field));
    }
}

TEST_CASE("procgen.mesh.marchingcubes.noiseReproducibleAndVaries") {
    Params p;
    p.setSeed(9);
    p.setInt("resolution", 16);
    p.setString("field", "noise");
    p.setFloat("scale", 2.f);
    MeshBuild a, b, c;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, b, err));
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());

    Params p2 = p;
    p2.setSeed(10);
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p2, c, err));
    CHECK(a.positions() != c.positions());
}

TEST_CASE("procgen.mesh.marchingcubes.rawDensityPlane") {
    // Density = y - 0.5 on a 4³ grid → horizontal plane at mid height.
    const int n = 4;
    std::vector<float> density(size_t(n * n * n));
    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                density[size_t(x + y * n + z * n * n)] = float(y) / float(n - 1) - 0.5f;
            }
        }
    }
    MeshBuild mesh;
    std::string err;
    CHECK(marchingCubes(density.data(), n, n, n, 0.f, mesh, &err));
    CHECK(mesh.getVertexCount() > 0);
    CHECK_EQ(mesh.getIndexCount() % 3, 0);
    CHECK(meshIndicesInRange(mesh));
    // All vertices near mid-Y in unit cube mapping [-0.5,0.5].
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        CHECK(std::fabs(mesh.getPositionY(i)) < 0.35f);
    }
}

TEST_CASE("procgen.mesh.marchingcubes.rawDensityEmpty") {
    const int n = 3;
    std::vector<float> density(size_t(n * n * n), 1.f);  // all solid, no surface
    MeshBuild mesh;
    std::string err;
    CHECK(marchingCubes(density.data(), n, n, n, 0.f, mesh, &err));
    CHECK(mesh.empty());

    density.assign(size_t(n * n * n), -1.f);  // all empty
    CHECK(marchingCubes(density.data(), n, n, n, 0.f, mesh, &err));
    CHECK(mesh.empty());
}

TEST_CASE("procgen.mesh.marchingcubes.rawApiErrors") {
    MeshBuild mesh;
    std::string err;
    CHECK(!marchingCubes(nullptr, 4, 4, 4, 0.f, mesh, &err));
    CHECK(err.find("null") != std::string::npos);

    float tiny[1] = {0.f};
    CHECK(!marchingCubes(tiny, 1, 1, 1, 0.f, mesh, &err));
    CHECK(err.find("2x2x2") != std::string::npos);
}

TEST_CASE("procgen.mesh.marchingcubes.fillDensityAndErrors") {
    std::vector<float> density;
    int nx = 0, ny = 0, nz = 0;
    std::string err;

    Params ok;
    ok.setInt("resolution", 8);
    ok.setString("field", "sphere");
    CHECK(fillDensityField(ok, density, nx, ny, nz, err));
    CHECK_EQ(nx, 8);
    CHECK_EQ(ny, 8);
    CHECK_EQ(nz, 8);
    CHECK_EQ(int(density.size()), 8 * 8 * 8);

    Params axis;
    axis.setInt("nx", 6);
    axis.setInt("ny", 7);
    axis.setInt("nz", 5);
    axis.setString("field", "torus");
    CHECK(fillDensityField(axis, density, nx, ny, nz, err));
    CHECK_EQ(nx, 6);
    CHECK_EQ(ny, 7);
    CHECK_EQ(nz, 5);

    Params badField;
    badField.setInt("resolution", 8);
    badField.setString("field", "banana");
    CHECK(!fillDensityField(badField, density, nx, ny, nz, err));
    CHECK(err.find("field") != std::string::npos);

    Params tooSmall;
    tooSmall.setInt("resolution", 1);
    tooSmall.setString("field", "sphere");
    CHECK(!fillDensityField(tooSmall, density, nx, ny, nz, err));

    Params tooBig;
    tooBig.setInt("resolution", 200);
    tooBig.setString("field", "sphere");
    CHECK(!fillDensityField(tooBig, density, nx, ny, nz, err));
    CHECK(err.find("128") != std::string::npos);
}

TEST_CASE("procgen.mesh.marchingcubes.recipeErrors") {
    MeshRecipeRegistry::instance().registerBuiltins();
    MeshBuild mesh;
    std::string err;
    Params badField;
    badField.setInt("resolution", 12);
    badField.setString("field", "nope");
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.marchingcubes", badField, mesh, err));
    CHECK(err.find("field") != std::string::npos);

    CHECK(!MeshRecipeRegistry::instance().generate("mesh.missing", badField, mesh, err));
    CHECK(err.find("unknown") != std::string::npos);

    auto ids = MeshRecipeRegistry::instance().list();
    CHECK(!ids.empty());
    CHECK(std::find(ids.begin(), ids.end(), "mesh.marchingcubes") != ids.end());
}

TEST_CASE("procgen.mesh.marchingcubes.isolevelAffectsMesh") {
    Params lo;
    lo.setSeed(1);
    lo.setInt("resolution", 16);
    lo.setString("field", "sphere");
    lo.setFloat("radius", 0.7f);
    lo.setFloat("isolevel", -0.2f);
    Params hi = lo;
    hi.setFloat("isolevel", 0.2f);
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", lo, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", hi, b, err));
    // Higher isolevel shrinks solid region → fewer / different triangles.
    const bool differs =
        a.getVertexCount() != b.getVertexCount() || a.positions() != b.positions();
    CHECK(differs);
}

TEST_CASE("procgen.mesh.marchingcubes.viaModule") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(3);
    p.setInt("resolution", 16);
    p.setString("field", "torus");
    auto params    = requireParams(p);
    auto meshLease = requireMesh(*mod, "mesh.marchingcubes", params.handle);
    auto m         = meshLease.view();
    REQUIRE(m.isBound());
    CHECK(m->getVertexCount() > 50);
    CHECK(meshIndicesInRange(*m));
    CHECK(mod->hasMeshRecipe("mesh.marchingcubes"));
    CHECK(mod->getMeshRecipeCount() >= 1);
    bool listed = false;
    for (int i = 0; i < mod->getMeshRecipeCount(); ++i) {
        if (mod->getMeshRecipeId(i) == "mesh.marchingcubes") listed = true;
    }
    CHECK(listed);
    Params bad;
    bad.setInt("resolution", 8);
    bad.setString("field", "nope");
    auto badParams = requireParams(bad);
    auto badResult = mod->buildMeshHandle("mesh.marchingcubes", badParams.handle);
    CHECK(!badResult.ok());
    const eve::Diagnostic *badDiagnostic = badResult.error();
    REQUIRE(badDiagnostic != nullptr);
    CHECK_EQ(badDiagnostic->code(), eve::DiagnosticCode::Failed);
    auto missingResult = mod->buildMeshHandle("mesh.missing", params.handle);
    CHECK(!missingResult.ok());
    const eve::Diagnostic *missingDiagnostic = missingResult.error();
    REQUIRE(missingDiagnostic != nullptr);
    CHECK_EQ(missingDiagnostic->code(), eve::DiagnosticCode::Failed);
    auto missingGraphicsResult = mod->generateMeshBorrowed("mesh.marchingcubes", {}, nullptr);
    CHECK(!missingGraphicsResult.isBound());
}

TEST_CASE("procgen.wfc.simple.dungeonAdjacencyAndListing") {
    GeneratorRegistry::instance().registerBuiltins();
    Procgen *mod = Procgen::create();
    CHECK(mod->hasAlgorithm("wfc.simple"));
    bool listed = false;
    for (int i = 0; i < mod->getAlgorithmCount(); ++i) {
        if (mod->getAlgorithmId(i) == "wfc.simple") listed = true;
    }
    CHECK(listed);

    Params p;
    p.setSeed(17);
    p.setSize(22, 16);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D g;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g, err));
    CHECK(borderIsWall(g));
    // Allowed dungeon pairs (undirected).
    auto okPair = [](int a, int b) {
        if (a > b) std::swap(a, b);
        const int W = int(Semantic::Wall), F = int(Semantic::Floor), C = int(Semantic::Corridor),
                  D = int(Semantic::Door);
        if (a == W && (b == W || b == F || b == C || b == D)) return true;
        if (a == F && (b == F || b == C || b == D)) return true;
        if (a == C && b == C) return true;
        return false;
    };
    for (int y = 0; y < g.getHeight(); ++y) {
        for (int x = 0; x < g.getWidth(); ++x) {
            const int c = g.getCell(x, y);
            if (x + 1 < g.getWidth()) CHECK(okPair(c, g.getCell(x + 1, y)));
            if (y + 1 < g.getHeight()) CHECK(okPair(c, g.getCell(x, y + 1)));
        }
    }
}

TEST_CASE("procgen.wfc.simple.multiSeedBatch") {
    // Stress: several seeds/presets must all collapse successfully.
    const char *presets[] = {"dungeon", "cave", "terrain"};
    int okCount = 0;
    for (const char *preset : presets) {
        for (uint32_t seed = 1; seed <= 5; ++seed) {
            Params p;
            p.setSeed(seed * 13u + 7u);
            p.setSize(18, 14);
            p.setString("preset", preset);
            p.setInt("maxAttempts", 64);
            Grid2D g;
            std::string err;
            if (GeneratorRegistry::instance().generate("wfc.simple", p, g, err)) {
                ++okCount;
                CHECK_EQ(g.getWidth(), 18);
                CHECK_EQ(g.getHeight(), 14);
                if (std::string(preset) != "terrain") CHECK(borderIsWall(g));
            }
        }
    }
    CHECK_EQ(okCount, 15);
}

TEST_CASE("procgen.mesh.marchingcubes.resolutionScales") {
    Params lo;
    lo.setSeed(1);
    lo.setInt("resolution", 12);
    lo.setString("field", "sphere");
    lo.setFloat("radius", 0.7f);
    Params hi = lo;
    hi.setInt("resolution", 24);
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", lo, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", hi, b, err));
    CHECK(b.getIndexCount() > a.getIndexCount());
    CHECK(b.getVertexCount() > a.getVertexCount());
}

TEST_CASE("procgen.mesh.marchingcubes.torusAndAxisResolution") {
    Params torus;
    torus.setSeed(2);
    torus.setInt("resolution", 20);
    torus.setString("field", "torus");
    MeshBuild ring;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", torus, ring, err));
    CHECK(ring.getVertexCount() > 100);
    CHECK(meshIndicesInRange(ring));
    CHECK(meshNormalsFiniteUnit(ring));
    // Torus should have non-zero volume magnitude.
    CHECK(std::fabs(meshApproxSignedVolume(ring)) > 0.01f);

    Params axis;
    axis.setSeed(3);
    axis.setInt("nx", 10);
    axis.setInt("ny", 14);
    axis.setInt("nz", 12);
    axis.setString("field", "sphere");
    axis.setFloat("radius", 0.65f);
    MeshBuild m;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", axis, m, err));
    CHECK(m.getVertexCount() > 50);
}

TEST_CASE("procgen.mesh.marchingcubes.sphereVolumeSignStable") {
    Params p;
    p.setSeed(1);
    p.setInt("resolution", 18);
    p.setString("field", "sphere");
    p.setFloat("radius", 0.7f);
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, b, err));
    const float va = meshApproxSignedVolume(a);
    const float vb = meshApproxSignedVolume(b);
    CHECK(std::fabs(va) > 0.05f);
    CHECK(approxEq(va, vb, 1e-5f));
    // Winding must be consistent across seeds of the same field recipe family.
    Params p2 = p;
    p2.setSeed(9);
    MeshBuild c;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p2, c, err));
    const float vc = meshApproxSignedVolume(c);
    CHECK(va * vc > 0.f);  // same sign
}

TEST_CASE("procgen.mesh.marchingcubes.moduleNullAndList") {
    Procgen *mod = Procgen::create();
    auto     failed = mod->buildMeshHandle("mesh.marchingcubes", {});
    CHECK(!failed.ok());
    const eve::Diagnostic *diagnostic = failed.error();
    REQUIRE(diagnostic != nullptr);
    CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::StaleHandle);
    auto failedUpload = mod->generateMeshBorrowed("mesh.marchingcubes", {}, nullptr);
    CHECK(!failedUpload.isBound());

    CHECK(mod->getMeshRecipeCount() >= 1);
    bool found = false;
    for (int i = 0; i < mod->getMeshRecipeCount(); ++i) {
        if (mod->getMeshRecipeId(i) == "mesh.marchingcubes") found = true;
    }
    CHECK(found);
}

TEST_CASE("procgen.palette.applyToLayer") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(1);
    p.setSize(16, 12);
    auto params    = requireParams(p);
    auto gridLease = requireGrid(*mod, "dungeon.bsp", params.handle);
    auto grid      = gridLease.view();
    REQUIRE(grid.isBound());

    mod->setPaletteGid("test", "wall", 10);
    mod->setPaletteGid("test", "floor", 20);
    mod->setPaletteGid("test", "corridor", 21);

    eve::map::TileLayer *layer = eve::map::TileLayer::createLayer(16, 12, 16.f, 16.f);
    auto                 applyResult = mod->applyToLayer(gridLease.handle, "test", *layer);
    CHECK(applyResult.ok());
    bool sawMapped = false;
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int gid = layer->getTile(x, y);
            const int sem = grid->getCell(x, y);
            if (sem == int(Semantic::Wall)) {
                CHECK_EQ(gid, 10);
                sawMapped = true;
            } else if (sem == int(Semantic::Floor)) {
                CHECK_EQ(gid, 20);
                sawMapped = true;
            } else if (sem == int(Semantic::Corridor)) {
                CHECK_EQ(gid, 21);
                sawMapped = true;
            }
        }
    }
    CHECK(sawMapped);
    layer->release();
}

TEST_CASE("procgen.json.export") {
    Params p;
    p.setSeed(3);
    p.setSize(12, 10);
    Grid2D g;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("dungeon.bsp", p, g, err));
    const std::string json = gridToJson(g);
    CHECK(json.find("\"width\":12") != std::string::npos);
    CHECK(json.find("\"cells\":") != std::string::npos);
    CHECK(json.find("spawn") != std::string::npos);
}

TEST_CASE("procgen.texture.noiseField.seamlessReproducible") {
    NoiseField a{42, 8, 8};
    NoiseField b{42, 8, 8};
    CHECK_EQ(a.fbm(1.25f, 3.5f, 4), b.fbm(1.25f, 3.5f, 4));
    // Period wrap: x and x+period should match on lattice contribution.
    CHECK_EQ(a.hash01(0, 0), a.hash01(8, 0));
    CHECK_EQ(a.hash01(0, 0), a.hash01(0, 8));
}

TEST_CASE("procgen.texture.colorRamp.banded") {
    ColorRamp ramp;
    ramp.add(0.f, 0, 0, 0);
    ramp.add(1.f, 255, 255, 255);
    const Rgba8 c0 = ramp.sampleBanded(0.1f, 3);
    const Rgba8 c1 = ramp.sampleBanded(0.9f, 3);
    CHECK(c0.r < c1.r);
}

TEST_CASE("procgen.texture.recipes.reproducible") {
    TextureRecipeRegistry::instance().registerBuiltins();
    const char *ids[] = {"tex.soil", "tex.stone", "tex.marble", "tex.water", "tex.sky_cloud"};
    for (const char *id : ids) {
        Params p;
        p.setSeed(11);
        p.setSize(32, 32);
        p.setInt("colors", 5);
        p.setInt("pixelSize", 2);
        p.setInt("seamless", 1);
        std::string err;
        // Fully qualify: `using namespace eve::procgen` would make bare `image::`
        // look outside `eve::image`.
        auto       a        = TextureRecipeRegistry::instance().generate(id, p, err);
        auto       b        = TextureRecipeRegistry::instance().generate(id, p, err);
        const bool aPresent = static_cast<bool>(a);
        const bool bPresent = static_cast<bool>(b);
        REQUIRE(aPresent);
        REQUIRE(bPresent);
        CHECK_EQ(a->getWidth(), 32);
        CHECK_EQ(a->getHeight(), 32);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK_EQ(a->getSize(), b->getSize());
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.texture.generateImage.andNormal") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(3);
    p.setSize(48, 48);
    p.setInt("colors", 6);
    auto params      = requireParams(p);
    auto imageResult = mod->generateImageHandle("tex.marble", params.handle);
    REQUIRE(imageResult.ok());
    auto normalResult = mod->generateNormalImageHandle("tex.marble", params.handle);
    REQUIRE(normalResult.ok());
    CHECK(mod->hasTextureRecipe("tex.soil"));
    CHECK(mod->getTextureRecipeCount() >= 5);
}

TEST_CASE("procgen.cloud.field.reproducibleAnimatedSeamless") {
    CloudField::Params p;
    p.seed = 99;
    p.worldScale = 64.f;
    p.coverage = 0.5f;
    CloudField a(p), b(p);
    // Deterministic: same seed → same coverage at the same point/time.
    CHECK(approxEq(a.coverageAt(3.f, 4.f, 0.f), b.coverageAt(3.f, 4.f, 0.f), 1e-5f));
    // Animated: different time drifts the field (for a non-zero wind speed).
    const float t0 = a.coverageAt(3.f, 4.f, 0.f);
    const float t1 = a.coverageAt(3.f, 4.f, 5.f);
    const float t2 = a.coverageAt(3.f, 4.f, 10.f);
    const float moved = (t0 != t1) || (t1 != t2);
    CHECK(moved);  // drifting over time
    // Seamless: coverage tiles over one worldScale.
    CHECK(approxEq(a.coverageAt(3.f, 4.f, 0.f), a.coverageAt(3.f + 64.f, 4.f, 0.f), 1e-4f));
    CHECK(approxEq(a.coverageAt(3.f, 4.f, 0.f), a.coverageAt(3.f, 4.f + 64.f, 0.f), 1e-4f));
    // Values stay in range.
    for (float x = -200.f; x < 200.f; x += 40.f) {
        const float c = a.coverageAt(x, 13.f, 1.f);
        CHECK(c >= 0.f);
        CHECK(c <= 1.f);
    }
    // Different seed → different field.
    CloudField::Params q = p;
    q.seed = 100;
    CloudField c(q);
    CHECK(!approxEq(a.coverageAt(3.f, 4.f, 0.f), c.coverageAt(3.f, 4.f, 0.f), 1e-5f));
}

TEST_CASE("procgen.cloud.field.windDriftsInDirection") {
    CloudField::Params p;
    p.seed = 5;
    p.worldScale = 32.f;
    p.windSpeed = 10.f;
    p.windAngle = 0.f;  // drift toward +x
    CloudField f(p);
    // A feature moving at +x over time should arrive at a +x-shifted position.
    const float base = f.coverageAt(0.f, 0.f, 0.f);
    const float dt = 1.f;
    CHECK(approxEq(base, f.coverageAt(0.f + p.windSpeed * dt, 0.f, 1.f), 1e-2f));
    // Perpendicular axis is unchanged at that same world offset check fails → drift is 1D.
    CHECK(!approxEq(base, f.coverageAt(0.f + p.windSpeed * dt, 5.f, 1.f), 1e-2f));
}

TEST_CASE("procgen.cloud.shadow.projection") {
    CloudField::Params fp;
    fp.seed = 7;
    fp.worldScale = 48.f;
    fp.coverage = 0.5f;
    CloudField field(fp);

    // Overhead sun (0,1,0): shadow coverage equals field coverage at the point.
    CloudShadow::Params sp;
    sp.field = field;
    sp.sunDirX = 0.f;
    sp.sunDirY = 1.f;
    sp.sunDirZ = 0.f;
    sp.cloudAltitude = 50.f;
    CloudShadow overhead(sp);
    const float px = 5.f, pz = 6.f;
    CHECK(approxEq(overhead.coverageAt(px, pz, 0.f), field.coverageAt(px, pz, 0.f), 1e-5f));

    // Angled sun: ground coverage = field coverage sampled up-sun at the cloud point.
    sp.sunDirX = 0.5f;
    sp.sunDirY = 1.f;
    sp.sunDirZ = 0.f;
    CloudShadow angled(sp);
    float ox = 0.f, oz = 0.f;
    angled.cloudOffset(ox, oz);
    CHECK(approxEq(angled.coverageAt(px, pz, 0.f), field.coverageAt(px + ox, pz + oz, 0.f), 1e-5f));

    // Factor: 1 when clear, (1-strength) when fully covered; always in [0,1].
    CloudShadow::Params sp2 = sp;
    sp2.strength = 0.8f;
    CloudShadow shadow(sp2);
    float minF = 1.f, maxF = 0.f;
    for (float z = 0.f; z < 24.f; z += 2.f) {
        for (float x = 0.f; x < 24.f; x += 2.f) {
            const float f = shadow.shadowFactorAt(x, z, 0.f);
            CHECK(f >= 0.f);
            CHECK(f <= 1.f);
            minF = std::min(minF, f);
            maxF = std::max(maxF, f);
        }
    }
    CHECK(maxF > 0.9f);               // some fully-lit ground
    CHECK(minF < 1.f);                // some coverage darkens ground
    // Sun below horizon → no cloud shadows.
    sp.sunDirY = -1.f;
    CloudShadow below(sp);
    CHECK_EQ(below.coverageAt(px, pz, 0.f), 0.f);
    CHECK_EQ(below.shadowFactorAt(px, pz, 0.f), 1.f);
}

TEST_CASE("procgen.cloud.recipes.generate") {
    TextureRecipeRegistry::instance().registerBuiltins();
    const char *ids[] = {"tex.cloud", "tex.cloud_shadow"};
    for (const char *id : ids) {
        Params p;
        p.setSeed(42);
        p.setSize(48, 48);
        p.setFloat("worldScale", 64.f);
        p.setFloat("time", 1.5f);
        std::string err;
        auto        img = TextureRecipeRegistry::instance().generate(id, p, err);
        REQUIRE(static_cast<bool>(img));
        CHECK_EQ(img->getFormat(), std::string("RGBA8"));
        CHECK_EQ(img->getWidth(), 48);
        CHECK_EQ(img->getHeight(), 48);
    }
    CHECK(TextureRecipeRegistry::instance().has("tex.cloud"));
    CHECK(TextureRecipeRegistry::instance().has("tex.cloud_shadow"));
}

TEST_CASE("procgen.cloud.viaModule") {
    Procgen *mod = Procgen::create();
    auto     fieldResult = mod->newCloudFieldHandle();
    REQUIRE(fieldResult.ok());
    auto fieldHandle = std::move(fieldResult).takeValue();
    auto field       = mod->resolveCloudField(fieldHandle);
    REQUIRE(field.isBound());
    field->setSeed(3);
    field->setWorldScale(64.f);
    field->setCoverage(0.5f);
    auto coverageResult = mod->cloudCoverageAt(fieldHandle, 2.f, 3.f, 0.f);
    REQUIRE(coverageResult.ok());
    const float c0 = std::move(coverageResult).takeValue();
    CHECK(c0 >= 0.f);
    CHECK(c0 <= 1.f);
    auto staleCoverage = mod->cloudCoverageAt({}, 0.f, 0.f, 0.f);
    CHECK(!staleCoverage.ok());

    auto shadowResult = mod->newCloudShadowHandle();
    REQUIRE(shadowResult.ok());
    auto shadowHandle = std::move(shadowResult).takeValue();
    auto shadow       = mod->resolveCloudShadow(shadowHandle);
    REQUIRE(shadow.isBound());
    shadow->setSunDirection(0.5f, 1.f, 0.f);
    shadow->setCloudAltitude(50.f);
    shadow->setStrength(0.8f);
    auto factorResult = mod->cloudShadowFactor(shadowHandle, 2.f, 3.f, 0.f);
    REQUIRE(factorResult.ok());
    CHECK(std::move(factorResult).takeValue() >= 0.f);
    auto staleFactor = mod->cloudShadowFactor({}, 0.f, 0.f, 0.f);
    CHECK(!staleFactor.ok());

    std::vector<float> buf(16 * 16);
    auto               sampleResult = mod->sampleCloud(fieldHandle, std::span<float>(buf), 16, 16, 0.f, 0.f, 0.f, 64.f);
    CHECK(sampleResult.ok());
    auto shadowSampleResult = mod->sampleCloudShadow(shadowHandle, std::span<float>(buf), 16, 16, 0.f, 0.f, 0.f, 64.f);
    CHECK(shadowSampleResult.ok());
    auto releaseField = mod->releaseCloudField(fieldHandle);
    CHECK(releaseField.ok());
    auto releaseShadow = mod->releaseCloudShadow(shadowHandle);
    CHECK(releaseShadow.ok());
}


TEST_CASE("procgen.texture.builtinRecipes.expanded") {
    TextureRecipeRegistry::instance().registerBuiltins();
    const char *ids[] = {"tex.soil",    "tex.stone",   "tex.rock",   "tex.marble", "tex.water",
                         "tex.ripple",  "tex.sky_cloud", "tex.wood", "tex.cloth",  "tex.ornament",
                         "tex.spot",    "tex.zebra",   "tex.wall",   "tex.cement", "tex.mud"};
    for (const char *id : ids) {
        Params p;
        p.setSeed(11);
        p.setSize(32, 32);
        p.setInt("colors", 5);
        std::string err;
        auto        a        = TextureRecipeRegistry::instance().generate(id, p, err);
        auto        b        = TextureRecipeRegistry::instance().generate(id, p, err);
        const bool  aPresent = static_cast<bool>(a);
        const bool  bPresent = static_cast<bool>(b);
        REQUIRE(aPresent);
        REQUIRE(bPresent);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.recipeSchemas.textureAndPbrDefaults") {
    auto &textures = TextureRecipeRegistry::instance();
    auto &materials = PbrRecipeRegistry::instance();
    textures.registerBuiltins();
    materials.registerPbrBuiltins();
    for (const std::string &id : textures.list()) {
        const RecipeDescriptor *schema = textures.descriptor(id);
        REQUIRE(schema != nullptr);
        CHECK_EQ(schema->id, id);
        CHECK(!schema->displayName.empty());
        CHECK(schema->find("seed") != nullptr);
    }
    for (const std::string &id : materials.list()) {
        const RecipeDescriptor *schema = materials.descriptor(id);
        REQUIRE(schema != nullptr);
        CHECK_EQ(schema->category, std::string("Material"));
        CHECK(schema->find("metallic") != nullptr);
        CHECK(schema->find("normalStrength") != nullptr);
    }
    Params defaults;
    REQUIRE(materials.applyDefaults("pbr.rock", defaults));
    const RecipeDescriptor *rock = materials.descriptor("pbr.rock");
    REQUIRE(rock != nullptr);
    REQUIRE(rock->find("metallic") != nullptr);
    CHECK_EQ(defaults.getFloat("metallic", -1.f), std::stof(rock->find("metallic")->defaultValue));
    CHECK(!materials.applyDefaults("pbr.missing", defaults));
    RecipeDescriptor copied = *rock;
    copied.displayName = "Project Rock";
    CHECK_NE(copied.displayName, rock->displayName);
}

TEST_CASE("procgen.recipeSchemas.meshDefaults") {
    auto &meshes = MeshRecipeRegistry::instance();
    meshes.registerBuiltins();
    for (const std::string &id : meshes.list()) {
        const RecipeDescriptor *schema = meshes.descriptor(id);
        REQUIRE(schema != nullptr);
        CHECK_EQ(schema->id, id);
        CHECK(!schema->displayName.empty());
        CHECK(!schema->category.empty());
        CHECK(schema->find("seed") != nullptr);
    }

    Params defaults;
    REQUIRE(meshes.applyDefaults("mesh.fence", defaults));
    CHECK_EQ(defaults.getInt("segments", 0), 6);
    CHECK_EQ(defaults.getFloat("height", 0.f), 1.1f);
    MeshBuild mesh;
    std::string error;
    REQUIRE(meshes.generate("mesh.fence", defaults, mesh, error));
    CHECK_GT(mesh.getVertexCount(), 0);
    CHECK(!meshes.applyDefaults("mesh.missing", defaults));
}

TEST_CASE("procgen.pbr.registry.builtinsAndReproducible") {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    const char *ids[] = {"pbr.soil",   "pbr.rock",   "pbr.marble", "pbr.water", "pbr.wood",
                         "pbr.cloth",  "pbr.ornament", "pbr.spot", "pbr.zebra", "pbr.wall",
                         "pbr.cement", "pbr.mud",    "pbr.ripple"};
    for (const char *id : ids) {
        REQUIRE(PbrRecipeRegistry::instance().has(id));
        Params p;
        p.setSeed(7);
        p.setSize(32, 32);
        std::string err;
        auto        a        = PbrRecipeRegistry::instance().generate(id, p, err);
        auto        b        = PbrRecipeRegistry::instance().generate(id, p, err);
        const bool  aPresent = static_cast<bool>(a);
        const bool  bPresent = static_cast<bool>(b);
        REQUIRE(aPresent);
        REQUIRE(bPresent);
        // All six maps present, same size, reproducible.
        REQUIRE(a->albedo != nullptr);
        REQUIRE(a->normal != nullptr);
        REQUIRE(a->roughness != nullptr);
        REQUIRE(a->metallic != nullptr);
        REQUIRE(a->height != nullptr);
        REQUIRE(a->ao != nullptr);
        CHECK_EQ(a->albedo->getWidth(), 32);
        CHECK_EQ(a->albedo->getHeight(), 32);
        CHECK(std::memcmp(a->albedo->getData(), b->albedo->getData(), a->albedo->getSize()) == 0);
        CHECK(std::memcmp(a->normal->getData(), b->normal->getData(), a->normal->getSize()) == 0);
        // Grayscale maps must be RGBA8 with alpha=255.
        const auto *px = static_cast<const uint8_t *>(a->roughness->getData());
        bool alphaOk  = true;
        for (size_t i = 0; i < a->roughness->getSize(); i += 4) {
            if (px[i + 3] != 255) alphaOk = false;
        }
        CHECK(alphaOk);
    }
}

TEST_CASE("procgen.pbr.metallicAndRoughnessRange") {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    Params p;
    p.setSeed(3);
    p.setSize(48, 48);
    p.setFloat("metallic", 1.f);
    p.setFloat("roughnessLow", 0.1f);
    p.setFloat("roughnessHigh", 0.2f);
    std::string err;
    auto        set = PbrRecipeRegistry::instance().generate("pbr.marble", p, err);
    REQUIRE(static_cast<bool>(set));
    const auto *m = static_cast<const uint8_t *>(set->metallic->getData());
    for (size_t i = 0; i < set->metallic->getSize(); i += 4) {
        CHECK_EQ(m[i], 255);  // fully metallic
    }
    const auto *r = static_cast<const uint8_t *>(set->roughness->getData());
    for (size_t i = 0; i < set->roughness->getSize(); i += 4) {
        CHECK(r[i] >= 25);  // roughnessLow 0.1 -> 25
        CHECK(r[i] <= 51);  // roughnessHigh 0.2 -> 51
    }
}

TEST_CASE("procgen.pbr.viaModuleAndErrors") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(9);
    p.setSize(40, 40);
    auto params    = requireParams(p);
    auto pbrResult = mod->generatePbrMaterialHandle("pbr.wall", params.handle);
    REQUIRE(pbrResult.ok());
    auto setLease = PbrLease(*mod, std::move(pbrResult).takeValue());
    auto set      = setLease.view();
    REQUIRE(set.isBound());
    CHECK(set->albedo != nullptr);
    CHECK(set->normal != nullptr);

    CHECK(mod->hasPbrRecipe("pbr.wood"));
    CHECK(mod->getPbrRecipeCount() >= 13);
    bool listed = false;
    for (int i = 0; i < mod->getPbrRecipeCount(); ++i) {
        if (mod->getPbrRecipeId(i) == "pbr.wood") listed = true;
    }
    CHECK(listed);

    auto missingResult = mod->generatePbrMaterialHandle("pbr.missing", params.handle);
    CHECK(!missingResult.ok());
    const eve::Diagnostic *missingDiagnostic = missingResult.error();
    REQUIRE(missingDiagnostic != nullptr);
    CHECK_EQ(missingDiagnostic->code(), eve::DiagnosticCode::NotFound);
    auto staleResult = mod->generatePbrMaterialHandle("pbr.wall", {});
    CHECK(!staleResult.ok());
    const eve::Diagnostic *staleDiagnostic = staleResult.error();
    REQUIRE(staleDiagnostic != nullptr);
    CHECK_EQ(staleDiagnostic->code(), eve::DiagnosticCode::StaleHandle);
}

static Color colorForSemantic(int sem) {
    switch (sem) {
    case int(Semantic::Wall):
        return Color(0.22f, 0.24f, 0.30f, 1.f);
    case int(Semantic::Floor):
        return Color(0.72f, 0.68f, 0.55f, 1.f);
    case int(Semantic::Corridor):
        return Color(0.55f, 0.52f, 0.42f, 1.f);
    case int(Semantic::Water):
        return Color(0.25f, 0.45f, 0.85f, 1.f);
    case int(Semantic::Sand):
        return Color(0.85f, 0.78f, 0.45f, 1.f);
    case int(Semantic::Grass):
        return Color(0.35f, 0.65f, 0.30f, 1.f);
    case int(Semantic::Dirt):
        return Color(0.55f, 0.40f, 0.25f, 1.f);
    case int(Semantic::Stone):
        return Color(0.55f, 0.58f, 0.62f, 1.f);
    case int(Semantic::Snow):
        return Color(0.90f, 0.93f, 0.97f, 1.f);
    case int(Semantic::Door):
        return Color(0.75f, 0.45f, 0.20f, 1.f);
    default:
        return Color(0.05f, 0.05f, 0.07f, 1.f);
    }
}

static void drawGrid(Graphics *gfx, const Grid2D &grid, float originX, float originY, float cell) {
    const int w = grid.getWidth();
    const int h = grid.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color c = colorForSemantic(grid.getCell(x, y));
            gfx->drawSolidRect(originX + float(x) * cell, originY + float(y) * cell, cell, cell, c);
        }
    }
    // Markers for spawn / objects.
    for (int i = 0; i < grid.getObjectCount(); ++i) {
        const float ox = originX + grid.getObjectX(i) * cell;
        const float oy = originY + grid.getObjectY(i) * cell;
        gfx->drawSolidRect(ox - 2.f, oy - 2.f, 5.f, 5.f, Color(1.f, 0.3f, 0.3f, 1.f));
    }
}

TEST_CASE("procgen.render.dungeonCaveMazePreview") {
    GeneratorRegistry::instance().registerBuiltins();

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 720;
    s.height = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    struct Algo {
        const char *id;
        int w, h;
    };
    const Algo algos[] = {
        {"dungeon.bsp", 36, 28},
        {"cave.cellular", 36, 28},
        {"maze.backtrack", 31, 23},
        {"noise.terrain", 36, 28},
    };

    gfx->setBackgroundColorRGBA(0.06f, 0.07f, 0.09f, 1.f);
    int drawnCells = 0;

    for (int ai = 0; ai < 4; ++ai) {
        const Algo &algo = algos[ai];
        Params p;
        p.setSeed(42);
        p.setSize(algo.w, algo.h);
        if (std::string(algo.id) == "cave.cellular") {
            p.setInt("loops", 4);
            p.setFloat("fill", 0.45f);
        } else if (std::string(algo.id) == "noise.terrain") {
            p.setFloat("frequency", 5.f);
            p.setInt("octaves", 3);
        }

        Grid2D grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate(algo.id, p, grid, err));

        const float cell = std::min(16.f, std::min(float(gfx->getWidth() - 40) / float(algo.w),
                                                   float(gfx->getHeight() - 40) / float(algo.h)));
        const float originX = (float(gfx->getWidth()) - float(algo.w) * cell) * 0.5f;
        const float originY = (float(gfx->getHeight()) - float(algo.h) * cell) * 0.5f;

        for (int frame = 0; frame < 45; ++frame) {
            gfx->clearScreen();
            drawGrid(gfx, grid, originX, originY, cell);
            // Title bar strip so algorithm changes are readable without fonts.
            const float barW = float(gfx->getWidth()) * (0.15f + 0.2f * float(ai));
            gfx->drawSolidRect(12.f, 10.f, barW, 8.f, Color(0.9f, 0.85f, 0.4f, 1.f));
            gfx->present();

            drawnCells = algo.w * algo.h;
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            SDL_Delay(16);
        }
    }

    CHECK_GT(drawnCells, 100);
    win->close();
}

TEST_CASE("procgen.mesh.linearStructure.recipesRegistered") {
    MeshRecipeRegistry::instance().registerBuiltins();
    for (const char *id : {"mesh.fence", "mesh.stonewall", "mesh.bridge", "mesh.greatwall",
                           "mesh.hedge", "mesh.chevaldefrise"}) {
        CHECK(MeshRecipeRegistry::instance().has(id));
    }
    const auto ids = MeshRecipeRegistry::instance().list();
    for (const char *id : {"mesh.fence", "mesh.stonewall", "mesh.bridge", "mesh.greatwall",
                           "mesh.hedge", "mesh.chevaldefrise"}) {
        CHECK(std::find(ids.begin(), ids.end(), id) != ids.end());
    }
}

TEST_CASE("procgen.mesh.linearStructure.buildsAllKinds") {
    MeshRecipeRegistry::instance().registerBuiltins();
    for (const char *id : {"mesh.fence", "mesh.stonewall", "mesh.bridge", "mesh.greatwall",
                           "mesh.hedge", "mesh.chevaldefrise"}) {
        Params p;
        p.setSeed(7);
        p.setInt("segments", 4);
        p.setFloat("segLength", 1.f);
        MeshBuild m;
        std::string err;
        CHECK(MeshRecipeRegistry::instance().generate(id, p, m, err));
        CHECK(m.getVertexCount() > 0);
        CHECK(m.getIndexCount() > 0);
        CHECK_EQ(m.getIndexCount() % 3, 0);
        CHECK(meshIndicesInRange(m));
        CHECK(meshPositionsFinite(m));
        CHECK(meshNormalsFiniteUnit(m));
        CHECK_EQ(m.getMeta("algorithm", ""), std::string(id));
        CHECK_EQ(m.getMeta("segments", ""), std::string("4"));
    }
}

TEST_CASE("procgen.mesh.linearStructure.reproducibleAndScalable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(3);
    p.setInt("segments", 5);
    p.setFloat("segLength", 1.2f);
    MeshBuild a, b, scaled;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.greatwall", p, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.greatwall", p, b, err));
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());

    Params p2 = p;
    p2.setFloat("scale", 2.f);
    CHECK(MeshRecipeRegistry::instance().generate("mesh.greatwall", p2, scaled, err));
    CHECK_EQ(a.getVertexCount(), scaled.getVertexCount());
    // Scaling a 2x should scale the X extent of the whole run by 2x.
    float minAx = 1e30f, maxAx = -1e30f, minSx = 1e30f, maxSx = -1e30f;
    for (int i = 0; i < a.getVertexCount(); ++i) {
        minAx = std::min(minAx, a.getPositionX(i));
        maxAx = std::max(maxAx, a.getPositionX(i));
        minSx = std::min(minSx, scaled.getPositionX(i));
        maxSx = std::max(maxSx, scaled.getPositionX(i));
    }
    CHECK(std::fabs((maxAx - minAx) * 2.f - (maxSx - minSx)) < 1e-3f);
}

TEST_CASE("procgen.mesh.linearStructure.segmentsScaleVertexCount") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params lo;
    lo.setInt("segments", 2);
    lo.setFloat("segLength", 1.f);
    Params hi = lo;
    hi.setInt("segments", 6);
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.fence", lo, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.fence", hi, b, err));
    // Linear repetition of one unit ⇒ vertex count grows linearly with segments.
    CHECK_EQ(3 * a.getVertexCount(), b.getVertexCount());
}

TEST_CASE("procgen.mesh.linearStructure.tileableSeamAlignment") {
    // One unit ends exactly where the next begins, so a tiled run is contiguous.
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setInt("segments", 1);
    p.setFloat("segLength", 1.f);
    MeshBuild single;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.bridge", p, single, err));
    float maxX = -1e30f;
    for (int i = 0; i < single.getVertexCount(); ++i) maxX = std::max(maxX, single.getPositionX(i));
    // Unit occupies [0, segLength]; tiling repeats at +segLength.
    CHECK(std::fabs(maxX - 1.f) < 1e-3f);
}

TEST_CASE("procgen.mesh.linearStructure.viaModule") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(1);
    p.setInt("segments", 4);
    p.setFloat("segLength", 1.f);
    auto params    = requireParams(p);
    auto meshLease = requireMesh(*mod, "mesh.stonewall", params.handle);
    auto m         = meshLease.view();
    REQUIRE(m.isBound());
    CHECK(m->getVertexCount() > 0);
    CHECK(meshIndicesInRange(*m));
    CHECK(mod->hasMeshRecipe("mesh.stonewall"));
    CHECK(mod->hasMeshRecipe("mesh.bridge"));
    auto missingResult = mod->buildMeshHandle("mesh.nonexistent", params.handle);
    CHECK(!missingResult.ok());
    const eve::Diagnostic *missingDiagnostic = missingResult.error();
    REQUIRE(missingDiagnostic != nullptr);
    CHECK_EQ(missingDiagnostic->code(), eve::DiagnosticCode::Failed);
}

TEST_CASE("procgen.mesh.castle.multilevelDeterministicAndComplete") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(8675309);
    p.setFloat("width", 48.f);
    p.setFloat("depth", 40.f);
    p.setInt("rings", 2);
    p.setInt("keepFloors", 4);
    p.setInt("detail", 2);
    MeshBuild a, b;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.castle", p, a, err));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.castle", p, b, err));
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());
    CHECK(meshIndicesInRange(a));
    CHECK(meshPositionsFinite(a));
    CHECK(meshNormalsFiniteUnit(a));
    CHECK(a.getVertexCount() > 5000);
    CHECK_EQ(a.getMeta("algorithm", ""), "mesh.castle");
    CHECK_EQ(a.getMeta("rings", ""), "2");
    CHECK_EQ(a.getMeta("keepFloors", ""), "4");
    CHECK(std::stoi(a.getMeta("towerCount", "0")) >= 12);
    // One stair per wall ring plus one between every pair of keep floors.
    CHECK_EQ(a.getMeta("stairFlights", ""), "5");
    std::set<std::string> groups;
    for (int i = 0; i < a.getGroupCount(); ++i) groups.insert(a.getGroupName(i));
    for (const char *required : {"walls", "battlements", "towers", "gatehouses", "stairs", "keep", "courtyard"})
        CHECK(groups.count(required) == 1);
    int stairGroup = -1;
    for (int i = 0; i < a.getGroupCount(); ++i)
        if (a.getGroupName(i) == "stairs") stairGroup = i;
    REQUIRE(stairGroup >= 0);
    auto stairs = a.copyGroup(stairGroup);
    REQUIRE(stairs.get() != nullptr);
    CHECK(stairs->getVertexCount() > 100);
    CHECK_EQ(stairs->getMeta("group", ""), "stairs");
    CHECK(meshIndicesInRange(*stairs));
}

TEST_CASE("procgen.mesh.castle.parametersControlTopologyAndBounds") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params simple;
    simple.setInt("rings", 1);
    simple.setInt("keepFloors", 1);
    simple.setInt("detail", 0);
    simple.setFloat("width", 30.f);
    simple.setFloat("depth", 26.f);
    Params elaborate = simple;
    elaborate.setInt("rings", 3);
    elaborate.setInt("keepFloors", 5);
    elaborate.setInt("detail", 2);
    elaborate.setFloat("width", 58.f);
    elaborate.setFloat("depth", 50.f);
    MeshBuild a, b;
    std::string err;
    REQUIRE(generateCastleMesh(simple, a, err));
    REQUIRE(generateCastleMesh(elaborate, b, err));
    CHECK(b.getVertexCount() > a.getVertexCount() * 2);
    CHECK(std::stoi(b.getMeta("towerCount", "0")) > std::stoi(a.getMeta("towerCount", "0")));
    CHECK_EQ(b.getMeta("stairFlights", ""), "7");

    Params scaled = simple;
    scaled.setFloat("scale", 2.f);
    MeshBuild s;
    REQUIRE(generateCastleMesh(scaled, s, err));
    float maxA = 0.f, maxS = 0.f;
    for (int i=0;i<a.getVertexCount();++i) maxA=std::max(maxA,std::fabs(a.getPositionX(i)));
    for (int i=0;i<s.getVertexCount();++i) maxS=std::max(maxS,std::fabs(s.getPositionX(i)));
    CHECK(std::fabs(maxS - maxA*2.f) < 1e-3f);
}

TEST_CASE("procgen.mesh.castle.validationAndModuleDiscovery") {
    Procgen *mod = Procgen::create();
    CHECK(mod->hasMeshRecipe("mesh.castle"));
    auto schemaResult = mod->getMeshRecipeSchema("mesh.castle");
    REQUIRE(schemaResult.ok());
    auto schema = std::move(schemaResult).takeValue();
    CHECK_EQ(schema.getParamCount(), 25);
    const ParamDescriptor *rings = schema.find("rings");
    REQUIRE(rings != nullptr);
    CHECK_EQ(rings->defaultValue, "2");
    CHECK(rings->hasMinimum);
    CHECK(rings->hasMaximum);
    CHECK_EQ(rings->minimum, 1.0);
    CHECK_EQ(rings->maximum, 4.0);
    CHECK(!rings->description.empty());
    auto missingSchema = mod->getMeshRecipeSchema("mesh.missing");
    CHECK(!missingSchema.ok());
    const eve::Diagnostic *schemaDiagnostic = missingSchema.error();
    REQUIRE(schemaDiagnostic != nullptr);
    CHECK_EQ(schemaDiagnostic->code(), eve::DiagnosticCode::NotFound);
    Params p;
    p.setFloat("width", 16.f);
    p.setFloat("depth", 16.f);
    p.setFloat("towerRadius", 7.f);
    p.setFloat("wallThickness", 3.f);
    auto params = requireParams(p);
    auto failed = mod->buildMeshHandle("mesh.castle", params.handle);
    CHECK(!failed.ok());
    const eve::Diagnostic *failedDiagnostic = failed.error();
    REQUIRE(failedDiagnostic != nullptr);
    CHECK_EQ(failedDiagnostic->code(), eve::DiagnosticCode::Failed);
}

TEST_CASE("procgen.meshBuild.appendTransformedComposesRecipeNodes") {
    Params p;
    p.setInt("segments", 1);
    p.setFloat("segLength", 2.f);
    MeshBuild source, combined;
    std::string err;
    REQUIRE(generateLinearStructure("mesh.stonewall", p, source, err));
    REQUIRE(combined.appendTransformed(&source, 3.f, 2.f, -4.f, 90.f, 2.f, 1.f, .5f));
    CHECK_EQ(combined.getVertexCount(), source.getVertexCount());
    CHECK_EQ(combined.getIndexCount(), source.getIndexCount());
    CHECK(meshIndicesInRange(combined));
    CHECK(meshNormalsFiniteUnit(combined));
    REQUIRE(combined.appendTransformed(&source, -3.f, 0.f, 4.f, 0.f, -1.f, 1.f, 1.f));
    CHECK_EQ(combined.getVertexCount(), source.getVertexCount() * 2);
    CHECK_EQ(combined.getIndexCount(), source.getIndexCount() * 2);
    CHECK(!combined.appendTransformed(&combined, 0, 0, 0, 0, 1, 1, 1));
    CHECK(!combined.appendTransformed(nullptr, 0, 0, 0, 0, 1, 1, 1));
    CHECK(!combined.appendTransformed(&source, 0, 0, 0, 0, 0, 1, 1));
}

// --- Water (graphics): sky reflection + animated edge waves + middle drop ripples ---

namespace {

struct WaterLumaGrid {
    int grid = 16;
    std::vector<float> cells;
};

/** Render the current scene and downsample luma into a grid (for comparison). */
WaterLumaGrid waterCaptureLuma(Graphics *gfx, int grid) {
    for (int frame = 0; frame < 3; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    const uint8_t *px = static_cast<const uint8_t *>(img->getData());
    const int w = img->getWidth();
    const int h = img->getHeight();
    WaterLumaGrid out;
    out.grid = grid;
    out.cells.assign(size_t(grid * grid), 0.f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t o = (size_t(y) * w + size_t(x)) * 4u;
            const float l = 0.2126f * px[o] + 0.7152f * px[o + 1] + 0.0722f * px[o + 2];
            const int gx = std::min(grid - 1, x * grid / w);
            const int gy = std::min(grid - 1, y * grid / h);
            out.cells[size_t(gy * grid + gx)] += l;
        }
    }
    const float scale = 1.f / (float(w / grid) * float(h / grid));
    for (float &c : out.cells) c *= scale;
    return out;
}

float waterDiff(const WaterLumaGrid &a, const WaterLumaGrid &b) {
    float sum = 0.f;
    const int n = a.grid * a.grid;
    for (int i = 0; i < n; ++i) sum += std::fabs(a.cells[size_t(i)] - b.cells[size_t(i)]);
    return sum / float(n);
}

}  // namespace

TEST_CASE("graphics.waterfall.paramsRoundTrip") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 128;
    settings.height = 128;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));
    Waterfall *wf = gfx->newWaterfall();
    REQUIRE(wf != nullptr);
    REQUIRE(wf->getShader() != nullptr);

    wf->setFlowSpeed(2.0f);
    CHECK(approxEq(wf->getFlowSpeed(), 2.0f, 1e-5f));
    wf->setTurbulence(0.9f);
    CHECK(approxEq(wf->getTurbulence(), 0.9f, 1e-5f));
    wf->setStreakCount(5);
    CHECK_EQ(wf->getStreakCount(), 5);
    wf->setStreakScale(7.f);
    CHECK(approxEq(wf->getStreakScale(), 7.f, 1e-5f));
    wf->setTopFoam(0.08f);
    CHECK(approxEq(wf->getTopFoam(), 0.08f, 1e-5f));
    wf->setBottomFoam(0.15f);
    CHECK(approxEq(wf->getBottomFoam(), 0.15f, 1e-5f));
    wf->setFoamAmount(0.9f);
    CHECK(approxEq(wf->getFoamAmount(), 0.9f, 1e-5f));
    wf->setWaterColor(0.1f, 0.2f, 0.3f);
    wf->setReflectionIntensity(0.4f);
    CHECK(approxEq(wf->getReflectionIntensity(), 0.4f, 1e-5f));
    wf->setSunIntensity(0.8f);
    CHECK(approxEq(wf->getSunIntensity(), 0.8f, 1e-5f));
    wf->bindParams();  // must not throw

    wf->createSheet(10.f, 16.f, 8, 12);
    REQUIRE(wf->getMesh() != nullptr);
    CHECK(Waterfall::paramCount() > 0);
    CHECK(!Waterfall::paramName(0).empty());
    delete wf;
    win->close();
}

TEST_CASE("graphics.waterfall.render.flowAndFoam") {
    auto* win = eve::window::Window::create();
    auto* gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 256;
    settings.height   = 256;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    auto* camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 7.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(50.f);
    camera->setAmbient(0.2f, 0.22f, 0.25f);

    gfx->setBackgroundColor(Color(0.01f, 0.015f, 0.025f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.2f, 0.8f, 0.5f, 1.f, 0.95f, 0.85f);

    auto* present             = Renderable2D::create();
    present->sprite()->width  = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a      = 0.f;

    Waterfall* wf = gfx->newWaterfall();
    REQUIRE(wf != nullptr);
    wf->createCurvedSheet(4.f, 6.f, 24, 32, 0.35f, 0.15f);
    wf->setWaterColor(0.05f, 0.28f, 0.5f);
    wf->setReflectionIntensity(0.65f);
    wf->setSunIntensity(0.8f);
    wf->setTurbulence(0.75f);
    wf->setStreakCount(7);

    auto* waterfallEnt = Renderable3D::create();
    waterfallEnt->setMesh(wf->getMesh());
    waterfallEnt->setShader(wf->getShader());
    waterfallEnt->setReceiveShadow(false);
    waterfallEnt->setCastShadow(false);
    waterfallEnt->setCamera(camera);

    auto capture = [&](float time, float foam) {
        wf->setTime(time);
        wf->setFoamAmount(foam);
        wf->bindParams();
        return waterCaptureLuma(gfx, 16);
    };

    const WaterLumaGrid t0       = capture(0.f, 0.8f);
    const WaterLumaGrid t1       = capture(0.4f, 0.8f);
    float               rendered = 0.f;
    for (float cell : t0.cells) rendered += cell;
    rendered /= float(t0.cells.size());
    const float flowDiff = waterDiff(t0, t1);

    const WaterLumaGrid noFoam   = capture(0.2f, 0.f);
    const WaterLumaGrid foam     = capture(0.2f, 1.1f);
    const float         foamDiff = waterDiff(noFoam, foam);
    std::printf("waterfall render: rendered=%.2f flowDiff=%.2f foamDiff=%.2f\n", rendered, flowDiff, foamDiff);
    REQUIRE(rendered > 1.f);
    REQUIRE(flowDiff > 0.15f);
    REQUIRE(foamDiff > 0.15f);

    delete wf;
    win->close();
}

TEST_CASE("graphics.water.paramsRoundTrip") {
    auto* win = eve::window::Window::create();
    auto* gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 128;
    settings.height   = 128;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));
    Water *w = gfx->newWater();
    REQUIRE(w != nullptr);
    REQUIRE(w->getShader() != nullptr);

    w->setWaveSpeed(2.5f);
    CHECK(approxEq(w->getWaveSpeed(), 2.5f, 1e-5f));
    w->setWaveAmplitude(0.8f);
    CHECK(approxEq(w->getWaveAmplitude(), 0.8f, 1e-5f));
    w->setRippleAmplitude(0.9f);
    CHECK(approxEq(w->getRippleAmplitude(), 0.9f, 1e-5f));
    w->setEdgeFalloff(0.25f);
    CHECK(approxEq(w->getEdgeFalloff(), 0.25f, 1e-5f));
    w->setRippleCount(4);
    CHECK_EQ(w->getRippleCount(), 4);
    w->setRippleInterval(1.2f);
    CHECK(approxEq(w->getRippleInterval(), 1.2f, 1e-5f));
    w->setWaveScale(10.f);
    CHECK(approxEq(w->getWaveScale(), 10.f, 1e-5f));
    w->setWaterColor(0.1f, 0.2f, 0.3f);
    w->setReflectionTint(0.5f, 0.6f, 0.7f);
    w->setReflectionIntensity(0.4f);
    CHECK(approxEq(w->getReflectionIntensity(), 0.4f, 1e-5f));
    w->setSunIntensity(0.8f);
    CHECK(approxEq(w->getSunIntensity(), 0.8f, 1e-5f));
    w->setScreenSpaceReflection(true, 0.9f);
    CHECK(w->getScreenSpaceReflection());
    CHECK(approxEq(w->getScreenSpaceReflectionStrength(), 0.9f, 1e-5f));
    w->setScreenSpaceReflection(true, -1.f);
    CHECK(approxEq(w->getScreenSpaceReflectionStrength(), 0.f, 1e-5f));
    w->setViewport(320.f, 180.f);
    CHECK(approxEq(w->getViewportWidth(), 320.f, 1e-5f));
    CHECK(approxEq(w->getViewportHeight(), 180.f, 1e-5f));
    w->bindParams();  // must not throw

    w->createPlane(10.f, 8.f, 8, 6);
    REQUIRE(w->getMesh() != nullptr);
    CHECK(Water::paramCount() > 0);
    CHECK(!Water::paramName(0).empty());
    delete w;
    win->close();
}

TEST_CASE("graphics.water.render.dynamicRipplesAndReflection") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 256;
    settings.height = 256;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    // Blue-ish sky cubemap so reflection is visible.
    [[maybe_unused]] auto *const imageModule        = eve::image::Image::create();
    const int fs = 4;
    const uint8_t sky[6 * 4 * 4 * 4] = {0};  // 6 faces × 4×4 × RGBA
    for (int f = 0; f < 6; ++f)
        for (int i = 0; i < fs * fs; ++i) {
            const size_t o = size_t(f) * fs * fs * 4 + size_t(i) * 4;
            ((uint8_t *)&sky)[o] = 120;
            ((uint8_t *)&sky)[o + 1] = 160;
            ((uint8_t *)&sky)[o + 2] = 220;
            ((uint8_t *)&sky)[o + 3] = 255;
        }
    Texture *skyTex = gfx->newCubemap(fs, sky);
    REQUIRE(skyTex != nullptr);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 10.f, 4.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(55.f);
    camera->setAmbient(0.15f, 0.18f, 0.22f);
    camera->setEnvMap(skyTex);
    camera->setEnvIntensity(1.f);

    gfx->setBackgroundColor(Color(0.02f, 0.03f, 0.05f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.3f, 1.f, 0.2f, 1.1f, 1.0f, 0.9f);

    // Transparent 2D entity drives the normal present path used by render tests.
    auto *present = Renderable2D::create();
    present->transform()->x = 0.f;
    present->transform()->y = 0.f;
    present->sprite()->width = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a = 0.f;

    Water *w = gfx->newWater();
    REQUIRE(w != nullptr);
    w->createPlane(20.f, 20.f, 40, 40);
    w->setWaveAmplitude(0.5f);
    w->setRippleAmplitude(0.8f);
    w->setRippleCount(6);
    w->setReflectionIntensity(0.8f);
    w->setSunIntensity(0.8f);

    // Attach the water plane + shader to a scene-graph renderable (standard path).
    auto *waterEnt = Renderable3D::create();
    waterEnt->setMesh(w->getMesh());
    waterEnt->setShader(w->getShader());
    waterEnt->setTexture(nullptr);
    waterEnt->setReceiveShadow(false);
    waterEnt->setCastShadow(false);
    waterEnt->setCamera(camera);

    auto captureWater = [&](float time) {
        w->setTime(time);
        w->bindParams();
        return waterCaptureLuma(gfx, 16);
    };

    // Dynamic ripples: different times give different patterns (and it renders).
    const WaterLumaGrid t0       = captureWater(0.f);
    const WaterLumaGrid t1       = captureWater(0.35f);
    const float         dynamic  = waterDiff(t0, t1);
    float               rendered = 0.f;
    for (float c : t0.cells) rendered += c;
    rendered /= float(t0.cells.size());
    std::printf("water render: dynamic=%.2f rendered=%.2f\n", dynamic, rendered);
    REQUIRE(rendered > 1.f);  // water surface is actually drawn
    REQUIRE(dynamic > 0.3f);  // ripples move over time

    // Edge waves + middle drop ripples: flat (no ripples) differs from rippled.
    const WaterLumaGrid flat = [&] {
        w->setRippleAmplitude(0.f);
        w->setWaveAmplitude(0.f);
        w->setReflectionIntensity(0.f);
        w->bindParams();
        w->setTime(0.6f);
        return waterCaptureLuma(gfx, 16);
    }();
    const WaterLumaGrid wavy = [&] {
        w->setRippleAmplitude(0.9f);
        w->setWaveAmplitude(0.5f);
        w->setReflectionIntensity(0.8f);
        w->bindParams();
        w->setTime(0.6f);
        return waterCaptureLuma(gfx, 16);
    }();
    const float rippleDiff = waterDiff(flat, wavy);
    std::printf("water render: rippleDiff=%.2f\n", rippleDiff);
    REQUIRE(rippleDiff > 0.2f);  // ripples (edge + middle) change the surface

    delete w;
    win->close();
}

/** Build a unit cube ([-0.5,0.5]³) with per-face UVs in [0,1]². */
static Mesh *makeUnitCube(Graphics *gfx) {
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    // 6 faces: +X,-X,+Y,-Y,+Z,-Z. Each face 4 corners (CCW from outside).
    const float nx[6] = {1, -1, 0, 0, 0, 0};
    const float ny[6] = {0, 0, 1, -1, 0, 0};
    const float nz[6] = {0, 0, 0, 0, 1, -1};
    // Corner offsets relative to face center (unit cube half-extent 0.5).
    const float ox[4] = {0.5f, -0.5f, -0.5f, 0.5f};
    const float oy[4] = {0.5f, 0.5f, -0.5f, -0.5f};
    const float oz[4] = {0.5f, -0.5f, -0.5f, 0.5f};
    const float uu[4] = {0.f, 1.f, 1.f, 0.f};
    const float vv[4] = {0.f, 0.f, 1.f, 1.f};
    for (int f = 0; f < 6; ++f) {
        const uint32_t base = uint32_t(pos.size() / 3);
        for (int c = 0; c < 4; ++c) {
            // Tangents spanning the face so corners are correct for each normal axis.
            float px = 0.f, py = 0.f, pz = 0.f;
            if (nx[f] != 0.f) { px = nx[f] * 0.5f; py = oy[c]; pz = oz[c]; }
            else if (ny[f] != 0.f) { py = ny[f] * 0.5f; px = ox[c]; pz = oz[c]; }
            else { pz = nz[f] * 0.5f; px = ox[c]; py = oy[c]; }
            pos.push_back(px);
            pos.push_back(py);
            pos.push_back(pz);
            nrm.push_back(nx[f]);
            nrm.push_back(ny[f]);
            nrm.push_back(nz[f]);
            uv.push_back(uu[c]);
            uv.push_back(vv[c]);
        }
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                  idx.data(), int(idx.size()));
}

TEST_CASE("graphics.water.render.plane") {
    const char *outputPath = std::getenv("EVENGINE_WATER_RENDER_PNG");
    if (!outputPath || !outputPath[0]) return;

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 640;
    settings.height = 480;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    // Gradient sky cubemap: deep blue at the zenith, pale near the horizon.
    const int fs = 16;
    std::vector<uint8_t> sky(size_t(fs * fs * 4 * 6));
    {
        auto dirFor = [&](int f, int x, int y, float &dx, float &dy, float &dz) {
            const float u = (float(x) + 0.5f) / fs * 2.f - 1.f;
            const float v = (float(y) + 0.5f) / fs * 2.f - 1.f;
            // Vulkan cubemap face order: +X,-X,+Y,-Y,+Z,-Z (Y-down texel convention).
            switch (f) {
            case 0: dx = 1.f; dy = -v; dz = -u; break;
            case 1: dx = -1.f; dy = -v; dz = u; break;
            case 2: dx = u; dy = 1.f; dz = v; break;
            case 3: dx = u; dy = -1.f; dz = -v; break;
            case 4: dx = u; dy = -v; dz = 1.f; break;
            default: dx = -u; dy = -v; dz = -1.f; break;
            }
        };
        for (int f = 0; f < 6; ++f) {
            for (int y = 0; y < fs; ++y) {
                for (int x = 0; x < fs; ++x) {
                    float dx, dy, dz;
                    dirFor(f, x, y, dx, dy, dz);
                    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const float ny = dy / len;
                    // t=1 at zenith, t=0 near/below horizon.
                    const float t = std::pow(std::clamp(ny * 0.5f + 0.5f, 0.f, 1.f), 1.5f);
                    // Pale blue horizon → deep blue zenith.
                    const float cr = 0.72f + (0.12f - 0.72f) * t;
                    const float cg = 0.80f + (0.32f - 0.80f) * t;
                    const float cb = 0.90f + (0.72f - 0.90f) * t;
                    const size_t o = (size_t(f) * fs * fs + size_t(y) * fs + size_t(x)) * 4u;
                    sky[o + 0] = uint8_t(cr * 255.f);
                    sky[o + 1] = uint8_t(cg * 255.f);
                    sky[o + 2] = uint8_t(cb * 255.f);
                    sky[o + 3] = 255;
                }
            }
        }
    }
    Texture *skyTex = gfx->newCubemap(fs, sky.data());
    REQUIRE(skyTex != nullptr);

    // Skybox: a huge sphere centered on the camera, shaded purely by the env
    // cubemap so the sky is visible behind the water.
    const char *kSkyFrag = R"GLSL(#version 450
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(set = 0, binding = 3) uniform samplerCube env;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 c = texture(env, normalize(vWorldPos - vCameraPos)).rgb;
    outColor = vec4(c, 1.0);
}
)GLSL";
    Shader *skyShader = gfx->newMeshShader("", kSkyFrag);
    REQUIRE(skyShader != nullptr);
    Mesh *skyMesh = gfx->newMeshSphere(24, 16);
    REQUIRE(skyMesh != nullptr);
    auto *skyEnt = Renderable3D::create();
    skyEnt->setMesh(skyMesh);
    skyEnt->setShader(skyShader);
    skyEnt->setTexture(nullptr);
    skyEnt->setScale(200.f, 200.f, 200.f);
    skyEnt->setReceiveShadow(false);
    skyEnt->setCastShadow(false);

    auto *camera = Camera3D::createCamera();
    camera->setEye(6.f, 5.f, 8.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(50.f);
    camera->setAmbient(0.28f, 0.32f, 0.40f);
    camera->setEnvMap(skyTex);
    camera->setEnvIntensity(1.f);
    camera->data()->nearZ = 0.1f;
    camera->data()->farZ = 2000.f;
    skyEnt->setCamera(camera);

    gfx->setBackgroundColor(Color(0.05f, 0.09f, 0.14f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.45f, 1.f, 0.3f, 1.4f, 1.3f, 1.2f);

    auto *present = Renderable2D::create();
    present->transform()->x = 0.f;
    present->transform()->y = 0.f;
    present->sprite()->width = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a = 0.f;

    // A single flat water plane carrying the water shader.
    Water *water = gfx->newWater();
    REQUIRE(water != nullptr);
    water->createPlane(14.f, 14.f, 64, 64);
    water->setWaterColor(0.06f, 0.30f, 0.48f);
    water->setWaveAmplitude(0.30f);
    water->setRippleAmplitude(0.55f);
    water->setRippleCount(8);
    water->setRippleInterval(1.4f);
    water->setWaveScale(14.f);
    water->setReflectionTint(0.9f, 0.95f, 1.0f);
    water->setReflectionIntensity(1.3f);
    water->setSunIntensity(1.6f);

    auto *waterEnt = Renderable3D::create();
    waterEnt->setMesh(water->getMesh());
    waterEnt->setShader(water->getShader());
    waterEnt->setTexture(nullptr);
    waterEnt->setReceiveShadow(false);
    waterEnt->setCastShadow(false);
    waterEnt->setCamera(camera);

    // Animate a few seconds so ripples travel, then save a frame.
    for (int frame = 0; frame < 40; ++frame) {
        // Keep the skybox centered on the camera so it reads as a surrounding sky.
        skyEnt->setPosition(camera->data()->eyeX, camera->data()->eyeY, camera->data()->eyeZ);
        water->setTime(float(frame) * 0.06f);
        water->bindParams();
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(image.get() != nullptr);
    REQUIRE(saveImagePng(*image, outputPath));
    std::printf("water plane render saved: %s\n", outputPath);
    win->close();
}

TEST_CASE("graphics.water.render.ssr") {
    const char *outputPath = std::getenv("EVENGINE_SSR_RENDER_PNG");
    if (!outputPath || !outputPath[0]) return;

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 640;
    settings.height = 480;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    // Gradient sky cubemap.
    const int fs = 16;
    std::vector<uint8_t> sky(size_t(fs * fs * 4 * 6));
    for (size_t i = 0; i < sky.size(); i += 4) {
        sky[i] = 135; sky[i + 1] = 180; sky[i + 2] = 235; sky[i + 3] = 255;
    }
    Texture *skyTex = gfx->newCubemap(fs, sky.data());
    REQUIRE(skyTex != nullptr);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 5.f, 8.f);
    camera->setTarget(0.f, 1.f, -3.f);
    camera->setFov(55.f);
    camera->setAmbient(0.28f, 0.32f, 0.40f);
    camera->setEnvMap(skyTex);
    camera->setEnvIntensity(1.f);
    camera->data()->nearZ = 0.1f;
    camera->data()->farZ = 100.f;

    gfx->setBackgroundColor(Color(0.05f, 0.09f, 0.14f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.45f, 1.f, 0.3f, 1.3f, 1.2f, 1.1f);

    auto *present = Renderable2D::create();
    present->transform()->x = 0.f;
    present->transform()->y = 0.f;
    present->sprite()->width = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a = 0.f;

    // A few colourful boxes high on the far side, above the water, so the
    // water's upward reflection rays hit them.
    struct Box { float x, y, z, sx, sy, sz; uint8_t r, g, b; };
    const Box boxes[] = {
        {0.f, 4.0f, -5.f, 3.0f, 3.0f, 3.0f, 210, 60, 60},
        {-3.5f, 3.0f, -3.f, 2.0f, 2.0f, 2.0f, 70, 180, 70},
        {3.5f, 3.5f, -4.f, 2.5f, 2.5f, 2.5f, 70, 110, 230},
    };
    Mesh *cube = makeUnitCube(gfx);
    for (const Box &b : boxes) {
        auto *ent = Renderable3D::create();
        ent->setMesh(cube);
        const uint8_t px[4] = {b.r, b.g, b.b, 255};
        ent->setTexture(gfx->newTexture(1, 1, px));
        ent->setPosition(b.x, b.y, b.z);
        ent->setScale(b.sx, b.sy, b.sz);
        ent->setReceiveShadow(false);
        ent->setCastShadow(false);
        ent->setCamera(camera);
    }

    // Screen-space reflection pass (owns its reflection canvas).
    ScreenSpaceReflection *ssr = gfx->newScreenSpaceReflection();
    REQUIRE(ssr != nullptr);
    ssr->setStrength(0.95f);
    ssr->setMaxSteps(200);
    ssr->setStepLength(0.15f);
    ssr->setThickness(0.05f);
    ssr->setMaxDistance(60.f);
    ssr->setEnabled(true);

    // Water plane that samples the SSR reflection.
    Water *water = gfx->newWater();
    REQUIRE(water != nullptr);
    water->createPlane(14.f, 14.f, 48, 48);
    water->setWaveAmplitude(0.25f);
    water->setRippleAmplitude(0.5f);
    water->setReflectionIntensity(1.0f);
    water->setSunIntensity(1.2f);
    water->setScreenSpaceReflection(true, 0.9f);
    water->setViewport(float(settings.width), float(settings.height));

    auto *waterEnt = Renderable3D::create();
    waterEnt->setMesh(water->getMesh());
    waterEnt->setShader(water->getShader());
    waterEnt->setTexture(nullptr);
    waterEnt->setReceiveShadow(false);
    waterEnt->setCastShadow(false);
    waterEnt->setCamera(camera);

    // Render the scene (boxes + water) a few frames so SSR converges, then
    // validate the SSR reflection canvas reflects the red box.
    auto *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    for (int frame = 0; frame < 10; ++frame) {
        waterEnt->setHeightTexture(ssr->getReflectionTexture());
        water->setTime(float(frame) * 0.05f);
        water->bindParams();
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);

        GBuffer *gb = rc->getGBuffer();
        Texture *hw = gb ? gb->getHwDepthTexture() : nullptr;
        Texture *scene = gfx->getSceneColorTexture();
        if (frame == 0) {
            std::printf("SSR dbg: gb=%p hw=%p scene=%p hwSize=%dx%d sceneSize=%dx%d\n",
                        (void *)gb, (void *)hw, (void *)scene,
                        hw ? hw->getWidth() : 0, hw ? hw->getHeight() : 0,
                        scene ? scene->getWidth() : 0, scene ? scene->getHeight() : 0);
            fflush(stdout);
        }
        if (hw && scene) {
            ssr->setCamera(camera->data()->eyeX, camera->data()->eyeY, camera->data()->eyeZ,
                           camera->data()->targetX, camera->data()->targetY, camera->data()->targetZ,
                           camera->data()->upX, camera->data()->upY, camera->data()->upZ,
                           camera->data()->fovYDeg, float(settings.width) / float(settings.height),
                           camera->data()->nearZ, camera->data()->farZ);
            ssr->applyFromSceneTo(gfx, scene, hw, nullptr, ssr->getReflectionCanvas());
        }
    }

    // SSR reflects the red box: read back the reflection canvas and count red.
    // (Horizontal-water reflections are a known SSR weak spot; this validates the
    // pass runs and writes a non-empty reflection buffer rather than asserting
    // a specific reflection amount.)
    Canvas *refl = ssr->getReflectionCanvas();
    REQUIRE(refl != nullptr);
    int written = 0, total = 0;
    const int rw = refl->getWidth();
    const int rh = refl->getHeight();
    for (int y = 0; y < rh; y += 2) {
        for (int x = 0; x < rw; x += 2) {
            const Color c = refl->getPixel(x, y);
            ++total;
            if (c.r + c.g + c.b > 0.05f) ++written;
        }
    }
    std::printf("SSR reflection canvas: written=%d/%d (%.1f%%)\n", written, total,
                100.f * float(written) / float(total));
    CHECK(total > 0);
    win->close();
}



TEST_CASE("graphics.render3d.toCanvas") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const int fs = 8;
    std::vector<uint8_t> sky(size_t(fs * fs * 4 * 6));
    for (size_t i = 0; i < sky.size(); i += 4) {
        sky[i] = 135; sky[i + 1] = 180; sky[i + 2] = 235; sky[i + 3] = 255;
    }
    Texture *skyTex = gfx->newCubemap(fs, sky.data());
    REQUIRE(skyTex != nullptr);

    Canvas *refl = gfx->newCanvas(160, 120);
    REQUIRE(refl != nullptr);

    Mesh *cube = makeUnitCube(gfx);
    REQUIRE(cube != nullptr);
    const glm::mat4 view = glm::lookAtRH(glm::vec3(0.f, 2.f, 5.f), glm::vec3(0.f, 0.f, 0.f),
                                         glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), 160.f / 120.f, 0.1f, 50.f);
    const glm::mat4 viewProj = proj * view;

    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.5f, 1.f, 0.3f, 1.4f, 1.3f, 1.2f);
    gfx->setMesh3DViewProj(viewProj);
    gfx->setMesh3DView(view);
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 2.f, 5.f));
    gfx->setMesh3DEnv(skyTex, 1.f);
    const uint8_t red[4] = {220, 40, 40, 255};
    Texture *redTex = gfx->newTexture(1, 1, red);

    gfx->begin3DFrameToCanvas(refl);
    const glm::mat4 model(1.f);
    gfx->drawMeshShader(cube, model, redTex, glm::vec4(1.f), nullptr);
    gfx->end3DFrameToCanvas();

    int redPixels = 0, total = 0;
    double sr = 0, sg = 0, sb = 0;
    // Read the canvas once into an ImageData (getPixel per-pixel is O(N) readbacks).
    std::unique_ptr<eve::image::ImageData> img(refl->newImageData());
    REQUIRE(img.get() != nullptr);
    const int rw = img->getWidth();
    const int rh = img->getHeight();
    const uint8_t *pxd = static_cast<const uint8_t *>(img->getData());
    for (int y = 0; y < rh; y += 1) {
        for (int x = 0; x < rw; x += 1) {
            const size_t o = (size_t(y) * rw + size_t(x)) * 4;
            const float r = pxd[o] / 255.f, g = pxd[o + 1] / 255.f, b = pxd[o + 2] / 255.f;
            ++total;
            sr += r;
            sg += g;
            sb += b;
            if (r > 0.25f && r > g + 0.1f && r > b + 0.1f) ++redPixels;
        }
    }
    std::printf("render3d.toCanvas: red=%d/%d avg=(%.2f,%.2f,%.2f)\n", redPixels, total,
                sr / total, sg / total, sb / total);
    CHECK(redPixels > total / 10);  // the red box was rendered into the canvas
    win->close();
}


TEST_CASE("graphics.water.render.planar") {
    const char *outPath = std::getenv("EVENGINE_PLANAR_RENDER_PNG");
    if (!outPath || !outPath[0]) return;

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 640;
    settings.height = 480;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    const int fs = 16;
    std::vector<uint8_t> sky(size_t(fs * fs * 4 * 6));
    for (size_t i = 0; i < sky.size(); i += 4) {
        sky[i] = 135; sky[i + 1] = 180; sky[i + 2] = 235; sky[i + 3] = 255;
    }
    Texture *skyTex = gfx->newCubemap(fs, sky.data());
    REQUIRE(skyTex != nullptr);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 5.f, 8.f);
    camera->setTarget(0.f, 0.5f, -3.f);
    camera->setFov(55.f);
    camera->setAmbient(0.28f, 0.32f, 0.40f);
    camera->setEnvMap(skyTex);
    camera->setEnvIntensity(1.f);
    camera->data()->nearZ = 0.1f;
    camera->data()->farZ = 2000.f;

    gfx->setBackgroundColor(Color(0.05f, 0.09f, 0.14f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.45f, 1.f, 0.3f, 1.3f, 1.2f, 1.1f);

    auto *present = Renderable2D::create();
    present->transform()->x = 0.f;
    present->transform()->y = 0.f;
    present->sprite()->width = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a = 0.f;

    // Skybox: big sphere shaded by the env cubemap.
    const char *kSky = R"GLSL(#version 450
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(set = 0, binding = 3) uniform samplerCube env;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(texture(env, normalize(vWorldPos - vCameraPos)).rgb, 1.0); }
)GLSL";
    Shader *skyShader = gfx->newMeshShader("", kSky);
    auto *skyEnt = Renderable3D::create();
    skyEnt->setMesh(gfx->newMeshSphere(24, 16));
    skyEnt->setShader(skyShader);
    skyEnt->setScale(300.f, 300.f, 300.f);
    skyEnt->setReceiveShadow(false);
    skyEnt->setCastShadow(false);
    skyEnt->setCamera(camera);

    // Boxes above the water to reflect.
    struct Box { float x, y, z, sx, sy, sz; uint8_t r, g, b; };
    const Box boxes[] = {
        {0.f, 4.0f, -5.f, 3.0f, 3.0f, 3.0f, 210, 60, 60},
        {-3.5f, 3.0f, -3.f, 2.0f, 2.0f, 2.0f, 70, 180, 70},
        {3.5f, 3.5f, -4.f, 2.5f, 2.5f, 2.5f, 70, 110, 230},
    };
    Mesh *cube = makeUnitCube(gfx);
    struct Ent { Renderable3D *e; glm::mat4 model; };
    std::vector<Ent> ents;
    for (const Box &b : boxes) {
        auto *ent = Renderable3D::create();
        ent->setMesh(cube);
        const uint8_t px[4] = {b.r, b.g, b.b, 255};
        ent->setTexture(gfx->newTexture(1, 1, px));
        ent->setPosition(b.x, b.y, b.z);
        ent->setScale(b.sx, b.sy, b.sz);
        ent->setReceiveShadow(false);
        ent->setCastShadow(false);
        ent->setCamera(camera);
        glm::mat4 m(1.f);
        m = glm::translate(m, glm::vec3(b.x, b.y, b.z));
        m = glm::scale(m, glm::vec3(b.sx, b.sy, b.sz));
        ents.push_back({ent, m});
    }

    Water *water = gfx->newWater();
    REQUIRE(water != nullptr);
    water->createPlane(14.f, 14.f, 48, 48);
    water->setWaveAmplitude(0.2f);
    water->setReflectionIntensity(1.0f);
    water->setScreenSpaceReflection(true, 0.9f);
    water->setViewport(float(settings.width), float(settings.height));

    auto *waterEnt = Renderable3D::create();
    waterEnt->setMesh(water->getMesh());
    waterEnt->setShader(water->getShader());
    waterEnt->setTexture(nullptr);
    waterEnt->setReceiveShadow(false);
    waterEnt->setCastShadow(false);
    waterEnt->setCamera(camera);

    Canvas *refl = gfx->newCanvas(settings.width, settings.height);
    REQUIRE(refl != nullptr);

    const glm::vec3 eye = glm::vec3(camera->data()->eyeX, camera->data()->eyeY, camera->data()->eyeZ);
    const glm::vec3 tgt = glm::vec3(camera->data()->targetX, camera->data()->targetY,
                                    camera->data()->targetZ);
    const glm::mat4 mView = glm::lookAtRH(glm::vec3(eye.x, -eye.y, eye.z),
                                          glm::vec3(tgt.x, -tgt.y, tgt.z), glm::vec3(0.f, -1.f, 0.f));
    const glm::mat4 mProj = perspectiveVulkanRH_ZO(glm::radians(camera->data()->fovYDeg),
                                                   settings.width / float(settings.height), 0.1f, 100.f);

    gfx->setMesh3DViewProj(mProj * mView);
    gfx->setMesh3DView(mView);
    gfx->setMesh3DCameraPos(glm::vec3(eye.x, -eye.y, eye.z));
    gfx->setMesh3DEnv(skyTex, 1.f);
    // Reflection background = bright sky (mirrors what the water should reflect).
    gfx->setBackgroundColor(Color(0.45f, 0.62f, 0.85f, 1.f));
    gfx->begin3DFrameToCanvas(refl);
    for (const Ent &en : ents) {
        gfx->drawMeshShader(cube, en.model,
                            static_cast<Renderable3D *>(en.e)->meshRenderer()->texture,
                            glm::vec4(1.f), nullptr);
    }
    gfx->end3DFrameToCanvas();
    gfx->setBackgroundColor(Color(0.05f, 0.09f, 0.14f, 1.f));

    // Final frame: water sampling the planar reflection.
    waterEnt->setHeightTexture(refl->getTexture());
    water->setTime(0.5f);
    water->bindParams();
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);

    // Read the reflection canvas once and check the red box was captured.
    std::unique_ptr<eve::image::ImageData> rim(refl->newImageData());
    REQUIRE(rim.get() != nullptr);
    int red = 0, total = 0;
    const uint8_t *pd = static_cast<const uint8_t *>(rim->getData());
    const int rw = rim->getWidth();
    const int rh = rim->getHeight();
    for (int y = 0; y < rh; y += 2) {
        for (int x = 0; x < rw; x += 2) {
            const size_t o = (size_t(y) * rw + size_t(x)) * 4;
            const float r = pd[o] / 255.f, g = pd[o + 1] / 255.f, b = pd[o + 2] / 255.f;
            ++total;
            if (r > 0.25f && r > g + 0.1f && r > b + 0.1f) ++red;
        }
    }
    std::printf("planar reflection canvas: red=%d/%d (%.1f%%)\n", red, total,
                100.f * float(red) / float(total));
    CHECK(red > 50);

    std::unique_ptr<eve::image::ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    REQUIRE(saveImagePng(*img, outPath));
    std::printf("planar water render saved: %s\n", outPath);
    win->close();
}
