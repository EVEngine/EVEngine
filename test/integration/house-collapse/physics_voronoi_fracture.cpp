#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "asset/CanonicalMesh.h"
#include "physics/World3D.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/FractureGeometry.h"
#include "physics/destruction/FractureRecipe.h"
#include "physics/destruction/cook/AssemblyFractureSettings.h"
#include "physics/destruction/cook/VoronoiCook.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
using namespace eve::physics::destruction_cook;
namespace {
eve::asset::CanonicalMeshData cube() {
    eve::asset::CanonicalMeshData m;
    m.positions = {-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1};
    m.indices   = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                   3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    return m;
}
using V = std::array<double, 3>;
V      sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V      cross(V a, V b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V      point(const FractureFragment& f, std::uint32_t i) {
    return {f.positions[3 * i], f.positions[3 * i + 1], f.positions[3 * i + 2]};
}
double volume(const FractureFragment& f) {
    double v = 0;
    for (std::size_t t = 0; t < f.indices.size(); t += 3)
        v += dot(point(f, f.indices[t]), cross(point(f, f.indices[t + 1]), point(f, f.indices[t + 2]))) / 6;
    return v;
}
bool contains(const FractureFragment& f, const GeometryCollectionBone& bone, V p) {
    p = sub(p, {bone.localX, bone.localY, bone.localZ});
    for (std::size_t t = 0; t < f.indices.size(); t += 3) {
        const auto a = point(f, f.indices[t]);
        const auto n = cross(sub(point(f, f.indices[t + 1]), a), sub(point(f, f.indices[t + 2]), a));
        if (dot(n, sub(p, a)) > 1e-9) return false;
    }
    return true;
}
}  // namespace
TEST_CASE("physics_voronoi.closedVolumePartitionAndDeterminism") {
    FractureRecipe r;
    r.siteCountMin = 4;
    r.siteCountMax = 4;
    r.seed         = 42;
    auto cooked    = cookVoronoi(cube(), r).expect("Voronoi cube");
    auto again     = cookVoronoi(cube(), r).expect("repeat");
    REQUIRE(cooked.geometry.validate().ok());
    REQUIRE(cooked.collection.bones.size() == 4);
    double sum      = 0;
    int    interior = 0;
    for (std::size_t i = 0; i < cooked.geometry.fragments.size(); ++i) {
        const auto& f = cooked.geometry.fragments[i];
        sum += volume(f);
        REQUIRE(f.positions == again.geometry.fragments[i].positions);
        REQUIRE(f.indices == again.geometry.fragments[i].indices);
        for (auto tag : f.materials) interior += tag == 1 ? 1 : 0;
    }
    REQUIRE(std::abs(sum - 8) < 1e-4);
    REQUIRE(interior > 0);
    REQUIRE(!cooked.collection.edges.empty());
    int badSamples = 0;
    for (int x = 0; x < 9; ++x)
        for (int y = 0; y < 9; ++y)
            for (int z = 0; z < 9; ++z) {
                V   p{-0.97 + x * .223, -0.93 + y * .219, -0.91 + z * .217};
                int hits = 0;
                for (std::size_t i = 0; i < cooked.geometry.fragments.size(); ++i)
                    if (contains(cooked.geometry.fragments[i], cooked.collection.bones[i], p)) ++hits;
                if (hits != 1) ++badSamples;
            }
    REQUIRE(badSamples == 0);
    r.seed         = 43;
    auto different = cookVoronoi(cube(), r).expect("different seed");
    REQUIRE(cooked.geometry.fragments[0].positions != different.geometry.fragments[0].positions);
}
TEST_CASE("physics_voronoi.convexTetrahedronAndClusteredSites") {
    eve::asset::CanonicalMeshData mesh;
    mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    mesh.indices   = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    FractureRecipe r;
    r.siteCountMin  = 3;
    r.siteCountMax  = 3;
    r.mode          = FractureMode::ClusteredVoronoi;
    r.clusterRadius = .2f;
    r.seed          = 7;
    auto   cooked   = cookVoronoi(mesh, r).expect("tetrahedron");
    double sum      = 0;
    for (const auto& f : cooked.geometry.fragments) sum += volume(f);
    REQUIRE(std::abs(sum - 1.0 / 6) < 1e-5);
    REQUIRE(cooked.geometry.validate().ok());
    r.mode         = FractureMode::UniformVoronoi;
    r.siteCountMin = 1;
    r.siteCountMax = 1;
    auto one       = cookVoronoi(cube(), r).expect("single cell");
    REQUIRE(one.collection.edges.empty());
    REQUIRE(std::abs(volume(one.geometry.fragments[0]) - 8) < 1e-5);
}
TEST_CASE("physics_voronoi.invalidInputAndMeshBudgets") {
    FractureRecipe r;
    auto           open = cube();
    open.indices.resize(open.indices.size() - 3);
    REQUIRE(!cookVoronoi(open, r).ok());
    auto concave          = cube();
    concave.positions[18] = 0;
    concave.positions[19] = 0;
    concave.positions[20] = 0;
    REQUIRE(!cookVoronoi(concave, r).ok());
    r.siteCountMax = 65;
    REQUIRE(!cookVoronoi(cube(), r).ok());
    auto f                               = cookVoronoi(cube(), FractureRecipe{}).expect("fixture");
    f.geometry.fragments[0].materials[0] = 2;
    REQUIRE(!f.geometry.validate().ok());
}
TEST_CASE("physics_voronoi.meshBakeSurvivesWorldAndDisk") {
    FractureRecipe r;
    r.siteCountMin           = 3;
    r.siteCountMax           = 3;
    auto              cooked = cookVoronoi(cube(), r).expect("cook");
    CollapseAnimation clip;
    {
        World3D world(0.f, -9.8f, 0.f, true);
        auto instance = GeometryCollectionInstance::createFractured(world, cooked.collection, cooked.geometry, 0, 4, 0)
                            .expect("convex instance");
        REQUIRE(clip.capture(*instance, 0).ok());
        DestructionField field;
        field.radius    = 100;
        field.magnitude = 100;
        REQUIRE(instance->applyField(field).ok());
        auto dt = eve::Duration::fromSeconds(1.0 / 60).expect("dt");
        REQUIRE(instance->step({eve::SimulationTick{1}, dt}).ok());
        for (int i = 0; i < 30; ++i) world.update(1.f / 60);
        REQUIRE(clip.capture(*instance, .5f).ok());
    }
    REQUIRE(clip.fragmentGeometry() != nullptr);
    const auto path = std::filesystem::temp_directory_path() / "eve-voronoi-bake.evecollapse";
    std::filesystem::remove(path);
    REQUIRE(clip.save(path.string()).ok());
    auto restored = CollapseAnimation::load(path.string()).expect("reload meshes");
    REQUIRE(restored.fragmentGeometry()->fragments.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE(restored.fragmentGeometry()->fragments[i].positions == cooked.geometry.fragments[i].positions);
        REQUIRE(restored.fragmentGeometry()->fragments[i].indices == cooked.geometry.fragments[i].indices);
        REQUIRE(restored.fragmentGeometry()->fragments[i].materials == cooked.geometry.fragments[i].materials);
    }
    REQUIRE(restored.sample(.5f, 0).expect("pose").worldY < restored.sample(0, 0).expect("initial").worldY - .1f);
    // Unknown version rejects before a candidate is published.
    {
        std::fstream out(path, std::ios::in | std::ios::out | std::ios::binary);
        out.seekp(8);
        out.put(99);
    }
    REQUIRE(!CollapseAnimation::load(path.string()).ok());
    std::filesystem::remove(path);
}
TEST_CASE("physics_voronoi.authoredAssemblyKeepsSupports") {
    auto source                     = GeometryCollectionAsset::makeWeldedBoxesFixture().expect("assembly");
    source.bones[0].anchoredDefault = true;
    FractureRecipe r;
    r.siteCountMin = 3;
    r.siteCountMax = 3;
    auto cooked    = cookVoronoiAssembly(source, r).expect("assembly cook");
    REQUIRE(cooked.collection.bones.size() == 6);
    for (int i = 0; i < 3; ++i) REQUIRE(cooked.collection.bones[static_cast<std::size_t>(i)].anchoredDefault);
    for (int i = 3; i < 6; ++i) REQUIRE(!cooked.collection.bones[static_cast<std::size_t>(i)].anchoredDefault);
    bool crossSupport = false;
    for (auto edge : cooked.collection.edges)
        if (edge.boneA < 3 && edge.boneB >= 3) crossSupport = true;
    REQUIRE(crossSupport);
    r.maximumBones = 2;
    REQUIRE(!cookVoronoiAssembly(source, r).ok());
}
TEST_CASE("physics_voronoi.legacyBoxBakeVersionOneMigration") {
    CollapseAnimation clip;
    {
        World3D world(0.f, 0.f, 0.f, true);
        auto    asset    = GeometryCollectionAsset::makeWeldedBoxesFixture().expect("boxes");
        auto    instance = GeometryCollectionInstance::create(world, asset, 0, 0, 0).expect("instance");
        REQUIRE(clip.capture(*instance, 0).ok());
    }
    const auto path = std::filesystem::temp_directory_path() / "eve-voronoi-v1.evecollapse";
    std::filesystem::remove(path);
    REQUIRE(clip.save(path.string()).ok());
    std::ifstream     in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    in.close();
    bytes[8] = 1;
    bytes.erase(bytes.begin() + 20, bytes.begin() + 24);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    auto old = CollapseAnimation::load(path.string()).expect("version 1 migration");
    REQUIRE(old.boneCount() == 2);
    REQUIRE(!old.fragmentGeometry());
    std::filesystem::remove(path);
}

TEST_CASE("physics_voronoi.houseSeedsRemainClosed") {
    auto mesh = cube();
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) mesh.positions[i] *= (i % 3 == 1 ? .38f : .48f);
    for (std::uint64_t i = 0; i < 93; ++i) {
        FractureRecipe r;
        r.seed      = 42 ^ (i * 0x9e3779b97f4a7c15ULL);
        auto result = cookVoronoi(mesh, r);
        if (!result.ok())
            throw std::runtime_error("house seed " + std::to_string(i) + ": " + result.status().describe());
    }
}

TEST_CASE("physics_voronoi.failedAdmissionDoesNotLeakBodies") {
    auto    cooked = cookVoronoi(cube(), FractureRecipe{}).expect("cook");
    World3D world(0.f, 0.f, 0.f, true);
    auto    bad                   = cooked.geometry;
    bad.fragments[0].materials[0] = 99;
    REQUIRE(!GeometryCollectionInstance::createFractured(world, cooked.collection, bad, 0, 0, 0).ok());
    REQUIRE(world.getBodyCount() == 0);
    REQUIRE(world.getShapeCount() == 0);
    auto bounds                 = cooked.collection;
    bounds.bones[0].halfExtentX = .0001f;
    REQUIRE(!GeometryCollectionInstance::createFractured(world, bounds, cooked.geometry, 0, 0, 0).ok());
    REQUIRE(world.getBodyCount() == 0);
    auto instance = GeometryCollectionInstance::createFractured(world, cooked.collection, cooked.geometry, 0, 0, 0)
                        .expect("instance");
    auto snapshot = instance->captureSnapshot().expect("snapshot");
    snapshot.bones[0].halfExtentX *= 2;
    REQUIRE(!instance->restoreSnapshot(snapshot).ok());
    REQUIRE(instance->bonePresentation(0).expect("pose").halfExtentX == cooked.collection.bones[0].halfExtentX);
    instance.reset();
    REQUIRE(world.getBodyCount() == 0);
    REQUIRE(world.getShapeCount() == 0);
}

TEST_CASE("physics_voronoi.perStructurePoliciesAndComponentOverride") {
    auto source               = GeometryCollectionAsset::makeWeldedBoxesFixture().expect("source");
    source.bones[0].clusterId = 1;
    source.bones[1].clusterId = 3;
    AssemblyFractureSettings settings;
    settings.contactTolerance = .15f;
    REQUIRE(settings.setGroupPolicy(1, {1, 24.f, 10.f}).ok());
    REQUIRE(settings.setGroupPolicy(3, {6, 1.2f, 1.f}).ok());
    auto cooked = cookVoronoiAssembly(source, settings).expect("policies");
    REQUIRE(cooked.collection.bones.size() == 7);
    int internal = 0;
    for (auto e : cooked.collection.edges)
        if (e.boneA > 0) {
            REQUIRE(std::abs(e.strainThreshold - 1.2f) < 1e-6f);
            ++internal;
        }
    REQUIRE(internal > 0);
    REQUIRE(settings.setComponentPolicy(1, {2, 14.f, 7.f}).ok());
    auto overridden = cookVoronoiAssembly(source, settings).expect("override");
    REQUIRE(overridden.collection.bones.size() == 3);
    REQUIRE(!settings.setGroupPolicy(1, {0, 1, 1}).ok());
    REQUIRE(settings.groups.at(1).fragments == 1);
    settings.contactTolerance = -1;
    REQUIRE(!cookVoronoiAssembly(source, settings).ok());
}
TEST_CASE("physics_voronoi.survivingBondsKeepDetachedChunkTogether") {
    GeometryCollectionAsset source;
    for (int i = 0; i < 3; ++i) {
        GeometryCollectionBone b;
        b.localX          = 1.1f * static_cast<float>(i);
        b.localY          = 4;
        b.anchoredDefault = i == 0;
        source.bones.push_back(b);
    }
    source.edges = {{0, 1, 1}, {1, 2, 100}};
    AssemblyFractureSettings settings;
    settings.recipe.siteCountMin = settings.recipe.siteCountMax = 1;
    settings.contactTolerance                                   = .15f;
    auto    cooked = cookVoronoiAssembly(source, settings).expect("chunk cook");
    World3D world(0.f, -9.8f, 0.f, true);
    auto    instance = GeometryCollectionInstance::createFractured(world, cooked.collection, cooked.geometry, 0, 0, 0)
                        .expect("welded chunk");
    REQUIRE(world.getJointCount() == 2);
    auto             intact = instance->captureSnapshot().expect("intact");
    DestructionField field;
    field.centerX   = .55f;
    field.centerY   = 4;
    field.radius    = .2f;
    field.magnitude = 2;
    REQUIRE(instance->applyField(field).ok());
    auto dt = eve::Duration::fromSeconds(1.0 / 60).expect("dt");
    REQUIRE(instance->step({eve::SimulationTick{1}, dt}).ok());
    REQUIRE(world.getJointCount() == 1);
    REQUIRE(instance->boneState(1) == BoneRuntimeState::Detached);
    field.kind      = DestructionFieldKind::Impulse;
    field.centerX   = 2.2f;
    field.magnitude = 3;
    field.dirY      = 0;
    field.dirZ      = 1;
    REQUIRE(instance->applyField(field).ok());
    for (int i = 0; i < 60; ++i) world.update(1.f / 60);
    auto        a = instance->bonePresentation(1).expect("a"), b = instance->bonePresentation(2).expect("b");
    const float dx = a.worldX - b.worldX, dy = a.worldY - b.worldY, dz = a.worldZ - b.worldZ;
    REQUIRE(std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) - 1.1f) < .04f);
    REQUIRE(a.worldY < 3.f);
    REQUIRE(!instance->isEdgeBroken(1));
    REQUIRE(instance->restoreSnapshot(intact).ok());
    REQUIRE(world.getJointCount() == 2);
    instance.reset();
    REQUIRE(world.getBodyCount() == 0);
    REQUIRE(world.getShapeCount() == 0);
    REQUIRE(world.getJointCount() == 0);
}

TEST_CASE("physics_voronoi.weldedWorldFirstTeardown") {
    auto cooked   = cookVoronoi(cube(), FractureRecipe{}).expect("cook");
    auto world    = std::make_unique<World3D>(0.f, 0.f, 0.f, true);
    auto instance = GeometryCollectionInstance::createFractured(*world, cooked.collection, cooked.geometry, 0, 0, 0)
                        .expect("instance");
    REQUIRE(world->getJointCount() > 0);
    world.reset();
    REQUIRE(!instance->hasLiveWorld());
    instance->releaseBodies();
    instance.reset();
}
