#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "filesystem/FileData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
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
#include "procgen/texture/ColorRamp.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>
#include "RenderImageAudit.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::procgen;
using namespace eve::graphics;

namespace {

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
    Mesh *treeMesh = generator.generateMesh("mesh.tree", &params, gfx);
    REQUIRE(treeMesh != nullptr);

    // 4px bark/foliage atlas; UVs are partitioned by the mesh recipe.
    const uint8_t atlasPixels[] = {
        111, 70, 42, 255, 128, 82, 47, 255,
        64, 119, 57, 255, 82, 145, 67, 255,
    };
    Texture *atlas = gfx->newTexture(4, 1, atlasPixels);
    REQUIRE(atlas != nullptr);

    auto *tree = Renderable3D::create();
    tree->setMesh(treeMesh);
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
    Mesh *bushMesh = generator.generateMesh("mesh.bush", &params, gfx);
    REQUIRE(bushMesh != nullptr);

    // A tiny two-tone foliage atlas (dark base / light highlight), like the tree test.
    const uint8_t atlasPixels[] = {
        104, 67, 38, 255, 84, 132, 60, 255,
    };
    Texture *atlas = gfx->newTexture(2, 1, atlasPixels);
    REQUIRE(atlas != nullptr);

    auto *bush = Renderable3D::create();
    bush->setMesh(bushMesh);
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
    Grid2D *grid = mod->generate("wfc.simple", &p);
    REQUIRE(grid != nullptr);
    CHECK(borderIsWall(*grid));
    CHECK_EQ(grid->getWidth(), 16);
    CHECK_EQ(grid->getHeight(), 12);
    delete grid;

    Params bad;
    bad.setSeed(1);
    bad.setSize(8, 8);
    bad.setString("preset", "invalid");
    CHECK(mod->generate("wfc.simple", &bad) == nullptr);
    CHECK(mod->lastError().find("preset") != std::string::npos);
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
    Mesh *planetMesh = procgen->generateMesh("mesh.hexplanet", &params, gfx);
    REQUIRE(planetMesh != nullptr);

    const uint8_t oceanBlue[4] = {42, 155, 181, 255};
    Texture *planetTexture = gfx->newTexture(1, 1, oceanBlue);
    REQUIRE(planetTexture != nullptr);

    auto *planet = eve::graphics::Renderable3D::create();
    planet->setMesh(planetMesh);
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

    eve::image::Image::create();
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
    Mesh *towerMesh = procgen->generateMesh("mesh.skyscraper", &params, gfx);
    REQUIRE(towerMesh != nullptr);

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
    tower->setMesh(towerMesh);
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

    eve::image::Image::create();
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
    MeshBuild *m = mod->buildMesh("mesh.marchingcubes", &p);
    REQUIRE(m != nullptr);
    CHECK(m->getVertexCount() > 50);
    CHECK(meshIndicesInRange(*m));
    CHECK(mod->hasMeshRecipe("mesh.marchingcubes"));
    CHECK(mod->getMeshRecipeCount() >= 1);
    bool listed = false;
    for (int i = 0; i < mod->getMeshRecipeCount(); ++i) {
        if (mod->getMeshRecipeId(i) == "mesh.marchingcubes") listed = true;
    }
    CHECK(listed);
    delete m;

    Params bad;
    bad.setInt("resolution", 8);
    bad.setString("field", "nope");
    CHECK(mod->buildMesh("mesh.marchingcubes", &bad) == nullptr);
    CHECK(mod->lastError().find("field") != std::string::npos);
    CHECK(mod->buildMesh("mesh.missing", &p) == nullptr);
    CHECK(mod->generateMesh("mesh.marchingcubes", nullptr, nullptr) == nullptr);
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
    CHECK(mod->buildMesh("mesh.marchingcubes", nullptr) == nullptr);
    CHECK(mod->lastError().find("null") != std::string::npos);
    CHECK(mod->generateMesh("mesh.marchingcubes", nullptr, nullptr) == nullptr);

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
    Grid2D *grid = mod->generate("dungeon.bsp", &p);
    CHECK(grid != nullptr);

    mod->setPaletteGid("test", "wall", 10);
    mod->setPaletteGid("test", "floor", 20);
    mod->setPaletteGid("test", "corridor", 21);

    eve::map::TileLayer *layer = eve::map::TileLayer::createLayer(16, 12, 16.f, 16.f);
    CHECK(mod->applyToLayer(grid, "test", layer));
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
    delete grid;
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
        eve::image::ImageData *a = TextureRecipeRegistry::instance().generate(id, p, err);
        eve::image::ImageData *b = TextureRecipeRegistry::instance().generate(id, p, err);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK_EQ(a->getWidth(), 32);
        CHECK_EQ(a->getHeight(), 32);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK_EQ(a->getSize(), b->getSize());
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
        delete a;
        delete b;
    }
}

TEST_CASE("procgen.texture.generateImage.andNormal") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(3);
    p.setSize(48, 48);
    p.setInt("colors", 6);
    eve::image::ImageData *img = mod->generateImage("tex.marble", &p);
    REQUIRE(img != nullptr);
    eve::image::ImageData *nrm = mod->generateNormalImage("tex.marble", &p);
    REQUIRE(nrm != nullptr);
    CHECK_EQ(nrm->getWidth(), 48);
    CHECK_EQ(nrm->getHeight(), 48);
    delete img;
    delete nrm;
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
        eve::image::ImageData *img = TextureRecipeRegistry::instance().generate(id, p, err);
        REQUIRE(img != nullptr);
        CHECK_EQ(img->getFormat(), std::string("RGBA8"));
        CHECK_EQ(img->getWidth(), 48);
        CHECK_EQ(img->getHeight(), 48);
        delete img;
    }
    CHECK(TextureRecipeRegistry::instance().has("tex.cloud"));
    CHECK(TextureRecipeRegistry::instance().has("tex.cloud_shadow"));
}

TEST_CASE("procgen.cloud.viaModule") {
    Procgen *mod = Procgen::create();
    CloudField *f = mod->newCloudField();
    REQUIRE(f != nullptr);
    f->setSeed(3);
    f->setWorldScale(64.f);
    f->setCoverage(0.5f);
    const float c0 = mod->cloudCoverageAt(f, 2.f, 3.f, 0.f);
    CHECK(c0 >= 0.f);
    CHECK(c0 <= 1.f);
    CHECK(mod->cloudCoverageAt(nullptr, 0.f, 0.f, 0.f) == 0.f);

    CloudShadow *s = mod->newCloudShadow();
    REQUIRE(s != nullptr);
    s->setSunDirection(0.5f, 1.f, 0.f);
    s->setCloudAltitude(50.f);
    s->setStrength(0.8f);
    CHECK(mod->cloudShadowFactor(s, 2.f, 3.f, 0.f) >= 0.f);
    CHECK(mod->cloudShadowFactor(nullptr, 0.f, 0.f, 0.f) == 1.f);

    std::vector<float> buf(16 * 16);
    mod->sampleCloud(f, buf.data(), 16, 16, 0.f, 0.f, 0.f, 64.f);
    mod->sampleCloudShadow(s, buf.data(), 16, 16, 0.f, 0.f, 0.f, 64.f);
    delete f;
    delete s;
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
        eve::image::ImageData *a = TextureRecipeRegistry::instance().generate(id, p, err);
        eve::image::ImageData *b = TextureRecipeRegistry::instance().generate(id, p, err);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
        delete a;
        delete b;
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
        PbrTextureSet *a = PbrRecipeRegistry::instance().generate(id, p, err);
        PbrTextureSet *b = PbrRecipeRegistry::instance().generate(id, p, err);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
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
        a->destroy();
        b->destroy();
        delete a;
        delete b;
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
    PbrTextureSet *set = PbrRecipeRegistry::instance().generate("pbr.marble", p, err);
    REQUIRE(set != nullptr);
    const auto *m = static_cast<const uint8_t *>(set->metallic->getData());
    for (size_t i = 0; i < set->metallic->getSize(); i += 4) {
        CHECK_EQ(m[i], 255);  // fully metallic
    }
    const auto *r = static_cast<const uint8_t *>(set->roughness->getData());
    for (size_t i = 0; i < set->roughness->getSize(); i += 4) {
        CHECK(r[i] >= 25);  // roughnessLow 0.1 -> 25
        CHECK(r[i] <= 51);  // roughnessHigh 0.2 -> 51
    }
    set->destroy();
    delete set;
}

TEST_CASE("procgen.pbr.viaModuleAndErrors") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(9);
    p.setSize(40, 40);
    PbrTextureSet *set = mod->generatePbrMaterial("pbr.wall", &p);
    REQUIRE(set != nullptr);
    CHECK(set->albedo != nullptr);
    CHECK(set->normal != nullptr);
    set->destroy();
    delete set;

    CHECK(mod->hasPbrRecipe("pbr.wood"));
    CHECK(mod->getPbrRecipeCount() >= 13);
    bool listed = false;
    for (int i = 0; i < mod->getPbrRecipeCount(); ++i) {
        if (mod->getPbrRecipeId(i) == "pbr.wood") listed = true;
    }
    CHECK(listed);

    CHECK(mod->generatePbrMaterial("pbr.missing", &p) == nullptr);
    CHECK(mod->generatePbrMaterial("pbr.wall", nullptr) == nullptr);
    CHECK(mod->lastError().find("null") != std::string::npos);
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
    MeshBuild *m = mod->buildMesh("mesh.stonewall", &p);
    REQUIRE(m != nullptr);
    CHECK(m->getVertexCount() > 0);
    CHECK(meshIndicesInRange(*m));
    CHECK(mod->hasMeshRecipe("mesh.stonewall"));
    CHECK(mod->hasMeshRecipe("mesh.bridge"));
    delete m;

    CHECK(mod->buildMesh("mesh.nonexistent", &p) == nullptr);
    CHECK(mod->lastError().find("unknown") != std::string::npos);
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
    eve::image::Image::create();
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
    eve::image::Image::create();

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
    eve::image::Image::create();

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
    eve::image::Image::create();

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
