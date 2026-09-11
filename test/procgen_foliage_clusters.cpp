// Tests for the `mesh.tree` blue-noise leaf-cluster foliage system, kept in
// their own translation unit so the shared procgen test file does not grow.
//
// The first four cases are property tests over MeshBuild output (sampling
// quality, spherical normal transfer, two-sided emission, parameter wiring).
// The last one is an opt-in visual dump: set EVENGINE_TREE_STEPS_DIR to a
// directory to render one PNG per construction step of the foliage system,
// otherwise it is a no-op and costs nothing in a normal suite run.
//
//   step1_blue_noise_points.png     one plane, only the Poisson-disk centres
//   step2_leaves_on_plane.png       leaves drawn at those centres, face on
//   step3_only_leaves_flat.png      the same plane seen from an angle: a flat
//                                   card whose empty areas carry no geometry
//   step4_planes_around_y.png       clusters of 1 / 4 / 10 planes around Y
//   step5_spherical_normals.png     flat card normals vs. the cluster sphere
//   step6_tree.png                  random branches + clusters = a whole tree

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/algorithms/FoliageCluster.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "window/Window.h"

#include "RenderImageAudit.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace eve::procgen;
using namespace eve::graphics;

namespace {

bool meshIndicesInRange(const MeshBuild &m) {
    const int vertexCount = m.getVertexCount();
    for (int i = 0; i < m.getIndexCount(); ++i) {
        const int id = m.getIndex(i);
        if (id < 0 || id >= vertexCount) return false;
    }
    return true;
}

bool meshPositionsFinite(const MeshBuild &m) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        if (!std::isfinite(m.getPositionX(i)) || !std::isfinite(m.getPositionY(i)) || !std::isfinite(m.getPositionZ(i)))
            return false;
    }
    return true;
}

bool meshNormalsFiniteUnit(const MeshBuild &m, float tolerance = 0.15f) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        const float nx = m.getNormalX(i);
        const float ny = m.getNormalY(i);
        const float nz = m.getNormalZ(i);
        if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) return false;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (std::fabs(length - 1.f) > tolerance) return false;
    }
    return true;
}

/** @brief One plane, no jitter and no size variation: the sampler is measurable. */
FoliageClusterDesc singlePlaneDesc() {
    FoliageClusterDesc desc;
    desc.seed               = 4242;
    desc.radius             = 1.f;
    desc.leafSize           = 0.25f;
    desc.leafSpacing        = 1.f;
    desc.planes             = 1;
    desc.capPlanes          = 0;
    desc.leafJitter         = 0.f;
    desc.leafScaleVariation = 0.f;
    desc.maxLeavesPerPlane  = 256;
    return desc;
}

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

void addDot(MeshBuild &out, V3 c, float r) {
    const V3       axis[6] = {{r, 0, 0}, {-r, 0, 0}, {0, r, 0}, {0, -r, 0}, {0, 0, r}, {0, 0, -r}};
    const uint32_t base    = uint32_t(out.getVertexCount());
    for (int i = 0; i < 6; ++i) {
        const float length = std::sqrt(axis[i].x * axis[i].x + axis[i].y * axis[i].y + axis[i].z * axis[i].z);
        out.addVertex(c.x + axis[i].x, c.y + axis[i].y, c.z + axis[i].z, axis[i].x / length, axis[i].y / length,
                      axis[i].z / length, 0.75f, 0.5f);
    }
    const int faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    for (const auto &face : faces) {
        out.addTriangle(base + uint32_t(face[0]), base + uint32_t(face[1]), base + uint32_t(face[2]));
    }
}

/**
 * @brief Recover the sampled blue-noise centres from an emitted single-plane cluster.
 *
 * Every petal writes six consecutive vertices, so the mean of each group is the
 * sampled centre offset by a constant fraction of the leaf size along the
 * leaf's own long axis. That offset is small next to the blue-noise spacing and
 * irrelevant for a marker dot.
 */
std::vector<V3> recoverCentres(const MeshBuild &mesh) {
    std::vector<V3> centres;
    for (int i = 0; i + 5 < mesh.getVertexCount(); i += 6) {
        float x = 0.f, y = 0.f, z = 0.f;
        for (int v = 0; v < 6; ++v) {
            x += mesh.getPositionX(i + v);
            y += mesh.getPositionY(i + v);
            z += mesh.getPositionZ(i + v);
        }
        centres.push_back({x / 6.f, y / 6.f, z / 6.f});
    }
    return centres;
}

/** @brief One shared window/graphics pair for the whole stage walk. */
struct Stage {
    eve::window::Window *win    = nullptr;
    Graphics            *gfx    = nullptr;
    Texture             *atlas  = nullptr;
    Renderable3D        *object = nullptr;
    Camera3D            *camera = nullptr;
    std::string          directory;
};

bool openStage(Stage &stage, const std::string &directory) {
    stage.directory = directory;
    stage.win       = eve::window::Window::create();
    stage.gfx       = Graphics::create();
    if (!stage.win || !stage.gfx) return false;
    eve::window::WindowSettings settings;
    settings.width    = 900;
    settings.height   = 700;
    settings.centered = true;
    if (!stage.win->setWindowSettings(settings)) return false;

    // Left half of the atlas is bark, right half is foliage.
    const uint8_t atlasPixels[] = {
        111, 70, 42, 255, 128, 82, 47, 255, 64, 119, 57, 255, 82, 145, 67, 255,
    };
    stage.atlas = stage.gfx->newTexture(4, 1, atlasPixels);
    if (!stage.atlas) return false;

    stage.object = Renderable3D::create();
    stage.object->setTexture(stage.atlas);
    stage.object->setTint(1.f, 1.f, 1.f, 1.f);
    stage.object->setRoughness(0.88f);

    stage.camera = Camera3D::createCamera();
    stage.camera->setUp(0.f, 1.f, 0.f);
    stage.camera->setAmbient(0.30f, 0.34f, 0.28f);
    stage.camera->setActive(true);

    stage.gfx->setScreenReadbackEnabled(true);
    stage.gfx->setBackgroundColor(Color(0.075f, 0.105f, 0.095f, 1.f));
    RenderSystem3D::setDirectionalLight(-0.55f, -1.f, -0.35f, 1.45f, 1.32f, 1.08f);
    return true;
}

/** @brief Upload a CPU mesh, aim the camera, and write one stable frame. */
bool shoot(Stage &stage, const MeshBuild &mesh, float eyeX, float eyeY, float eyeZ, float targetX, float targetY,
           float targetZ, float fov, const std::string &file) {
    Procgen generator;
    auto    uploaded = generator.uploadMeshBorrowed(mesh, *stage.gfx);
    if (!uploaded.isBound()) return false;
    stage.object->setMesh(uploaded.get());
    stage.camera->setEye(eyeX, eyeY, eyeZ);
    stage.camera->setTarget(targetX, targetY, targetZ);
    stage.camera->setFov(fov);
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*stage.gfx);
        RenderSystem::render(*stage.gfx);
    }
    std::unique_ptr<eve::image::ImageData> image(stage.gfx->newImageData());
    if (!image) return false;
    if (!saveImagePng(*image, stage.directory + "/" + file)) return false;
    std::printf("step render saved: %s\n", (stage.directory + "/" + file).c_str());
    return true;
}

FoliageClusterDesc stagePlaneDesc() {
    FoliageClusterDesc desc;
    desc.seed               = 20260910;
    desc.radius             = 0.85f;
    desc.leafSize           = 0.24f;
    desc.leafSpacing        = 0.95f;
    desc.planes             = 1;
    desc.capPlanes          = 0;
    desc.leafJitter         = 0.f;
    desc.leafScaleVariation = 0.22f;
    desc.maxLeavesPerPlane  = 256;
    return desc;
}

}  // namespace

TEST_CASE("procgen.mesh.foliageCluster.blueNoiseAndSphereNormals") {
    const FoliageClusterDesc desc = singlePlaneDesc();

    MeshBuild first, second;
    const int leaves = addFoliageCluster(first, desc);
    CHECK(leaves > 0);
    // zeroerr re-evaluates CHECK_EQ operands when it formats a message, so
    // anything that appends to a mesh has to be hoisted into a variable first.
    const int repeated = addFoliageCluster(second, desc);
    CHECK_EQ(repeated, leaves);
    CHECK(first.positions() == second.positions());
    CHECK(first.normals() == second.normals());
    CHECK(first.indices() == second.indices());
    CHECK(meshIndicesInRange(first));
    CHECK(meshPositionsFinite(first));
    CHECK(meshNormalsFiniteUnit(first));

    // Six vertices per petal, four front triangles plus the four back-facing
    // ones the default two-sided emission adds.
    CHECK_EQ(first.getVertexCount(), leaves * 6);
    CHECK_EQ(first.getIndexCount(), leaves * 8 * 3);

    // Petal centroids sit 0.06 * leafSize off the sampled centre because the
    // lanceolate outline is not symmetric about its own mid-line.
    constexpr float    kCentroidOffset = 0.06f;
    const float        minDistance     = desc.leafSize * desc.leafSpacing;
    std::vector<float> cx, cy, cz;
    for (int leaf = 0; leaf < leaves; ++leaf) {
        float x = 0.f, y = 0.f, z = 0.f;
        for (int v = 0; v < 6; ++v) {
            const int i = leaf * 6 + v;
            x += first.getPositionX(i);
            y += first.getPositionY(i);
            z += first.getPositionZ(i);
        }
        cx.push_back(x / 6.f);
        cy.push_back(y / 6.f);
        cz.push_back(z / 6.f);
    }
    const float allowed = minDistance - 2.f * kCentroidOffset * desc.leafSize;
    float       closest = std::numeric_limits<float>::max();
    for (size_t i = 0; i < cx.size(); ++i) {
        for (size_t j = i + 1; j < cx.size(); ++j) {
            const float dx = cx[i] - cx[j], dy = cy[i] - cy[j], dz = cz[i] - cz[j];
            closest = std::min(closest, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    }
    // Blue noise guarantees a floor on the nearest-neighbour distance; an
    // uncorrelated draw of ~58 points in a unit disk does not.
    CHECK(closest >= allowed);
    // A sampler that bailed out early would leave the disk mostly empty. The
    // hexagonal-packing bound is 2A / (sqrt(3) d^2) centres; the disk is only
    // an inscribed region of the square the module sampler fills, and Bridson
    // reaches well under the ideal packing, so the floor sits near half of it.
    const float packingBound = 2.f * 3.14159265f * desc.radius * desc.radius / (1.7320508f * minDistance * minDistance);
    CHECK(float(leaves) >= packingBound * 0.4f);
    CHECK(float(leaves) <= packingBound * 1.05f);

    // Spherical normal transfer: away from the cluster centre every vertex
    // normal has to point outward, which is what makes the flat cards shade as
    // a rounded mass.
    const float roundingDistance = desc.radius * 0.5f;
    int         outward          = 0;
    for (int i = 0; i < first.getVertexCount(); ++i) {
        const float px = first.getPositionX(i), py = first.getPositionY(i), pz = first.getPositionZ(i);
        const float length = std::sqrt(px * px + py * py + pz * pz);
        if (length < roundingDistance) continue;
        const float alignment =
            (px * first.getNormalX(i) + py * first.getNormalY(i) + pz * first.getNormalZ(i)) / length;
        CHECK(alignment > 0.99f);
        ++outward;
    }
    CHECK(outward > 0);

    // Two-sided emission doubles the index stream without duplicating vertices.
    FoliageClusterDesc singleSided = desc;
    singleSided.doubleSided        = false;
    MeshBuild oneSided;
    const int singleCount = addFoliageCluster(oneSided, singleSided);
    CHECK_EQ(singleCount, leaves);
    CHECK_EQ(oneSided.getVertexCount(), first.getVertexCount());
    CHECK_EQ(oneSided.getIndexCount() * 2, first.getIndexCount());

    // Flat card normals (rounding off) must reproduce the plane normal instead.
    FoliageClusterDesc flat = desc;
    flat.normalRounding     = 0.f;
    MeshBuild faceted;
    const int facetedCount = addFoliageCluster(faceted, flat);
    CHECK_EQ(facetedCount, leaves);
    CHECK(faceted.normals() != first.normals());

    // More planes fill the same volume with more leaves.
    FoliageClusterDesc many = desc;
    many.planes             = 6;
    many.capPlanes          = 2;
    MeshBuild full;
    const int fullCount = addFoliageCluster(full, many);
    CHECK(fullCount > leaves);
}

TEST_CASE("procgen.mesh.tree.clusterFoliage") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(90210);
    p.setString("style", "lowpoly");
    p.setString("leafMode", "clusters");
    p.setString("branchAlgorithm", "spaceColonization");
    p.setFloat("leafDensity", 0.8f);
    p.setFloat("height", 6.2f);
    p.setFloat("crownRadius", 2.15f);

    MeshBuild   a, b;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, a, err));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, b, err));
    CHECK(a.getVertexCount() > 0);
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());
    CHECK(meshIndicesInRange(a));
    CHECK(meshPositionsFinite(a));
    CHECK(meshNormalsFiniteUnit(a));
    CHECK_EQ(a.getMeta("leafMode", ""), "clusters");
    CHECK(std::stoi(a.getMeta("clusters", "0")) > 0);

    // Bare skeleton for comparison.
    Params bare = p;
    bare.setString("leafMode", "none");
    MeshBuild skeleton;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", bare, skeleton, err));
    CHECK(skeleton.getVertexCount() < a.getVertexCount());

    // Density widens the blue-noise spacing, so the leaves get sparser.
    Params sparse = p;
    sparse.setFloat("leafDensity", 0.25f);
    MeshBuild sparseMesh;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", sparse, sparseMesh, err));
    CHECK(sparseMesh.getVertexCount() < a.getVertexCount());

    // The cluster budget caps the foliage cost.
    Params limited = p;
    limited.setInt("clusterLimit", 4);
    MeshBuild fewClusters;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", limited, fewClusters, err));
    CHECK(std::stoi(fewClusters.getMeta("clusters", "0")) <= 4);
    CHECK(fewClusters.getVertexCount() < a.getVertexCount());

    // Plane count must change the leaf budget monotonically.
    Params dense = p;
    dense.setInt("clusterPlanes", 14);
    dense.setInt("clusterCaps", 3);
    MeshBuild denseMesh;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", dense, denseMesh, err));
    CHECK(denseMesh.getVertexCount() > a.getVertexCount());
    CHECK(meshNormalsFiniteUnit(denseMesh));

    // Every vertex must sit inside the crown envelope. The floor is the trunk
    // radius, not zero: the first trunk ring is perpendicular to an already
    // bent trunk axis, so its lowest vertices dip slightly below the root.
    const float crownTop    = p.getFloat("height", 6.2f) + p.getFloat("crownRadius", 2.15f);
    const float trunkRadius = p.getFloat("height", 6.2f) * 0.055f;
    int         outside     = 0;
    for (int i = 0; i < a.getVertexCount(); ++i) {
        const float y = a.getPositionY(i);
        if (y < -trunkRadius || y > crownTop) ++outside;
    }
    CHECK_EQ(outside, 0);
}

TEST_CASE("procgen.mesh.tree.clusterFoliageWeberPenn") {
    // The cluster mode has to work over both skeleton algorithms.
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(5150);
    p.setString("leafMode", "clusters");
    p.setString("branchAlgorithm", "weberPenn");
    p.setFloat("leafDensity", 0.7f);
    p.setInt("branchLevels", 3);
    p.setInt("branchCount", 8);
    MeshBuild   mesh;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, mesh, err));
    CHECK(meshIndicesInRange(mesh));
    CHECK(meshPositionsFinite(mesh));
    CHECK(meshNormalsFiniteUnit(mesh));
    CHECK(std::stoi(mesh.getMeta("clusters", "0")) > 0);
    CHECK_EQ(mesh.getMeta("branchAlgorithm", ""), "weberPenn");
}

TEST_CASE("procgen.mesh.tree.clusterParamsAreExposedAndEffective") {
    MeshRecipeRegistry::instance().registerBuiltins();
    const RecipeDescriptor *recipe = MeshRecipeRegistry::instance().descriptor("mesh.tree");
    REQUIRE(recipe != nullptr);

    // Every knob the cluster generator reads must be reachable from the recipe
    // schema, otherwise it can never be set from Squirrel or the editor.
    const char *keys[] = {"clusterSize",   "clusterLeafScale", "clusterSpacing", "clusterSeparation", "clusterTilt",
                          "clusterPlanes", "clusterCaps",      "clusterLeaves",  "clusterLimit"};
    for (const char *key : keys) {
        const ParamDescriptor *param = recipe->find(key);
        REQUIRE(param != nullptr);
        CHECK_EQ(param->key, key);
        CHECK(param->advanced);
        CHECK(!param->displayName.empty());
    }
    // The schema is the source of truth for `applyMeshRecipeDefaults`, so its
    // defaults have to agree with the generator's own fallbacks.
    Params defaults;
    defaults.setSeed(1);
    recipe->applyDefaults(defaults);
    CHECK_EQ(defaults.getFloat("clusterSize", -1.f), 0.30f);
    CHECK_EQ(defaults.getFloat("clusterLeafScale", -1.f), 0.85f);
    CHECK_EQ(defaults.getFloat("clusterSpacing", -1.f), 0.80f);
    CHECK_EQ(defaults.getFloat("clusterSeparation", -1.f), 0.55f);
    CHECK_EQ(defaults.getFloat("clusterTilt", -1.f), 26.f);
    CHECK_EQ(defaults.getInt("clusterPlanes", -1), 10);
    CHECK_EQ(defaults.getInt("clusterCaps", -1), 2);
    CHECK_EQ(defaults.getInt("clusterLeaves", -1), 28);
    CHECK_EQ(defaults.getInt("clusterLimit", -1), 120);

    Params base;
    base.setSeed(4711);
    base.setString("style", "lowpoly");
    base.setString("leafMode", "clusters");
    base.setString("branchAlgorithm", "weberPenn");
    base.setFloat("leafDensity", 0.8f);
    base.setFloat("height", 6.2f);
    base.setFloat("crownRadius", 2.15f);
    base.setInt("branchLevels", 3);
    base.setInt("branchCount", 8);
    MeshBuild   reference;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", base, reference, err));
    const int referenceClusters = std::stoi(reference.getMeta("clusters", "0"));
    REQUIRE(referenceClusters > 0);

    // Each knob must actually move the geometry or the cluster count; a
    // parameter that is accepted but ignored is worse than a missing one.
    const auto differs = [&](const Params &variant) {
        MeshBuild mesh;
        if (!MeshRecipeRegistry::instance().generate("mesh.tree", variant, mesh, err)) return false;
        return mesh.positions() != reference.positions();
    };

    Params p = base;
    p.setFloat("clusterSize", 0.45f);
    CHECK(differs(p));
    p = base;
    p.setFloat("clusterLeafScale", 1.6f);
    CHECK(differs(p));
    p = base;
    p.setFloat("clusterSpacing", 0.35f);
    CHECK(differs(p));
    p = base;
    p.setFloat("clusterSeparation", 1.4f);
    CHECK(differs(p));
    p = base;
    p.setFloat("clusterTilt", 70.f);
    CHECK(differs(p));
    p = base;
    p.setInt("clusterPlanes", 3);
    CHECK(differs(p));
    p = base;
    p.setInt("clusterCaps", 6);
    CHECK(differs(p));

    // The two budget knobs are observable through the cluster count / cost.
    p = base;
    p.setInt("clusterLeaves", 4);
    MeshBuild cheap;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, cheap, err));
    CHECK(cheap.getVertexCount() < reference.getVertexCount());

    p = base;
    p.setInt("clusterLimit", 6);
    MeshBuild few;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, few, err));
    CHECK(std::stoi(few.getMeta("clusters", "0")) <= 6);
    CHECK(std::stoi(few.getMeta("clusters", "0")) < referenceClusters);

    // Out-of-range values are clamped rather than rejected, so a hostile or
    // stale preset can never blow the geometry budget.
    Params extreme = base;
    extreme.setInt("clusterPlanes", 9999);
    extreme.setInt("clusterCaps", 9999);
    extreme.setInt("clusterLeaves", 99999);
    extreme.setInt("clusterLimit", 99999);
    extreme.setFloat("clusterSize", -5.f);
    extreme.setFloat("clusterSpacing", 0.f);
    MeshBuild clamped;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", extreme, clamped, err));
    CHECK(meshIndicesInRange(clamped));
    CHECK(meshNormalsFiniteUnit(clamped));
    CHECK(meshPositionsFinite(clamped));
}

TEST_CASE("procgen.mesh.foliageCluster.stages") {
    const char *directory = std::getenv("EVENGINE_TREE_STEPS_DIR");
    if (!directory || !directory[0]) return;

    Stage stage;
    REQUIRE(openStage(stage, directory));

    // ---- step 1: the blue-noise points on the plane -------------------------
    const FoliageClusterDesc desc = stagePlaneDesc();
    MeshBuild                plane;
    const int                leaves = addFoliageCluster(plane, desc);
    REQUIRE(leaves > 0);
    const std::vector<V3> centres = recoverCentres(plane);

    MeshBuild dots;
    for (const V3 &c : centres) addDot(dots, c, desc.leafSize * 0.16f);
    CHECK(shoot(stage, dots, 0.f, 0.f, 3.4f, 0.f, 0.f, 0.f, 40.f, "step1_blue_noise_points.png"));

    // ---- step 2: random leaves drawn at those points ------------------------
    CHECK(shoot(stage, plane, 0.f, 0.f, 3.4f, 0.f, 0.f, 0.f, 40.f, "step2_leaves_on_plane.png"));

    // ---- step 3: only the leaves carry geometry ----------------------------
    // Seen from an angle the bundle is a flat card full of holes: the area
    // between leaves is not drawn at all, which is how "leaves opaque, rest
    // transparent" is expressed by a CPU mesh.
    CHECK(shoot(stage, plane, 2.9f, 0.35f, 1.9f, 0.f, 0.f, 0.f, 40.f, "step3_only_leaves_flat.png"));

    // ---- step 4: 1 / 4 / 10 planes fanned around the cluster's Y axis -------
    // Seen from high above, a vertical leaf plane is an edge-on line: one plane
    // is a single line, ten planes form the star that gives the bundle volume.
    MeshBuild fan;
    for (int variant = 0; variant < 3; ++variant) {
        const int          counts[3] = {1, 4, 10};
        FoliageClusterDesc one       = desc;
        one.planes                   = counts[variant];
        one.capPlanes                = counts[variant] > 1 ? 2 : 0;
        one.seed                     = desc.seed + std::uint32_t(variant);
        MeshBuild part;
        addFoliageCluster(part, one);
        CHECK(fan.appendTransformed(&part, float(variant) * 2.5f - 2.5f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f));
    }
    CHECK(shoot(stage, fan, 0.f, 5.4f, 4.6f, 0.f, 0.f, 0.f, 40.f, "step4_planes_around_y.png"));

    // ---- step 5: flat card normals vs. the transferred sphere normal --------
    MeshBuild          normals;
    FoliageClusterDesc flat = desc;
    flat.planes             = 10;
    flat.capPlanes          = 2;
    flat.normalRounding     = 0.f;
    MeshBuild faceted;
    addFoliageCluster(faceted, flat);
    CHECK(normals.appendTransformed(&faceted, -1.35f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f));
    FoliageClusterDesc round = flat;
    round.normalRounding     = 1.f;
    round.seed               = desc.seed + 991u;
    MeshBuild rounded;
    addFoliageCluster(rounded, round);
    CHECK(normals.appendTransformed(&rounded, 1.35f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f));
    CHECK(shoot(stage, normals, 0.f, 0.5f, 4.4f, 0.f, 0.f, 0.f, 40.f, "step5_spherical_normals.png"));

    // ---- step 6: random branches + clusters = a whole tree ------------------
    Params params;
    params.setSeed(31415);
    params.setString("style", "lowpoly");
    params.setString("leafMode", "clusters");
    params.setString("branchAlgorithm", "spaceColonization");
    params.setFloat("leafDensity", 0.82f);
    params.setFloat("height", 6.2f);
    params.setFloat("crownRadius", 2.15f);
    params.setInt("attractorCount", 120);
    params.setInt("colonizationIterations", 38);
    params.setFloat("growthStep", 0.25f);
    MeshBuild   tree;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", params, tree, error));
    stage.object->setRotation(-18.f, 0.f, 0.f);
    CHECK(shoot(stage, tree, 9.2f, 5.1f, 11.4f, 0.f, 3.1f, 0.f, 36.f, "step6_tree.png"));

    stage.win->close();
}
