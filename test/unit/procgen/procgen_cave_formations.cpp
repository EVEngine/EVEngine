#include "CaveFixtures.h"

// procgen_cave_formations.cpp
TEST_CASE("procgen.mesh.cave.biogenicCorrosionErasesFluvialScallopsButFastFilmsProtectThem") {
    CaveBiogenicCorrosionInput dry;
    dry.along             = 0.37f;
    dry.angle             = 1.2f;
    dry.distance          = 0.17f;
    dry.radius            = 0.16f;
    dry.hydraulicExposure = 0.45f;
    dry.strength          = 0.9f;
    dry.seed              = 20260830u;
    const auto first      = sampleCaveBiogenicCorrosion(dry);
    const auto repeat     = sampleCaveBiogenicCorrosion(dry);
    CHECK_EQ(first.erosion, repeat.erosion);
    CHECK_EQ(first.fluvialScallopRetention, repeat.fluvialScallopRetention);
    CHECK(first.erosion > 0.f);
    CHECK(first.fluvialScallopRetention < 0.35f);

    CaveBiogenicCorrosionInput wet = dry;
    wet.hydraulicExposure          = 1.45f;
    const auto protectedSurface    = sampleCaveBiogenicCorrosion(wet);
    CHECK(protectedSurface.erosion < first.erosion);
    CHECK(protectedSurface.fluvialScallopRetention > first.fluvialScallopRetention);

    dry.strength        = 0.f;
    const auto disabled = sampleCaveBiogenicCorrosion(dry);
    CHECK_EQ(disabled.erosion, 0.f);
    CHECK_EQ(disabled.fluvialScallopRetention, 1.f);
}

TEST_CASE("procgen.mesh.cave.biogenicCorrosionIsOptInDistinctAndObservable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 24);
    params.setInt("chambers", 4);
    params.setInt("branches", 2);
    params.setFloat("erosion", 0.f);
    params.setFloat("scallopErosion", 0.f);
    MeshBuild   baseline, first, repeat;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, baseline, error));
    params.setFloat("biogenicCorrosion", 0.9f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeat, error));
    CHECK(first.positions() == repeat.positions());
    CHECK(first.indices() == repeat.indices());
    CHECK(first.positions() != baseline.positions());
    CHECK_EQ(first.getMeta("biogenicCorrosionModel", ""), std::string("ammonia-nitrification-aero-speleogen-v1"));
    CHECK(std::stoi(first.getMeta("biogenicAffectedVoxels", "0")) > 0);
    CHECK(std::stof(first.getMeta("minimumFluvialScallopRetention", "1")) < 1.f);
    CHECK(std::stof(first.getMeta("maximumBiogenicErosion", "0")) > 0.f);

    params.setFloat("biogenicCorrosion", 1.1f);
    MeshBuild invalid;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.condensationCorrosionIsShallowCeilingBiasedAndDeterministic") {
    constexpr int      n     = 17;
    auto               index = [](int x, int y, int z) { return size_t(x + y * n + z * n * n); };
    std::vector<float> density(size_t(n * n * n));
    std::vector<float> exposure(density.size(), 0.35f);
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float px          = float(x) / float(n - 1) * 2.f - 1.f;
                const float py          = float(y) / float(n - 1) * 2.f - 1.f;
                const float pz          = float(z) / float(n - 1) * 2.f - 1.f;
                density[index(x, y, z)] = std::sqrt(px * px + py * py + pz * pz) - 0.62f;
            }
    const std::vector<float> original       = density;
    std::vector<float>       repeated       = density;
    std::vector<float>       disabled       = density;
    const auto               disabledResult = erodeCaveByCondensation(disabled, exposure, n, n, n, 0.f, 20260830u);
    CHECK(disabled == original);
    CHECK_EQ(disabledResult.affectedVoxels, 0);

    const auto result         = erodeCaveByCondensation(density, exposure, n, n, n, 1.f, 20260830u);
    const auto repeatedResult = erodeCaveByCondensation(repeated, exposure, n, n, n, 1.f, 20260830u);
    CHECK(density == repeated);
    CHECK_EQ(result.affectedVoxels, repeatedResult.affectedVoxels);
    CHECK(result.affectedVoxels > 0);
    CHECK(result.maximumRetreat > 0.f);
    CHECK(result.maximumRetreat <= 0.032001f);
    int upperChanged = 0, lowerChanged = 0;
    for (int z = 1; z < n - 1; ++z)
        for (int y = 1; y < n - 1; ++y)
            for (int x = 1; x < n - 1; ++x)
                if (density[index(x, y, z)] != original[index(x, y, z)]) {
                    if (y > n / 2) ++upperChanged;
                    if (y < n / 2) ++lowerChanged;
                }
    CHECK(upperChanged > lowerChanged * 4);
}

TEST_CASE("procgen.mesh.cave.condensationCorrosionIsObservableAndOptIn") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 24);
    params.setInt("chambers", 4);
    params.setInt("branches", 2);
    params.setFloat("condensationCorrosion", 0.9f);
    MeshBuild   first, second;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, second, error));
    CHECK(first.positions() == second.positions());
    CHECK_EQ(first.getMeta("condensationCorrosionModel", ""), std::string("cool-wall-co2-film-pitting-v1"));
    CHECK(std::stoi(first.getMeta("condensationAffectedVoxels", "0")) > 0);
    CHECK(std::stof(first.getMeta("maximumCondensationRetreat", "0")) > 0.f);

    params.setFloat("condensationCorrosion", 1.1f);
    MeshBuild invalid;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.stylesAreDeterministicAndDistinct") {
    MeshRecipeRegistry::instance().registerBuiltins();
    REQUIRE(MeshRecipeRegistry::instance().has("mesh.cave"));
    const char* styles[]         = {"cavern", "tunnels", "vertical", "labyrinth", "mixed"};
    int         previousVertices = -1;
    for (const char* style : styles) {
        Params params;
        params.setSeed(20260830);
        params.setString("style", style);
        params.setInt("resolution", 28);
        params.setInt("chambers", 5);
        params.setInt("branches", 3);
        MeshBuild   first, second;
        std::string error;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, first, error));
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, second, error));
        CHECK(first.getVertexCount() > 100);
        CHECK_EQ(first.getIndexCount() % 3, 0);
        CHECK(first.positions() == second.positions());
        CHECK(first.indices() == second.indices());
        CHECK_EQ(first.getMeta("style", ""), std::string(style));
        CHECK_EQ(first.getMeta("determinism", ""), std::string("bit-exact-cpu"));
        if (previousVertices >= 0) CHECK(first.getVertexCount() != previousVertices);
        previousVertices = first.getVertexCount();
    }
}

TEST_CASE("procgen.mesh.cave.parametersControlBoundsAndTopology") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(77);
    params.setInt("resolution", 24);
    params.setFloat("width", 42.f);
    params.setFloat("height", 14.f);
    params.setFloat("depth", 26.f);
    params.setInt("chambers", 4);
    params.setInt("branches", 1);
    MeshBuild   sparse, branched;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, sparse, error));
    params.setInt("branches", 8);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, branched, error));
    REQUIRE(sparse.positions() != branched.positions());
    REQUIRE_EQ(branched.getMeta("branches", ""), std::string("8"));
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    for (int vertex = 0; vertex < branched.getVertexCount(); ++vertex) {
        minX = std::min(minX, branched.getPositionX(vertex));
        maxX = std::max(maxX, branched.getPositionX(vertex));
        minY = std::min(minY, branched.getPositionY(vertex));
        maxY = std::max(maxY, branched.getPositionY(vertex));
        REQUIRE(std::fabs(branched.getPositionX(vertex)) <= 21.01f);
        REQUIRE(std::fabs(branched.getPositionY(vertex)) <= 7.01f);
        REQUIRE(std::fabs(branched.getPositionZ(vertex)) <= 13.01f);
        const float nx = branched.getNormalX(vertex);
        const float ny = branched.getNormalY(vertex);
        const float nz = branched.getNormalZ(vertex);
        REQUIRE(std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.f) < 1e-3f);
    }
    REQUIRE(minX < -8.f);
    REQUIRE(maxX > 8.f);
    REQUIRE(maxX - minX > 24.f);
    REQUIRE(minY < -1.f);
    REQUIRE(maxY > 1.f);
}

TEST_CASE("procgen.mesh.cave.rejectsInvalidInputs") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setString("style", "lava-tube-ish");
    MeshBuild   mesh;
    std::string error;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("unknown style") != std::string::npos);
    params.setString("style", "mixed");
    params.setInt("resolution", 4);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("resolution") != std::string::npos);
    params.setInt("resolution", 24);
    params.setInt("surfaceRefinement", 3);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setInt("surfaceRefinement", 0);
    params.setInt("isosurfaceSampling", 3);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setInt("isosurfaceSampling", 1);
    params.setString("surfaceNormalMode", "invented");
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("surfaceNormalMode") != std::string::npos);
    params.setString("surfaceNormalMode", "faceAverage");
    params.setInt("wetnessRefinement", 2);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setInt("wetnessRefinement", 0);
    params.setFloat("boundaryClosure", 1.1f);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setFloat("boundaryClosure", 0.f);
    params.setFloat("permeabilityContrast", 1.1f);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setFloat("permeabilityContrast", 0.65f);
    params.setFloat("bendUndercut", 1.1f);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setFloat("bendUndercut", 0.f);
    params.setFloat("scallopMaturity", 1.1f);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setFloat("scallopMaturity", 0.f);
    params.setFloat("fragmentDetachment", 1.1f);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setFloat("fragmentDetachment", 0.f);
    params.setFloat("curvatureDissolution", 1.1f);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setFloat("curvatureDissolution", 0.f);
    params.setFloat("reactiveSurfaceCoupling", 1.1f);
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("invalid") != std::string::npos);
    params.setFloat("reactiveSurfaceCoupling", 0.f);
    params.setString("genesis", "volcanic");
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
    CHECK(error.find("unknown genesis") != std::string::npos);
}

TEST_CASE("procgen.mesh.cave.microstructureControlsDistributedAndLocalizedDissolution") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260319);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("erosion", 0.95f);
    params.setFloat("hydraulicErosion", 0.9f);
    params.setFloat("microstructure", 0.f);
    MeshBuild   baseline, disabledWithDifferentRock, distributed, localized, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, baseline, error));

    params.setFloat("microporosityAccess", 0.f);
    params.setFloat("permeabilityContrast", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, disabledWithDifferentRock, error));
    REQUIRE(baseline.positions() == disabledWithDifferentRock.positions());
    REQUIRE(baseline.indices() == disabledWithDifferentRock.indices());

    params.setFloat("microstructure", 1.f);
    params.setFloat("microporosityAccess", 1.f);
    params.setFloat("permeabilityContrast", 0.2f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, distributed, error));
    params.setFloat("microporosityAccess", 0.f);
    params.setFloat("permeabilityContrast", 0.95f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, localized, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(distributed.positions() != baseline.positions());
    REQUIRE(localized.positions() != baseline.positions());
    REQUIRE(distributed.positions() != localized.positions());
    REQUIRE(localized.positions() == repeated.positions());
    REQUIRE(localized.indices() == repeated.indices());
    REQUIRE_EQ(localized.getMeta("erosionModel", ""), std::string("karst-reactive-microstructure-v6"));
    REQUIRE_EQ(localized.getMeta("microstructureModel", ""), std::string("dual-scale-accessibility-permeability-v1"));
    REQUIRE_EQ(distributed.getMeta("microporosityAccess", ""), std::string("1.000000"));
    REQUIRE_EQ(localized.getMeta("permeabilityContrast", ""), std::string("0.950000"));
}

TEST_CASE("procgen.mesh.cave.hypogeneRisingFlowIsDistinctConnectedAndDeterministic") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(5322505);
    params.setString("style", "labyrinth");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setString("genesis", "epigene");
    MeshBuild   epigene, hypogene, repeated, hypogeneWithVadose;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, epigene, error));

    params.setString("genesis", "hypogene");
    params.setInt("cupolas", 9);
    params.setInt("feeders", 5);
    params.setFloat("vadoseIncision", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogene, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    params.setFloat("vadoseIncision", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneWithVadose, error));

    REQUIRE(epigene.positions() != hypogene.positions());
    REQUIRE(hypogene.positions() == repeated.positions());
    REQUIRE(hypogene.indices() == repeated.indices());
    REQUIRE(hypogene.positions() == hypogeneWithVadose.positions());
    REQUIRE_EQ(epigene.getMeta("cupolas", ""), std::string("0"));
    REQUIRE_EQ(hypogene.getMeta("cupolas", ""), std::string("9"));
    REQUIRE_EQ(hypogene.getMeta("feeders", ""), std::string("5"));
    REQUIRE_EQ(hypogene.getMeta("risingFlowModel", ""), std::string("feeder-half-tube-cupola-v1"));
}

TEST_CASE("procgen.mesh.cave.karstErosionIsDeterministicAndObservable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(41029);
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 4);
    params.setFloat("erosion", 0.f);
    MeshBuild   uneroded, eroded, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, uneroded, error));

    params.setFloat("erosion", 0.85f);
    params.setFloat("bedding", 0.9f);
    params.setFloat("fractureDissolution", 0.8f);
    params.setFloat("vadoseIncision", 0.7f);
    params.setInt("fractureCount", 8);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(uneroded.positions() != eroded.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("erosionModel", ""), std::string("karst-fracture-bedding-vadose-scallop-v2"));
    REQUIRE_EQ(eroded.getMeta("determinism", ""), std::string("bit-exact-cpu"));
}

TEST_CASE("procgen.mesh.cave.hydraulicFlowFocusesReactiveErosion") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20250831);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("erosion", 0.9f);
    params.setFloat("hydraulicErosion", 0.f);
    MeshBuild   legacy, legacyDifferentHydrology, lowFlow, highFlow, repeated, unfocused;
    MeshBuild   uniformDissolution, wormholeDissolution;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, legacy, error));

    params.setFloat("hydraulicGradient", 1.8f);
    params.setFloat("recharge", 0.1f);
    params.setFloat("damkohler", 0.04f);
    params.setFloat("transportG", 4.5f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, legacyDifferentHydrology, error));
    REQUIRE(legacy.positions() == legacyDifferentHydrology.positions());
    REQUIRE(legacy.indices() == legacyDifferentHydrology.indices());

    params.setFloat("hydraulicErosion", 1.f);
    params.setFloat("hydraulicGradient", 0.08f);
    params.setFloat("recharge", 0.2f);
    params.setFloat("flowFocusing", 0.85f);
    params.setFloat("damkohler", 0.002f);
    params.setFloat("transportG", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, lowFlow, error));
    params.setFloat("hydraulicGradient", 1.2f);
    params.setFloat("recharge", 0.9f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, highFlow, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    params.setFloat("flowFocusing", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, unfocused, error));
    params.setFloat("flowFocusing", 0.85f);
    params.setFloat("damkohler", 0.0002f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, uniformDissolution, error));
    params.setFloat("damkohler", 0.02f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, wormholeDissolution, error));

    REQUIRE(lowFlow.positions() != highFlow.positions());
    REQUIRE(unfocused.positions() != highFlow.positions());
    REQUIRE(uniformDissolution.positions() != highFlow.positions());
    REQUIRE(wormholeDissolution.positions() != highFlow.positions());
    REQUIRE(uniformDissolution.positions() != wormholeDissolution.positions());
    REQUIRE(highFlow.positions() == repeated.positions());
    REQUIRE(highFlow.indices() == repeated.indices());
    REQUIRE_EQ(highFlow.getMeta("erosionModel", ""), std::string("karst-reactive-network-da-v5"));
    REQUIRE_EQ(highFlow.getMeta("hydraulicNetwork", ""), std::string("tributary-confluence-feedback-v1"));
    REQUIRE_EQ(highFlow.getMeta("hydraulicConfluences", ""), std::string("4"));
    REQUIRE(std::stof(highFlow.getMeta("minimumFlowWeight", "1")) <
            std::stof(highFlow.getMeta("maximumFlowWeight", "1")));
    REQUIRE_EQ(highFlow.getMeta("hydraulicGradient", ""), std::string("1.200000"));
    REQUIRE_EQ(highFlow.getMeta("recharge", ""), std::string("0.900000"));
    REQUIRE_EQ(highFlow.getMeta("dissolutionRegime", ""), std::string("channeling"));
    REQUIRE_EQ(uniformDissolution.getMeta("dissolutionRegime", ""), std::string("uniform"));
    REQUIRE_EQ(wormholeDissolution.getMeta("dissolutionRegime", ""), std::string("wormholing"));
    REQUIRE(std::stof(uniformDissolution.getMeta("reactantPenetration", "0")) >
            std::stof(highFlow.getMeta("reactantPenetration", "0")));
    REQUIRE(std::stof(highFlow.getMeta("reactantPenetration", "0")) >
            std::stof(wormholeDissolution.getMeta("reactantPenetration", "0")));
}

TEST_CASE("procgen.mesh.cave.flowScallopsAreDirectionalDeterministicErosion") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20250033);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("bedding", 0.f);
    params.setFloat("fractureDissolution", 0.f);
    params.setFloat("erosion", 0.9f);
    params.setFloat("scallopErosion", 0.f);
    MeshBuild   smoothWall, scalloped, repeated, scalingWithoutFlow, fixedHydraulic, scaledHydraulic, scaledRepeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, smoothWall, error));

    params.setFloat("scallopErosion", 1.f);
    params.setFloat("scallopScale", 0.10f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, scalloped, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    params.setFloat("scallopHydraulicScaling", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, scalingWithoutFlow, error));
    params.setFloat("hydraulicErosion", 1.f);
    params.setFloat("hydraulicGradient", 1.1f);
    params.setFloat("recharge", 0.9f);
    params.setFloat("flowFocusing", 0.9f);
    params.setFloat("scallopHydraulicScaling", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, fixedHydraulic, error));
    params.setFloat("scallopHydraulicScaling", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, scaledHydraulic, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, scaledRepeated, error));

    REQUIRE(smoothWall.positions() != scalloped.positions());
    REQUIRE(scalloped.positions() == repeated.positions());
    REQUIRE(scalloped.indices() == repeated.indices());
    REQUIRE(scalloped.positions() == scalingWithoutFlow.positions());
    REQUIRE(fixedHydraulic.positions() != scaledHydraulic.positions());
    REQUIRE(scaledHydraulic.positions() == scaledRepeated.positions());
    REQUIRE(scaledHydraulic.indices() == scaledRepeated.indices());
    REQUIRE_EQ(scalloped.getMeta("scallopScale", ""), std::string("0.100000"));
    REQUIRE_EQ(scalloped.getMeta("erosionModel", ""), std::string("karst-fracture-bedding-vadose-scallop-v2"));
    REQUIRE_EQ(scaledHydraulic.getMeta("erosionModel", ""), std::string("karst-hydraulic-scallop-v7"));
    REQUIRE(std::stof(scaledHydraulic.getMeta("minimumScallopScale", "1")) <
            std::stof(scaledHydraulic.getMeta("maximumScallopScale", "0")));
}

TEST_CASE("procgen.mesh.cave.scallopScalePatchesAndFlowSeparationAreDeterministicOptIns") {
    CaveScallopInput input;
    input.along                    = 0.037f;
    input.angle                    = 0.42f;
    input.distance                 = 0.17f;
    input.radius                   = 0.16f;
    input.hydraulicIntensity       = 0.9f;
    input.baseScale                = 0.1f;
    input.maturity                 = 0.65f;
    input.seed                     = 20250033u;
    const CaveScallopSample legacy = sampleCaveScallops(input);
    CHECK_EQ(legacy.scaleMultiplier, 1.f);

    input.scaleVariability         = 0.8f;
    const CaveScallopSample varied = sampleCaveScallops(input);
    CHECK_EQ(varied.scaleMultiplier, sampleCaveScallops(input).scaleMultiplier);
    CHECK(varied.scaleMultiplier != 1.f);
    CHECK(varied.scale != legacy.scale);

    input.flowSeparation              = 0.85f;
    const CaveScallopSample separated = sampleCaveScallops(input);
    CHECK_EQ(separated.erosion, sampleCaveScallops(input).erosion);
    CHECK(separated.erosion != varied.erosion);
}

TEST_CASE("procgen.mesh.cave.scallopVariabilityAndSeparationAreObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20250033);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 1.f);
    params.setFloat("scallopErosion", 1.f);
    MeshBuild   legacy, explicitLegacy, varied, repeated, separated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, legacy, error));
    params.setFloat("scallopScaleVariability", 0.f);
    params.setFloat("scallopFlowSeparation", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(legacy.positions() == explicitLegacy.positions());

    params.setFloat("scallopScaleVariability", 0.8f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, varied, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(varied.positions() != legacy.positions());
    REQUIRE(varied.positions() == repeated.positions());
    REQUIRE_EQ(varied.getMeta("scallopScaleDistribution", ""), std::string("correlated-lognormal-proxy-v1"));
    REQUIRE(std::stof(varied.getMeta("scallopGeometricStdDev", "1")) > 1.f);

    params.setFloat("scallopFlowSeparation", 0.8f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, separated, error));
    REQUIRE(separated.positions() != varied.positions());
    REQUIRE_EQ(separated.getMeta("scallopFlowProfile", ""), std::string("slope-separated-travelling-wave-v2"));

    params.setFloat("scallopScaleVariability", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.curvedPassagesUndercutOuterBanksDeterministically") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20250481);
    params.setString("style", "labyrinth");
    params.setInt("resolution", 24);
    params.setInt("branches", 0);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("bedding", 0.f);
    params.setFloat("fractureDissolution", 0.f);
    params.setFloat("erosion", 1.f);
    params.setFloat("scallopErosion", 1.f);
    params.setFloat("bendUndercut", 0.f);
    MeshBuild   symmetric, undercut, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, symmetric, error));

    params.setFloat("bendUndercut", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, undercut, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(symmetric.positions() != undercut.positions());
    REQUIRE(undercut.positions() == repeated.positions());
    REQUIRE(undercut.indices() == repeated.indices());
    REQUIRE_EQ(undercut.getMeta("erosionModel", ""), std::string("karst-reactive-curvature-v8"));
    REQUIRE_EQ(undercut.getMeta("bendErosionModel", ""), std::string("curvature-outer-bank-v1"));
    REQUIRE_EQ(undercut.getMeta("bendUndercut", ""), std::string("1.000000"));
}

TEST_CASE("procgen.mesh.cave.matureScallopsCoarsenIntoSharpCellularRidges") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(37988469);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("bedding", 0.f);
    params.setFloat("fractureDissolution", 0.f);
    params.setFloat("erosion", 1.f);
    params.setFloat("scallopErosion", 1.f);
    params.setFloat("scallopScale", 0.09f);
    params.setFloat("scallopMaturity", 0.f);
    MeshBuild   young, mature, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, young, error));

    params.setFloat("scallopMaturity", 0.8f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, mature, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(young.positions() != mature.positions());
    REQUIRE(mature.positions() == repeated.positions());
    REQUIRE(mature.indices() == repeated.indices());
    REQUIRE_EQ(mature.getMeta("erosionModel", ""), std::string("karst-reactive-coarsening-v9"));
    REQUIRE_EQ(mature.getMeta("scallopEvolutionModel", ""), std::string("normal-ablation-coarsening-v1"));
    REQUIRE_EQ(mature.getMeta("scallopMaturity", ""), std::string("0.800000"));
}

TEST_CASE("procgen.mesh.cave.fragmentDetachmentRemovesOnlyHostDisconnectedRock") {
    constexpr int      size = 7;
    std::vector<float> density(size_t(size * size * size), -1.f);
    auto               index = [](int x, int y, int z) { return size_t(x + y * size + z * size * size); };
    density[index(0, 2, 2)]  = 1.f;
    density[index(1, 2, 2)]  = 1.f;
    density[index(2, 2, 2)]  = 1.f;
    density[index(3, 3, 3)]  = 1.f;
    density[index(5, 5, 5)]  = 1.f;

    const std::vector<float>   original = density;
    const CaveDetachmentResult disabled = detachUnsupportedCaveFragments(density, size, size, size, 0.f);
    REQUIRE(density == original);
    REQUIRE_EQ(disabled.unsupportedVoxels, 0);

    const CaveDetachmentResult detached = detachUnsupportedCaveFragments(density, size, size, size, 1.f);
    REQUIRE(density[index(0, 2, 2)] > 0.f);
    REQUIRE(density[index(1, 2, 2)] > 0.f);
    REQUIRE(density[index(2, 2, 2)] > 0.f);
    REQUIRE(density[index(3, 3, 3)] > 0.f);  // Diagonal neighbors remain host-connected.
    REQUIRE(density[index(5, 5, 5)] < 0.f);
    REQUIRE_EQ(detached.unsupportedVoxels, 1);
    REQUIRE_EQ(detached.detachedVoxels, 1);
}

TEST_CASE("procgen.mesh.cave.curvatureEvolutionRetreatsConvexWallsDeterministically") {
    constexpr int      size  = 17;
    auto               index = [](int x, int y, int z) { return size_t(x + y * size + z * size * size); };
    std::vector<float> density(size_t(size * size * size));
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float px          = float(x - size / 2) * 2.f / float(size - 1);
                const float py          = float(y - size / 2) * 2.f / float(size - 1);
                const float pz          = float(z - size / 2) * 2.f / float(size - 1);
                density[index(x, y, z)] = std::sqrt(px * px + py * py + pz * pz) - 0.45f;
            }
        }
    }
    const std::vector<float> original = density;
    std::vector<float>       repeated = density;
    const auto               disabled = evolveCaveSurfaceByCurvature(density, size, size, size, 0.f);
    CHECK(density == original);
    CHECK_EQ(disabled.affectedVoxels, 0);

    const auto evolved        = evolveCaveSurfaceByCurvature(density, size, size, size, 0.8f);
    const auto repeatedResult = evolveCaveSurfaceByCurvature(repeated, size, size, size, 0.8f);
    CHECK(density == repeated);
    CHECK_EQ(evolved.affectedVoxels, repeatedResult.affectedVoxels);
    CHECK(evolved.affectedVoxels > 0);
    CHECK(evolved.maximumRetreat > 0.f);
    CHECK(density[index(size / 2 + 4, size / 2, size / 2)] < original[index(size / 2 + 4, size / 2, size / 2)]);
    CHECK(density[index(0, 0, 0)] == original[index(0, 0, 0)]);
}

TEST_CASE("procgen.mesh.cave.depositsAnchorToFinalDensitySurface") {
    constexpr int      size = 33;
    std::vector<float> density(size_t(size * size * size));
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float px                                  = float(x) / float(size - 1) * 2.f - 1.f;
                const float py                                  = float(y) / float(size - 1) * 2.f - 1.f;
                const float pz                                  = float(z) / float(size - 1) * 2.f - 1.f;
                density[size_t(x + y * size + z * size * size)] = std::sqrt(px * px + py * py + pz * pz) - 0.65f;
            }
        }
    }

    const auto span = findCaveVerticalSpan(density, size, size, size, 0.f, 0.f, 0.1f);
    REQUIRE(span.has_value());
    CHECK(std::abs(span->floor.position.y + 0.65f) < 0.01f);
    CHECK(std::abs(span->ceiling.position.y - 0.65f) < 0.01f);
    CHECK(span->floor.rockNormal.y < -0.9f);
    CHECK(span->ceiling.rockNormal.y > 0.9f);

    const auto wall = projectToFinalCaveSurface(density, size, size, size, {0.58f, 0.1f, 0.f}, 0.2f);
    REQUIRE(wall.has_value());
    CHECK(std::abs(sampleCaveDensity(density, size, size, size, wall->position)) < 0.01f);
    CHECK(wall->rockNormal.x > 0.9f);
    CHECK(!findCaveVerticalSpan(density, size, size, size, 0.9f, 0.9f, 0.f).has_value());
    CHECK(!projectToFinalCaveSurface(density, size, size, size, {0.f, 0.f, 0.f}, 0.2f).has_value());
}

TEST_CASE("procgen.mesh.cave.curvatureDissolutionIsObservableBeforeDeposition") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 12);
    params.setInt("flowstones", 7);
    params.setInt("curtains", 5);
    params.setFloat("curvatureDissolution", 0.f);
    MeshBuild   baseline, evolved, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, baseline, error));

    params.setFloat("curvatureDissolution", 0.8f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, evolved, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(evolved.positions() == repeated.positions());
    REQUIRE(evolved.indices() == repeated.indices());
    REQUIRE(baseline.positions() != evolved.positions());
    REQUIRE_EQ(evolved.getMeta("surfaceEvolutionModel", ""), std::string("convex-normal-retreat-v1"));
    REQUIRE(std::stoi(evolved.getMeta("curvatureAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(evolved.getMeta("maximumCurvatureRetreat", "0")) > 0.f);
}

TEST_CASE("procgen.mesh.cave.reactiveAccessibilityModulatesCurvatureRetreat") {
    constexpr int      size  = 17;
    auto               index = [](int x, int y, int z) { return size_t(x + y * size + z * size * size); };
    std::vector<float> density(size_t(size * size * size));
    std::vector<float> rates(density.size(), 0.35f);
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float px          = float(x - size / 2) * 2.f / float(size - 1);
                const float py          = float(y - size / 2) * 2.f / float(size - 1);
                const float pz          = float(z - size / 2) * 2.f / float(size - 1);
                density[index(x, y, z)] = std::sqrt(px * px + py * py + pz * pz) - 0.45f;
                if (x > size / 2) rates[index(x, y, z)] = 2.1f;
            }
        }
    }
    const std::vector<float> original = density;
    const auto               result   = evolveCaveSurfaceByCurvature(density, rates, size, size, size, 0.8f);
    const float              fastRetreat =
        original[index(size / 2 + 4, size / 2, size / 2)] - density[index(size / 2 + 4, size / 2, size / 2)];
    const float slowRetreat =
        original[index(size / 2 - 4, size / 2, size / 2)] - density[index(size / 2 - 4, size / 2, size / 2)];
    CHECK(fastRetreat > slowRetreat * 3.f);
    CHECK(result.minimumRateMultiplier < 0.5f);
    CHECK(result.maximumRateMultiplier > 2.f);
    CHECK(result.totalRetreat > result.maximumRetreat);
}

TEST_CASE("procgen.mesh.cave.surfaceRateCouplingChangesSameHydrologyDeterministically") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20251120);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("curvatureDissolution", 0.85f);
    params.setFloat("microstructure", 0.9f);
    params.setFloat("hydraulicErosion", 0.85f);
    params.setFloat("reactiveSurfaceCoupling", 0.f);
    MeshBuild   uniform, coupled, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, uniform, error));

    params.setFloat("reactiveSurfaceCoupling", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, coupled, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(uniform.positions() != coupled.positions());
    REQUIRE(coupled.positions() == repeated.positions());
    REQUIRE(coupled.indices() == repeated.indices());
    REQUIRE_EQ(coupled.getMeta("surfaceRateModel", ""), std::string("microstructure-hydraulic-access-v1"));
    REQUIRE(std::stof(coupled.getMeta("minimumSurfaceRate", "1")) < 1.f);
    REQUIRE(std::stof(coupled.getMeta("maximumSurfaceRate", "1")) > 1.f);
}

TEST_CASE("procgen.mesh.cave.detachmentIsObservableAndDeterministic") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(7);  // This fixture produces a mesh-visible effect.
    params.setString("style", "mixed");
    params.setInt("resolution", 36);
    params.setInt("dripstones", 18);
    params.setInt("flowstones", 8);
    params.setInt("curtains", 6);
    params.setFloat("erosion", 0.9f);
    params.setFloat("fragmentDetachment", 0.f);
    MeshBuild   retained, detached, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, retained, error));

    params.setFloat("fragmentDetachment", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, detached, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE(detached.positions() == repeated.positions());
    REQUIRE(detached.indices() == repeated.indices());
    REQUIRE_EQ(detached.getMeta("detachmentModel", ""), std::string("host-rock-connectivity-v1"));
    REQUIRE(std::stoi(detached.getMeta("detachedVoxels", "0")) > 0);
    REQUIRE_EQ(std::stoi(detached.getMeta("unsupportedVoxels", "0")),
               std::stoi(detached.getMeta("detachedVoxels", "0")));
    REQUIRE(retained.positions() != detached.positions());
}

TEST_CASE("procgen.mesh.cave.surfaceRefinementProjectsDeterministicDetail") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(7741397);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("scallopErosion", 0.9f);
    params.setFloat("scallopScale", 0.10f);
    params.setInt("surfaceRefinement", 0);
    MeshBuild   coarse, refined, adaptive, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, coarse, error));

    params.setInt("surfaceRefinement", 1);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, refined, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));

    REQUIRE_EQ(refined.getIndexCount(), coarse.getIndexCount() * 4);
    REQUIRE_EQ(refined.getVertexCount(), coarse.getVertexCount() * 4);
    REQUIRE(refined.positions() == repeated.positions());
    REQUIRE(refined.indices() == repeated.indices());
    REQUIRE(refined.triangleGroups() == repeated.triangleGroups());
    REQUIRE_EQ(refined.getMeta("surfaceProjection", ""), std::string("trilinear-newton-v1"));
    REQUIRE_EQ(refined.getMeta("surfaceRefinement", ""), std::string("1"));

    params.setInt("surfaceRefinement", 2);
    params.setFloat("refinementThreshold", 0.0015f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, adaptive, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(adaptive.getIndexCount() > coarse.getIndexCount());
    REQUIRE(adaptive.getIndexCount() < refined.getIndexCount());
    REQUIRE(adaptive.positions() == repeated.positions());
    REQUIRE(adaptive.triangleGroups() == repeated.triangleGroups());
    REQUIRE(std::stoi(adaptive.getMeta("adaptiveSplitEdges", "0")) > 0);
    REQUIRE(std::stoi(adaptive.getMeta("refinedSourceTriangles", "0")) > 0);
    REQUIRE_EQ(adaptive.getMeta("refinementTriangulation", ""), std::string("conforming-edge-mask-v2"));
}

TEST_CASE("procgen.mesh.cave.trilinearSupersamplingPreservesSourceSamples") {
    constexpr int      size = 5;
    std::vector<float> density(size_t(size * size * size));
    auto               index = [](int x, int y, int z, int width, int height) {
        return size_t(x) + size_t(y) * size_t(width) + size_t(z) * size_t(width) * size_t(height);
    };
    for (int z = 0; z < size; ++z)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) density[index(x, y, z, size, size)] = float(x * x + 3 * y - 2 * z) - 8.5f;

    const CaveResampledField doubled = resampleCaveDensity(density, size, size, size, 2);
    CHECK_EQ(doubled.nx, 9);
    CHECK_EQ(doubled.ny, 9);
    CHECK_EQ(doubled.nz, 9);
    for (int z = 0; z < size; ++z)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                CHECK_EQ(doubled.density[index(x * 2, y * 2, z * 2, doubled.nx, doubled.ny)],
                         density[index(x, y, z, size, size)]);
    CHECK_EQ(doubled.density[index(1, 0, 0, doubled.nx, doubled.ny)],
             0.5f * (density[index(0, 0, 0, size, size)] + density[index(1, 0, 0, size, size)]));
}

// procgen_cave_hydrology.cpp
TEST_CASE("procgen.mesh.cave.floodPluckingIsBlockyObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 4);
    params.setInt("fractureCount", 10);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, plucked, repeated, noFracturesOff, noFracturesOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("floodPlucking", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("floodPlucking", 1.f);
    params.setFloat("pluckingBlockScale", 0.14f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, plucked, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(plucked.positions() != explicitLegacy.positions());
    REQUIRE(plucked.positions() == repeated.positions());
    REQUIRE(plucked.indices() == repeated.indices());
    REQUIRE_EQ(plucked.getMeta("floodPluckingModel", ""), std::string("thresholded-fracture-block-release-v1"));
    REQUIRE(std::stoi(plucked.getMeta("pluckingAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(plucked.getMeta("maximumPluckingRetreat", "0")) > 0.f);
    REQUIRE(std::stof(plucked.getMeta("maximumPluckingPredisposition", "0")) > 0.f);

    params.setInt("fractureCount", 0);
    params.setFloat("floodPlucking", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noFracturesOff, error));
    params.setFloat("floodPlucking", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noFracturesOn, error));
    REQUIRE(noFracturesOff.positions() == noFracturesOn.positions());
    REQUIRE(noFracturesOff.indices() == noFracturesOn.indices());
    REQUIRE_EQ(noFracturesOn.getMeta("floodPluckingModel", ""), std::string("inactive-no-fracture-network"));

    params.setFloat("floodPlucking", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.floodAbrasionIsNearBedFlowDrivenAndToolsLimited") {
    const CaveAbrasionInput  floorInput{0.42f, 3.1415926535f, 1.f, 0.45f, 3.1415926535f, 0.48f, 20260830u};
    const CaveAbrasionSample floor    = sampleCaveFloodAbrasion(floorInput);
    const CaveAbrasionSample repeated = sampleCaveFloodAbrasion(floorInput);
    CHECK_EQ(floor.erosion, repeated.erosion);
    CHECK(floor.floorMask > 0.99f);
    CHECK(floor.erosion > 0.f);
    CHECK(floor.coverProtection > 0.f);

    CaveAbrasionInput crownInput   = floorInput;
    crownInput.angle               = 0.f;
    const CaveAbrasionSample crown = sampleCaveFloodAbrasion(crownInput);
    CHECK(crown.erosion < floor.erosion * 0.01f);

    CaveAbrasionInput lowFlowInput  = floorInput;
    lowFlowInput.hydraulicIntensity = 0.15f;
    CHECK(sampleCaveFloodAbrasion(lowFlowInput).erosion < floor.erosion);

    CaveAbrasionInput noToolsInput   = floorInput;
    noToolsInput.sedimentLoad        = 0.f;
    const CaveAbrasionSample noTools = sampleCaveFloodAbrasion(noToolsInput);
    CHECK_EQ(noTools.erosion, 0.f);
    CHECK_EQ(noTools.toolAvailability, 0.f);
}

TEST_CASE("procgen.mesh.cave.floodAbrasionIsObservableBoundedAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 4);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, abraded, repeated, noToolsOff, noToolsOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("floodAbrasion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("floodAbrasion", 1.f);
    params.setFloat("sedimentLoad", 0.48f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, abraded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(abraded.positions() != explicitLegacy.positions());
    REQUIRE(abraded.positions() == repeated.positions());
    REQUIRE(abraded.indices() == repeated.indices());
    REQUIRE_EQ(abraded.getMeta("floodAbrasionModel", ""), std::string("near-bed-tools-cover-vortex-v1"));
    REQUIRE(std::stoi(abraded.getMeta("abrasionAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(abraded.getMeta("maximumAbrasionRetreat", "0")) > 0.f);
    REQUIRE(std::stof(abraded.getMeta("maximumAbrasionVortex", "0")) > 0.f);

    params.setFloat("sedimentLoad", 0.f);
    params.setFloat("floodAbrasion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOff, error));
    params.setFloat("floodAbrasion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOn, error));
    REQUIRE(noToolsOff.positions() == noToolsOn.positions());
    REQUIRE(noToolsOff.indices() == noToolsOn.indices());
    REQUIRE_EQ(noToolsOn.getMeta("floodAbrasionModel", ""), std::string("inactive-no-tools"));

    params.setFloat("floodAbrasion", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.lithologyFormsBedCoherentFlowAccessibleStyloliteRetreat") {
    CaveLithologyInput  input{0.12f, -1.f, -0.18f, 0.42f, 1.f, 1.f, 20260830u};
    CaveLithologySample sample;
    for (int step = 0; step <= 400; ++step) {
        input.y                             = -1.f + float(step) / 200.f;
        const CaveLithologySample candidate = sampleCaveLithology(input);
        if (candidate.styloliteMask > sample.styloliteMask) sample = candidate;
    }
    REQUIRE(sample.styloliteMask > 0.2f);
    input.y = (float(sample.bedIndex) / 7.25f) -
              (std::sin(input.x * 2.1f + input.z * 1.7f + float(input.seed % 4093u) * 0.001534326f) * 0.035f +
               std::sin(input.x * 4.7f - input.z * 3.2f + float(input.seed % 4093u) * 0.001534326f * 1.61f) * 0.012f);
    const CaveLithologySample repeated      = sampleCaveLithology(input);
    const CaveLithologySample repeatedAgain = sampleCaveLithology(input);
    CHECK_EQ(repeated.bedIndex, repeatedAgain.bedIndex);
    CHECK_EQ(repeated.styloliteMask, repeatedAgain.styloliteMask);
    CHECK(repeated.bedResistance >= 0.65f);
    CHECK(repeated.bedResistance <= 1.35f);

    CaveLithologyInput stagnantInput   = input;
    stagnantInput.hydraulicIntensity   = 0.f;
    const CaveLithologySample stagnant = sampleCaveLithology(stagnantInput);
    CHECK(repeated.retreat > stagnant.retreat);

    CaveLithologyInput disabledInput   = input;
    disabledInput.heterogeneity        = 0.f;
    const CaveLithologySample disabled = sampleCaveLithology(disabledInput);
    CHECK_EQ(disabled.retreat, 0.f);
    CHECK_EQ(disabled.styloliteMask, 0.f);
    CHECK_EQ(disabled.bedResistance, 1.f);
}

TEST_CASE("procgen.mesh.cave.lithologicHeterogeneityIsSelectiveObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 4);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, selective, repeated, noBeddingOff, noBeddingOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("lithologicHeterogeneity", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("lithologicHeterogeneity", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, selective, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(selective.positions() != explicitLegacy.positions());
    REQUIRE(selective.positions() == repeated.positions());
    REQUIRE(selective.indices() == repeated.indices());
    REQUIRE_EQ(selective.getMeta("lithologyErosionModel", ""), std::string("flow-accessible-stylolite-beds-v1"));
    REQUIRE(std::stoi(selective.getMeta("lithologyAffectedVoxels", "0")) > 0);
    REQUIRE(std::stoi(selective.getMeta("styloliteAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(selective.getMeta("minimumBedResistance", "1")) < 1.f);
    REQUIRE(std::stof(selective.getMeta("maximumLithologyRetreat", "0")) > 0.f);

    params.setFloat("bedding", 0.f);
    params.setFloat("lithologicHeterogeneity", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noBeddingOff, error));
    params.setFloat("lithologicHeterogeneity", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noBeddingOn, error));
    REQUIRE(noBeddingOff.positions() == noBeddingOn.positions());
    REQUIRE(noBeddingOff.indices() == noBeddingOn.indices());
    REQUIRE_EQ(noBeddingOn.getMeta("lithologyErosionModel", ""), std::string("disabled"));

    params.setFloat("lithologicHeterogeneity", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.mixingSitesReuseConfluencesAndCarveLocally") {
    const std::vector<CaveHydrologyPoint> trunk{
        {{-0.5f, 0.f, 0.f}, 0.20f}, {{0.f, 0.f, 0.f}, 0.22f}, {{0.5f, 0.f, 0.f}, 0.21f}};
    CaveHydrologyBranch branch;
    branch.trunkAnchor                              = 1;
    branch.points                                   = {{{0.f, 0.f, 0.f}, 0.22f}, {{0.f, 0.f, 0.35f}, 0.18f}};
    const std::vector<CaveMixingSite> sites         = createCaveMixingSites(trunk, {branch}, 20260830u);
    const std::vector<CaveMixingSite> repeatedSites = createCaveMixingSites(trunk, {branch}, 20260830u);
    REQUIRE_EQ(sites.size(), size_t(1));
    CHECK_EQ(sites[0].mixingPotential, repeatedSites[0].mixingPotential);
    CHECK(sites[0].mixingPotential > 0.f);
    CHECK(sites[0].mixingPotential <= 1.f);

    const CaveMixingCorrosionSample centre = sampleCaveMixingCorrosion({0.f, 0.f, 0.f}, sites);
    const CaveMixingCorrosionSample far    = sampleCaveMixingCorrosion({0.8f, 0.8f, 0.8f}, sites);
    CHECK_EQ(centre.siteIndex, 0);
    CHECK(centre.erosion > far.erosion);
    CHECK_EQ(far.siteIndex, -1);
}

TEST_CASE("procgen.mesh.cave.mixingCorrosionIsConfluenceBoundedObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, mixed, repeated, noConfluenceOff, noConfluenceOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("mixingCorrosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("mixingCorrosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, mixed, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(mixed.positions() != explicitLegacy.positions());
    REQUIRE(mixed.positions() == repeated.positions());
    REQUIRE(mixed.indices() == repeated.indices());
    REQUIRE_EQ(mixed.getMeta("mixingCorrosionModel", ""), std::string("chemistry-weighted-confluence-v1"));
    REQUIRE_EQ(mixed.getMeta("mixingCorrosionSites", ""), std::string("5"));
    REQUIRE(std::stoi(mixed.getMeta("mixingCorrosionAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(mixed.getMeta("maximumMixingCorrosionRetreat", "0")) > 0.f);

    params.setInt("branches", 0);
    params.setFloat("mixingCorrosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noConfluenceOff, error));
    params.setFloat("mixingCorrosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noConfluenceOn, error));
    REQUIRE(noConfluenceOff.positions() == noConfluenceOn.positions());
    REQUIRE(noConfluenceOff.indices() == noConfluenceOn.indices());
    REQUIRE_EQ(noConfluenceOn.getMeta("mixingCorrosionModel", ""), std::string("inactive-no-confluence"));

    params.setFloat("mixingCorrosion", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.waterTableBeltsAreLevelSidewallBiasedAndStaged") {
    CaveWaterTableInput input;
    input.y                             = 0.30f;
    input.passageAngle                  = 1.57079632679f;
    input.along                         = 0.4f;
    input.level                         = 0.30f;
    input.stageDrop                     = 0.20f;
    input.fluctuation                   = 0.f;
    input.stages                        = 3;
    input.seed                          = 20260830u;
    const CaveWaterTableSample upper    = sampleCaveWaterTableCorrosion(input);
    const CaveWaterTableSample repeated = sampleCaveWaterTableCorrosion(input);
    CHECK_EQ(upper.erosion, repeated.erosion);
    CHECK_EQ(upper.dominantStage, 0);
    CHECK(upper.erosion > 0.99f);

    input.y                           = 0.10f;
    const CaveWaterTableSample middle = sampleCaveWaterTableCorrosion(input);
    CHECK_EQ(middle.dominantStage, 1);
    CHECK(middle.erosion > 0.70f);
    CHECK(middle.erosion < upper.erosion);

    input.y                          = 0.30f;
    input.passageAngle               = 0.f;
    const CaveWaterTableSample crown = sampleCaveWaterTableCorrosion(input);
    CHECK(crown.erosion < upper.erosion * 0.01f);
}

TEST_CASE("procgen.mesh.cave.waterTableCorrosionIsEpiphreaticObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "cavern");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 6);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, corroded, repeated, hypogeneOff, hypogeneOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("waterTableCorrosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("waterTableCorrosion", 1.f);
    params.setFloat("waterTableLevel", 0.3f);
    params.setInt("waterTableStages", 3);
    params.setFloat("waterTableDrop", 0.2f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, corroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(corroded.positions() != explicitLegacy.positions());
    REQUIRE(corroded.positions() == repeated.positions());
    REQUIRE(corroded.indices() == repeated.indices());
    REQUIRE_EQ(corroded.getMeta("waterTableCorrosionModel", ""), std::string("descending-epiphreatic-belts-v1"));
    REQUIRE(std::stoi(corroded.getMeta("waterTableAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(corroded.getMeta("maximumWaterTableRetreat", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("waterTableCorrosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("waterTableCorrosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("waterTableCorrosionModel", ""), std::string("inactive-hypogene"));

    params.setFloat("waterTableCorrosion", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.multiscaleWallReliefIsBoundedAndDeterministic") {
    const CaveRoughnessSample first    = sampleCaveWallRoughness({0.17f, -0.23f, 0.41f, 20260830u});
    const CaveRoughnessSample repeat   = sampleCaveWallRoughness({0.17f, -0.23f, 0.41f, 20260830u});
    const CaveRoughnessSample shifted  = sampleCaveWallRoughness({0.19f, -0.23f, 0.41f, 20260830u});
    const CaveRoughnessSample reseeded = sampleCaveWallRoughness({0.17f, -0.23f, 0.41f, 20260831u});
    CHECK_EQ(first.relief, repeat.relief);
    CHECK(first.relief >= -1.f);
    CHECK(first.relief <= 1.f);
    CHECK(first.relief != shifted.relief);
    CHECK(first.relief != reseeded.relief);
    CHECK(first.macroBand != first.mesoBand);
    CHECK(first.mesoBand != first.fineBand);
}

TEST_CASE("procgen.mesh.cave.multiscaleRoughnessIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.28f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, spectral, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("multiscaleRoughness", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("multiscaleRoughness", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, spectral, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(spectral.positions() != explicitLegacy.positions());
    REQUIRE(spectral.positions() == repeated.positions());
    REQUIRE(spectral.indices() == repeated.indices());
    REQUIRE_EQ(spectral.getMeta("wallRoughnessSpectrum", ""), std::string("band-limited-three-scale-v1"));
    REQUIRE(std::stof(spectral.getMeta("minimumWallRelief", "0")) < 0.f);
    REQUIRE(std::stof(spectral.getMeta("maximumWallRelief", "0")) > 0.f);

    params.setFloat("multiscaleRoughness", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.fractureApertureAndStressSplittingAreDeterministicOptIns") {
    CaveFractureInput input;
    input.x        = 0.03f;
    input.y        = 0.17f;
    input.z        = -0.04f;
    input.fracture = {0.8f, 0.6f, 0.01f, 0.025f};
    input.seed     = 20260830u;

    const CaveFractureSample legacy = sampleCaveFracture(input);
    CHECK_EQ(legacy.apertureMultiplier, 1.f);
    CHECK_EQ(legacy.branchOpenness, 1.f);

    input.apertureVariability              = 0.8f;
    const CaveFractureSample heterogeneous = sampleCaveFracture(input);
    const CaveFractureSample repeated      = sampleCaveFracture(input);
    CHECK(heterogeneous.apertureMultiplier != 1.f);
    CHECK_EQ(heterogeneous.mask, repeated.mask);
    CHECK_EQ(heterogeneous.apertureMultiplier, repeated.apertureMultiplier);

    input.stressControl               = 0.9f;
    const CaveFractureSample stressed = sampleCaveFracture(input);
    CHECK(stressed.branchOpenness < 1.f);
    CHECK(stressed.mask != heterogeneous.mask);
}

TEST_CASE("procgen.mesh.cave.fractureChannelizationFollowsCubicApertureIntersectionsAndReactantAccess") {
    CaveFractureChannelizationInput input;
    input.primaryMask               = 0.9f;
    input.primaryApertureMultiplier = 1.55f;
    input.hydraulicIntensity        = 1.2f;
    input.distanceFromInlet         = 0.15f;
    input.reactantPenetration       = 0.8f;

    const CaveFractureChannelizationSample isolated = sampleCaveFractureChannelization(input);
    CHECK(isolated.erosion > 0.f);
    CHECK(isolated.flowConcentration > 0.f);
    CHECK_EQ(isolated.intersectionAmplification, 0.f);
    CHECK_EQ(isolated.erosion, sampleCaveFractureChannelization(input).erosion);

    input.secondaryMask                                 = 0.8f;
    input.secondaryApertureMultiplier                   = 1.45f;
    const CaveFractureChannelizationSample intersection = sampleCaveFractureChannelization(input);
    CHECK(intersection.intersectionAmplification > 0.f);
    CHECK(intersection.erosion > isolated.erosion);

    input.distanceFromInlet = 1.f;
    CHECK(sampleCaveFractureChannelization(input).reactantAccess < intersection.reactantAccess);
    input.primaryApertureMultiplier = 1.f;
    CHECK_EQ(sampleCaveFractureChannelization(input).erosion, 0.f);
}

TEST_CASE("procgen.mesh.cave.fractureFlowFeedbackIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("bedding", 0.f);
    params.setFloat("erosion", 1.f);
    params.setFloat("fractureDissolution", 1.f);
    params.setFloat("fractureApertureVariability", 0.9f);
    params.setInt("fractureCount", 10);

    MeshBuild   omitted, explicitLegacy, channelized, repeated, inactive;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("fractureFlowFeedback", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("fractureFlowFeedback", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, channelized, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(channelized.positions() != explicitLegacy.positions());
    REQUIRE(channelized.positions() == repeated.positions());
    REQUIRE(channelized.indices() == repeated.indices());
    REQUIRE_EQ(channelized.getMeta("fractureFlowFeedbackModel", ""),
               std::string("cubic-aperture-reactive-channelization-v1"));
    REQUIRE(std::stoi(channelized.getMeta("fractureChannelAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(channelized.getMeta("maximumFractureChannelRetreat", "0")) > 0.f);
    REQUIRE(std::stof(channelized.getMeta("maximumFractureFlowConcentration", "0")) > 0.f);

    params.setFloat("fractureApertureVariability", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, inactive, error));
    REQUIRE_EQ(inactive.getMeta("fractureFlowFeedbackModel", ""), std::string("inactive-no-aperture-contrast"));
    params.setFloat("fractureFlowFeedback", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.stressControlledFracturesAreObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("bedding", 0.f);
    params.setFloat("erosion", 1.f);
    params.setFloat("fractureDissolution", 1.f);
    params.setInt("fractureCount", 9);

    MeshBuild   omitted, explicitLegacy, heterogeneous, stressed, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("fractureApertureVariability", 0.f);
    params.setFloat("fractureStressControl", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("fractureApertureVariability", 0.85f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, heterogeneous, error));
    params.setFloat("fractureStressControl", 0.9f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, stressed, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(heterogeneous.positions() != explicitLegacy.positions());
    REQUIRE(stressed.positions() != heterogeneous.positions());
    REQUIRE(stressed.positions() == repeated.positions());
    REQUIRE(stressed.indices() == repeated.indices());
    REQUIRE_EQ(stressed.getMeta("fractureApertureDistribution", ""), std::string("correlated-lognormal-proxy-v1"));
    REQUIRE_EQ(stressed.getMeta("fractureDissolutionFront", ""), std::string("stress-split-branching-v1"));
    REQUIRE(std::stof(stressed.getMeta("fractureApertureGeometricStdDev", "1")) > 1.f);
    REQUIRE(std::stof(stressed.getMeta("minimumFractureApertureMultiplier", "1")) < 1.f);
    REQUIRE(std::stof(stressed.getMeta("maximumFractureApertureMultiplier", "1")) > 1.f);
    REQUIRE(std::stof(stressed.getMeta("minimumFractureBranchOpenness", "1")) < 1.f);

    params.setFloat("fractureStressControl", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.sedimentBarsFollowFlowAndClastsImbricateDeterministically") {
    const std::vector<CaveSedimentPathPoint> path{{-0.5f, 0.f, 0.f, 0.18f}, {0.5f, 0.f, 0.f, 0.18f}};
    const auto                               first  = createCaveSediment(path, 3, 0.8f, 0.f, 20260830u);
    const auto                               repeat = createCaveSediment(path, 3, 0.8f, 0.f, 20260830u);
    REQUIRE_EQ(first.bars.size(), size_t(3));
    CHECK_EQ(first.clastCount, repeat.clastCount);
    CHECK_EQ(first.depositedVolume, repeat.depositedVolume);
    CHECK_EQ(first.meanImbricationDegrees, repeat.meanImbricationDegrees);
    CHECK(first.clastCount >= 9);
    CHECK(first.depositedVolume > 0.f);
    CHECK(first.meanImbricationDegrees >= 17.f);
    CHECK(first.meanImbricationDegrees <= 28.1f);
    CHECK(std::fabs(first.bars[0].flowX) > 0.99f);
    CHECK(std::fabs(first.bars[0].flowZ) < 0.01f);
    CHECK(first.bars[0].clasts[0].pitch < 0.f);

    const auto& bar = first.bars[0];
    CHECK(addCaveSediment(bar.x, bar.y, bar.z, -1.f, first) > 0.f);
    CHECK(isCaveSedimentSurface(bar.x + bar.flowX * bar.length, bar.y, bar.z + bar.flowZ * bar.length, 0.002f, first));
    CHECK(createCaveSediment(path, 3, 0.f, 0.f, 20260830u).bars.empty());
}

TEST_CASE("procgen.mesh.cave.paragenesisRequiresSedimentAndCarvesCeilingAlongPaleoflow") {
    const std::vector<CaveSedimentPathPoint> path{{-0.5f, 0.f, 0.f, 0.18f}, {0.5f, 0.f, 0.f, 0.18f}};
    const auto                               present = createCaveSediment(path, 3, 0.8f, 0.75f, 20260830u);
    REQUIRE_EQ(present.parageneticChannels, 3);
    CHECK(present.maximumCeilingLift > 0.f);
    CHECK(present.meanParageneticWidth > 0.f);
    const auto& bar = present.bars[0];
    CHECK(bar.channelHalfLength > bar.length);
    CHECK(carveCaveParagenesis(bar.x, bar.ceilingY, bar.z, 1.f, present) < 0.f);
    CHECK(carveCaveParagenesis(bar.x + bar.flowX * bar.channelHalfLength, bar.ceilingY,
                               bar.z + bar.flowZ * bar.channelHalfLength, 1.f, present) < 0.f);
    CHECK(carveCaveParagenesis(bar.x - bar.flowZ * bar.notchHalfWidth * 0.9f, bar.palaeofillY,
                               bar.z + bar.flowX * bar.notchHalfWidth * 0.9f, 1.f, present) < 0.f);
    CHECK(present.meanPalaeofillRatio > 0.6f);
    CHECK(present.maximumNotchRetreat > 0.f);
    CHECK(present.meanNotchThickness > 0.f);
    CHECK(bar.notchHalfHeight > bar.passageRadius * 0.1f * present.paragenesisStrength);

    const auto lowSupply  = createCaveSediment(path, 3, 0.2f, 0.75f, 20260830u);
    const auto highSupply = createCaveSediment(path, 3, 0.9f, 0.75f, 20260830u);
    CHECK(lowSupply.meanParageneticWidth > highSupply.meanParageneticWidth);
    CHECK(lowSupply.maximumCeilingLift < highSupply.maximumCeilingLift);
    CHECK(lowSupply.meanPalaeofillRatio < highSupply.meanPalaeofillRatio);

    const auto absent = createCaveSediment(path, 3, 0.f, 0.75f, 20260830u);
    CHECK(absent.bars.empty());
    CHECK_EQ(absent.parageneticChannels, 0);
    CHECK_EQ(carveCaveParagenesis(0.f, 0.15f, 0.f, 0.25f, absent), 0.25f);
}

TEST_CASE("procgen.mesh.cave.sedimentDepositionIsOptInGroupedAndObservable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 24);
    params.setInt("chambers", 4);
    params.setInt("branches", 2);
    MeshBuild   baseline, first, repeat;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, baseline, error));
    REQUIRE_EQ(baseline.getMeta("sedimentModel", ""), std::string("disabled"));
    params.setFloat("sedimentDeposition", 0.9f);
    params.setInt("sedimentBars", 4);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeat, error));
    REQUIRE(first.positions() == repeat.positions());
    REQUIRE(first.indices() == repeat.indices());
    REQUIRE(first.positions() != baseline.positions());
    REQUIRE_EQ(first.getMeta("sedimentModel", ""), std::string("longitudinal-bar-imbrication-v1"));
    REQUIRE_EQ(std::stoi(first.getMeta("sedimentBars", "0")), 4);
    REQUIRE(std::stoi(first.getMeta("sedimentClasts", "0")) >= 12);
    REQUIRE(std::stof(first.getMeta("sedimentVolume", "0")) > 0.f);
    REQUIRE(std::stof(first.getMeta("meanImbricationDegrees", "0")) > 0.f);
    int sedimentGroup = -1;
    for (int group = 0; group < first.getGroupCount(); ++group)
        if (first.getGroupName(group) == "sediment") sedimentGroup = group;
    REQUIRE(sedimentGroup >= 0);
    const bool hasSedimentGeometry = first.copyGroup(sedimentGroup) != nullptr;
    REQUIRE(hasSedimentGeometry);

    params.setFloat("sedimentDeposition", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.paragenesisIsSedimentDependentOptInAndObservable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 24);
    params.setInt("chambers", 4);
    MeshBuild   baseline, noProvider, sedimentOnly, coupled;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, baseline, error));
    params.setFloat("paragenesis", 0.8f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noProvider, error));
    REQUIRE(noProvider.positions() == baseline.positions());
    REQUIRE_EQ(noProvider.getMeta("paragenesisStatus", ""), std::string("inactive-no-sediment"));
    REQUIRE_EQ(std::stoi(noProvider.getMeta("parageneticChannels", "-1")), 0);

    params.setFloat("paragenesis", 0.f);
    params.setFloat("sedimentDeposition", 0.9f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, sedimentOnly, error));
    params.setFloat("paragenesis", 0.8f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, coupled, error));
    REQUIRE(coupled.positions() != sedimentOnly.positions());
    REQUIRE_EQ(coupled.getMeta("paragenesisStatus", ""), std::string("applied"));
    REQUIRE(std::stoi(coupled.getMeta("parageneticChannels", "0")) > 0);
    REQUIRE(std::stof(coupled.getMeta("maximumParageneticLift", "0")) > 0.f);
    REQUIRE(std::stof(coupled.getMeta("meanParageneticWidth", "0")) > 0.f);
    REQUIRE(std::stof(coupled.getMeta("meanPalaeofillRatio", "0")) > 0.6f);
    REQUIRE(std::stof(coupled.getMeta("maximumAlluvialNotchRetreat", "0")) > 0.f);
    REQUIRE(std::stof(coupled.getMeta("meanAlluvialNotchThickness", "0")) > 0.f);

    params.setFloat("paragenesis", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.breakdownPairsCeilingScarsWithLandedBlocksDeterministically") {
    const std::vector<CaveBreakdownChamber> chambers{{0.1f, 0.f, -0.2f, 0.35f, 0.22f, 0.3f}};
    const auto                              first  = createCaveBreakdown(chambers, 3, 0.8f, 20260830u);
    const auto                              repeat = createCaveBreakdown(chambers, 3, 0.8f, 20260830u);
    REQUIRE_EQ(first.events.size(), size_t(3));
    REQUIRE_EQ(first.blockCount, repeat.blockCount);
    REQUIRE_EQ(first.detachedVolume, repeat.detachedVolume);
    REQUIRE_EQ(first.depositedVolume, repeat.depositedVolume);
    REQUIRE(first.blockCount >= 6);
    REQUIRE(first.detachedVolume > 0.f);
    REQUIRE(first.depositedVolume > 0.f);
    REQUIRE_EQ(first.events[0].x, repeat.events[0].x);
    REQUIRE_EQ(first.events[0].ceilingY, repeat.events[0].ceilingY);
    REQUIRE_EQ(first.events[0].blocks[0].x, repeat.events[0].blocks[0].x);
    REQUIRE(first.events[0].blocks[0].y < first.events[0].ceilingY);

    const auto& event = first.events[0];
    REQUIRE(carveCaveBreakdownScars(event.x, event.ceilingY, event.z, 1.f, first) < 0.f);
    const auto& block = event.blocks[0];
    REQUIRE(addCaveBreakdownBlocks(block.x, block.y, block.z, -1.f, first) > 0.f);
    REQUIRE(isCaveBreakdownBlockSurface(block.x + (block.hx + 0.006f) * std::cos(block.yaw), block.y,
                                        block.z + (block.hx + 0.006f) * std::sin(block.yaw), 0.002f, first));
    REQUIRE(createCaveBreakdown(chambers, 3, 0.f, 20260830u).events.empty());
}

TEST_CASE("procgen.mesh.cave.breakdownIsOptInPairedGroupedAndObservable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 24);
    params.setInt("chambers", 4);
    params.setInt("branches", 2);
    params.setFloat("fragmentDetachment", 1.f);
    MeshBuild   baseline, first, repeat;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, baseline, error));
    REQUIRE_EQ(baseline.getMeta("breakdownModel", ""), std::string("disabled"));
    params.setFloat("breakdown", 0.9f);
    params.setInt("breakdownEvents", 4);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeat, error));
    REQUIRE(first.positions() == repeat.positions());
    REQUIRE(first.indices() == repeat.indices());
    REQUIRE(first.positions() != baseline.positions());
    REQUIRE_EQ(first.getMeta("breakdownModel", ""), std::string("paired-ceiling-spall-talus-v1"));
    REQUIRE_EQ(std::stoi(first.getMeta("breakdownEvents", "0")), 4);
    REQUIRE(std::stoi(first.getMeta("breakdownBlocks", "0")) >= 8);
    REQUIRE(std::stof(first.getMeta("breakdownDetachedVolume", "0")) > 0.f);
    REQUIRE(std::stof(first.getMeta("breakdownDepositedVolume", "0")) > 0.f);
    int breakdownGroup = -1;
    for (int group = 0; group < first.getGroupCount(); ++group)
        if (first.getGroupName(group) == "breakdown") breakdownGroup = group;
    REQUIRE(breakdownGroup >= 0);
    const bool hasBreakdownGeometry = first.copyGroup(breakdownGroup) != nullptr;
    REQUIRE(hasBreakdownGeometry);

    params.setFloat("breakdown", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.differentialVeinErosionIsBoundedProtectedAndDeterministic") {
    CaveDifferentialErosionInput input;
    input.x           = 0.21f;
    input.y           = -0.08f;
    input.z           = 0.37f;
    input.distance    = 0.17f;
    input.radius      = 0.16f;
    input.strength    = 1.f;
    input.seed        = 20260830u;
    const auto first  = sampleCaveDifferentialVeinErosion(input);
    const auto repeat = sampleCaveDifferentialVeinErosion(input);
    CHECK_EQ(first.hostRetreat, repeat.hostRetreat);
    CHECK_EQ(first.veinProtection, repeat.veinProtection);
    CHECK(first.hostRetreat >= 0.f);
    CHECK(first.hostRetreat <= 0.040001f);
    CHECK(first.veinProtection >= 0.f);
    CHECK(first.veinProtection <= 1.f);

    input.distance = 0.8f;
    CHECK_EQ(sampleCaveDifferentialVeinErosion(input).hostRetreat, 0.f);
    input.strength = 0.f;
    CHECK_EQ(sampleCaveDifferentialVeinErosion(input).hostRetreat, 0.f);
}

TEST_CASE("procgen.mesh.cave.differentialVeinErosionIsOptInDistinctAndObservable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 24);
    params.setInt("chambers", 4);
    params.setInt("branches", 2);
    MeshBuild   baseline, first, repeat;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, baseline, error));
    params.setFloat("differentialVeinErosion", 0.9f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeat, error));
    CHECK(first.positions() == repeat.positions());
    CHECK(first.indices() == repeat.indices());
    CHECK(first.positions() != baseline.positions());
    CHECK_EQ(first.getMeta("differentialVeinModel", ""), std::string("resistant-vein-host-retreat-v1"));
    CHECK(std::stoi(first.getMeta("differentialVeinAffectedVoxels", "0")) > 0);
    CHECK(std::stof(first.getMeta("maximumDifferentialVeinRetreat", "0")) > 0.f);
    CHECK(std::stof(first.getMeta("maximumVeinProtection", "0")) > 0.f);

    params.setFloat("differentialVeinErosion", 1.1f);
    MeshBuild invalid;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.condensationFacetsArePlanarBoundedAndDeterministic") {
    CaveFacetInput input;
    input.along       = 0.31f;
    input.angle       = 0.72f;
    input.distance    = 0.17f;
    input.radius      = 0.16f;
    input.strength    = 1.f;
    input.seed        = 20260830u;
    const auto first  = sampleCaveCondensationFacets(input);
    const auto repeat = sampleCaveCondensationFacets(input);
    CHECK_EQ(first.retreat, repeat.retreat);
    CHECK_EQ(first.planarWeight, repeat.planarWeight);
    CHECK(first.facetCount >= 5);
    CHECK(first.facetCount <= 7);
    CHECK(first.retreat > 0.f);
    CHECK(first.retreat <= 0.055001f);

    input.distance = 0.8f;
    CHECK_EQ(sampleCaveCondensationFacets(input).retreat, 0.f);
    input.strength = 0.f;
    CHECK_EQ(sampleCaveCondensationFacets(input).retreat, 0.f);
}

TEST_CASE("procgen.mesh.cave.condensationFacetingIsOptInDistinctAndObservable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 24);
    params.setInt("chambers", 4);
    params.setInt("branches", 2);
    MeshBuild   baseline, first, repeat;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, baseline, error));
    params.setFloat("condensationFaceting", 0.9f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeat, error));
    CHECK(first.positions() == repeat.positions());
    CHECK(first.indices() == repeat.indices());
    CHECK(first.positions() != baseline.positions());
    CHECK_EQ(first.getMeta("condensationFacetModel", ""), std::string("local-convection-planar-envelope-v1"));
    CHECK(std::stoi(first.getMeta("facetAffectedVoxels", "0")) > 0);
    CHECK(std::stof(first.getMeta("maximumFacetRetreat", "0")) > 0.f);

    params.setFloat("condensationFaceting", 1.1f);
    MeshBuild invalid;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

// procgen_cave_surface.cpp
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
