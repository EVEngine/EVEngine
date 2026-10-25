// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "physics_destruction_cook.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#include "asset/CanonicalMesh.h"
#include "physics/destruction/FractureRecipe.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/cook/GeometryCollectionCooker.h"
#include "schema/SchemaRegistry.h"

#include <cmath>
#include <cstdint>

using eve::asset::CanonicalMeshData;
using eve::physics::FractureMode;
using eve::physics::FractureRecipe;
using eve::physics::GeometryCollectionAsset;
using eve::physics::destruction_cook::cookGeometryCollection;

namespace {

CanonicalMeshData cube() {
    CanonicalMeshData mesh;
    mesh.positions = {-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1};
    mesh.indices   = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                      1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0};
    return mesh;
}

FractureRecipe fixedSites(int count, std::uint64_t seed = 1) {
    FractureRecipe recipe;
    recipe.mode             = FractureMode::UniformVoronoi;
    recipe.siteCountMin     = count;
    recipe.siteCountMax     = count;
    recipe.seed             = seed;
    recipe.minimumThickness = 0.05f;
    return recipe;
}

}  // namespace

TEST_CASE("physics.destruction.cook recipe serializes a versioned deterministic definition") {
    auto input   = fixedSites(5, 42);
    auto encoded = input.toValue();
    REQUIRE(encoded.ok());
    auto decoded = FractureRecipe::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().mode == FractureMode::UniformVoronoi);
    CHECK_EQ(decoded.value().siteCountMin, 5);
    CHECK_EQ(decoded.value().siteCountMax, 5);
    CHECK_EQ(decoded.value().seed, std::uint64_t{42});
    CHECK_EQ(decoded.value().randomStreamName, input.randomStreamName);
    REQUIRE(FractureRecipe::ensureSchemaRegistered().ok());
    CHECK(eve::schema::SchemaRegistry::resolve(std::string(FractureRecipe::SchemaId), FractureRecipe::SchemaVersion) !=
          nullptr);
}

TEST_CASE("physics.destruction.cook fixed seed and stream give identical owning assets") {
    const auto input  = cube();
    const auto recipe = fixedSites(4, 20260929);
    auto       first  = cookGeometryCollection(input, recipe);
    auto       second = cookGeometryCollection(input, recipe);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().bones.size(), std::size_t{4});
    CHECK_EQ(first.value().bones.size(), second.value().bones.size());
    CHECK_EQ(first.value().edges.size(), second.value().edges.size());
    for (std::size_t i = 0; i < first.value().bones.size(); ++i) {
        const auto& a = first.value().bones[i];
        const auto& b = second.value().bones[i];
        CHECK_EQ(a.localX, b.localX);
        CHECK_EQ(a.localY, b.localY);
        CHECK_EQ(a.localZ, b.localZ);
        CHECK_EQ(a.halfExtentX, b.halfExtentX);
        CHECK_EQ(a.mass, b.mass);
        CHECK_EQ(a.fractureLevel, 0);
    }
    for (std::size_t i = 0; i < first.value().edges.size(); ++i) {
        CHECK_EQ(first.value().edges[i].boneA, second.value().edges[i].boneA);
        CHECK_EQ(first.value().edges[i].boneB, second.value().edges[i].boneB);
        CHECK_EQ(first.value().edges[i].strainThreshold, second.value().edges[i].strainThreshold);
    }
    REQUIRE(first.value().validate().ok());
    REQUIRE(first.value().toValue().ok());
}

TEST_CASE("physics.destruction.cook seed changes site placement while count stays fixed") {
    const auto input  = cube();
    auto       recipe = fixedSites(5, 1);
    auto       first  = cookGeometryCollection(input, recipe);
    recipe.seed       = 2;
    auto second       = cookGeometryCollection(input, recipe);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE_EQ(first.value().bones.size(), std::size_t{5});
    REQUIRE_EQ(second.value().bones.size(), std::size_t{5});
    bool changed = false;
    for (std::size_t i = 0; i < first.value().bones.size(); ++i) {
        const auto& a = first.value().bones[i];
        const auto& b = second.value().bones[i];
        if (a.localX != b.localX || a.localY != b.localY || a.localZ != b.localZ) changed = true;
    }
    CHECK(changed);
}

TEST_CASE("physics.destruction.cook planar and radial recipes both produce valid fragments") {
    FractureRecipe planar;
    planar.mode         = FractureMode::Planar;
    planar.planeNormals = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    planar.planeOffsets = {0.f, 0.f};
    auto planarAsset    = cookGeometryCollection(cube(), planar);
    REQUIRE(planarAsset.ok());
    CHECK(planarAsset.value().bones.size() >= 2u);
    CHECK(planarAsset.value().validate().ok());

    FractureRecipe radial;
    radial.mode         = FractureMode::Radial;
    radial.radialSpokes = 4;
    radial.radialPlanes = 1;
    auto radialAsset    = cookGeometryCollection(cube(), radial);
    REQUIRE(radialAsset.ok());
    CHECK(radialAsset.value().bones.size() >= 2u);
    CHECK(radialAsset.value().validate().ok());
}

TEST_CASE("physics.destruction.cook clustered recipe assigns groups and two bond strengths") {
    auto recipe                   = fixedSites(6, 7);
    recipe.mode                   = FractureMode::ClusteredVoronoi;
    recipe.clusterCount           = 2;
    recipe.clusterRadius          = 0.4f;
    recipe.defaultStrainThreshold = 1.f;
    auto asset                    = cookGeometryCollection(cube(), recipe);
    REQUIRE(asset.ok());
    REQUIRE_EQ(asset.value().bones.size(), std::size_t{6});
    bool group0 = false, group1 = false, strong = false, weak = false;
    for (const auto& bone : asset.value().bones) {
        group0 = group0 || bone.clusterId == 0;
        group1 = group1 || bone.clusterId == 1;
        CHECK(bone.clusterId >= 0);
        CHECK(bone.clusterId < 2);
    }
    for (const auto& edge : asset.value().edges) {
        strong = strong || std::fabs(edge.strainThreshold - 2.f) < 1e-5f;
        weak   = weak || std::fabs(edge.strainThreshold - 0.5f) < 1e-5f;
    }
    CHECK(group0);
    CHECK(group1);
    CHECK(strong);
    CHECK(weak);
}

TEST_CASE("physics.destruction.cook rejects thin meshes and excessive output budgets") {
    CanonicalMeshData flat;
    flat.positions          = {-1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, 1};
    flat.indices            = {0, 1, 2, 0, 2, 3};
    auto recipe             = fixedSites(2);
    recipe.minimumThickness = 0.2f;
    CHECK(!cookGeometryCollection(flat, recipe).ok());
    recipe              = fixedSites(5);
    recipe.maximumBones = 4;
    REQUIRE(!cookGeometryCollection(cube(), recipe).ok());
}
