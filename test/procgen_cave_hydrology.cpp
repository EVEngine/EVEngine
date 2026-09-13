#include "CaveFixtures.h"

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
