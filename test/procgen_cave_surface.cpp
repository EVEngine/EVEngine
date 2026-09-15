#include "CaveFixtures.h"

TEST_CASE("procgen.mesh.cave.isosurfaceSupersamplingImprovesDeterministicTessellation") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(250609579);
    params.setString("style", "mixed");
    params.setInt("resolution", 30);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setInt("surfaceRefinement", 0);
    params.setInt("isosurfaceSampling", 1);
    MeshBuild   native, supersampled, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, native, error));

    params.setInt("isosurfaceSampling", 2);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, supersampled, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    CHECK(supersampled.getVertexCount() > native.getVertexCount() * 2);
    CHECK(supersampled.positions() == repeated.positions());
    CHECK(supersampled.indices() == repeated.indices());
    CHECK_EQ(supersampled.getMeta("isosurfaceReconstruction", ""), std::string("trilinear-supersample-v1"));
    CHECK_EQ(supersampled.getMeta("extractionResolution", ""), std::string("59x35x59"));
    CHECK_EQ(supersampled.getMeta("isosurfaceSampling", ""), std::string("2"));
}

TEST_CASE("procgen.mesh.cave.densityGradientTracksContinuousField") {
    constexpr int      n = 5;
    std::vector<float> density(size_t(n * n * n));
    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const float px                         = float(x) / float(n - 1) * 2.f - 1.f;
                const float py                         = float(y) / float(n - 1) * 2.f - 1.f;
                const float pz                         = float(z) / float(n - 1) * 2.f - 1.f;
                density[size_t(x + y * n + z * n * n)] = 2.f * px - 3.f * py + 0.5f * pz;
            }
        }
    }
    const CaveFieldPoint gradient = sampleCaveDensityGradient(density, n, n, n, {0.13f, -0.27f, 0.31f});
    CHECK(std::fabs(gradient.x - 2.f) < 1e-4f);
    CHECK(std::fabs(gradient.y + 3.f) < 1e-4f);
    CHECK(std::fabs(gradient.z - 0.5f) < 1e-4f);
}

TEST_CASE("procgen.mesh.cave.dripstonesUseDistinctStableTriangleGroup") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20250317);
    params.setString("style", "cavern");
    params.setInt("resolution", 40);
    params.setInt("chambers", 6);
    params.setInt("dripstones", 20);
    params.setFloat("dripstoneScale", 1.05f);
    params.setString("stalagmiteShape", "mixed");
    MeshBuild   formations, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, formations, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE_EQ(formations.getGroupCount(), 5);
    REQUIRE_EQ(formations.getGroupName(3), std::string("breakdown"));
    REQUIRE_EQ(formations.getGroupName(4), std::string("sediment"));
    REQUIRE_EQ(formations.getGroupName(0), std::string("caveWalls"));
    REQUIRE_EQ(formations.getGroupName(1), std::string("speleothems"));
    REQUIRE_EQ(formations.getGroupName(2), std::string("wetWalls"));
    int wallTriangles       = 0;
    int speleothemTriangles = 0;
    int wetTriangles        = 0;
    for (int triangle = 0; triangle < formations.getIndexCount() / 3; ++triangle) {
        if (formations.getTriangleGroup(triangle) == 0) ++wallTriangles;
        if (formations.getTriangleGroup(triangle) == 1) ++speleothemTriangles;
        if (formations.getTriangleGroup(triangle) == 2) ++wetTriangles;
    }
    REQUIRE(wallTriangles > 100);
    REQUIRE(speleothemTriangles > 20);
    REQUIRE(wetTriangles > 20);
    REQUIRE(formations.positions() == repeated.positions());
    REQUIRE(formations.triangleGroups() == repeated.triangleGroups());
    REQUIRE_EQ(formations.getMeta("depositionModel", ""), std::string("damkohler-thin-film-ripple-v2"));
}

TEST_CASE("procgen.mesh.cave.normalSmoothingChangesShadingNotTopology") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(9981);
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setFloat("normalSmoothing", 0.f);
    MeshBuild   faceted, smooth, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, faceted, error));
    params.setFloat("normalSmoothing", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, smooth, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(faceted.positions() == smooth.positions());
    REQUIRE(faceted.indices() == smooth.indices());
    REQUIRE(faceted.normals() != smooth.normals());
    REQUIRE(smooth.normals() == repeated.normals());
    REQUIRE_EQ(smooth.getMeta("wetnessModel", ""), std::string("drainage-proximity-v1"));
    for (int vertex = 0; vertex < smooth.getVertexCount(); ++vertex) {
        const float nx = smooth.getNormalX(vertex);
        const float ny = smooth.getNormalY(vertex);
        const float nz = smooth.getNormalZ(vertex);
        REQUIRE(std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.f) < 1e-3f);
    }
}

TEST_CASE("procgen.mesh.cave.densityGradientNormalsAreDeterministicAndPreserveTopology") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(9981);
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setFloat("normalSmoothing", 1.f);
    MeshBuild   faceAverage, gradient, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, faceAverage, error));
    params.setString("surfaceNormalMode", "densityGradient");
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, gradient, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(faceAverage.positions() == gradient.positions());
    REQUIRE(faceAverage.indices() == gradient.indices());
    REQUIRE(faceAverage.normals() != gradient.normals());
    REQUIRE(gradient.normals() == repeated.normals());
    REQUIRE_EQ(gradient.getMeta("surfaceNormalMode", ""), std::string("densityGradient"));
    for (int vertex = 0; vertex < gradient.getVertexCount(); ++vertex) {
        const float nx = gradient.getNormalX(vertex);
        const float ny = gradient.getNormalY(vertex);
        const float nz = gradient.getNormalZ(vertex);
        REQUIRE(std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.f) < 1e-3f);
    }
}

TEST_CASE("procgen.mesh.cave.wetnessContourRefinesOnlyMaterialBoundaryDeterministically") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setInt("wetnessRefinement", 0);
    MeshBuild   coarse, refined, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, coarse, error));
    params.setInt("wetnessRefinement", 1);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, refined, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(refined.getIndexCount() > coarse.getIndexCount());
    REQUIRE(refined.positions() == repeated.positions());
    REQUIRE(refined.indices() == repeated.indices());
    REQUIRE(refined.triangleGroups() == repeated.triangleGroups());
    REQUIRE_EQ(refined.getMeta("wetnessModel", ""), std::string("gravity-drainage-contour-v2"));
    REQUIRE(std::stoi(refined.getMeta("wetnessBoundaryTriangles", "0")) > 0);
    REQUIRE(std::stoi(refined.getMeta("wetnessAddedTriangles", "0")) > 0);
    int dryTriangles = 0, wetTriangles = 0;
    for (int triangle = 0; triangle < refined.getIndexCount() / 3; ++triangle) {
        if (refined.getTriangleGroup(triangle) == 0) ++dryTriangles;
        if (refined.getTriangleGroup(triangle) == 2) ++wetTriangles;
    }
    REQUIRE(dryTriangles > 0);
    REQUIRE(wetTriangles > 0);
}

TEST_CASE("procgen.mesh.cave.boundaryClosureBuildsRoughHostRockEnvelope") {
    constexpr int             n = 9;
    std::vector<float>        density(size_t(n * n * n), -0.5f);
    const CaveBoundaryClosure closure = closeCaveDensityBoundary(density, n, n, n, 1.f, 20260830u);
    REQUIRE(closure.airSamplesBefore > 0);
    REQUIRE_EQ(closure.airSamplesAfter, 0);
    REQUIRE(closure.changedVoxels > 0);
    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                if (x == 0 || y == 0 || z == 0 || x == n - 1 || y == n - 1 || z == n - 1)
                    REQUIRE(density[size_t(x + y * n + z * n * n)] >= 0.f);
            }
        }
    }
    REQUIRE_EQ(density[size_t(4 + 4 * n + 4 * n * n)], -0.5f);
}

TEST_CASE("procgen.mesh.cave.sealedDomainRemovesBoundaryAirDeterministically") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 8);
    params.setFloat("chamberScale", 1.05f);
    params.setFloat("tunnelRadius", 0.16f);
    params.setInt("dripstones", 0);
    params.setFloat("boundaryClosure", 0.f);
    MeshBuild   open, sealed, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, open, error));
    params.setFloat("boundaryClosure", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, sealed, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(std::stoi(open.getMeta("boundaryAirSamplesBefore", "0")) > 0);
    REQUIRE_EQ(open.getMeta("boundaryAirSamplesAfter", ""), open.getMeta("boundaryAirSamplesBefore", ""));
    REQUIRE_EQ(sealed.getMeta("boundaryAirSamplesAfter", "-1"), std::string("0"));
    REQUIRE(std::stoi(sealed.getMeta("boundaryClosureChangedVoxels", "0")) > 0);
    REQUIRE(open.positions() != sealed.positions());
    REQUIRE(sealed.positions() == repeated.positions());
    REQUIRE(sealed.indices() == repeated.indices());
    REQUIRE_EQ(sealed.getMeta("boundaryClosureModel", ""), std::string("rough-host-envelope-v1"));
}

TEST_CASE("procgen.mesh.cave.hierarchicalMacroMorphologyIsOptInAndDeterministic") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 7);
    params.setInt("dripstones", 0);
    MeshBuild   legacy, hierarchical, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, legacy, error));
    REQUIRE_EQ(legacy.getMeta("macroMorphologyModel", ""), std::string("uniform-chambers-v1"));

    params.setFloat("chamberHierarchy", 0.9f);
    params.setFloat("passageVariation", 0.8f);
    params.setFloat("chamberIrregularity", 0.7f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hierarchical, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE_EQ(hierarchical.getMeta("macroMorphologyModel", ""), std::string("hierarchical-primary-hall-v2"));
    REQUIRE(std::stof(hierarchical.getMeta("primaryChamberVerticalRadius", "0")) > 0.3f);
    REQUIRE(legacy.positions() != hierarchical.positions());
    REQUIRE(hierarchical.positions() == repeated.positions());
    REQUIRE(hierarchical.indices() == repeated.indices());
}

TEST_CASE("procgen.mesh.cave.flowstonesAndCurtainsChangeDepositionalGeometry") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(2568);
    params.setString("style", "cavern");
    params.setInt("resolution", 40);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    MeshBuild   bare, deposited, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, bare, error));
    params.setInt("flowstones", 12);
    params.setInt("curtains", 10);
    params.setFloat("flowstoneScale", 1.15f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, deposited, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    CHECK(bare.positions() != deposited.positions());
    CHECK(deposited.positions() == repeated.positions());
    CHECK(deposited.indices() == repeated.indices());
    int depositTriangles = 0;
    for (int triangle = 0; triangle < deposited.getIndexCount() / 3; ++triangle)
        if (deposited.getTriangleGroup(triangle) == 1) ++depositTriangles;
    CHECK(depositTriangles > 20);
    CHECK_EQ(deposited.getMeta("flowstones", ""), std::string("12"));
    CHECK_EQ(deposited.getMeta("curtains", ""), std::string("10"));
}
