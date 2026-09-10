#include "CaveFixtures.h"

TEST_CASE("procgen.mesh.cave.correlatedReactivePatchesAreCoherentSeededAndSurfaceBound") {
    constexpr int                  size = 21;
    std::vector<float>             first(size_t(size * size * size));
    std::vector<float>             rates(first.size(), 1.f);
    std::vector<CaveHydrologyVec3> flow(first.size(), {1.f, 0.f, 0.f});
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float px                                = float(x) / float(size - 1) * 2.f - 1.f;
                first[size_t(x + y * size + z * size * size)] = px;
            }
        }
    }
    std::vector<float> repeated  = first;
    std::vector<float> otherSeed = first;
    const auto result = evolveCaveSurfaceByCorrelatedReactivity(first, rates, flow, size, size, size, 1.f, 20260830u);
    const auto repeatedResult =
        evolveCaveSurfaceByCorrelatedReactivity(repeated, rates, flow, size, size, size, 1.f, 20260830u);
    const auto otherResult =
        evolveCaveSurfaceByCorrelatedReactivity(otherSeed, rates, flow, size, size, size, 1.f, 20260831u);
    CHECK(first == repeated);
    CHECK(first != otherSeed);
    CHECK_EQ(result.totalRetreat, repeatedResult.totalRetreat);
    CHECK(result.affectedVoxels > 0);
    CHECK(otherResult.affectedVoxels > 0);
    CHECK(result.minimumPatchRate < 1.f);
    CHECK(result.maximumPatchRate > 1.f);
    CHECK(result.meanNeighborCoherence > 0.8f);
    CHECK(result.meanFlowCoherence > result.meanTransverseCoherence);
    CHECK(result.channelAnisotropy > 1.5f);
    CHECK_EQ(first.front(), -1.f);
    CHECK_EQ(first.back(), 1.f);
}

TEST_CASE("procgen.mesh.cave.reactivePatchinessIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    MeshBuild   omitted, disabled, patched, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("reactivePatchiness", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, disabled, error));
    REQUIRE(omitted.positions() == disabled.positions());
    REQUIRE(omitted.indices() == disabled.indices());

    params.setFloat("reactivePatchiness", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, patched, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(disabled.positions() != patched.positions());
    REQUIRE(patched.positions() == repeated.positions());
    REQUIRE(patched.indices() == repeated.indices());
    REQUIRE_EQ(patched.getMeta("reactivePatchModel", ""), std::string("flow-aligned-correlated-psd-v2"));
    REQUIRE(std::stoi(patched.getMeta("reactivePatchAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(patched.getMeta("maximumReactivePatchRate", "0")) > 1.f);
    REQUIRE(std::stof(patched.getMeta("minimumReactivePatchRate", "1")) < 1.f);
    REQUIRE(std::stof(patched.getMeta("reactivePatchNeighborCoherence", "0")) > 0.8f);
    REQUIRE(std::stof(patched.getMeta("reactivePatchFlowCoherence", "0")) >
            std::stof(patched.getMeta("reactivePatchTransverseCoherence", "1")));
    REQUIRE(std::stof(patched.getMeta("reactivePatchChannelAnisotropy", "0")) > 1.f);
    params.setFloat("reactivePatchiness", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.surfaceReactivityDistinguishesEdgesFromPlanesRotationInvariantly") {
    constexpr int      size = 15;
    std::vector<float> plane(size_t(size * size * size));
    std::vector<float> corner(size_t(size * size * size));
    std::vector<float> rotatedCorner(size_t(size * size * size));
    std::vector<float> rates(plane.size(), 1.f);
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float  px      = float(x) / float(size - 1) * 2.f - 1.f;
                const float  py      = float(y) / float(size - 1) * 2.f - 1.f;
                const float  pz      = float(z) / float(size - 1) * 2.f - 1.f;
                const size_t index   = size_t(x + y * size + z * size * size);
                plane[index]         = px;
                corner[index]        = std::max(px, py);
                rotatedCorner[index] = std::max((px + py) * 0.70710678f, pz);
            }
        }
    }
    std::vector<float> repeated       = corner;
    const auto         planeResult    = evolveCaveSurfaceByReactivity(plane, rates, size, size, size, 1.f);
    const auto         cornerResult   = evolveCaveSurfaceByReactivity(corner, rates, size, size, size, 1.f);
    const auto         repeatedResult = evolveCaveSurfaceByReactivity(repeated, rates, size, size, size, 1.f);
    const auto         rotatedResult  = evolveCaveSurfaceByReactivity(rotatedCorner, rates, size, size, size, 1.f);
    CHECK_EQ(planeResult.affectedVoxels, 0);
    CHECK(cornerResult.affectedVoxels > 0);
    CHECK(rotatedResult.affectedVoxels > 0);
    CHECK(cornerResult.maximumNormalDispersion > 0.2f);
    CHECK(rotatedResult.maximumNormalDispersion > 0.2f);
    CHECK(corner == repeated);
    CHECK_EQ(cornerResult.affectedVoxels, repeatedResult.affectedVoxels);
    CHECK_EQ(cornerResult.totalRetreat, repeatedResult.totalRetreat);
}

TEST_CASE("procgen.mesh.cave.surfaceSlopeReactivityIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 6);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.18f);
    params.setFloat("multiscaleRoughness", 1.f);
    MeshBuild   omitted, disabled, reactive, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("surfaceSlopeReactivity", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, disabled, error));
    REQUIRE(omitted.positions() == disabled.positions());
    REQUIRE(omitted.indices() == disabled.indices());

    params.setFloat("surfaceSlopeReactivity", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, reactive, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(disabled.positions() != reactive.positions());
    REQUIRE(reactive.positions() == repeated.positions());
    REQUIRE(reactive.indices() == repeated.indices());
    REQUIRE_EQ(reactive.getMeta("surfaceReactivityModel", ""), std::string("rotation-invariant-normal-dispersion-v1"));
    REQUIRE(std::stoi(reactive.getMeta("surfaceReactivityAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(reactive.getMeta("maximumSurfaceNormalDispersion", "0")) > 0.f);
    REQUIRE(std::stof(reactive.getMeta("maximumSurfaceReactivityRetreat", "0")) > 0.f);
    params.setFloat("surfaceSlopeReactivity", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.roughnessTransferExposesRidgesAndSheltersRecesses") {
    const CaveRoughnessTransferSample disabled = sampleCaveRoughnessTransfer({0.8f, 1.f, 0.f});
    CHECK_EQ(disabled.massTransferMultiplier, 1.f);

    const CaveRoughnessTransferSample ridge         = sampleCaveRoughnessTransfer({0.8f, 1.f, 1.f});
    const CaveRoughnessTransferSample recess        = sampleCaveRoughnessTransfer({-0.8f, 1.f, 1.f});
    const CaveRoughnessTransferSample flushedRecess = sampleCaveRoughnessTransfer({-0.8f, 2.f, 1.f});
    CHECK(ridge.massTransferMultiplier > 1.f);
    CHECK(recess.massTransferMultiplier < 1.f);
    CHECK(flushedRecess.massTransferMultiplier > recess.massTransferMultiplier);
    CHECK(ridge.ridgeExposure > 0.f);
    CHECK(recess.recessShelter > 0.f);
    CHECK(ridge.massTransferMultiplier <= 1.45f);
    CHECK(recess.massTransferMultiplier >= 0.60f);
    CHECK_EQ(ridge.massTransferMultiplier, sampleCaveRoughnessTransfer({0.8f, 1.f, 1.f}).massTransferMultiplier);
}

TEST_CASE("procgen.mesh.cave.roughnessFlowCouplingIsObservableAndDefaultCompatible") {
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
    params.setFloat("roughness", 0.16f);
    params.setFloat("multiscaleRoughness", 1.f);
    params.setFloat("erosion", 1.f);
    MeshBuild   omitted, disabled, coupled, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("roughnessFlowCoupling", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, disabled, error));
    REQUIRE(omitted.positions() == disabled.positions());
    REQUIRE(omitted.indices() == disabled.indices());

    params.setFloat("roughnessFlowCoupling", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, coupled, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(disabled.positions() != coupled.positions());
    REQUIRE(coupled.positions() == repeated.positions());
    REQUIRE(coupled.indices() == repeated.indices());
    REQUIRE_EQ(coupled.getMeta("roughnessMassTransferModel", ""), std::string("ridge-exposure-recess-shelter-v1"));
    REQUIRE(std::stoi(coupled.getMeta("roughnessTransferAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(coupled.getMeta("minimumRoughnessTransferMultiplier", "1")) < 1.f);
    REQUIRE(std::stof(coupled.getMeta("maximumRoughnessTransferMultiplier", "1")) > 1.f);

    params.setFloat("multiscaleRoughness", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE_EQ(repeated.getMeta("roughnessMassTransferModel", ""), std::string("inactive-no-resolved-relief"));
    params.setFloat("roughnessFlowCoupling", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.scallopHistoryPreservesOlderReliefAndAddsPartitionedYoungerFlow") {
    CaveScallopInput base;
    base.along              = 0.42f;
    base.angle              = 0.65f;
    base.distance           = 0.17f;
    base.radius             = 0.18f;
    base.hydraulicIntensity = 1.2f;
    base.baseScale          = 0.12f;
    base.flowSeparation     = 0.8f;
    base.seed               = 20260830u;

    const CaveScallopSample        legacy   = sampleCaveScallops(base);
    const CaveScallopHistorySample disabled = sampleCaveScallopHistory({base, 0.f});
    CHECK_EQ(disabled.erosion, legacy.erosion);
    CHECK_EQ(disabled.youngerErosion, 0.f);

    const CaveScallopHistorySample staged = sampleCaveScallopHistory({base, 1.f});
    CHECK(staged.erosion >= legacy.erosion);
    CHECK(staged.youngerCoverage > 0.f);
    CHECK(staged.youngerErosion > 0.f);
    CHECK(staged.secondaryScaleRatio < 1.f);
    CHECK_EQ(staged.erosion, sampleCaveScallopHistory({base, 1.f}).erosion);

    float minimumReversal = 1.f;
    float maximumReversal = 0.f;
    for (int i = 0; i <= 20; ++i) {
        base.along                           = float(i) * 0.08f;
        const CaveScallopHistorySample reach = sampleCaveScallopHistory({base, 1.f});
        minimumReversal                      = std::min(minimumReversal, reach.reversalMask);
        maximumReversal                      = std::max(maximumReversal, reach.reversalMask);
    }
    CHECK(maximumReversal - minimumReversal > 0.35f);
}

TEST_CASE("procgen.mesh.cave.scallopFlowHistoryIsObservableAndDefaultCompatible") {
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
    params.setFloat("erosion", 1.f);
    params.setFloat("bedding", 0.f);
    params.setFloat("fractureDissolution", 0.f);
    params.setFloat("scallopErosion", 1.f);
    params.setFloat("scallopFlowSeparation", 0.8f);

    MeshBuild   omitted, explicitLegacy, staged, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("scallopFlowHistory", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("scallopFlowHistory", 0.85f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, staged, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(staged.positions() != explicitLegacy.positions());
    REQUIRE(staged.positions() == repeated.positions());
    REQUIRE(staged.indices() == repeated.indices());
    REQUIRE_EQ(staged.getMeta("scallopFlowHistoryModel", ""),
               std::string("partitioned-base-flood-reversal-overprint-v1"));
    REQUIRE(std::stoi(staged.getMeta("scallopHistoryAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(staged.getMeta("maximumYoungerScallopErosion", "0")) > 0.f);
    REQUIRE(std::stof(staged.getMeta("minimumSecondaryScallopScaleRatio", "1")) < 1.f);

    params.setFloat("scallopFlowHistory", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.secondaryMineralArmoringIsPatchyProtectiveAndHydraulicallyStripped") {
    CaveMineralArmoringInput input;
    input.passageAlong       = 0.31f;
    input.passageAngle       = 0.72f;
    input.hydraulicIntensity = 0.35f;
    input.mineralSupply      = 1.f;
    input.seed               = 20260830u;

    const CaveMineralArmoringSample sheltered = sampleCaveMineralArmoring(input);
    CHECK(sheltered.coatingCoverage > 0.f);
    CHECK(sheltered.hydraulicRetention > 0.f);
    CHECK(sheltered.dissolutionRetention < 1.f);
    CHECK_EQ(sheltered.dissolutionRetention, sampleCaveMineralArmoring(input).dissolutionRetention);

    input.hydraulicIntensity                = 1.4f;
    const CaveMineralArmoringSample flushed = sampleCaveMineralArmoring(input);
    CHECK_EQ(flushed.coatingCoverage, sheltered.coatingCoverage);
    CHECK(flushed.hydraulicRetention < sheltered.hydraulicRetention);
    CHECK(flushed.dissolutionRetention > sheltered.dissolutionRetention);

    input.mineralSupply                    = 0.f;
    const CaveMineralArmoringSample absent = sampleCaveMineralArmoring(input);
    CHECK_EQ(absent.coatingCoverage, 0.f);
    CHECK_EQ(absent.dissolutionRetention, 1.f);
}

TEST_CASE("procgen.mesh.cave.mineralArmoringIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 1.f);
    params.setFloat("bedding", 0.7f);
    params.setFloat("fractureDissolution", 0.8f);

    MeshBuild   omitted, explicitLegacy, armored, repeated, noChemicalLegacy, inactive;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("mineralArmoring", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("mineralArmoring", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, armored, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(armored.positions() != explicitLegacy.positions());
    REQUIRE(armored.positions() == repeated.positions());
    REQUIRE(armored.indices() == repeated.indices());
    REQUIRE_EQ(armored.getMeta("mineralArmoringModel", ""),
               std::string("genesis-supplied-hydraulic-stripping-shield-v1"));
    REQUIRE(std::stoi(armored.getMeta("mineralArmoringAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(armored.getMeta("maximumMineralCoatingCoverage", "0")) > 0.f);
    REQUIRE(std::stof(armored.getMeta("minimumArmoredDissolutionRetention", "1")) < 1.f);

    params.setFloat("erosion", 0.f);
    params.setFloat("mineralArmoring", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noChemicalLegacy, error));
    params.setFloat("mineralArmoring", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, inactive, error));
    REQUIRE(noChemicalLegacy.positions() == inactive.positions());
    REQUIRE(noChemicalLegacy.indices() == inactive.indices());
    REQUIRE_EQ(inactive.getMeta("mineralArmoringModel", ""), std::string("inactive-no-chemical-retreat"));

    params.setFloat("mineralArmoring", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.breakdownBlocksDriveUpstreamHorseshoeAndDownstreamWakeScour") {
    CaveBreakdownSet   breakdown;
    CaveBreakdownEvent event;
    event.blocks.push_back({0.f, -0.15f, 0.f, 0.07f, 0.05f, 0.06f, 0.f});
    breakdown.events.push_back(event);
    breakdown.blockCount = 1;
    const std::vector<CaveHydrologyPoint>    trunk{{{-0.5f, 0.f, 0.f}, 0.2f}, {{0.5f, 0.f, 0.f}, 0.2f}};
    const std::vector<CaveObstacleScourSite> smooth =
        createCaveObstacleScourSites(breakdown, trunk, {1.2f}, 0.48f, 0.f);
    const std::vector<CaveObstacleScourSite> rough = createCaveObstacleScourSites(breakdown, trunk, {1.2f}, 0.48f, 1.f);
    REQUIRE_EQ(smooth.size(), size_t(1));
    REQUIRE_EQ(rough.size(), size_t(1));
    CHECK(smooth[0].frontCenter.x < smooth[0].blockCenter.x);
    CHECK(smooth[0].wakeCenter.x > smooth[0].blockCenter.x);
    CHECK(smooth[0].erosionPotential > rough[0].erosionPotential);

    CaveHydrologyVec3 frontLobe = smooth[0].frontCenter;
    frontLobe.z += smooth[0].frontLateralRadius * 0.42f;
    const CaveObstacleScourSample front = sampleCaveObstacleScour(frontLobe, smooth);
    const CaveObstacleScourSample wake  = sampleCaveObstacleScour(smooth[0].wakeCenter, smooth);
    CHECK(front.horseshoeScour > 0.f);
    CHECK(wake.wakeScour > 0.f);
    CHECK(front.horseshoeScour > wake.wakeScour);
    CHECK_EQ(front.erosion, sampleCaveObstacleScour(frontLobe, smooth).erosion);

    CHECK(createCaveObstacleScourSites({}, trunk, {1.2f}, 0.48f, 0.f).empty());
    CHECK(createCaveObstacleScourSites(breakdown, trunk, {1.2f}, 0.f, 0.f).empty());
}

TEST_CASE("procgen.mesh.cave.breakdownScourIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(9);  // Produces a mesh-visible effect at resolution 24.
    params.setString("style", "mixed");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 6);
    params.setInt("branches", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);
    params.setFloat("breakdown", 1.f);
    params.setInt("breakdownEvents", 5);
    params.setFloat("sedimentLoad", 0.48f);

    MeshBuild   omitted, explicitLegacy, eroded, repeated, hypogeneOff, hypogeneOn, noBlocksOff, noBlocksOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("breakdownScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("breakdownScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(eroded.positions() != explicitLegacy.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("breakdownScourModel", ""), std::string("roughness-damped-horseshoe-wake-scour-v1"));
    REQUIRE(std::stoi(eroded.getMeta("breakdownScourSites", "0")) > 0);
    REQUIRE(std::stoi(eroded.getMeta("breakdownScourAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(eroded.getMeta("maximumBreakdownScourRetreat", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumHorseshoeScour", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumWakeScour", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("breakdownScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("breakdownScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("breakdownScourModel", ""), std::string("inactive-hypogene"));

    params.setString("genesis", "epigene");
    params.setFloat("breakdown", 0.f);
    params.setFloat("breakdownScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noBlocksOff, error));
    params.setFloat("breakdownScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noBlocksOn, error));
    REQUIRE(noBlocksOff.positions() == noBlocksOn.positions());
    REQUIRE(noBlocksOff.indices() == noBlocksOn.indices());
    REQUIRE_EQ(noBlocksOn.getMeta("breakdownScourModel", ""), std::string("inactive-no-breakdown-blocks"));

    params.setFloat("breakdownScour", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.eddyPotholesRequireCrossingFracturesAndResolveGravelBias") {
    const std::vector<CaveHydrologyPoint> trunk{{{-0.45f, 0.f, 0.f}, 0.2f}, {{0.45f, 0.f, 0.f}, 0.2f}};
    const std::vector<float>              hydraulic{1.2f};
    const std::vector<CaveFracture>       fractures{{1.f, 0.f, 0.f, 0.04f}, {0.f, 1.f, 0.f, 0.04f}};
    const std::vector<CavePotholeSite>    fine =
        createCavePotholeSites(trunk, hydraulic, fractures, 0.f, 0.f, 0.48f, 0.f, 20260830u);
    const std::vector<CavePotholeSite> coarse =
        createCavePotholeSites(trunk, hydraulic, fractures, 0.f, 0.f, 0.48f, 1.f, 20260830u);
    REQUIRE_EQ(fine.size(), size_t(1));
    REQUIRE_EQ(coarse.size(), size_t(1));
    CHECK(fine[0].center.y < 0.f);
    CHECK(fine[0].fractureIntersection > 0.f);

    CaveHydrologyVec3 downstream = fine[0].center;
    downstream.x += fine[0].alongRadius * 0.22f;
    const CavePotholeSample fineDownstream   = sampleCavePotholeErosion(downstream, fine);
    const CavePotholeSample coarseDownstream = sampleCavePotholeErosion(downstream, coarse);
    CHECK(fineDownstream.erosion > coarseDownstream.erosion);
    CHECK(fineDownstream.downstreamBias > 0.f);
    CHECK(coarseDownstream.downstreamBias < 0.f);

    CaveHydrologyVec3 deep = fine[0].center;
    deep.y -= fine[0].depthRadius * 0.72f;
    CHECK(sampleCavePotholeErosion(deep, fine).secondaryPothole > 0.f);
    CHECK(createCavePotholeSites(trunk, hydraulic, {fractures[0]}, 0.f, 0.f, 0.48f, 0.5f, 20260830u).empty());
    CHECK(createCavePotholeSites(trunk, hydraulic, fractures, 0.f, 0.f, 0.f, 0.5f, 20260830u).empty());
}

TEST_CASE("procgen.mesh.cave.eddyPotholesAreObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(1);  // Produces a mesh-visible effect at resolution 24.
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("fractureCount", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);
    params.setFloat("sedimentLoad", 0.48f);

    MeshBuild   omitted, explicitLegacy, eroded, repeated, hypogeneOff, hypogeneOn, noToolsOff, noToolsOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("eddyPotholes", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("eddyPotholes", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(eroded.positions() != explicitLegacy.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("eddyPotholeModel", ""), std::string("gravel-size-dependent-compound-eddy-pothole-v1"));
    REQUIRE(std::stoi(eroded.getMeta("eddyPotholeSites", "0")) > 0);
    REQUIRE(std::stoi(eroded.getMeta("eddyPotholeAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(eroded.getMeta("maximumPotholeRetreat", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumPotholeSecondaryErosion", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumPotholeFractureIntersection", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("eddyPotholes", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("eddyPotholes", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("eddyPotholeModel", ""), std::string("inactive-hypogene"));

    params.setString("genesis", "epigene");
    params.setFloat("sedimentLoad", 0.f);
    params.setFloat("eddyPotholes", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOff, error));
    params.setFloat("eddyPotholes", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOn, error));
    REQUIRE(noToolsOff.positions() == noToolsOn.positions());
    REQUIRE(noToolsOff.indices() == noToolsOn.indices());
    REQUIRE_EQ(noToolsOn.getMeta("eddyPotholeModel", ""), std::string("inactive-no-tools"));

    params.setFloat("eddyPotholes", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.streamBedKarrenRequiresFloorFlowAndFractures") {
    const CaveKarrenSample floor    = sampleCaveStreamKarren({3.1415926535f, 1.2f, 0.9f, 0.65f});
    const CaveKarrenSample roof     = sampleCaveStreamKarren({0.f, 1.2f, 0.9f, 0.65f});
    const CaveKarrenSample dry      = sampleCaveStreamKarren({3.1415926535f, 0.2f, 0.9f, 0.65f});
    const CaveKarrenSample oneSet   = sampleCaveStreamKarren({3.1415926535f, 1.2f, 0.9f, 0.f});
    const CaveKarrenSample crossing = sampleCaveStreamKarren({3.1415926535f, 1.2f, 0.9f, 0.9f});
    CHECK(floor.erosion > 0.f);
    CHECK(floor.floorExposure > 0.99f);
    CHECK(roof.erosion == 0.f);
    CHECK(dry.erosion == 0.f);
    CHECK(crossing.erosion > oneSet.erosion);
    CHECK(crossing.intersectionPocket > 0.f);
    CHECK_EQ(floor.erosion, sampleCaveStreamKarren({3.1415926535f, 1.2f, 0.9f, 0.65f}).erosion);
}

TEST_CASE("procgen.mesh.cave.streamBedKarrenIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("fractureCount", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, eroded, repeated, hypogeneOff, hypogeneOn, noFracturesOff, noFracturesOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("streamBedKarren", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("streamBedKarren", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(eroded.positions() != explicitLegacy.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("streamBedKarrenModel", ""),
               std::string("lidar-constrained-fracture-guided-bed-karren-v1"));
    REQUIRE(std::stoi(eroded.getMeta("streamBedKarrenAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(eroded.getMeta("maximumStreamBedKarrenRetreat", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumKarrenFractureGuidance", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumKarrenIntersectionPocket", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("streamBedKarren", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("streamBedKarren", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("streamBedKarrenModel", ""), std::string("inactive-hypogene"));

    params.setString("genesis", "epigene");
    params.setInt("fractureCount", 1);
    params.setFloat("streamBedKarren", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noFracturesOff, error));
    params.setFloat("streamBedKarren", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noFracturesOn, error));
    REQUIRE(noFracturesOff.positions() == noFracturesOn.positions());
    REQUIRE(noFracturesOff.indices() == noFracturesOn.indices());
    REQUIRE_EQ(noFracturesOn.getMeta("streamBedKarrenModel", ""), std::string("inactive-no-crossing-fractures"));

    params.setFloat("streamBedKarren", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.knickpointsFollowDownstreamSlopeBreaks") {
    const std::vector<CaveHydrologyPoint> trunk{{{-0.8f, 0.10f, 0.f}, 0.2f},
                                                {{-0.4f, 0.08f, 0.f}, 0.2f},
                                                {{0.f, 0.06f, 0.f}, 0.2f},
                                                {{0.4f, -0.20f, 0.f}, 0.2f},
                                                {{0.8f, -0.22f, 0.f}, 0.2f}};
    const std::vector<float>              hydraulic{0.5f, 0.7f, 1.1f, 1.2f, 1.2f};
    const std::vector<CaveKnickpointSite> sites = createCaveKnickpointSites(trunk, hydraulic, 0.48f);
    REQUIRE_EQ(sites.size(), size_t(1));
    CHECK(sites[0].poolCenter.x > sites[0].lip.x);
    CHECK(sites[0].poolCenter.y < sites[0].lip.y);
    CHECK(sites[0].slopeBreak > 0.4f);
    CHECK(sites[0].drop > 0.2f);

    const CaveKnickpointSample pool     = sampleCaveKnickpointErosion(sites[0].poolCenter, sites);
    const CaveKnickpointSample repeated = sampleCaveKnickpointErosion(sites[0].poolCenter, sites);
    const CaveKnickpointSample far      = sampleCaveKnickpointErosion({0.9f, 0.9f, 0.9f}, sites);
    CHECK_EQ(pool.erosion, repeated.erosion);
    CHECK(pool.verticalDrilling > pool.headwallUndercut);
    CHECK(pool.erosion > far.erosion);
    CHECK(createCaveKnickpointSites(trunk, hydraulic, 0.f).empty());

    std::vector<CaveHydrologyPoint> mild = trunk;
    for (size_t i = 0; i < mild.size(); ++i) mild[i].position.y = 0.1f - float(i) * 0.02f;
    CHECK(createCaveKnickpointSites(mild, hydraulic, 0.48f).empty());
    CHECK(createCaveKnickpointSites(trunk, hydraulic, 0.48f)[0].erosionPotential >
          createCaveKnickpointSites(trunk, hydraulic, 1.f)[0].erosionPotential);
}

TEST_CASE("procgen.mesh.cave.knickpointErosionIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);
    params.setFloat("sedimentLoad", 0.48f);

    MeshBuild   omitted, explicitLegacy, eroded, repeated, hypogeneOff, hypogeneOn, noToolsOff, noToolsOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("knickpointErosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("knickpointErosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(eroded.positions() != explicitLegacy.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("knickpointErosionModel", ""), std::string("sediment-driven-headward-plunge-pool-v1"));
    REQUIRE(std::stoi(eroded.getMeta("knickpointSites", "0")) > 0);
    REQUIRE(std::stoi(eroded.getMeta("knickpointAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(eroded.getMeta("maximumKnickpointRetreat", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumKnickpointSlopeBreak", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumKnickpointDrop", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("knickpointErosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("knickpointErosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("knickpointErosionModel", ""), std::string("inactive-hypogene"));

    params.setString("genesis", "epigene");
    params.setFloat("sedimentLoad", 0.f);
    params.setFloat("knickpointErosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOff, error));
    params.setFloat("knickpointErosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOn, error));
    REQUIRE(noToolsOff.positions() == noToolsOn.positions());
    REQUIRE(noToolsOff.indices() == noToolsOn.indices());
    REQUIRE_EQ(noToolsOn.getMeta("knickpointErosionModel", ""), std::string("inactive-no-tools"));

    params.setFloat("knickpointErosion", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.constrictionScourFollowsRadiusMinimumDownstream") {
    const std::vector<CaveHydrologyPoint> trunk{{{-0.8f, 0.10f, 0.f}, 0.22f},
                                                {{-0.4f, 0.06f, 0.f}, 0.21f},
                                                {{0.f, 0.f, 0.f}, 0.11f},
                                                {{0.4f, -0.04f, 0.f}, 0.22f},
                                                {{0.8f, -0.08f, 0.f}, 0.24f}};
    CaveHydrologyWeights                  hydrology;
    hydrology.trunk                                    = {0.7f, 0.8f, 1.1f, 1.2f, 1.25f};
    const std::vector<CaveConstrictionScourSite> sites = createCaveConstrictionScourSites(trunk, {}, hydrology);
    REQUIRE_EQ(sites.size(), size_t(1));
    CHECK(sites[0].poolCenter.x > sites[0].constriction.x);
    CHECK(sites[0].poolCenter.y < sites[0].constriction.y);
    CHECK(sites[0].constrictionRatio > 0.4f);

    const CaveConstrictionScourSample pool     = sampleCaveConstrictionScour(sites[0].poolCenter, sites);
    const CaveConstrictionScourSample repeated = sampleCaveConstrictionScour(sites[0].poolCenter, sites);
    const CaveConstrictionScourSample far      = sampleCaveConstrictionScour({0.9f, 0.9f, 0.9f}, sites);
    CHECK_EQ(pool.erosion, repeated.erosion);
    CHECK(pool.bedScour > 0.f);
    CHECK(pool.erosion > far.erosion);

    std::vector<CaveHydrologyPoint> uniform = trunk;
    for (CaveHydrologyPoint& point : uniform) point.radius = 0.2f;
    CHECK(createCaveConstrictionScourSites(uniform, {}, hydrology).empty());
}

TEST_CASE("procgen.mesh.cave.plungingEfficiencyHasDischargeDependentOptimum") {
    auto makeSite = [&](float ratio, float hydraulicIntensity) {
        const float                           shoulderRadius = 0.2f;
        const std::vector<CaveHydrologyPoint> path{{{-0.8f, 0.f, 0.f}, shoulderRadius},
                                                   {{-0.4f, 0.f, 0.f}, shoulderRadius},
                                                   {{0.f, 0.f, 0.f}, shoulderRadius * (1.f - ratio)},
                                                   {{0.4f, 0.f, 0.f}, shoulderRadius},
                                                   {{0.8f, 0.f, 0.f}, shoulderRadius}};
        CaveHydrologyWeights                  hydrology;
        hydrology.trunk = {hydraulicIntensity, hydraulicIntensity, hydraulicIntensity, hydraulicIntensity,
                           hydraulicIntensity};
        const std::vector<CaveConstrictionScourSite> sites = createCaveConstrictionScourSites(path, {}, hydrology);
        REQUIRE_EQ(sites.size(), size_t(1));
        return sites.front();
    };
    const CaveConstrictionScourSite lowOptimal         = makeSite(0.35f, 0.25f);
    const CaveConstrictionScourSite lowOverConstricted = makeSite(0.75f, 0.25f);
    CHECK(lowOptimal.plungingEfficiency > lowOverConstricted.plungingEfficiency * 4.f);
    CHECK(std::fabs(lowOptimal.optimalConstriction - 0.35f) < 1e-5f);

    const CaveConstrictionScourSite highAtFifty      = makeSite(0.50f, 1.6f);
    const CaveConstrictionScourSite highAtThirtyFive = makeSite(0.35f, 1.6f);
    CHECK(highAtFifty.plungingEfficiency > highAtThirtyFive.plungingEfficiency);
    CHECK(std::fabs(highAtFifty.optimalConstriction - 0.50f) < 1e-5f);
}

TEST_CASE("procgen.mesh.cave.constrictionScourIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, scoured, repeated, hypogeneOff, hypogeneOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("constrictionScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("constrictionScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, scoured, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(scoured.positions() != explicitLegacy.positions());
    REQUIRE(scoured.positions() == repeated.positions());
    REQUIRE(scoured.indices() == repeated.indices());
    REQUIRE_EQ(scoured.getMeta("constrictionScourModel", ""), std::string("discharge-optimal-plunging-flow-cpw-v2"));
    REQUIRE(std::stoi(scoured.getMeta("constrictionScourSites", "0")) > 0);
    REQUIRE(std::stoi(scoured.getMeta("constrictionScourAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(scoured.getMeta("maximumConstrictionScourRetreat", "0")) > 0.f);
    REQUIRE(std::stof(scoured.getMeta("maximumConstrictionRatio", "0")) > 0.f);
    REQUIRE(std::stof(scoured.getMeta("maximumPlungingEfficiency", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("constrictionScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("constrictionScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("constrictionScourModel", ""), std::string("inactive-hypogene"));

    params.setFloat("constrictionScour", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.floodPluckingRequiresFracturesFlowAndSelectedBlocks") {
    CavePluckingInput  input{0.035f, -0.012f, 0.041f, 3.1415926535f, 1.2f, 1.f, 0.85f, 0.11f, 20260830u};
    CavePluckingSample selected;
    for (int z = -12; z <= 12 && selected.blockMask <= 0.1f; ++z) {
        for (int x = -12; x <= 12 && selected.blockMask <= 0.1f; ++x) {
            input.x                            = float(x) * 0.025f;
            input.z                            = float(z) * 0.025f;
            const CavePluckingSample candidate = sampleCaveFloodPlucking(input);
            if (candidate.blockMask > selected.blockMask) selected = candidate;
        }
    }
    REQUIRE(selected.blockMask > 0.1f);
    CHECK(selected.erosion > 0.f);
    CHECK(selected.fracturePredisposition > 0.f);
    CHECK(selected.hydraulicActivation > 0.f);

    const CavePluckingSample repeated = sampleCaveFloodPlucking(input);
    CHECK_EQ(repeated.erosion, sampleCaveFloodPlucking(input).erosion);

    CavePluckingInput unfractured     = input;
    unfractured.primaryFractureMask   = 0.f;
    unfractured.secondaryFractureMask = 0.f;
    CHECK_EQ(sampleCaveFloodPlucking(unfractured).erosion, 0.f);

    CavePluckingInput lowFlow  = input;
    lowFlow.hydraulicIntensity = 0.1f;
    CHECK_EQ(sampleCaveFloodPlucking(lowFlow).erosion, 0.f);
}
