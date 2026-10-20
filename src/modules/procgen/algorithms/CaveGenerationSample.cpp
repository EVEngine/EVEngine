#include "procgen/algorithms/CaveGenerationState.h"

namespace eve::procgen::cave_detail {

Result<void> State::sample() {
    density                                  = std::vector<float>(size_t(nx) * size_t(ny) * size_t(nz), 1.f);
    reactiveRate                             = std::vector<float>(density.size(), 1.f);
    hydraulicExposure                        = std::vector<float>(density.size(), 1.f);
    flowDirection                            = std::vector<CaveHydrologyVec3>(density.size(), {1.f, 0.f, 0.f});
    biogenicAffectedVoxels                   = 0;
    minimumScallopRetention                  = 1.f;
    maximumBiogenicErosion                   = 0.f;
    totalBiogenicErosion                     = 0.f;
    facetAffectedVoxels                      = 0;
    maximumFacetRetreat                      = 0.f;
    differentialVeinAffectedVoxels           = 0;
    maximumDifferentialVeinRetreat           = 0.f;
    maximumVeinProtection                    = 0.f;
    minimumFractureAperture                  = 1.f;
    maximumFractureAperture                  = 1.f;
    minimumFractureBranchOpenness            = 1.f;
    fractureChannelAffectedVoxels            = 0;
    maximumFractureChannelRetreat            = 0.f;
    maximumFractureFlowConcentration         = 0.f;
    maximumFractureIntersectionAmplification = 0.f;
    minimumFractureReactantAccess            = 1.4f;
    minimumWallRelief                        = 1.f;
    maximumWallRelief                        = -1.f;
    roughnessTransferAffectedVoxels          = 0;
    minimumRoughnessTransfer                 = 1.f;
    maximumRoughnessTransfer                 = 1.f;
    maximumRidgeExposure                     = 0.f;
    maximumRecessShelter                     = 0.f;
    waterTableAffectedVoxels                 = 0;
    maximumWaterTableRetreat                 = 0.f;
    mixingCorrosionAffectedVoxels            = 0;
    maximumMixingCorrosionRetreat            = 0.f;
    lithologyAffectedVoxels                  = 0;
    styloliteAffectedVoxels                  = 0;
    minimumBedResistance                     = 1.f;
    maximumLithologyRetreat                  = 0.f;
    abrasionAffectedVoxels                   = 0;
    maximumAbrasionRetreat                   = 0.f;
    maximumAbrasionVortex                    = 0.f;
    pluckingAffectedVoxels                   = 0;
    maximumPluckingRetreat                   = 0.f;
    maximumPluckingPredisposition            = 0.f;
    constrictionScourAffectedVoxels          = 0;
    maximumConstrictionScourRetreat          = 0.f;
    maximumConstrictionRatio                 = 0.f;
    maximumPlungingEfficiency                = 0.f;
    knickpointAffectedVoxels                 = 0;
    maximumKnickpointRetreat                 = 0.f;
    maximumKnickpointSlopeBreak              = 0.f;
    maximumKnickpointDrop                    = 0.f;
    streamBedKarrenAffectedVoxels            = 0;
    maximumStreamBedKarrenRetreat            = 0.f;
    maximumKarrenFractureGuidance            = 0.f;
    maximumKarrenIntersectionPocket          = 0.f;
    potholeAffectedVoxels                    = 0;
    maximumPotholeRetreat                    = 0.f;
    maximumPotholeSecondaryErosion           = 0.f;
    maximumPotholeFractureIntersection       = 0.f;
    obstacleScourAffectedVoxels              = 0;
    maximumObstacleScourRetreat              = 0.f;
    maximumHorseshoeScour                    = 0.f;
    maximumWakeScour                         = 0.f;
    minimumObstacleRoughnessRetention        = 1.f;
    mineralArmoringAffectedVoxels            = 0;
    maximumMineralCoatingCoverage            = 0.f;
    maximumMineralHydraulicRetention         = 0.f;
    minimumArmoredDissolutionRetention       = 1.f;
    scallopHistoryAffectedVoxels             = 0;
    maximumYoungerScallopErosion             = 0.f;
    maximumYoungerScallopCoverage            = 0.f;
    maximumScallopReversalMask               = 0.f;
    minimumSecondaryScallopScaleRatio        = 1.f;
    auto carvePath                           = [](Vec3 p, const std::vector<PassagePoint>& path, float current) {
        for (size_t i = 1; i < path.size(); ++i) {
            const float radius = 0.5f * (path[i - 1].radius + path[i].radius);
            current = std::min(current, distanceToSegment(p, path[i - 1].position, path[i].position) - radius);
        }
        return current;
    };
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const Vec3 p{float(x) / float(nx - 1) * 2.f - 1.f, float(y) / float(ny - 1) * 2.f - 1.f,
                             float(z) / float(nz - 1) * 2.f - 1.f};
                float      d = carvePath(p, spine, 1.f);
                for (const auto& branch : branches) d = carvePath(p, branch.points, d);
                CavePassageFrame passage = nearestCavePassageFrame(p, spine, hydrology.trunk);
                for (size_t branch = 0; branch < branches.size(); ++branch) {
                    const CavePassageFrame candidate =
                        nearestCavePassageFrame(p, branches[branch].points, hydrology.branches[branch]);
                    if (candidate.distance < passage.distance) passage = candidate;
                }

                // After a phreatic tube drains, gravity-driven streams incise a narrower
                // canyon into its floor.  This second connected carve preserves traversability.
                const float effectiveVadose = genesis == "hypogene" ? 0.f : vadoseIncision;
                if (effectiveVadose > 0.f) {
                    const Vec3 incisedPoint{p.x, p.y + tunnelRadius * (0.55f + 0.55f * vadoseIncision), p.z};
                    float      incision = 1.f;
                    for (size_t i = 1; i < spine.size(); ++i) {
                        const float radius = 0.5f * (spine[i - 1].radius + spine[i].radius) *
                                             (0.38f + 0.22f * vadoseIncision) * std::sqrt(hydrology.trunk[i - 1]);
                        incision = std::min(
                            incision,
                            distanceToSegment(incisedPoint, spine[i - 1].position, spine[i].position) - radius);
                    }
                    for (size_t branch = 0; branch < branches.size(); ++branch) {
                        const auto& path = branches[branch].points;
                        for (size_t i = 1; i < path.size(); ++i) {
                            const float radius = 0.5f * (path[i - 1].radius + path[i].radius) *
                                                 (0.30f + 0.18f * vadoseIncision) *
                                                 std::sqrt(hydrology.branches[branch][i - 1]);
                            incision = std::min(
                                incision,
                                distanceToSegment(incisedPoint, path[i - 1].position, path[i].position) - radius);
                        }
                    }
                    d = std::min(d, incision);
                }
                for (const Chamber& chamber : chambers) {
                    d = std::min(d, ellipsoidDistance(p, chamber));
                }
                for (const Chamber& cupola : cupolas) d = std::min(d, ellipsoidDistance(p, cupola));
                for (const RisingConduit& feeder : feeders)
                    d = std::min(d, distanceToSegment(p, feeder.start, feeder.end) - feeder.radius);
                for (const RisingConduit& channel : ceilingChannels)
                    d = std::min(d, distanceToSegment(p, channel.start, channel.end) - channel.radius);
                const float legacyStrata =
                    latticeNoise((p.x + 2.f) * 5.5f, (p.y + 3.f) * 3.2f, (p.z + 5.f) * 5.5f, params.getSeed());
                float strata = legacyStrata;
                if (multiscaleRoughness > 0.f) {
                    const CaveRoughnessSample spectrum = sampleCaveWallRoughness({p.x, p.y, p.z, params.getSeed()});
                    strata = legacyStrata * (1.f - multiscaleRoughness) + spectrum.relief * multiscaleRoughness;
                }
                minimumWallRelief = std::min(minimumWallRelief, strata);
                maximumWallRelief = std::max(maximumWallRelief, strata);
                d += strata * roughness * 0.32f;
                d = carveCaveBreakdownScars(p.x, p.y, p.z, d, breakdownSet);
                d = carveCaveParagenesis(p.x, p.y, p.z, d, sedimentSet);

                // Apply dissolution only in a shell around the connected void.  Bedding bands
                // produce horizontal notches, joints produce tall slots, and positive fine
                // noise creates solution pockets/scallops instead of uniformly inflating rock.
                const float shell = 1.f - smoothstep(0.025f, 0.22f, std::fabs(d));
                const float beddingWarp =
                    latticeNoise(p.x * 2.3f, p.y * 1.1f, p.z * 2.3f, params.getSeed() ^ 0x51a7u) * 0.12f;
                const float beddingWave               = std::fabs(std::sin((p.y + beddingWarp) * 8.f * 3.1415926535f));
                const float beddingMask               = 1.f - smoothstep(0.05f, 0.42f, beddingWave);
                float       fractureMask              = 0.f;
                float       secondaryFractureMask     = 0.f;
                float       fractureAperture          = 1.f;
                float       secondaryFractureAperture = 1.f;
                for (const CaveFracture& fracture : fractures) {
                    const CaveFractureSample sample =
                        sampleCaveFracture({p.x, p.y, p.z, fracture, fractureApertureVariability, fractureStressControl,
                                            params.getSeed()});
                    if (sample.mask > fractureMask) {
                        secondaryFractureMask     = fractureMask;
                        secondaryFractureAperture = fractureAperture;
                        fractureMask              = sample.mask;
                        fractureAperture          = sample.apertureMultiplier;
                    } else {
                        if (sample.mask > secondaryFractureMask) {
                            secondaryFractureMask     = sample.mask;
                            secondaryFractureAperture = sample.apertureMultiplier;
                        }
                    }
                    minimumFractureAperture       = std::min(minimumFractureAperture, sample.apertureMultiplier);
                    maximumFractureAperture       = std::max(maximumFractureAperture, sample.apertureMultiplier);
                    minimumFractureBranchOpenness = std::min(minimumFractureBranchOpenness, sample.branchOpenness);
                }
                const float pockets =
                    std::max(0.f, latticeNoise(p.x * 15.f, p.y * 11.f, p.z * 15.f, params.getSeed() ^ 0xb5297a4du));
                // Fowler's boundary-layer model motivates travelling, cusp-edged scallops.
                // Local flow and passage curvature add hydraulic scale and outer-bank bias.
                const CaveScallopHistorySample scallops = sampleCaveScallopHistory(
                    {{passage.along, passage.angle, passage.distance, passage.radius, passage.hydraulicIntensity,
                      scallopScale, scallopHydraulicScaling, scallopMaturity, bendUndercut, passage.bendStrength,
                      passage.outerBankAngle, scallopScaleVariability, scallopFlowSeparation, params.getSeed()},
                     scallopFlowHistory});
                if (scallops.youngerErosion > 1e-6f) {
                    ++scallopHistoryAffectedVoxels;
                    maximumYoungerScallopErosion  = std::max(maximumYoungerScallopErosion, scallops.youngerErosion);
                    maximumYoungerScallopCoverage = std::max(maximumYoungerScallopCoverage, scallops.youngerCoverage);
                    maximumScallopReversalMask    = std::max(maximumScallopReversalMask, scallops.reversalMask);
                    minimumSecondaryScallopScaleRatio =
                        std::min(minimumSecondaryScallopScaleRatio, scallops.secondaryScaleRatio);
                }
                const CaveBiogenicCorrosionSample biogenic =
                    sampleCaveBiogenicCorrosion({passage.along, passage.angle, passage.distance, passage.radius,
                                                 passage.hydraulicIntensity, biogenicCorrosion, params.getSeed()});
                const CaveFacetSample facets =
                    sampleCaveCondensationFacets({passage.along, passage.angle, passage.distance, passage.radius,
                                                  condensationFaceting, params.getSeed()});
                const CaveDifferentialErosionSample differentialVeins = sampleCaveDifferentialVeinErosion(
                    {p.x, p.y, p.z, passage.distance, passage.radius, differentialVeinErosion, params.getSeed()});
                const float mineralSupply = genesis == "hypogene" ? 1.f : (genesis == "mixed" ? 0.72f : 0.28f);
                CaveMineralArmoringSample armoring;
                const bool                chemicalArmoringPotential =
                    erosion > 0.f || biogenicCorrosion > 0.f || waterTableCorrosion > 0.f || mixingCorrosion > 0.f ||
                    lithologicHeterogeneity > 0.f || condensationFaceting > 0.f || differentialVeinErosion > 0.f;
                if (mineralArmoring > 0.f && chemicalArmoringPotential) {
                    armoring = sampleCaveMineralArmoring(
                        {passage.along, passage.angle, passage.hydraulicIntensity, mineralSupply, params.getSeed()});
                }
                const float chemicalRetention = 1.f - mineralArmoring * (1.f - armoring.dissolutionRetention);
                CaveRoughnessTransferSample roughnessTransfer;
                if (roughnessFlowCoupling > 0.f && multiscaleRoughness > 0.f && roughness > 0.f &&
                    chemicalArmoringPotential) {
                    roughnessTransfer =
                        sampleCaveRoughnessTransfer({strata, passage.hydraulicIntensity, roughnessFlowCoupling});
                }
                const float chemicalMassTransfer = roughnessTransfer.massTransferMultiplier;
                if (shell > 1e-4f && std::fabs(chemicalMassTransfer - 1.f) > 1e-6f) {
                    ++roughnessTransferAffectedVoxels;
                    minimumRoughnessTransfer = std::min(minimumRoughnessTransfer, chemicalMassTransfer);
                    maximumRoughnessTransfer = std::max(maximumRoughnessTransfer, chemicalMassTransfer);
                    maximumRidgeExposure     = std::max(maximumRidgeExposure, roughnessTransfer.ridgeExposure);
                    maximumRecessShelter     = std::max(maximumRecessShelter, roughnessTransfer.recessShelter);
                }
                CaveWaterTableSample waterTable;
                if (waterTableCorrosion > 0.f && genesis != "hypogene") {
                    waterTable = sampleCaveWaterTableCorrosion({p.y, passage.angle, passage.along, waterTableLevel,
                                                                waterTableDrop, waterTableFluctuation, waterTableStages,
                                                                params.getSeed()});
                }
                CaveMixingCorrosionSample mixing;
                if (mixingCorrosion > 0.f) mixing = sampleCaveMixingCorrosion(p, mixingSites);
                const CaveLithologySample lithology =
                    sampleCaveLithology({p.x, p.y, p.z, passage.along, passage.hydraulicIntensity,
                                         lithologicHeterogeneity, params.getSeed()});
                CaveAbrasionSample abrasion;
                if (floodAbrasion > 0.f && genesis != "hypogene") {
                    abrasion = sampleCaveFloodAbrasion({passage.along, passage.angle, passage.hydraulicIntensity,
                                                        passage.bendStrength, passage.outerBankAngle, sedimentLoad,
                                                        params.getSeed()});
                }
                CavePluckingSample plucking;
                if (floodPlucking > 0.f && genesis != "hypogene" && fractures.size() >= 2) {
                    plucking =
                        sampleCaveFloodPlucking({p.x, p.y, p.z, passage.angle, passage.hydraulicIntensity, fractureMask,
                                                 secondaryFractureMask, pluckingBlockScale, params.getSeed()});
                }
                CaveConstrictionScourSample constriction;
                if (constrictionScour > 0.f && genesis != "hypogene") {
                    constriction = sampleCaveConstrictionScour(p, constrictionScourSites);
                }
                CaveKnickpointSample knickpoint;
                if (knickpointErosion > 0.f && genesis != "hypogene") {
                    knickpoint = sampleCaveKnickpointErosion(p, knickpointSites);
                }
                CaveKarrenSample karren;
                if (streamBedKarren > 0.f && genesis != "hypogene" && fractures.size() >= 2) {
                    karren = sampleCaveStreamKarren(
                        {passage.angle, passage.hydraulicIntensity, fractureMask, secondaryFractureMask});
                }
                CavePotholeSample pothole;
                if (eddyPotholes > 0.f && genesis != "hypogene") {
                    pothole = sampleCavePotholeErosion(p, potholeSites);
                }
                CaveObstacleScourSample obstacleScour;
                if (breakdownScour > 0.f && genesis != "hypogene") {
                    obstacleScour = sampleCaveObstacleScour(p, obstacleScourSites);
                }
                CaveFractureChannelizationSample fractureChannel;
                if (fractureFlowFeedback > 0.f && fractureApertureVariability > 0.f) {
                    fractureChannel = sampleCaveFractureChannelization(
                        {fractureMask, secondaryFractureMask, fractureAperture, secondaryFractureAperture,
                         passage.hydraulicIntensity, passage.along, hydrology.reactantPenetration});
                }
                if (facets.retreat > 1e-6f) {
                    ++facetAffectedVoxels;
                    maximumFacetRetreat = std::max(maximumFacetRetreat, facets.retreat);
                }
                if (biogenic.erosion > 1e-6f) {
                    ++biogenicAffectedVoxels;
                    minimumScallopRetention = std::min(minimumScallopRetention, biogenic.fluvialScallopRetention);
                    maximumBiogenicErosion  = std::max(maximumBiogenicErosion, biogenic.erosion);
                    totalBiogenicErosion += biogenic.erosion;
                }
                if (differentialVeins.hostRetreat > 1e-6f) {
                    ++differentialVeinAffectedVoxels;
                    maximumDifferentialVeinRetreat =
                        std::max(maximumDifferentialVeinRetreat, differentialVeins.hostRetreat);
                    maximumVeinProtection = std::max(maximumVeinProtection, differentialVeins.veinProtection);
                }
                const float dissolution = bedding * beddingMask * 0.055f + fractureDissolution * fractureMask * 0.075f +
                                          pockets * 0.032f +
                                          scallopErosion * scallops.erosion * biogenic.fluvialScallopRetention *
                                              0.045f * (genesis == "hypogene" ? 0.35f : 1.f);
                const CaveMicrostructureSample rock = sampleCaveMicrostructure(
                    p.x, p.y, p.z, params.getSeed(), microstructure, microporosityAccess, permeabilityContrast);
                // Preferential paths transport fresher undersaturated water. Their local
                // permeability co-evolves with accessible reactive surface instead of
                // treating every limestone voxel as chemically identical.
                const float flowWeightedDissolution =
                    dissolution * rock.reactiveSurface *
                    std::clamp(passage.hydraulicIntensity * rock.permeability, 0.2f, 1.7f);
                const float biogenicDissolution = biogenic.erosion * rock.reactiveSurface * 0.052f;
                d -= shell * chemicalRetention * chemicalMassTransfer *
                     (erosion * flowWeightedDissolution + biogenicDissolution);
                if (shell > 1e-4f && chemicalRetention < 0.999999f) {
                    ++mineralArmoringAffectedVoxels;
                    maximumMineralCoatingCoverage = std::max(maximumMineralCoatingCoverage, armoring.coatingCoverage);
                    maximumMineralHydraulicRetention =
                        std::max(maximumMineralHydraulicRetention, armoring.hydraulicRetention);
                    minimumArmoredDissolutionRetention =
                        std::min(minimumArmoredDissolutionRetention, chemicalRetention);
                }
                const float fractureChannelRetreat = shell * chemicalMassTransfer * erosion * fractureDissolution *
                                                     fractureFlowFeedback * fractureChannel.erosion * 0.062f;
                d -= fractureChannelRetreat;
                if (fractureChannelRetreat > 1e-6f) {
                    ++fractureChannelAffectedVoxels;
                    maximumFractureChannelRetreat = std::max(maximumFractureChannelRetreat, fractureChannelRetreat);
                    maximumFractureFlowConcentration =
                        std::max(maximumFractureFlowConcentration, fractureChannel.flowConcentration);
                    maximumFractureIntersectionAmplification =
                        std::max(maximumFractureIntersectionAmplification, fractureChannel.intersectionAmplification);
                    minimumFractureReactantAccess =
                        std::min(minimumFractureReactantAccess, fractureChannel.reactantAccess);
                }
                const float waterTableRetreat = shell * chemicalRetention * chemicalMassTransfer * waterTableCorrosion *
                                                waterTable.erosion * 0.065f;
                d -= waterTableRetreat;
                if (waterTableRetreat > 1e-6f) {
                    ++waterTableAffectedVoxels;
                    maximumWaterTableRetreat = std::max(maximumWaterTableRetreat, waterTableRetreat);
                }
                const float mixingRetreat =
                    shell * chemicalRetention * chemicalMassTransfer * mixingCorrosion * mixing.erosion * 0.07f;
                d -= mixingRetreat;
                if (mixingRetreat > 1e-6f) {
                    ++mixingCorrosionAffectedVoxels;
                    maximumMixingCorrosionRetreat = std::max(maximumMixingCorrosionRetreat, mixingRetreat);
                }
                const float lithologyRetreat =
                    shell * chemicalRetention * chemicalMassTransfer * bedding * lithology.retreat * 0.052f;
                d -= lithologyRetreat;
                if (lithologyRetreat > 1e-6f) {
                    ++lithologyAffectedVoxels;
                    if (lithology.styloliteMask > 0.05f) ++styloliteAffectedVoxels;
                    minimumBedResistance    = std::min(minimumBedResistance, lithology.bedResistance);
                    maximumLithologyRetreat = std::max(maximumLithologyRetreat, lithologyRetreat);
                }
                const float abrasionRetreat = shell * floodAbrasion * abrasion.erosion * 0.058f;
                d -= abrasionRetreat;
                if (abrasionRetreat > 1e-6f) {
                    ++abrasionAffectedVoxels;
                    maximumAbrasionRetreat = std::max(maximumAbrasionRetreat, abrasionRetreat);
                    maximumAbrasionVortex  = std::max(maximumAbrasionVortex, abrasion.vortexMask);
                }
                const float pluckingRetreat = shell * floodPlucking * plucking.erosion * 0.082f;
                d -= pluckingRetreat;
                if (pluckingRetreat > 1e-6f) {
                    ++pluckingAffectedVoxels;
                    maximumPluckingRetreat = std::max(maximumPluckingRetreat, pluckingRetreat);
                    maximumPluckingPredisposition =
                        std::max(maximumPluckingPredisposition, plucking.fracturePredisposition);
                }
                const float constrictionRetreat = shell * constrictionScour * constriction.erosion * 0.115f;
                d -= constrictionRetreat;
                if (constrictionRetreat > 1e-6f) {
                    ++constrictionScourAffectedVoxels;
                    maximumConstrictionScourRetreat = std::max(maximumConstrictionScourRetreat, constrictionRetreat);
                    maximumConstrictionRatio  = std::max(maximumConstrictionRatio, constriction.constrictionRatio);
                    maximumPlungingEfficiency = std::max(maximumPlungingEfficiency, constriction.plungingEfficiency);
                }
                const float knickpointRetreat = shell * knickpointErosion * knickpoint.erosion * 0.12f;
                d -= knickpointRetreat;
                if (knickpointRetreat > 1e-6f) {
                    ++knickpointAffectedVoxels;
                    maximumKnickpointRetreat    = std::max(maximumKnickpointRetreat, knickpointRetreat);
                    maximumKnickpointSlopeBreak = std::max(maximumKnickpointSlopeBreak, knickpoint.slopeBreak);
                    maximumKnickpointDrop =
                        std::max(maximumKnickpointDrop, knickpointSites[size_t(knickpoint.siteIndex)].drop);
                }
                const float karrenRetreat = shell * streamBedKarren * karren.erosion * 0.052f;
                d -= karrenRetreat;
                if (karrenRetreat > 1e-6f) {
                    ++streamBedKarrenAffectedVoxels;
                    maximumStreamBedKarrenRetreat = std::max(maximumStreamBedKarrenRetreat, karrenRetreat);
                    maximumKarrenFractureGuidance = std::max(maximumKarrenFractureGuidance, karren.fractureGuidance);
                    maximumKarrenIntersectionPocket =
                        std::max(maximumKarrenIntersectionPocket, karren.intersectionPocket);
                }
                const float potholeRetreat = shell * eddyPotholes * pothole.erosion * 0.09f;
                d -= potholeRetreat;
                if (potholeRetreat > 1e-6f) {
                    ++potholeAffectedVoxels;
                    maximumPotholeRetreat          = std::max(maximumPotholeRetreat, potholeRetreat);
                    maximumPotholeSecondaryErosion = std::max(maximumPotholeSecondaryErosion, pothole.secondaryPothole);
                    maximumPotholeFractureIntersection =
                        std::max(maximumPotholeFractureIntersection,
                                 potholeSites[size_t(pothole.siteIndex)].fractureIntersection);
                }
                const float obstacleScourRetreat = shell * breakdownScour * obstacleScour.erosion * 0.082f;
                d -= obstacleScourRetreat;
                if (obstacleScourRetreat > 1e-6f) {
                    ++obstacleScourAffectedVoxels;
                    maximumObstacleScourRetreat = std::max(maximumObstacleScourRetreat, obstacleScourRetreat);
                    maximumHorseshoeScour       = std::max(maximumHorseshoeScour, obstacleScour.horseshoeScour);
                    maximumWakeScour            = std::max(maximumWakeScour, obstacleScour.wakeScour);
                    minimumObstacleRoughnessRetention =
                        std::min(minimumObstacleRoughnessRetention, obstacleScour.roughnessRetention);
                }
                d -= shell * chemicalRetention * chemicalMassTransfer * facets.retreat;
                d -= shell * chemicalRetention * chemicalMassTransfer * differentialVeins.hostRetreat;
                const size_t voxel = size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny);
                density[voxel]     = d;
                const float exposedRate =
                    rock.reactiveSurface * std::clamp(passage.hydraulicIntensity * rock.permeability, 0.2f, 1.7f);
                reactiveRate[voxel]      = std::clamp(1.f + reactiveSurfaceCoupling * (exposedRate - 1.f), 0.25f, 2.5f);
                hydraulicExposure[voxel] = passage.hydraulicIntensity;
                flowDirection[voxel]     = passage.tangent;
            }
        }
    }


    return Result<void>::success();
}

}  // namespace eve::procgen::cave_detail
