#include "CaveFixtures.h"

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
