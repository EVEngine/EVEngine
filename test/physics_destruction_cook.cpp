#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "asset/CanonicalMesh.h"
#include "physics/destruction/FractureRecipe.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/cook/GeometryCollectionCooker.h"
#include "schema/SchemaRegistry.h"

#include <cmath>

using eve::asset::CanonicalMeshData;
using eve::physics::FractureMode;
using eve::physics::FractureRecipe;
using eve::physics::GeometryCollectionAsset;
using eve::physics::destruction_cook::cookGeometryCollection;

namespace {

CanonicalMeshData unitCubeMesh() {
    CanonicalMeshData mesh;
    // 8 cube corners around the origin, edge length 2.
    const float p[] = {
        -1, -1, -1,  1, -1, -1,  1, 1, -1,  -1, 1, -1,
        -1, -1,  1,  1, -1,  1,  1, 1,  1,  -1, 1,  1,
    };
    mesh.positions.assign(p, p + 24);
    const std::uint32_t idx[] = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
        1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0,
    };
    mesh.indices.assign(idx, idx + 36);
    return mesh;
}

}  // namespace

TEST_CASE("physics_destruction_cook.recipeRoundTrip") {
    FractureRecipe recipe;
    recipe.mode = FractureMode::UniformVoronoi;
    recipe.siteCountMin = 3;
    recipe.siteCountMax = 5;
    recipe.seed = 42;
    auto encoded = recipe.toValue();
    REQUIRE(encoded.ok());
    auto decoded = FractureRecipe::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().mode == FractureMode::UniformVoronoi);
    REQUIRE_EQ(decoded.value().siteCountMin, 3);
    REQUIRE_EQ(decoded.value().siteCountMax, 5);
    REQUIRE_EQ(decoded.value().seed, 42u);
    REQUIRE(FractureRecipe::ensureSchemaRegistered().ok());
    REQUIRE(eve::schema::SchemaRegistry::resolve(std::string(FractureRecipe::SchemaId), 1) != nullptr);
}

TEST_CASE("physics_destruction_cook.uniformVoronoiIsBitExactForSeed") {
    FractureRecipe recipe;
    recipe.mode = FractureMode::UniformVoronoi;
    recipe.siteCountMin = 4;
    recipe.siteCountMax = 4;
    recipe.seed = 20260929;
    recipe.randomStreamName = "destruction.fracture";
    recipe.minimumThickness = 0.05f;
    auto a = cookGeometryCollection(unitCubeMesh(), recipe);
    auto b = cookGeometryCollection(unitCubeMesh(), recipe);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE_EQ(a.value().bones.size(), b.value().bones.size());
    REQUIRE_EQ(a.value().edges.size(), b.value().edges.size());
    for (std::size_t i = 0; i < a.value().bones.size(); ++i) {
        REQUIRE_EQ(a.value().bones[i].localX, b.value().bones[i].localX);
        REQUIRE_EQ(a.value().bones[i].localY, b.value().bones[i].localY);
        REQUIRE_EQ(a.value().bones[i].localZ, b.value().bones[i].localZ);
        REQUIRE_EQ(a.value().bones[i].halfExtentX, b.value().bones[i].halfExtentX);
    }
    for (std::size_t i = 0; i < a.value().edges.size(); ++i) {
        REQUIRE_EQ(a.value().edges[i].boneA, b.value().edges[i].boneA);
        REQUIRE_EQ(a.value().edges[i].boneB, b.value().edges[i].boneB);
        REQUIRE_EQ(a.value().edges[i].strainThreshold, b.value().edges[i].strainThreshold);
    }
    auto encoded = a.value().toValue();
    REQUIRE(encoded.ok());
}

TEST_CASE("physics_destruction_cook.seedChangeChangesTopology") {
    FractureRecipe recipe;
    recipe.mode = FractureMode::UniformVoronoi;
    recipe.siteCountMin = 5;
    recipe.siteCountMax = 5;
    recipe.seed = 1;
    auto a = cookGeometryCollection(unitCubeMesh(), recipe);
    recipe.seed = 2;
    auto b = cookGeometryCollection(unitCubeMesh(), recipe);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    bool differ = a.value().bones.size() != b.value().bones.size();
    if (!differ) {
        for (std::size_t i = 0; i < a.value().bones.size(); ++i) {
            if (a.value().bones[i].localX != b.value().bones[i].localX ||
                a.value().bones[i].localY != b.value().bones[i].localY ||
                a.value().bones[i].localZ != b.value().bones[i].localZ) {
                differ = true;
                break;
            }
        }
    }
    REQUIRE(differ);
}

TEST_CASE("physics_destruction_cook.planarAndRadialProduceBones") {
    FractureRecipe planar;
    planar.mode = FractureMode::Planar;
    planar.planeNormals = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    planar.planeOffsets = {0.f, 0.f};
    planar.minimumThickness = 0.05f;
    auto planarAsset = cookGeometryCollection(unitCubeMesh(), planar);
    REQUIRE(planarAsset.ok());
    REQUIRE(planarAsset.value().bones.size() >= 2u);

    FractureRecipe radial;
    radial.mode = FractureMode::Radial;
    radial.radialSpokes = 4;
    radial.radialPlanes = 1;
    radial.minimumThickness = 0.05f;
    auto radialAsset = cookGeometryCollection(unitCubeMesh(), radial);
    REQUIRE(radialAsset.ok());
    REQUIRE(radialAsset.value().bones.size() >= 2u);
}

TEST_CASE("physics_destruction_cook.clusteredVoronoiUsesDualThresholds") {
    FractureRecipe recipe;
    recipe.mode = FractureMode::ClusteredVoronoi;
    recipe.siteCountMin = 6;
    recipe.siteCountMax = 6;
    recipe.clusterCount = 2;
    recipe.clusterRadius = 0.4f;
    recipe.seed = 7;
    recipe.defaultStrainThreshold = 1.f;
    auto asset = cookGeometryCollection(unitCubeMesh(), recipe);
    REQUIRE(asset.ok());
    REQUIRE(asset.value().bones.size() == 6u);
    bool sawStrong = false;
    bool sawWeak = false;
    for (const auto& edge : asset.value().edges) {
        if (std::fabs(edge.strainThreshold - 2.f) < 1e-5f) sawStrong = true;
        if (std::fabs(edge.strainThreshold - 0.5f) < 1e-5f) sawWeak = true;
    }
    REQUIRE(sawStrong);
    REQUIRE(sawWeak);
    bool sawCluster0 = false;
    bool sawCluster1 = false;
    for (const auto& bone : asset.value().bones) {
        if (bone.clusterId == 0) sawCluster0 = true;
        if (bone.clusterId == 1) sawCluster1 = true;
        REQUIRE(bone.clusterId >= 0);
        REQUIRE(bone.clusterId < 2);
        REQUIRE_EQ(bone.fractureLevel, 0);
    }
    REQUIRE(sawCluster0);
    REQUIRE(sawCluster1);
}

TEST_CASE("physics_destruction_cook.rejectsThinMesh") {
    CanonicalMeshData flat;
    flat.positions = {-1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, 1};
    flat.indices = {0, 1, 2, 0, 2, 3};
    FractureRecipe recipe;
    recipe.mode = FractureMode::UniformVoronoi;
    recipe.siteCountMin = 2;
    recipe.siteCountMax = 2;
    recipe.minimumThickness = 0.2f;
    auto cooked = cookGeometryCollection(flat, recipe);
    REQUIRE(!cooked.ok());
}
