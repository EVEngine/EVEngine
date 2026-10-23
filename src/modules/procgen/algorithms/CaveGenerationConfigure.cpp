#include "procgen/algorithms/CaveGenerationState.h"

namespace eve::procgen::cave_detail {

Result<void> State::configure() {
    std::string error;
    style                       = params.getString("style", "mixed");
    genesis                     = params.getString("genesis", "epigene");
    resolution                  = params.getInt("resolution", 40);
    nx                          = params.getInt("nx", resolution);
    ny                          = params.getInt("ny", std::max(12, resolution * 3 / 5));
    nz                          = params.getInt("nz", resolution);
    chamberCount                = params.getInt("chambers", 7);
    branchCount                 = params.getInt("branches", 4);
    tunnelRadius                = params.getFloat("tunnelRadius", 0.16f);
    chamberScale                = params.getFloat("chamberScale", 1.f);
    chamberHierarchy            = params.getFloat("chamberHierarchy", 0.f);
    passageVariation            = params.getFloat("passageVariation", 0.f);
    chamberIrregularity         = params.getFloat("chamberIrregularity", 0.f);
    roughness                   = params.getFloat("roughness", 0.12f);
    multiscaleRoughness         = params.getFloat("multiscaleRoughness", 0.f);
    roughnessFlowCoupling       = params.getFloat("roughnessFlowCoupling", 0.f);
    erosion                     = params.getFloat("erosion", 0.55f);
    bedding                     = params.getFloat("bedding", 0.6f);
    fractureDissolution         = params.getFloat("fractureDissolution", 0.55f);
    fractureApertureVariability = params.getFloat("fractureApertureVariability", 0.f);
    fractureStressControl       = params.getFloat("fractureStressControl", 0.f);
    fractureFlowFeedback        = params.getFloat("fractureFlowFeedback", 0.f);
    vadoseIncision              = params.getFloat("vadoseIncision", 0.35f);
    waterTableCorrosion         = params.getFloat("waterTableCorrosion", 0.f);
    waterTableLevel             = params.getFloat("waterTableLevel", 0.18f);
    waterTableStages            = params.getInt("waterTableStages", 1);
    waterTableDrop              = params.getFloat("waterTableDrop", 0.18f);
    waterTableFluctuation       = params.getFloat("waterTableFluctuation", 0.35f);
    scallopErosion              = params.getFloat("scallopErosion", 0.45f);
    scallopScale                = params.getFloat("scallopScale", 0.12f);
    scallopHydraulicScaling     = params.getFloat("scallopHydraulicScaling", 0.f);
    scallopMaturity             = params.getFloat("scallopMaturity", 0.f);
    scallopScaleVariability     = params.getFloat("scallopScaleVariability", 0.f);
    scallopFlowSeparation       = params.getFloat("scallopFlowSeparation", 0.f);
    scallopFlowHistory          = params.getFloat("scallopFlowHistory", 0.f);
    bendUndercut                = params.getFloat("bendUndercut", 0.f);
    fragmentDetachment          = params.getFloat("fragmentDetachment", 0.f);
    curvatureDissolution        = params.getFloat("curvatureDissolution", 0.f);
    reactiveSurfaceCoupling     = params.getFloat("reactiveSurfaceCoupling", 0.f);
    surfaceSlopeReactivity      = params.getFloat("surfaceSlopeReactivity", 0.f);
    reactivePatchiness          = params.getFloat("reactivePatchiness", 0.f);
    hydraulicErosion            = params.getFloat("hydraulicErosion", 0.f);
    mixingCorrosion             = params.getFloat("mixingCorrosion", 0.f);
    lithologicHeterogeneity     = params.getFloat("lithologicHeterogeneity", 0.f);
    floodAbrasion               = params.getFloat("floodAbrasion", 0.f);
    sedimentLoad                = params.getFloat("sedimentLoad", 0.55f);
    floodPlucking               = params.getFloat("floodPlucking", 0.f);
    pluckingBlockScale          = params.getFloat("pluckingBlockScale", 0.1f);
    constrictionScour           = params.getFloat("constrictionScour", 0.f);
    knickpointErosion           = params.getFloat("knickpointErosion", 0.f);
    streamBedKarren             = params.getFloat("streamBedKarren", 0.f);
    eddyPotholes                = params.getFloat("eddyPotholes", 0.f);
    potholeGravelSize           = params.getFloat("potholeGravelSize", 0.5f);
    breakdownScour              = params.getFloat("breakdownScour", 0.f);
    hydraulicGradient           = params.getFloat("hydraulicGradient", 0.35f);
    recharge                    = params.getFloat("recharge", 0.65f);
    flowFocusing                = params.getFloat("flowFocusing", 0.7f);
    damkohler                   = params.getFloat("damkohler", 0.002f);
    transportG                  = params.getFloat("transportG", 1.f);
    microstructure              = params.getFloat("microstructure", 0.f);
    microporosityAccess         = params.getFloat("microporosityAccess", 0.55f);
    permeabilityContrast        = params.getFloat("permeabilityContrast", 0.65f);
    fractureCount               = params.getInt("fractureCount", 5);
    cupolaCount                 = params.getInt("cupolas", 6);
    feederCount                 = params.getInt("feeders", 4);
    dripstoneCount              = params.getInt("dripstones", 12);
    dripstoneScale              = params.getFloat("dripstoneScale", 0.7f);
    stalagmiteShape             = params.getString("stalagmiteShape", "mixed");
    normalSmoothing             = params.getFloat("normalSmoothing", 0.78f);
    surfaceNormalMode           = params.getString("surfaceNormalMode", "faceAverage");
    wetnessRefinement           = params.getInt("wetnessRefinement", 0);
    boundaryClosure             = params.getFloat("boundaryClosure", 0.f);
    condensationCorrosion       = params.getFloat("condensationCorrosion", 0.f);
    biogenicCorrosion           = params.getFloat("biogenicCorrosion", 0.f);
    mineralArmoring             = params.getFloat("mineralArmoring", 0.f);
    condensationFaceting        = params.getFloat("condensationFaceting", 0.f);
    differentialVeinErosion     = params.getFloat("differentialVeinErosion", 0.f);
    breakdown                   = params.getFloat("breakdown", 0.f);
    breakdownEvents             = params.getInt("breakdownEvents", 4);
    sedimentDeposition          = params.getFloat("sedimentDeposition", 0.f);
    paragenesis                 = params.getFloat("paragenesis", 0.f);
    sedimentBars                = params.getInt("sedimentBars", 4);
    flowstoneCount              = params.getInt("flowstones", 7);
    curtainCount                = params.getInt("curtains", 5);
    flowstoneScale              = params.getFloat("flowstoneScale", 0.75f);
    surfaceRefinement           = params.getInt("surfaceRefinement", 0);
    isosurfaceSampling          = params.getInt("isosurfaceSampling", 1);
    refinementThreshold         = params.getFloat("refinementThreshold", 0.0015f);
    width                       = params.getFloat("width", 30.f);
    height                      = params.getFloat("height", 12.f);
    depth                       = params.getFloat("depth", 24.f);
    if (!validStyle(style)) {
        error = "mesh.cave: unknown style '" + style + "' (use cavern|tunnels|vertical|labyrinth|mixed)";
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }
    if (!validGenesis(genesis)) {
        error = "mesh.cave: unknown genesis '" + genesis + "' (use epigene|hypogene|mixed)";
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }
    if (stalagmiteShape != "conical" && stalagmiteShape != "columnar" && stalagmiteShape != "flatTop" &&
        stalagmiteShape != "mixed") {
        error = "mesh.cave: unknown stalagmiteShape '" + stalagmiteShape + "' (use conical|columnar|flatTop|mixed)";
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }
    if (surfaceNormalMode != "faceAverage" && surfaceNormalMode != "densityGradient") {
        error = "mesh.cave: unknown surfaceNormalMode '" + surfaceNormalMode + "' (use faceAverage|densityGradient)";
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }
    if (nx < 8 || ny < 8 || nz < 8 || nx > 128 || ny > 128 || nz > 128) {
        error = "mesh.cave: each resolution axis must be in [8, 128]";
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }
    if (chamberCount < 1 || chamberCount > 64 || branchCount < 0 || branchCount > 32) {
        error = "mesh.cave: chambers must be in [1, 64] and branches in [0, 32]";
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }
    if (!(tunnelRadius >= 0.04f && tunnelRadius <= 0.4f) || chamberScale < 0.35f || chamberScale > 2.5f ||
        chamberHierarchy < 0.f || chamberHierarchy > 1.f || passageVariation < 0.f || passageVariation > 1.f ||
        chamberIrregularity < 0.f || chamberIrregularity > 1.f || roughness < 0.f || roughness > 0.45f ||
        multiscaleRoughness < 0.f || multiscaleRoughness > 1.f || roughnessFlowCoupling < 0.f ||
        roughnessFlowCoupling > 1.f || erosion < 0.f || erosion > 1.f || bedding < 0.f || bedding > 1.f ||
        fractureDissolution < 0.f || fractureDissolution > 1.f || fractureApertureVariability < 0.f ||
        fractureApertureVariability > 1.f || fractureStressControl < 0.f || fractureStressControl > 1.f ||
        fractureFlowFeedback < 0.f || fractureFlowFeedback > 1.f || vadoseIncision < 0.f || vadoseIncision > 1.f ||
        waterTableCorrosion < 0.f || waterTableCorrosion > 1.f || waterTableLevel < -0.8f || waterTableLevel > 0.8f ||
        waterTableStages < 1 || waterTableStages > 4 || waterTableDrop < 0.05f || waterTableDrop > 0.5f ||
        waterTableFluctuation < 0.f || waterTableFluctuation > 1.f || scallopErosion < 0.f || scallopErosion > 1.f ||
        scallopScale < 0.04f || scallopScale > 0.32f || scallopHydraulicScaling < 0.f ||
        scallopHydraulicScaling > 1.f || scallopMaturity < 0.f || scallopMaturity > 1.f ||
        scallopScaleVariability < 0.f || scallopScaleVariability > 1.f || scallopFlowSeparation < 0.f ||
        scallopFlowSeparation > 1.f || scallopFlowHistory < 0.f || scallopFlowHistory > 1.f || bendUndercut < 0.f ||
        bendUndercut > 1.f || fragmentDetachment < 0.f || fragmentDetachment > 1.f || curvatureDissolution < 0.f ||
        curvatureDissolution > 1.f || reactiveSurfaceCoupling < 0.f || reactiveSurfaceCoupling > 1.f ||
        surfaceSlopeReactivity < 0.f || surfaceSlopeReactivity > 1.f || reactivePatchiness < 0.f ||
        reactivePatchiness > 1.f || hydraulicErosion < 0.f || hydraulicErosion > 1.f || mixingCorrosion < 0.f ||
        mixingCorrosion > 1.f || lithologicHeterogeneity < 0.f || lithologicHeterogeneity > 1.f ||
        floodAbrasion < 0.f || floodAbrasion > 1.f || sedimentLoad < 0.f || sedimentLoad > 1.f || floodPlucking < 0.f ||
        floodPlucking > 1.f || pluckingBlockScale < 0.04f || pluckingBlockScale > 0.2f || constrictionScour < 0.f ||
        constrictionScour > 1.f || knickpointErosion < 0.f || knickpointErosion > 1.f || streamBedKarren < 0.f ||
        streamBedKarren > 1.f || eddyPotholes < 0.f || eddyPotholes > 1.f || potholeGravelSize < 0.f ||
        potholeGravelSize > 1.f || breakdownScour < 0.f || breakdownScour > 1.f || hydraulicGradient < 0.01f ||
        hydraulicGradient > 2.f || recharge < 0.f || recharge > 1.f || flowFocusing < 0.f || flowFocusing > 1.f ||
        damkohler < 0.00005f || damkohler > 0.05f || transportG < 0.1f || transportG > 5.f || cupolaCount < 0 ||
        cupolaCount > 32 || microstructure < 0.f || microstructure > 1.f || microporosityAccess < 0.f ||
        microporosityAccess > 1.f || permeabilityContrast < 0.f || permeabilityContrast > 1.f || feederCount < 0 ||
        feederCount > 24 || fractureCount < 0 || fractureCount > 24 || dripstoneCount < 0 || dripstoneCount > 64 ||
        dripstoneScale < 0.25f || dripstoneScale > 1.5f || normalSmoothing < 0.f || normalSmoothing > 1.f ||
        flowstoneCount < 0 || flowstoneCount > 32 || curtainCount < 0 || curtainCount > 32 || flowstoneScale < 0.25f ||
        flowstoneScale > 1.5f || surfaceRefinement < 0 || surfaceRefinement > 2 || isosurfaceSampling < 1 ||
        isosurfaceSampling > 2 || wetnessRefinement < 0 || wetnessRefinement > 1 || boundaryClosure < 0.f ||
        boundaryClosure > 1.f || condensationCorrosion < 0.f || condensationCorrosion > 1.f ||
        biogenicCorrosion < 0.f || biogenicCorrosion > 1.f || mineralArmoring < 0.f || mineralArmoring > 1.f ||
        condensationFaceting < 0.f || condensationFaceting > 1.f || differentialVeinErosion < 0.f ||
        differentialVeinErosion > 1.f || breakdown < 0.f || breakdown > 1.f || breakdownEvents < 0 ||
        breakdownEvents > 16 || refinementThreshold < 0.0001f || refinementThreshold > 0.02f ||
        sedimentDeposition < 0.f || sedimentDeposition > 1.f || paragenesis < 0.f || paragenesis > 1.f ||
        sedimentBars < 0 || sedimentBars > 16 || width <= 0.f || height <= 0.f || depth <= 0.f) {
        error = "mesh.cave: invalid radius, chamber scale, roughness, or world dimensions";
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }


    return Result<void>::success();
}

}  // namespace eve::procgen::cave_detail
