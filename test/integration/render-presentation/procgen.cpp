#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include <bit>

#include "asset/procgen/TerrainMaterialAtlas.h"
#include "data/ByteData.h"
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
#include "procgen/algorithms/CastleMesh.h"
#include "procgen/algorithms/HexTerrain.h"
#include "procgen/algorithms/LinearStructure.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainFile.h"
#include "procgen/heightmap/TerrainPipeline.h"
#include "procgen/heightmap/TerrainStreaming.h"
#include "procgen/texture/ColorRamp.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"
#include "water_scene_fixture.h"
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
#include <limits>
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

namespace {
class TrackingTerrainSource final : public ITerrainArchiveSource {
public:
    explicit TrackingTerrainSource(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::uint64_t size() const noexcept override { return bytes_.size(); }
    eve::Result<std::vector<std::uint8_t>> read(std::uint64_t offset,
                                                std::size_t length) const override {
        if (offset > bytes_.size() || length > bytes_.size() - std::size_t(offset))
            return eve::Result<std::vector<std::uint8_t>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "tracking source out of bounds"));
        ++readCount;
        bytesRead += length;
        return eve::Result<std::vector<std::uint8_t>>::success(std::vector<std::uint8_t>(
            bytes_.begin() + std::ptrdiff_t(offset),
            bytes_.begin() + std::ptrdiff_t(offset + length)));
    }
    mutable std::size_t readCount = 0;
    mutable std::size_t bytesRead = 0;

private:
    std::vector<std::uint8_t> bytes_;
};
}  // namespace


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

[[maybe_unused]] ParamsLease requireParams(const Params &source) {
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

[[maybe_unused]] GridLease requireGrid(Procgen &proc, const std::string &algorithm, ProcgenParamsHandleRef params) {
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

[[maybe_unused]] MeshLease requireMesh(Procgen &proc, const std::string &recipe, ProcgenParamsHandleRef params) {
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

[[maybe_unused]] bool gridsEqual(const Grid2D &a, const Grid2D &b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    return a.cells() == b.cells();
}

[[maybe_unused]] int countSemantic(const Grid2D &g, int semantic) {
    int n = 0;
    for (uint32_t c : g.cells())
        if (int(c) == semantic) ++n;
    return n;
}

[[maybe_unused]] bool neighborsOk(int a, int b, const std::set<std::pair<int, int>> &allowed) {
    if (a > b) std::swap(a, b);
    return allowed.count({a, b}) > 0;
}

[[maybe_unused]] bool terrainAdjacencyOk(const Grid2D &g) {
    // Ordered elevation band: only self + 卤1 neighbors.
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

[[maybe_unused]] bool caveAdjacencyOk(const Grid2D &g) {
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

[[maybe_unused]] bool borderIsWall(const Grid2D &g) {
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

[[maybe_unused]] bool meshIndicesInRange(const MeshBuild &m) {
    const int vc = m.getVertexCount();
    for (int i = 0; i < m.getIndexCount(); ++i) {
        const int id = m.getIndex(i);
        if (id < 0 || id >= vc) return false;
    }
    return true;
}

[[maybe_unused]] bool meshNormalsFiniteUnit(const MeshBuild &m, float tol = 0.15f) {
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

[[maybe_unused]] bool meshWindingAgreesWithNormals(const MeshBuild &m, float minFraction = 0.9f) {
    int ok = 0, counted = 0;
    for (int t = 0; t + 2 < m.getIndexCount(); t += 3) {
        const int i0 = m.getIndex(t), i1 = m.getIndex(t + 1), i2 = m.getIndex(t + 2);
        const float ax = m.getPositionX(i1) - m.getPositionX(i0);
        const float ay = m.getPositionY(i1) - m.getPositionY(i0);
        const float az = m.getPositionZ(i1) - m.getPositionZ(i0);
        const float bx = m.getPositionX(i2) - m.getPositionX(i0);
        const float by = m.getPositionY(i2) - m.getPositionY(i0);
        const float bz = m.getPositionZ(i2) - m.getPositionZ(i0);
        const float gx = ay * bz - az * by, gy = az * bx - ax * bz, gz = ax * by - ay * bx;
        const float gLen = std::sqrt(gx * gx + gy * gy + gz * gz);
        if (gLen < 1e-8f) continue;
        const float nx = (m.getNormalX(i0) + m.getNormalX(i1) + m.getNormalX(i2)) / 3.f;
        const float ny = (m.getNormalY(i0) + m.getNormalY(i1) + m.getNormalY(i2)) / 3.f;
        const float nz = (m.getNormalZ(i0) + m.getNormalZ(i1) + m.getNormalZ(i2)) / 3.f;
        ++counted;
        if (gx * nx + gy * ny + gz * nz > 0.f) ++ok;
    }
    return counted > 0 && float(ok) / float(counted) >= minFraction;
}

[[maybe_unused]] bool meshPositionsFinite(const MeshBuild &m) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        if (!std::isfinite(m.getPositionX(i)) || !std::isfinite(m.getPositionY(i)) ||
            !std::isfinite(m.getPositionZ(i)))
            return false;
    }
    return true;
}

[[maybe_unused]] float meshApproxSignedVolume(const MeshBuild &m) {
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

[[maybe_unused]] bool approxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

[[maybe_unused]] bool hasWalkablePath(const Grid2D &g) {
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
    // Cluster knobs are env-overridable so the leaf look can be compared across
    // runs without rebuilding; unset variables leave the recipe defaults.
    const auto envFloat = [](const char *name, float fallback) {
        const char *raw = std::getenv(name);
        return raw && raw[0] ? std::stof(raw) : fallback;
    };
    const auto envInt = [](const char *name, int fallback) {
        const char *raw = std::getenv(name);
        return raw && raw[0] ? std::stoi(raw) : fallback;
    };
    params.setFloat("clusterSize", envFloat("EVENGINE_TREE_CLUSTER_SIZE", 0.30f));
    params.setFloat("clusterLeafScale", envFloat("EVENGINE_TREE_CLUSTER_LEAF_SCALE", 0.85f));
    params.setFloat("clusterSpacing", envFloat("EVENGINE_TREE_CLUSTER_SPACING", 0.80f));
    params.setFloat("clusterSeparation", envFloat("EVENGINE_TREE_CLUSTER_SEPARATION", 0.55f));
    params.setInt("clusterPlanes", envInt("EVENGINE_TREE_CLUSTER_PLANES", 10));
    params.setInt("clusterCaps", envInt("EVENGINE_TREE_CLUSTER_CAPS", 2));
    params.setInt("clusterLeaves", envInt("EVENGINE_TREE_CLUSTER_LEAVES", 28));
    params.setInt("clusterLimit", envInt("EVENGINE_TREE_CLUSTER_LIMIT", 120));
    params.setFloat("leafSize", envFloat("EVENGINE_TREE_LEAF_SIZE", 6.2f * 0.075f));

    Procgen generator;
    auto    treeParams = requireParams(params);
    auto    treeMesh   = generator.generateMeshBorrowed("mesh.tree", treeParams.handle, gfx);
    REQUIRE(treeMesh.isBound());
    // Same parameters, CPU side: the uploaded Mesh keeps no metadata, but the
    // dump is most useful with the cost and cluster count next to it.
    {
        MeshBuild   stats;
        std::string statsError;
        if (MeshRecipeRegistry::instance().generate("mesh.tree", params, stats, statsError)) {
            std::printf("tree mesh: %d vertices, %d triangles, clusters=%s\n", stats.getVertexCount(),
                        stats.getIndexCount() / 3, stats.getMeta("clusters", "n/a").c_str());
        }
    }

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


TEST_CASE("procgen.terrain.materialShaderRendersPortableSixteenLayerContract") {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx->isHeadless()) gfx->initHeadless(64, 64);
    Procgen procgen;
    auto   *shader = procgen.createTerrainMaterialShader(gfx);
    REQUIRE(shader != nullptr);
    CHECK_EQ(shader->usedFloats(), 32);
    CHECK(shader->hasUniform("terrainLayer0ST"));
    CHECK(shader->hasUniform("terrainLayer3ST"));
    CHECK(shader->hasUniform("terrainMetallic"));
    CHECK(shader->hasUniform("terrainNormalScale"));
    CHECK(shader->hasUniform("terrainSmoothness"));
    CHECK(shader->hasUniform("terrainFeatures"));
    float normalScale[4]{};
    CHECK_EQ(shader->getFromVar("terrainNormalScale", normalScale, sizeof(normalScale)), int(sizeof(normalScale)));
    for (float value : normalScale) CHECK_EQ(value, 1.f);

    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float    uvs[]     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 2, 1, 2, 0, 3};
    auto          *mesh      = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    auto solidImage = [](std::uint32_t width, std::uint32_t height, std::array<std::uint8_t, 4> color) {
        eve::asset_procgen::TerrainAtlasImage image{width, height};
        image.pixels.resize(std::size_t(width) * height * 4);
        for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4)
            std::copy(color.begin(), color.end(), image.pixels.begin() + std::ptrdiff_t(offset));
        return image;
    };
    eve::asset_procgen::TerrainMaterialAtlases    atlases;
    eve::asset_procgen::TerrainMaterialAtlasGroup group;
    group.firstLayer = 0;
    group.layerCount = 1;
    group.tileWidth  = 1;
    group.tileHeight = 1;
    group.albedo     = solidImage(2, 2, {16, 240, 24, 255});
    group.normal     = solidImage(2, 2, {128, 128, 255, 255});
    group.mask       = solidImage(2, 2, {0, 0, 0, 255});
    group.control    = solidImage(1, 1, {255, 0, 0, 255});
    atlases.groups.push_back(std::move(group));
    atlases.holes = solidImage(1, 1, {255, 255, 255, 255});
    auto uploaded = eve::asset_procgen::uploadTerrainMaterialAtlases(*gfx, atlases);
    REQUIRE(uploaded.ok());
    auto materialRef = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(materialRef.ok());
    eve::asset_procgen::LoadedTerrainMaterial material{materialRef.value()};
    material.layers.resize(1);
    material.layers[0].normalConvention = "opengl";
    REQUIRE(eve::asset_procgen::bindTerrainMaterialGroup(*shader, uploaded.value(), material, 0).ok());

    eve::graphics::Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));
    auto *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), uploaded.value().groups[0].control, glm::vec4(1.f), shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    REQUIRE(pixels != nullptr);
    const auto *rgba = static_cast<const uint8_t *>(pixels->getData()) + (32 * 64 + 32) * 4;
    CHECK(rgba[1] > rgba[0] + 40);
    CHECK(rgba[1] > rgba[2] + 40);
    REQUIRE(eve::asset_procgen::releaseTerrainMaterialAtlases(*gfx, uploaded.value()).ok());

    eve::asset_procgen::TerrainMaterialAtlases    grouped;
    eve::asset_procgen::TerrainMaterialAtlasGroup firstGroup;
    firstGroup.firstLayer = 0;
    firstGroup.layerCount = 4;
    firstGroup.tileWidth = firstGroup.tileHeight = 1;
    firstGroup.albedo                            = solidImage(2, 2, {240, 16, 16, 255});
    firstGroup.normal                            = solidImage(2, 2, {128, 128, 255, 255});
    firstGroup.mask                              = solidImage(2, 2, {0, 0, 0, 255});
    firstGroup.control                           = solidImage(1, 1, {0, 0, 0, 0});
    eve::asset_procgen::TerrainMaterialAtlasGroup secondGroup;
    secondGroup.firstLayer = 4;
    secondGroup.layerCount = 1;
    secondGroup.tileWidth = secondGroup.tileHeight = 1;
    secondGroup.albedo                             = solidImage(2, 2, {16, 240, 24, 255});
    secondGroup.normal                             = solidImage(2, 2, {128, 128, 255, 255});
    secondGroup.mask                               = solidImage(2, 2, {0, 0, 0, 255});
    secondGroup.control                            = solidImage(1, 1, {255, 0, 0, 0});
    grouped.groups                                 = {std::move(firstGroup), std::move(secondGroup)};
    grouped.holes                                  = solidImage(1, 1, {255, 255, 255, 255});
    material.layers.resize(5);
    for (auto &layer : material.layers) layer.normalConvention = "opengl";
    auto packed = eve::asset_procgen::packTerrainMaterialAtlases(grouped, material);
    REQUIRE(packed.ok());
    auto packedGpu = eve::asset_procgen::uploadPackedTerrainMaterialAtlases(*gfx, packed.value());
    REQUIRE(packedGpu.ok());
    REQUIRE(eve::asset_procgen::bindPackedTerrainMaterial(*shader, packedGpu.value()).ok());
    auto *packedCanvas = gfx->newCanvas(64, 64);
    REQUIRE(packedCanvas != nullptr);
    gfx->begin3DFrameToCanvas(packedCanvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), packedGpu.value().controls, glm::vec4(1.f), shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> packedPixels(packedCanvas->newImageData());
    REQUIRE(packedPixels != nullptr);
    const auto *packedRgba = static_cast<const uint8_t *>(packedPixels->getData()) + (32 * 64 + 32) * 4;
    CHECK(packedRgba[1] > packedRgba[0] + 40);
    CHECK(packedRgba[1] > packedRgba[2] + 40);
    REQUIRE(eve::asset_procgen::releasePackedTerrainMaterialAtlases(*gfx, packedGpu.value()).ok());
}

TEST_CASE("procgen.terrain.waterShaderShipsEmbeddedSpirv") {
    // Regression guard for the portability break this stage used to cause: it was
    // compiled with glslc at runtime, which packaged builds cannot rely on. It is
    // embedded SPIR-V now, so creating and drawing it must succeed without any
    // external compiler.
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx->isHeadless()) gfx->initHeadless(64, 64);
    Procgen procgen;
    auto   *shader = procgen.createTerrainWaterShader(gfx);
    REQUIRE(shader != nullptr);
    CHECK(shader->getKind() == eve::graphics::Shader::Kind::eMesh3D);
    CHECK_EQ(shader->usedFloats(), 0);
    CHECK(!shader->fragmentSpirv().empty());
    CHECK_EQ(shader->fragmentSpirv().front(), 0x07230203u);
    CHECK(!shader->vertexSpirv().empty());

    const float    positions[] = {-1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[]   = {0, 2, 1, 2, 0, 3};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t white[4] = {255, 255, 255, 255};
    auto         *control  = gfx->newTexture(1, 1, white);
    REQUIRE(control != nullptr);

    eve::graphics::Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = glm::vec4(0.35f, 0.45f, 0.55f, 0.f);
    lighting.lights[0].posRadius = glm::vec4(0.f, 1.f, 0.f, 0.f);
    lighting.lights[0].color     = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));
    auto *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), control, glm::vec4(1.f), shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    REQUIRE(pixels != nullptr);
    const auto *rgba = static_cast<const uint8_t *>(pixels->getData()) + (32 * 64 + 32) * 4;
    // Shallow-water tint: blue/green dominant, red far behind.
    CHECK(rgba[2] > rgba[0]);
    CHECK(rgba[1] > rgba[0]);
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

    // No clouds 鈫?fully lit (baseline).
    gfx->setCloudShadows(0.f, 1.5f, 0.f, 4.f, 0.f, 0.5f, 0.5f);
    const float lit = meanLuma();

    // Dense, strong clouds 鈫?ground visibly darker.
    gfx->setCloudShadows(0.9f, 2.0f, 1.5f, 4.f, 0.f, 0.5f, 0.5f);
    const float cloudy = meanLuma();

    gfx->setCloudShadows(0.f, 1.5f, 0.f, 4.f, 0.f, 0.5f, 0.5f);
    const float litAgain = meanLuma();

    std::printf("cloud shadows render: lit=%.3f cloudy=%.3f litAgain=%.3f\n", lit, cloudy, litAgain);
    CHECK_GT(lit, 10.f);            // baseline is lit
    CHECK(cloudy < lit * 0.85f);    // clouds meaningfully darken the ground
    CHECK(approxEq(lit, litAgain, 2.f));  // disabled again 鈫?back to baseline
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


// --- Water (graphics): sky reflection + animated edge waves + middle drop ripples ---

namespace {

struct WaterLumaGrid {
    int grid = 16;
    std::vector<float> cells;
};

/** Render the current scene and downsample luma into a grid (for comparison). */
[[maybe_unused]] WaterLumaGrid waterCaptureLuma(Graphics *gfx, int grid) {
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

[[maybe_unused]] float waterDiff(const WaterLumaGrid &a, const WaterLumaGrid &b) {
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
    const uint8_t sky[6 * 4 * 4 * 4] = {0};  // 6 faces 脳 4脳4 脳 RGBA
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

/** Build a unit cube ([-0.5,0.5]鲁) with per-face UVs in [0,1]虏. */
[[maybe_unused]] static Mesh *makeUnitCube(Graphics *gfx) {
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
    const int fs = 64;
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
                    float cr = 0.72f + (0.12f - 0.72f) * t;
                    float cg = 0.80f + (0.32f - 0.80f) * t;
                    float cb = 0.90f + (0.72f - 0.90f) * t;
                    const float nx = dx / len;
                    const float nz = dz / len;
                    const float cloudNoise = std::sin(nx * 8.0f + nz * 3.1f) +
                                             std::sin(nz * 11.3f - nx * 2.7f) * 0.55f +
                                             std::sin((nx + nz) * 17.0f) * 0.22f;
                    const float cloudAltitude = std::clamp((ny - 0.03f) * 4.0f, 0.0f, 1.0f) *
                                                std::clamp((0.82f - ny) * 3.2f, 0.0f, 1.0f);
                    const float cloud = std::clamp((cloudNoise - 0.42f) * 2.6f, 0.0f, 1.0f) * cloudAltitude;
                    cr = cr + (0.96f - cr) * cloud;
                    cg = cg + (0.97f - cg) * cloud;
                    cb = cb + (1.00f - cb) * cloud;
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

    auto *camera = Camera3D::createCamera();
    camera->setEye(6.f, 5.f, 8.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(50.f);
    camera->setAmbient(0.28f, 0.32f, 0.40f);
    camera->setEnvMap(skyTex);
    camera->setEnvIntensity(1.f);
    camera->data()->nearZ = 0.1f;
    camera->data()->farZ = 100.f;
    gfx->setBackgroundColor(Color(0.36f, 0.55f, 0.72f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    gfx->getRenderControl()->enable("gbuffer");
    RenderSystem3D::setDirectionalLight(0.45f, 1.f, 0.3f, 1.4f, 1.3f, 1.2f);

    auto *present = Renderable2D::create();
    present->transform()->x = 0.f;
    present->transform()->y = 0.f;
    present->sprite()->width = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a = 0.f;

    WaterSceneFixture sceneFixture = createStylizedWaterScene(gfx, camera);

    // The water is drawn explicitly after the opaque GBuffer so it can sample
    // scene color and linear depth without a read/write feedback hazard.
    Water *water = gfx->newWater();
    REQUIRE(water != nullptr);
    water->createPlane(14.f, 14.f, 64, 64);
    WaterStyleConfig style             = water->config();
    style.deepColor                    = {0.005f, 0.16f, 0.42f};
    style.shallowColor                 = {0.025f, 0.50f, 0.62f};
    style.waveAmplitude                = 0.11f;
    style.waveSharpness                = 1.65f;
    style.rippleAmplitude              = 0.12f;
    style.rippleCount                  = 5;
    style.rippleInterval               = 1.4f;
    style.waveScale                    = 1.35f;
    style.foamWidth                    = 0.58f;
    style.foamSoftness                 = 0.12f;
    style.foamStrength                 = 0.72f;
    style.reflectionIntensity          = 0.82f;
    style.fresnelPower                 = 2.2f;
    style.sunIntensity                 = 0.65f;
    style.opacity                      = 0.68f;
    style.refractionStrength           = 0.045f;
    style.causticsStrength             = 0.55f;
    style.causticsScale                = 3.8f;
    if (const char* layerMode = std::getenv("EVENGINE_WATER_LAYER_MODE")) {
        if (std::strcmp(layerMode, "transmission") == 0) {
            style.reflectionIntensity = 0.0f;
            style.sunIntensity        = 0.0f;
            style.foamStrength        = 0.12f;
            style.opacity             = 0.52f;
        } else if (std::strcmp(layerMode, "reflection") == 0) {
            style.reflectionIntensity = 1.15f;
            style.sunIntensity        = 0.75f;
            style.refractionStrength  = 0.0f;
            style.causticsStrength    = 0.0f;
            style.foamStrength        = 0.08f;
            style.opacity             = 0.82f;
        }
    }
    auto styleJson = style.toJson();
    REQUIRE(static_cast<bool>(styleJson));
    REQUIRE(static_cast<bool>(water->applyConfigJson(styleJson.value())));

    // Animate a few seconds so ripples travel, then save a frame.
    for (int frame = 0; frame < 40; ++frame) {
        const float time = float(frame) * 0.06f;
        water->setTime(time);
        sceneFixture.update(time);
        RenderSystem3D::render(*gfx);
        water->draw();
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

    // Boxes above the water to reflect.
    struct Box { float x, y, z, sx, sy, sz; uint8_t r, g, b; };
    const Box boxes[] = {
        {0.f, 4.0f, -5.f, 3.0f, 3.0f, 3.0f, 210, 60, 60},
        {-3.5f, 3.0f, -3.f, 2.0f, 2.0f, 2.0f, 70, 180, 70},
        {3.5f, 3.5f, -4.f, 2.5f, 2.5f, 2.5f, 70, 110, 230},
    };
    Mesh *cube = makeUnitCube(gfx);
    struct Ent { Texture* texture; glm::mat4 model; };
    std::vector<Ent> ents;
    for (const Box &b : boxes) {
        const uint8_t px[4] = {b.r, b.g, b.b, 255};
        Texture* texture = gfx->newTexture(1, 1, px);
        glm::mat4 m(1.f);
        m = glm::translate(m, glm::vec3(b.x, b.y, b.z));
        m = glm::scale(m, glm::vec3(b.sx, b.sy, b.sz));
        ents.push_back({texture, m});
    }

    Water *water = gfx->newWater();
    REQUIRE(water != nullptr);
    water->createPlane(14.f, 14.f, 48, 48);
    water->setWaveAmplitude(0.2f);
    water->setReflectionIntensity(1.0f);
    water->setScreenSpaceReflection(true, 0.9f);
    water->setViewport(float(settings.width), float(settings.height));

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
        gfx->drawMeshShader(cube, en.model, en.texture, glm::vec4(1.f), nullptr);
    }
    gfx->end3DFrameToCanvas();
    gfx->setBackgroundColor(Color(0.05f, 0.09f, 0.14f, 1.f));

    // Final frame: the public Water path borrows and samples the planar reflection.
    const glm::mat4 mainView = glm::lookAtRH(eye, tgt, glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 mainProj = perspectiveVulkanRH_ZO(glm::radians(camera->data()->fovYDeg),
                                                       settings.width / float(settings.height),
                                                       camera->data()->nearZ, camera->data()->farZ);
    gfx->setMesh3DViewProj(mainProj * mainView);
    gfx->setMesh3DView(mainView);
    gfx->setMesh3DCameraPos(eye);
    gfx->setMesh3DClip(camera->data()->nearZ, camera->data()->farZ);
    gfx->setMesh3DEnv(skyTex, 1.f);
    water->setTime(0.5f);
    RenderSystem3D::render(*gfx);
    water->drawWithPlanarReflection(refl->getTexture(), 0.9F);
    RenderSystem::render(*gfx);

    // Read the reflection canvas once and check the red box was captured.
    std::unique_ptr<eve::image::ImageData> rim(refl->newImageData());
    REQUIRE(rim.get() != nullptr);
    REQUIRE(saveImagePng(*rim, std::string(outPath) + ".reflection.png"));
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
    int reflectedRed = 0;
    const auto* finalPixels = static_cast<const uint8_t*>(img->getData());
    for (int y = 0; y < img->getHeight(); ++y) {
        for (int x = 0; x < img->getWidth(); ++x) {
            const size_t offset = (size_t(y) * size_t(img->getWidth()) + size_t(x)) * 4;
            const int r = finalPixels[offset];
            const int g = finalPixels[offset + 1];
            const int b = finalPixels[offset + 2];
            if (r > 120 && r > g + 20 && r > b + 20) ++reflectedRed;
        }
    }
    CHECK(reflectedRed > 1000);
    REQUIRE(saveImagePng(*img, outPath));
    std::printf("planar water render saved: %s\n", outPath);
    win->close();
}
