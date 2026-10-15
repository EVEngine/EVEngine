#include "procgen/algorithms/CaveGenerationState.h"

namespace eve::procgen::cave_detail {

void State::metadata(MeshBuild& out) {
    const std::vector<CaveSurfaceOperation> legacySchedule{
        CaveSurfaceOperation::Reactivity, CaveSurfaceOperation::ReactivePatches, CaveSurfaceOperation::Condensation,
        CaveSurfaceOperation::Curvature};
    if (surfaceSchedule != legacySchedule || !depositsApplied || fieldEdited || skeletonEdited) {
        std::string schedule;
        for (const auto operation : surfaceSchedule) {
            if (!schedule.empty()) schedule += ',';
            switch (operation) {
                case CaveSurfaceOperation::Reactivity: schedule += "reactivity"; break;
                case CaveSurfaceOperation::ReactivePatches: schedule += "reactivePatches"; break;
                case CaveSurfaceOperation::Condensation: schedule += "condensation"; break;
                case CaveSurfaceOperation::Curvature: schedule += "curvature"; break;
            }
        }
        out.setMeta("surfaceSchedule", schedule);
        out.setMeta("depositionStage", depositsApplied ? "applied" : "omitted");
        out.setMeta("fieldEdited", fieldEdited ? "true" : "false");
        out.setMeta("skeletonEdited", skeletonEdited ? "true" : "false");
    }
    out.setMeta("algorithm", "mesh.cave");
    out.setMeta("style", style);
    out.setMeta("genesis", genesis);
    out.setMeta("seed", std::to_string(params.getSeed()));
    out.setMeta("chambers", std::to_string(chamberCount));
    out.setMeta("branches", std::to_string(branchCount));
    out.setMeta("chamberHierarchy", std::to_string(chamberHierarchy));
    out.setMeta("passageVariation", std::to_string(passageVariation));
    out.setMeta("chamberIrregularity", std::to_string(chamberIrregularity));
    out.setMeta("macroMorphologyModel",
                chamberHierarchy > 0.f ? "hierarchical-primary-hall-v2" : "uniform-chambers-v1");
    out.setMeta("primaryChamberVerticalRadius", std::to_string(primaryChamberVerticalRadius));
    out.setMeta("cupolas", std::to_string(cupolas.size()));
    out.setMeta("feeders", std::to_string(feeders.size()));
    out.setMeta("risingFlowModel", genesis == "epigene" ? "none" : "feeder-half-tube-cupola-v1");
    out.setMeta(
        "erosionModel",
        biogenicCorrosion > 0.f ? "karst-biogenic-aero-overprint-v10"
        : scallopMaturity > 0.f ? "karst-reactive-coarsening-v9"
        : bendUndercut > 0.f    ? "karst-reactive-curvature-v8"
        : microstructure > 0.f && scallopHydraulicScaling > 0.f
            ? "karst-reactive-microstructure-scallop-v7"
            : (microstructure > 0.f ? "karst-reactive-microstructure-v6"
                                    : (scallopHydraulicScaling > 0.f
                                           ? "karst-hydraulic-scallop-v7"
                                           : (hydraulicErosion > 0.f ? "karst-reactive-network-da-v5"
                                                                     : "karst-fracture-bedding-vadose-scallop-v2"))));
    out.setMeta("erosion", std::to_string(erosion));
    out.setMeta("waterTableCorrosion", std::to_string(waterTableCorrosion));
    out.setMeta("waterTableCorrosionModel", waterTableCorrosion <= 0.f ? "disabled"
                                            : genesis == "hypogene"    ? "inactive-hypogene"
                                                                       : "descending-epiphreatic-belts-v1");
    out.setMeta("waterTableLevel", std::to_string(waterTableLevel));
    out.setMeta("waterTableStages", std::to_string(waterTableStages));
    out.setMeta("waterTableDrop", std::to_string(waterTableDrop));
    out.setMeta("waterTableAffectedVoxels", std::to_string(waterTableAffectedVoxels));
    out.setMeta("maximumWaterTableRetreat", std::to_string(maximumWaterTableRetreat));
    out.setMeta("mixingCorrosion", std::to_string(mixingCorrosion));
    out.setMeta("mixingCorrosionModel", mixingCorrosion <= 0.f ? "disabled"
                                        : mixingSites.empty()  ? "inactive-no-confluence"
                                                               : "chemistry-weighted-confluence-v1");
    out.setMeta("mixingCorrosionSites", std::to_string(mixingSites.size()));
    out.setMeta("mixingCorrosionAffectedVoxels", std::to_string(mixingCorrosionAffectedVoxels));
    out.setMeta("maximumMixingCorrosionRetreat", std::to_string(maximumMixingCorrosionRetreat));
    out.setMeta("lithologicHeterogeneity", std::to_string(lithologicHeterogeneity));
    out.setMeta("lithologyErosionModel",
                lithologicHeterogeneity > 0.f && bedding > 0.f ? "flow-accessible-stylolite-beds-v1" : "disabled");
    out.setMeta("lithologyAffectedVoxels", std::to_string(lithologyAffectedVoxels));
    out.setMeta("styloliteAffectedVoxels", std::to_string(styloliteAffectedVoxels));
    out.setMeta("minimumBedResistance", std::to_string(minimumBedResistance));
    out.setMeta("maximumLithologyRetreat", std::to_string(maximumLithologyRetreat));
    out.setMeta("floodAbrasion", std::to_string(floodAbrasion));
    out.setMeta("sedimentLoad", std::to_string(sedimentLoad));
    out.setMeta("floodAbrasionModel", floodAbrasion <= 0.f    ? "disabled"
                                      : genesis == "hypogene" ? "inactive-hypogene"
                                      : sedimentLoad <= 0.f   ? "inactive-no-tools"
                                                              : "near-bed-tools-cover-vortex-v1");
    out.setMeta("abrasionAffectedVoxels", std::to_string(abrasionAffectedVoxels));
    out.setMeta("maximumAbrasionRetreat", std::to_string(maximumAbrasionRetreat));
    out.setMeta("maximumAbrasionVortex", std::to_string(maximumAbrasionVortex));
    out.setMeta("floodPlucking", std::to_string(floodPlucking));
    out.setMeta("pluckingBlockScale", std::to_string(pluckingBlockScale));
    out.setMeta("floodPluckingModel", floodPlucking <= 0.f    ? "disabled"
                                      : genesis == "hypogene" ? "inactive-hypogene"
                                      : fractures.size() < 2  ? "inactive-no-fracture-network"
                                                              : "thresholded-fracture-block-release-v1");
    out.setMeta("pluckingAffectedVoxels", std::to_string(pluckingAffectedVoxels));
    out.setMeta("maximumPluckingRetreat", std::to_string(maximumPluckingRetreat));
    out.setMeta("maximumPluckingPredisposition", std::to_string(maximumPluckingPredisposition));
    out.setMeta("constrictionScour", std::to_string(constrictionScour));
    out.setMeta("constrictionScourModel", constrictionScour <= 0.f         ? "disabled"
                                          : genesis == "hypogene"          ? "inactive-hypogene"
                                          : constrictionScourSites.empty() ? "inactive-no-constriction"
                                                                           : "discharge-optimal-plunging-flow-cpw-v2");
    out.setMeta("constrictionScourSites", std::to_string(constrictionScourSites.size()));
    out.setMeta("constrictionScourAffectedVoxels", std::to_string(constrictionScourAffectedVoxels));
    out.setMeta("maximumConstrictionScourRetreat", std::to_string(maximumConstrictionScourRetreat));
    out.setMeta("maximumConstrictionRatio", std::to_string(maximumConstrictionRatio));
    out.setMeta("maximumPlungingEfficiency", std::to_string(maximumPlungingEfficiency));
    out.setMeta("knickpointErosion", std::to_string(knickpointErosion));
    out.setMeta("knickpointErosionModel", knickpointErosion <= 0.f  ? "disabled"
                                          : genesis == "hypogene"   ? "inactive-hypogene"
                                          : sedimentLoad <= 0.f     ? "inactive-no-tools"
                                          : knickpointSites.empty() ? "inactive-no-slope-break"
                                                                    : "sediment-driven-headward-plunge-pool-v1");
    out.setMeta("knickpointSites", std::to_string(knickpointSites.size()));
    out.setMeta("knickpointAffectedVoxels", std::to_string(knickpointAffectedVoxels));
    out.setMeta("maximumKnickpointRetreat", std::to_string(maximumKnickpointRetreat));
    out.setMeta("maximumKnickpointSlopeBreak", std::to_string(maximumKnickpointSlopeBreak));
    out.setMeta("maximumKnickpointDrop", std::to_string(maximumKnickpointDrop));
    out.setMeta("streamBedKarren", std::to_string(streamBedKarren));
    out.setMeta("streamBedKarrenModel", streamBedKarren <= 0.f  ? "disabled"
                                        : genesis == "hypogene" ? "inactive-hypogene"
                                        : fractures.size() < 2  ? "inactive-no-crossing-fractures"
                                                                : "lidar-constrained-fracture-guided-bed-karren-v1");
    out.setMeta("streamBedKarrenAffectedVoxels", std::to_string(streamBedKarrenAffectedVoxels));
    out.setMeta("maximumStreamBedKarrenRetreat", std::to_string(maximumStreamBedKarrenRetreat));
    out.setMeta("maximumKarrenFractureGuidance", std::to_string(maximumKarrenFractureGuidance));
    out.setMeta("maximumKarrenIntersectionPocket", std::to_string(maximumKarrenIntersectionPocket));
    out.setMeta("eddyPotholes", std::to_string(eddyPotholes));
    out.setMeta("potholeGravelSize", std::to_string(potholeGravelSize));
    out.setMeta("eddyPotholeModel", eddyPotholes <= 0.f     ? "disabled"
                                    : genesis == "hypogene" ? "inactive-hypogene"
                                    : sedimentLoad <= 0.f   ? "inactive-no-tools"
                                    : fractures.size() < 2  ? "inactive-no-crossing-fractures"
                                    : potholeSites.empty()  ? "inactive-no-fracture-seeded-vortex"
                                                            : "gravel-size-dependent-compound-eddy-pothole-v1");
    out.setMeta("eddyPotholeSites", std::to_string(potholeSites.size()));
    out.setMeta("eddyPotholeAffectedVoxels", std::to_string(potholeAffectedVoxels));
    out.setMeta("maximumPotholeRetreat", std::to_string(maximumPotholeRetreat));
    out.setMeta("maximumPotholeSecondaryErosion", std::to_string(maximumPotholeSecondaryErosion));
    out.setMeta("maximumPotholeFractureIntersection", std::to_string(maximumPotholeFractureIntersection));
    out.setMeta("breakdownScour", std::to_string(breakdownScour));
    out.setMeta("breakdownScourModel", breakdownScour <= 0.f          ? "disabled"
                                       : genesis == "hypogene"        ? "inactive-hypogene"
                                       : sedimentLoad <= 0.f          ? "inactive-no-tools"
                                       : breakdownSet.blockCount <= 0 ? "inactive-no-breakdown-blocks"
                                       : obstacleScourSites.empty()   ? "inactive-no-stream-obstacle"
                                                                      : "roughness-damped-horseshoe-wake-scour-v1");
    out.setMeta("breakdownScourSites", std::to_string(obstacleScourSites.size()));
    out.setMeta("breakdownScourAffectedVoxels", std::to_string(obstacleScourAffectedVoxels));
    out.setMeta("maximumBreakdownScourRetreat", std::to_string(maximumObstacleScourRetreat));
    out.setMeta("maximumHorseshoeScour", std::to_string(maximumHorseshoeScour));
    out.setMeta("maximumWakeScour", std::to_string(maximumWakeScour));
    out.setMeta("minimumObstacleRoughnessRetention", std::to_string(minimumObstacleRoughnessRetention));
    out.setMeta("mineralArmoring", std::to_string(mineralArmoring));
    out.setMeta("mineralArmoringModel", mineralArmoring <= 0.f ? "disabled"
                                        : erosion <= 0.f && biogenicCorrosion <= 0.f && waterTableCorrosion <= 0.f &&
                                                mixingCorrosion <= 0.f && lithologicHeterogeneity <= 0.f &&
                                                condensationFaceting <= 0.f && differentialVeinErosion <= 0.f
                                            ? "inactive-no-chemical-retreat"
                                            : "genesis-supplied-hydraulic-stripping-shield-v1");
    out.setMeta("mineralArmoringAffectedVoxels", std::to_string(mineralArmoringAffectedVoxels));
    out.setMeta("maximumMineralCoatingCoverage", std::to_string(maximumMineralCoatingCoverage));
    out.setMeta("maximumMineralHydraulicRetention", std::to_string(maximumMineralHydraulicRetention));
    out.setMeta("minimumArmoredDissolutionRetention",
                std::to_string(mineralArmoringAffectedVoxels > 0 ? minimumArmoredDissolutionRetention : 1.f));
    out.setMeta("multiscaleRoughness", std::to_string(multiscaleRoughness));
    out.setMeta("wallRoughnessSpectrum",
                multiscaleRoughness > 0.f ? "band-limited-three-scale-v1" : "legacy-single-band");
    out.setMeta("minimumWallRelief", std::to_string(minimumWallRelief));
    out.setMeta("maximumWallRelief", std::to_string(maximumWallRelief));
    out.setMeta("roughnessFlowCoupling", std::to_string(roughnessFlowCoupling));
    out.setMeta("roughnessMassTransferModel",
                roughnessFlowCoupling <= 0.f                     ? "disabled"
                : multiscaleRoughness <= 0.f || roughness <= 0.f ? "inactive-no-resolved-relief"
                : erosion <= 0.f && biogenicCorrosion <= 0.f && waterTableCorrosion <= 0.f && mixingCorrosion <= 0.f &&
                        lithologicHeterogeneity <= 0.f && condensationFaceting <= 0.f && differentialVeinErosion <= 0.f
                    ? "inactive-no-chemical-retreat"
                    : "ridge-exposure-recess-shelter-v1");
    out.setMeta("roughnessTransferAffectedVoxels", std::to_string(roughnessTransferAffectedVoxels));
    out.setMeta("minimumRoughnessTransferMultiplier", std::to_string(minimumRoughnessTransfer));
    out.setMeta("maximumRoughnessTransferMultiplier", std::to_string(maximumRoughnessTransfer));
    out.setMeta("maximumRidgeExposure", std::to_string(maximumRidgeExposure));
    out.setMeta("maximumRecessShelter", std::to_string(maximumRecessShelter));
    out.setMeta("fractureApertureVariability", std::to_string(fractureApertureVariability));
    out.setMeta("fractureApertureDistribution",
                fractureApertureVariability > 0.f ? "correlated-lognormal-proxy-v1" : "uniform");
    out.setMeta("fractureStressControl", std::to_string(fractureStressControl));
    out.setMeta("fractureDissolutionFront",
                fractureStressControl > 0.f ? "stress-split-branching-v1" : "uniform-plane");
    out.setMeta("fractureApertureGeometricStdDev", std::to_string(std::exp(0.55f * fractureApertureVariability)));
    out.setMeta("minimumFractureApertureMultiplier", std::to_string(minimumFractureAperture));
    out.setMeta("maximumFractureApertureMultiplier", std::to_string(maximumFractureAperture));
    out.setMeta("minimumFractureBranchOpenness", std::to_string(minimumFractureBranchOpenness));
    out.setMeta("fractureFlowFeedback", std::to_string(fractureFlowFeedback));
    out.setMeta("fractureFlowFeedbackModel",
                fractureFlowFeedback <= 0.f                    ? "disabled"
                : fractureDissolution <= 0.f || erosion <= 0.f ? "inactive-no-chemical-retreat"
                : fractures.empty()                            ? "inactive-no-fracture-network"
                : fractureApertureVariability <= 0.f           ? "inactive-no-aperture-contrast"
                                                               : "cubic-aperture-reactive-channelization-v1");
    out.setMeta("fractureChannelAffectedVoxels", std::to_string(fractureChannelAffectedVoxels));
    out.setMeta("maximumFractureChannelRetreat", std::to_string(maximumFractureChannelRetreat));
    out.setMeta("maximumFractureFlowConcentration", std::to_string(maximumFractureFlowConcentration));
    out.setMeta("maximumFractureIntersectionAmplification", std::to_string(maximumFractureIntersectionAmplification));
    out.setMeta("minimumFractureReactantAccess",
                std::to_string(fractureChannelAffectedVoxels > 0 ? minimumFractureReactantAccess : 0.f));
    out.setMeta("scallopErosion", std::to_string(scallopErosion));
    out.setMeta("scallopScale", std::to_string(scallopScale));
    out.setMeta("scallopHydraulicScaling", std::to_string(scallopHydraulicScaling));
    out.setMeta("scallopMaturity", std::to_string(scallopMaturity));
    out.setMeta("scallopScaleVariability", std::to_string(scallopScaleVariability));
    out.setMeta("scallopScaleDistribution",
                scallopScaleVariability > 0.f ? "correlated-lognormal-proxy-v1" : "uniform");
    out.setMeta("scallopFlowSeparation", std::to_string(scallopFlowSeparation));
    out.setMeta("scallopFlowProfile", scallopFlowSeparation > 0.f ? "slope-separated-travelling-wave-v2" : "legacy");
    out.setMeta("scallopFlowHistory", std::to_string(scallopFlowHistory));
    out.setMeta("scallopFlowHistoryModel",
                scallopFlowHistory <= 0.f ? "disabled" : "partitioned-base-flood-reversal-overprint-v1");
    out.setMeta("scallopHistoryAffectedVoxels", std::to_string(scallopHistoryAffectedVoxels));
    out.setMeta("maximumYoungerScallopErosion", std::to_string(maximumYoungerScallopErosion));
    out.setMeta("maximumYoungerScallopCoverage", std::to_string(maximumYoungerScallopCoverage));
    out.setMeta("maximumScallopReversalMask", std::to_string(maximumScallopReversalMask));
    out.setMeta("minimumSecondaryScallopScaleRatio",
                std::to_string(scallopHistoryAffectedVoxels > 0 ? minimumSecondaryScallopScaleRatio : 1.f));
    out.setMeta("scallopEvolutionModel", scallopMaturity > 0.f ? "normal-ablation-coarsening-v1" : "stationary-wave");
    out.setMeta("bendUndercut", std::to_string(bendUndercut));
    out.setMeta("bendErosionModel", bendUndercut > 0.f ? "curvature-outer-bank-v1" : "disabled");
    out.setMeta("fragmentDetachment", std::to_string(fragmentDetachment));
    out.setMeta("detachmentModel", fragmentDetachment > 0.f ? "host-rock-connectivity-v1" : "disabled");
    out.setMeta("unsupportedVoxels", std::to_string(detachment.unsupportedVoxels));
    out.setMeta("detachedVoxels", std::to_string(detachment.detachedVoxels));
    out.setMeta("curvatureDissolution", std::to_string(curvatureDissolution));
    out.setMeta("surfaceEvolutionModel", curvatureDissolution > 0.f ? "convex-normal-retreat-v1" : "disabled");
    out.setMeta("depositionAnchoringModel",
                dripstoneCount + flowstoneCount + curtainCount > 0 ? "post-erosion-zero-crossing-v1" : "disabled");
    out.setMeta("anchoredDripstonePairs", std::to_string(anchoredDripstonePairs));
    out.setMeta("rejectedDripstonePairs", std::to_string(rejectedDripstonePairs));
    out.setMeta("anchoredFlowstones", std::to_string(flowstones.size()));
    out.setMeta("rejectedFlowstones", std::to_string(rejectedFlowstones));
    out.setMeta("anchoredCurtains", std::to_string(curtains.size()));
    out.setMeta("rejectedCurtains", std::to_string(rejectedCurtains));
    out.setMeta("curvatureAffectedVoxels", std::to_string(surfaceEvolution.affectedVoxels));
    out.setMeta("maximumCurvatureRetreat", std::to_string(surfaceEvolution.maximumRetreat));
    out.setMeta("totalCurvatureRetreat", std::to_string(surfaceEvolution.totalRetreat));
    out.setMeta("reactiveSurfaceCoupling", std::to_string(reactiveSurfaceCoupling));
    out.setMeta("surfaceSlopeReactivity", std::to_string(surfaceSlopeReactivity));
    out.setMeta("surfaceReactivityModel",
                surfaceSlopeReactivity > 0.f ? "rotation-invariant-normal-dispersion-v1" : "disabled");
    out.setMeta("surfaceReactivityAffectedVoxels", std::to_string(surfaceReactivity.affectedVoxels));
    out.setMeta("maximumSurfaceReactivityRetreat", std::to_string(surfaceReactivity.maximumRetreat));
    out.setMeta("totalSurfaceReactivityRetreat", std::to_string(surfaceReactivity.totalRetreat));
    out.setMeta("maximumSurfaceNormalDispersion", std::to_string(surfaceReactivity.maximumNormalDispersion));
    out.setMeta("reactivePatchiness", std::to_string(reactivePatchiness));
    out.setMeta("reactivePatchModel", reactivePatchiness > 0.f ? "flow-aligned-correlated-psd-v2" : "disabled");
    out.setMeta("reactivePatchAffectedVoxels", std::to_string(reactivePatchEvolution.affectedVoxels));
    out.setMeta("maximumReactivePatchRetreat", std::to_string(reactivePatchEvolution.maximumRetreat));
    out.setMeta("totalReactivePatchRetreat", std::to_string(reactivePatchEvolution.totalRetreat));
    out.setMeta("minimumReactivePatchRate", std::to_string(reactivePatchEvolution.minimumPatchRate));
    out.setMeta("maximumReactivePatchRate", std::to_string(reactivePatchEvolution.maximumPatchRate));
    out.setMeta("reactivePatchNeighborCoherence", std::to_string(reactivePatchEvolution.meanNeighborCoherence));
    out.setMeta("reactivePatchFlowCoherence", std::to_string(reactivePatchEvolution.meanFlowCoherence));
    out.setMeta("reactivePatchTransverseCoherence", std::to_string(reactivePatchEvolution.meanTransverseCoherence));
    out.setMeta("reactivePatchChannelAnisotropy", std::to_string(reactivePatchEvolution.channelAnisotropy));
    out.setMeta("condensationCorrosion", std::to_string(condensationCorrosion));
    out.setMeta("condensationCorrosionModel",
                condensationCorrosion > 0.f ? "cool-wall-co2-film-pitting-v1" : "disabled");
    out.setMeta("condensationAffectedVoxels", std::to_string(condensation.affectedVoxels));
    out.setMeta("maximumCondensationRetreat", std::to_string(condensation.maximumRetreat));
    out.setMeta("totalCondensationRetreat", std::to_string(condensation.totalRetreat));
    out.setMeta("biogenicCorrosion", std::to_string(biogenicCorrosion));
    out.setMeta("biogenicCorrosionModel",
                biogenicCorrosion > 0.f ? "ammonia-nitrification-aero-speleogen-v1" : "disabled");
    out.setMeta("biogenicTransportModel",
                biogenicCorrosion > 0.f ? "passage-airflow-wet-film-protection-v1" : "disabled");
    out.setMeta("biogenicAffectedVoxels", std::to_string(biogenicAffectedVoxels));
    out.setMeta("minimumFluvialScallopRetention", std::to_string(minimumScallopRetention));
    out.setMeta("maximumBiogenicErosion", std::to_string(maximumBiogenicErosion));
    out.setMeta("totalBiogenicErosion", std::to_string(totalBiogenicErosion));
    out.setMeta("condensationFaceting", std::to_string(condensationFaceting));
    out.setMeta("condensationFacetModel",
                condensationFaceting > 0.f ? "local-convection-planar-envelope-v1" : "disabled");
    out.setMeta("condensationFacetCount",
                condensationFaceting > 0.f ? std::to_string(5 + int(params.getSeed() % 3u)) : "0");
    out.setMeta("facetAffectedVoxels", std::to_string(facetAffectedVoxels));
    out.setMeta("maximumFacetRetreat", std::to_string(maximumFacetRetreat));
    out.setMeta("differentialVeinErosion", std::to_string(differentialVeinErosion));
    out.setMeta("differentialVeinModel", differentialVeinErosion > 0.f ? "resistant-vein-host-retreat-v1" : "disabled");
    out.setMeta("differentialVeinAffectedVoxels", std::to_string(differentialVeinAffectedVoxels));
    out.setMeta("maximumDifferentialVeinRetreat", std::to_string(maximumDifferentialVeinRetreat));
    out.setMeta("maximumVeinProtection", std::to_string(maximumVeinProtection));
    out.setMeta("breakdown", std::to_string(breakdown));
    out.setMeta("breakdownModel", breakdown > 0.f ? "paired-ceiling-spall-talus-v1" : "disabled");
    out.setMeta("breakdownEvents", std::to_string(breakdownSet.events.size()));
    out.setMeta("breakdownBlocks", std::to_string(breakdownSet.blockCount));
    out.setMeta("breakdownDetachedVolume", std::to_string(breakdownSet.detachedVolume));
    out.setMeta("breakdownDepositedVolume", std::to_string(breakdownSet.depositedVolume));
    out.setMeta("sedimentDeposition", std::to_string(sedimentDeposition));
    out.setMeta("sedimentModel", sedimentDeposition > 0.f ? "longitudinal-bar-imbrication-v1" : "disabled");
    out.setMeta("sedimentBars", std::to_string(sedimentSet.bars.size()));
    out.setMeta("sedimentClasts", std::to_string(sedimentSet.clastCount));
    out.setMeta("sedimentVolume", std::to_string(sedimentSet.depositedVolume));
    out.setMeta("meanImbricationDegrees", std::to_string(sedimentSet.meanImbricationDegrees));
    out.setMeta("paragenesis", std::to_string(paragenesis));
    out.setMeta("paragenesisModel", paragenesis > 0.f ? "alluvial-notch-ceiling-half-tube-v2" : "disabled");
    out.setMeta("paragenesisStatus",
                paragenesis <= 0.f ? "disabled" : (sedimentSet.bars.empty() ? "inactive-no-sediment" : "applied"));
    out.setMeta("parageneticChannels", std::to_string(sedimentSet.parageneticChannels));
    out.setMeta("maximumParageneticLift", std::to_string(sedimentSet.maximumCeilingLift));
    out.setMeta("meanParageneticWidth", std::to_string(sedimentSet.meanParageneticWidth));
    out.setMeta("meanPalaeofillRatio", std::to_string(sedimentSet.meanPalaeofillRatio));
    out.setMeta("maximumAlluvialNotchRetreat", std::to_string(sedimentSet.maximumNotchRetreat));
    out.setMeta("meanAlluvialNotchThickness", std::to_string(sedimentSet.meanNotchThickness));
    out.setMeta("surfaceRateModel", reactiveSurfaceCoupling > 0.f ? "microstructure-hydraulic-access-v1" : "uniform");
    out.setMeta("minimumSurfaceRate", std::to_string(surfaceEvolution.minimumRateMultiplier));
    out.setMeta("maximumSurfaceRate", std::to_string(surfaceEvolution.maximumRateMultiplier));
    minimumScallopScale = scallopScale * (1.f + scallopHydraulicScaling * (1.f / std::sqrt(hydrology.maximum) - 1.f)) *
                          std::exp(-0.38f * scallopScaleVariability);
    maximumScallopScale = scallopScale * (1.f + scallopHydraulicScaling * (1.f / std::sqrt(hydrology.minimum) - 1.f)) *
                          std::exp(0.38f * scallopScaleVariability);
    out.setMeta("scallopGeometricStdDev", std::to_string(std::exp(0.38f * scallopScaleVariability)));
    out.setMeta("minimumScallopScale", std::to_string(minimumScallopScale));
    out.setMeta("maximumScallopScale", std::to_string(maximumScallopScale));
    out.setMeta("hydraulicErosion", std::to_string(hydraulicErosion));
    out.setMeta("hydraulicGradient", std::to_string(hydraulicGradient));
    out.setMeta("recharge", std::to_string(recharge));
    out.setMeta("flowFocusing", std::to_string(flowFocusing));
    out.setMeta("effectiveDamkohler", std::to_string(damkohler));
    out.setMeta("transportG", std::to_string(transportG));
    out.setMeta("dissolutionRegime", hydrology.dissolutionRegime);
    out.setMeta("reactantPenetration", std::to_string(hydrology.reactantPenetration));
    out.setMeta("microstructure", std::to_string(microstructure));
    out.setMeta("microporosityAccess", std::to_string(microporosityAccess));
    out.setMeta("permeabilityContrast", std::to_string(permeabilityContrast));
    out.setMeta("microstructureModel", microstructure > 0.f ? "dual-scale-accessibility-permeability-v1" : "disabled");
    out.setMeta("hydraulicNetwork", hydraulicErosion > 0.f ? "tributary-confluence-feedback-v1" : "disabled");
    out.setMeta("hydraulicConfluences", std::to_string(branches.size()));
    out.setMeta("minimumFlowWeight", std::to_string(hydrology.minimum));
    out.setMeta("maximumFlowWeight", std::to_string(hydrology.maximum));
    out.setMeta("surfaceRefinement", std::to_string(surfaceRefinement));
    out.setMeta("isosurfaceSampling", std::to_string(isosurfaceSampling));
    out.setMeta("isosurfaceReconstruction", isosurfaceSampling > 1 ? "trilinear-supersample-v1" : "native-grid");
    out.setMeta("extractionResolution",
                std::to_string(extractionNx) + "x" + std::to_string(extractionNy) + "x" + std::to_string(extractionNz));
    out.setMeta("extractionVoxels", std::to_string(extractionVoxels));
    out.setMeta("surfaceProjection", surfaceRefinement > 0 ? "trilinear-newton-v1" : "none");
    out.setMeta("refinementTriangulation", surfaceRefinement == 2
                                               ? "conforming-edge-mask-v2"
                                               : (surfaceRefinement == 1 ? "symmetric-four-way-v1" : "none"));
    out.setMeta("refinementThreshold", std::to_string(refinementThreshold));
    out.setMeta("refinedSourceTriangles", std::to_string(refinedSourceTriangles));
    out.setMeta("adaptiveSplitEdges", std::to_string(adaptiveSplitEdges));
    out.setMeta("dripstones", std::to_string(dripstoneCount));
    out.setMeta("columns", std::to_string(columnCount));
    out.setMeta("flowstones", std::to_string(flowstoneCount));
    out.setMeta("curtains", std::to_string(curtainCount));
    out.setMeta("depositionModel", "damkohler-thin-film-ripple-v2");
    out.setMeta("normalSmoothing", std::to_string(normalSmoothing));
    out.setMeta("surfaceNormalMode", surfaceNormalMode);
    out.setMeta("wetnessRefinement", std::to_string(wetnessRefinement));
    out.setMeta("wetnessModel", wetnessRefinement > 0 ? "gravity-drainage-contour-v2" : "drainage-proximity-v1");
    out.setMeta("wetnessBoundaryTriangles", std::to_string(wetness.boundaryTriangles));
    out.setMeta("wetnessAddedTriangles", std::to_string(wetness.addedTriangles));
    out.setMeta("boundaryClosure", std::to_string(boundaryClosure));
    out.setMeta("boundaryClosureModel", boundaryClosure > 0.f ? "rough-host-envelope-v1" : "open-domain");
    out.setMeta("boundaryAirSamplesBefore", std::to_string(boundary.airSamplesBefore));
    out.setMeta("boundaryAirSamplesAfter", std::to_string(boundary.airSamplesAfter));
    out.setMeta("boundaryClosureChangedVoxels", std::to_string(boundary.changedVoxels));
    out.setMeta("determinism", "bit-exact-cpu");
}

}  // namespace eve::procgen::cave_detail
