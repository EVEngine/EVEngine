#pragma once

#include "procgen/algorithms/CaveGeneration.h"
#include "procgen/algorithms/CaveMesh.h"

#include "procgen/algorithms/CaveAbrasion.h"
#include "procgen/algorithms/CaveBiogenicCorrosion.h"
#include "procgen/algorithms/CaveBoundary.h"
#include "procgen/algorithms/CaveBreakdown.h"
#include "procgen/algorithms/CaveCondensation.h"
#include "procgen/algorithms/CaveConstrictionScour.h"
#include "procgen/algorithms/CaveDepositAnchoring.h"
#include "procgen/algorithms/CaveDifferentialErosion.h"
#include "procgen/algorithms/CaveFacets.h"
#include "procgen/algorithms/CaveFieldSampling.h"
#include "procgen/algorithms/CaveFractureChannelization.h"
#include "procgen/algorithms/CaveFractures.h"
#include "procgen/algorithms/CaveHydrology.h"
#include "procgen/algorithms/CaveKarren.h"
#include "procgen/algorithms/CaveKnickpoint.h"
#include "procgen/algorithms/CaveLithology.h"
#include "procgen/algorithms/CaveMeshInternal.h"
#include "procgen/algorithms/CaveMicrostructure.h"
#include "procgen/algorithms/CaveMineralArmoring.h"
#include "procgen/algorithms/CaveMixingCorrosion.h"
#include "procgen/algorithms/CaveNormals.h"
#include "procgen/algorithms/CaveObstacleScour.h"
#include "procgen/algorithms/CavePlucking.h"
#include "procgen/algorithms/CavePotholes.h"
#include "procgen/algorithms/CaveReactivePatchiness.h"
#include "procgen/algorithms/CaveRoughness.h"
#include "procgen/algorithms/CaveRoughnessTransfer.h"
#include "procgen/algorithms/CaveScallopHistory.h"
#include "procgen/algorithms/CaveScallops.h"
#include "procgen/algorithms/CaveSediment.h"
#include "procgen/algorithms/CaveSupport.h"
#include "procgen/algorithms/CaveSurfaceEvolution.h"
#include "procgen/algorithms/CaveSurfaceReactivity.h"
#include "procgen/algorithms/CaveWaterTable.h"
#include "procgen/algorithms/CaveWetness.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen::cave_detail {

using Vec3         = CaveHydrologyVec3;
using PassagePoint = CaveHydrologyPoint;

struct Chamber {
    Vec3  center;
    Vec3  radii;
    float irregularity = 0.f;
    float phase        = 0.f;
};

struct RisingConduit {
    Vec3  start;
    Vec3  end;
    float radius = 0.05f;
};

struct Dripstone {
    Vec3  start;
    Vec3  end;
    float startRadius      = 0.05f;
    float endRadius        = 0.01f;
    float profileAmplitude = 0.f;
    float profileFrequency = 2.f;
    float profilePhase     = 0.f;
};

struct Flowstone {
    Vec3  center;
    Vec3  normal;
    Vec3  tangent;
    float halfWidth       = 0.08f;
    float halfHeight      = 0.12f;
    float thickness       = 0.02f;
    float rippleAmplitude = 0.01f;
    float rippleFrequency = 5.f;
    float phase           = 0.f;
};

struct Curtain {
    Vec3  anchor;
    Vec3  normal;
    Vec3  tangent;
    float halfWidth     = 0.08f;
    float length        = 0.18f;
    float thickness     = 0.018f;
    float waveAmplitude = 0.018f;
    float waveFrequency = 3.f;
    float phase         = 0.f;
};

struct QuantizedPoint {
    int x     = 0;
    int y     = 0;
    int z     = 0;
    int group = 0;

    bool operator==(const QuantizedPoint& other) const {
        return x == other.x && y == other.y && z == other.z && group == other.group;
    }
};

struct EdgeKey {
    int ax = 0, ay = 0, az = 0;
    int bx = 0, by = 0, bz = 0;

    bool operator==(const EdgeKey& other) const {
        return ax == other.ax && ay == other.ay && az == other.az && bx == other.bx && by == other.by && bz == other.bz;
    }
};

struct EdgeProjection {
    Vec3 projected;
    bool split = false;
};

uint32_t hash(uint32_t value);

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& key) const {
        size_t value = size_t(hash(uint32_t(key.ax)));
        value ^= size_t(hash(uint32_t(key.ay))) << 1u;
        value ^= size_t(hash(uint32_t(key.az))) << 2u;
        value ^= size_t(hash(uint32_t(key.bx))) << 3u;
        value ^= size_t(hash(uint32_t(key.by))) << 4u;
        value ^= size_t(hash(uint32_t(key.bz))) << 5u;
        return value;
    }
};

float    dot(Vec3 a, Vec3 b);
Vec3     sub(Vec3 a, Vec3 b);
Vec3     add(Vec3 a, Vec3 b);
Vec3     mul(Vec3 a, float scale);
EdgeKey  makeEdgeKey(Vec3 a, Vec3 b);
float    smoothstep(float edge0, float edge1, float value);
float    smoothMaximum(float a, float b, float blendRadius);
float    distanceToSegment(Vec3 point, Vec3 a, Vec3 b);
float    ellipsoidDistance(Vec3 point, const Chamber& shape);
float    taperedSegmentDistance(Vec3 point, const Dripstone& shape, float horizontalX = 1.f, float horizontalZ = 1.f);
float    flowstoneDistance(Vec3 point, const Flowstone& shape);
float    curtainDistance(Vec3 point, const Curtain& shape);
uint32_t hash(uint32_t value);
float    latticeNoise(float x, float y, float z, uint32_t seed);
Vec3     projectToDensitySurface(Vec3 meshPoint, const std::vector<float>& density, int nx, int ny, int nz);
void     addFacetedTriangle(MeshBuild& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 referenceNormal);
bool     validStyle(const std::string& style);
bool     validGenesis(const std::string& genesis);

struct State {
    std::vector<CaveSurfaceOperation>      surfaceSchedule;
    bool                                   depositsApplied = false;
    bool                                   fieldEdited     = false;
    bool                                   skeletonEdited  = false;
    Params                                 params;
    std::string                            style{};
    std::string                            genesis{};
    int                                    resolution{};
    int                                    nx{};
    int                                    ny{};
    int                                    nz{};
    int                                    chamberCount{};
    int                                    branchCount{};
    float                                  tunnelRadius{};
    float                                  chamberScale{};
    float                                  chamberHierarchy{};
    float                                  passageVariation{};
    float                                  chamberIrregularity{};
    float                                  roughness{};
    float                                  multiscaleRoughness{};
    float                                  roughnessFlowCoupling{};
    float                                  erosion{};
    float                                  bedding{};
    float                                  fractureDissolution{};
    float                                  fractureApertureVariability{};
    float                                  fractureStressControl{};
    float                                  fractureFlowFeedback{};
    float                                  vadoseIncision{};
    float                                  waterTableCorrosion{};
    float                                  waterTableLevel{};
    int                                    waterTableStages{};
    float                                  waterTableDrop{};
    float                                  waterTableFluctuation{};
    float                                  scallopErosion{};
    float                                  scallopScale{};
    float                                  scallopHydraulicScaling{};
    float                                  scallopMaturity{};
    float                                  scallopScaleVariability{};
    float                                  scallopFlowSeparation{};
    float                                  scallopFlowHistory{};
    float                                  bendUndercut{};
    float                                  fragmentDetachment{};
    float                                  curvatureDissolution{};
    float                                  reactiveSurfaceCoupling{};
    float                                  surfaceSlopeReactivity{};
    float                                  reactivePatchiness{};
    float                                  hydraulicErosion{};
    float                                  mixingCorrosion{};
    float                                  lithologicHeterogeneity{};
    float                                  floodAbrasion{};
    float                                  sedimentLoad{};
    float                                  floodPlucking{};
    float                                  pluckingBlockScale{};
    float                                  constrictionScour{};
    float                                  knickpointErosion{};
    float                                  streamBedKarren{};
    float                                  eddyPotholes{};
    float                                  potholeGravelSize{};
    float                                  breakdownScour{};
    float                                  hydraulicGradient{};
    float                                  recharge{};
    float                                  flowFocusing{};
    float                                  damkohler{};
    float                                  transportG{};
    float                                  microstructure{};
    float                                  microporosityAccess{};
    float                                  permeabilityContrast{};
    int                                    fractureCount{};
    int                                    cupolaCount{};
    int                                    feederCount{};
    int                                    dripstoneCount{};
    float                                  dripstoneScale{};
    std::string                            stalagmiteShape{};
    float                                  normalSmoothing{};
    std::string                            surfaceNormalMode{};
    int                                    wetnessRefinement{};
    float                                  boundaryClosure{};
    float                                  condensationCorrosion{};
    float                                  biogenicCorrosion{};
    float                                  mineralArmoring{};
    float                                  condensationFaceting{};
    float                                  differentialVeinErosion{};
    float                                  breakdown{};
    int                                    breakdownEvents{};
    float                                  sedimentDeposition{};
    float                                  paragenesis{};
    int                                    sedimentBars{};
    int                                    flowstoneCount{};
    int                                    curtainCount{};
    float                                  flowstoneScale{};
    int                                    surfaceRefinement{};
    int                                    isosurfaceSampling{};
    float                                  refinementThreshold{};
    float                                  width{};
    float                                  height{};
    float                                  depth{};
    std::vector<PassagePoint>              spine{};
    int                                    spinePoints{};
    std::vector<CaveHydrologyBranch>       branches{};
    CaveHydrologyWeights                   hydrology{};
    std::vector<CaveMixingSite>            mixingSites{};
    std::vector<CaveConstrictionScourSite> constrictionScourSites{};
    std::vector<CaveKnickpointSite>        knickpointSites{};
    std::vector<Chamber>                   chambers{};
    int                                    primaryChamber{};
    float                                  primaryChamberVerticalRadius{};
    std::vector<CaveBreakdownChamber>      breakdownChambers{};
    CaveBreakdownSet                       breakdownSet{};
    std::vector<CaveObstacleScourSite>     obstacleScourSites{};
    std::vector<CaveSedimentPathPoint>     sedimentPath{};
    CaveSedimentSet                        sedimentSet{};
    std::vector<CaveFracture>              fractures{};
    std::vector<CavePotholeSite>           potholeSites{};
    std::vector<Chamber>                   cupolas{};
    std::vector<RisingConduit>             feeders{};
    std::vector<RisingConduit>             ceilingChannels{};
    std::vector<Dripstone>                 dripstones{};
    int                                    columnCount{};
    std::vector<Flowstone>                 flowstones{};
    std::vector<Curtain>                   curtains{};
    std::vector<float>                     density{};
    std::vector<float>                     reactiveRate{};
    std::vector<float>                     hydraulicExposure{};
    std::vector<CaveHydrologyVec3>         flowDirection{};
    int                                    biogenicAffectedVoxels{};
    float                                  minimumScallopRetention{};
    float                                  maximumBiogenicErosion{};
    float                                  totalBiogenicErosion{};
    int                                    facetAffectedVoxels{};
    float                                  maximumFacetRetreat{};
    int                                    differentialVeinAffectedVoxels{};
    float                                  maximumDifferentialVeinRetreat{};
    float                                  maximumVeinProtection{};
    float                                  minimumFractureAperture{};
    float                                  maximumFractureAperture{};
    float                                  minimumFractureBranchOpenness{};
    int                                    fractureChannelAffectedVoxels{};
    float                                  maximumFractureChannelRetreat{};
    float                                  maximumFractureFlowConcentration{};
    float                                  maximumFractureIntersectionAmplification{};
    float                                  minimumFractureReactantAccess{};
    float                                  minimumWallRelief{};
    float                                  maximumWallRelief{};
    int                                    roughnessTransferAffectedVoxels{};
    float                                  minimumRoughnessTransfer{};
    float                                  maximumRoughnessTransfer{};
    float                                  maximumRidgeExposure{};
    float                                  maximumRecessShelter{};
    int                                    waterTableAffectedVoxels{};
    float                                  maximumWaterTableRetreat{};
    int                                    mixingCorrosionAffectedVoxels{};
    float                                  maximumMixingCorrosionRetreat{};
    int                                    lithologyAffectedVoxels{};
    int                                    styloliteAffectedVoxels{};
    float                                  minimumBedResistance{};
    float                                  maximumLithologyRetreat{};
    int                                    abrasionAffectedVoxels{};
    float                                  maximumAbrasionRetreat{};
    float                                  maximumAbrasionVortex{};
    int                                    pluckingAffectedVoxels{};
    float                                  maximumPluckingRetreat{};
    float                                  maximumPluckingPredisposition{};
    int                                    constrictionScourAffectedVoxels{};
    float                                  maximumConstrictionScourRetreat{};
    float                                  maximumConstrictionRatio{};
    float                                  maximumPlungingEfficiency{};
    int                                    knickpointAffectedVoxels{};
    float                                  maximumKnickpointRetreat{};
    float                                  maximumKnickpointSlopeBreak{};
    float                                  maximumKnickpointDrop{};
    int                                    streamBedKarrenAffectedVoxels{};
    float                                  maximumStreamBedKarrenRetreat{};
    float                                  maximumKarrenFractureGuidance{};
    float                                  maximumKarrenIntersectionPocket{};
    int                                    potholeAffectedVoxels{};
    float                                  maximumPotholeRetreat{};
    float                                  maximumPotholeSecondaryErosion{};
    float                                  maximumPotholeFractureIntersection{};
    int                                    obstacleScourAffectedVoxels{};
    float                                  maximumObstacleScourRetreat{};
    float                                  maximumHorseshoeScour{};
    float                                  maximumWakeScour{};
    float                                  minimumObstacleRoughnessRetention{};
    int                                    mineralArmoringAffectedVoxels{};
    float                                  maximumMineralCoatingCoverage{};
    float                                  maximumMineralHydraulicRetention{};
    float                                  minimumArmoredDissolutionRetention{};
    int                                    scallopHistoryAffectedVoxels{};
    float                                  maximumYoungerScallopErosion{};
    float                                  maximumYoungerScallopCoverage{};
    float                                  maximumScallopReversalMask{};
    float                                  minimumSecondaryScallopScaleRatio{};
    CaveSurfaceReactivityResult            surfaceReactivity{};
    CaveReactivePatchinessResult           reactivePatchEvolution{};
    CaveCondensationResult                 condensation{};
    CaveSurfaceEvolutionResult             surfaceEvolution{};
    float                                  voxelSize{};
    float                                  embedDepth{};
    float                                  minimumFeatureRadius{};
    int                                    rejectedDripstonePairs{};
    int                                    anchoredDripstonePairs{};
    std::vector<Dripstone>                 anchoredDripstones{};
    int                                    rejectedFlowstones{};
    std::vector<Flowstone>                 anchoredFlowstones{};
    int                                    rejectedCurtains{};
    std::vector<Curtain>                   anchoredCurtains{};
    std::vector<float>                     depositDelta{};
    CaveDetachmentResult                   detachment{};
    CaveBoundaryClosure                    boundary{};
    CaveResampledField                     resampled{};
    std::vector<int>                       caveWallGroups{};
    float                                  groupTolerance{};
    int                                    refinedSourceTriangles{};
    int                                    adaptiveSplitEdges{};
    CaveWetnessRefinement                  wetness{};
    float                                  sx{};
    float                                  sy{};
    float                                  sz{};
    float                                  minimumScallopScale{};
    float                                  maximumScallopScale{};
    int                                    extractionNx{}, extractionNy{}, extractionNz{};
    size_t                                 extractionVoxels{};
    Result<void>                           configure();
    Result<void>                           skeleton();
    Result<void>                           sample();
    Result<void>                           evolve(CaveSurfaceOperation operation);
    Result<void>                           deposit();
    Result<void>                           mesh(MeshBuild& out);
    void                                   metadata(MeshBuild& out);
};

}  // namespace eve::procgen::cave_detail
