// Geological preset and composition schedule are editable application policy.
local cavePipeline = {
    surface = ["reactivity", "reactivePatches", "condensation", "curvature"],
    deposits = true,
    // Optional normalized ellipsoids: { x=0.0,y=0.0,z=0.0,rx=0.3,ry=0.2,rz=0.3 }.
    extraChambers = []
};
local stageNeed = function(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
};
export function generateCave(seed, style, microporosity, permeabilityContrast) {
    local p = stageNeed(procgen.newParams());
    stageNeed(p.setSeed(seed));
    stageNeed(p.setString("style", style));
    stageNeed(p.setString("genesis", "mixed"));
    stageNeed(p.setInt("resolution", 64));
    stageNeed(p.setFloat("width", 32.0));
    stageNeed(p.setFloat("height", 13.0));
    stageNeed(p.setFloat("depth", 25.0));
    stageNeed(p.setInt("chambers", 7));
    stageNeed(p.setInt("branches", 6));
    stageNeed(p.setFloat("tunnelRadius", 0.18));
    stageNeed(p.setFloat("chamberScale", 1.22));
    stageNeed(p.setFloat("chamberHierarchy", 0.92));
    stageNeed(p.setFloat("passageVariation", 0.82));
    stageNeed(p.setFloat("chamberIrregularity", 0.72));
    stageNeed(p.setFloat("roughness", 0.10));
    stageNeed(p.setFloat("multiscaleRoughness", 0.70));
    stageNeed(p.setFloat("roughnessFlowCoupling", 0.72));
    stageNeed(p.setFloat("surfaceSlopeReactivity", 0.74));
    stageNeed(p.setFloat("reactivePatchiness", 0.68));
    stageNeed(p.setFloat("erosion", 0.72));
    stageNeed(p.setFloat("bedding", 0.68));
    stageNeed(p.setFloat("fractureDissolution", 0.62));
    stageNeed(p.setFloat("fractureApertureVariability", 0.76));
    stageNeed(p.setFloat("fractureStressControl", 0.82));
    stageNeed(p.setFloat("fractureFlowFeedback", 0.74));
    stageNeed(p.setFloat("vadoseIncision", 0.48));
    stageNeed(p.setFloat("waterTableCorrosion", 0.72));
    stageNeed(p.setFloat("waterTableLevel", 0.26));
    stageNeed(p.setInt("waterTableStages", 3));
    stageNeed(p.setFloat("waterTableDrop", 0.19));
    stageNeed(p.setFloat("waterTableFluctuation", 0.42));
    stageNeed(p.setFloat("scallopErosion", 0.72));
    stageNeed(p.setFloat("scallopScale", 0.11));
    stageNeed(p.setFloat("scallopHydraulicScaling", 1.0));
    stageNeed(p.setFloat("scallopMaturity", 0.68));
    stageNeed(p.setFloat("scallopScaleVariability", 0.72));
    stageNeed(p.setFloat("scallopFlowSeparation", 0.78));
    stageNeed(p.setFloat("scallopFlowHistory", 0.68));
    stageNeed(p.setFloat("bendUndercut", 0.72));
    stageNeed(p.setFloat("fragmentDetachment", 1.0));
    stageNeed(p.setFloat("curvatureDissolution", 0.72));
    stageNeed(p.setFloat("reactiveSurfaceCoupling", 0.88));
    stageNeed(p.setFloat("hydraulicErosion", 0.86));
    stageNeed(p.setFloat("mixingCorrosion", 0.76));
    stageNeed(p.setFloat("lithologicHeterogeneity", 0.82));
    stageNeed(p.setFloat("floodAbrasion", 0.78));
    stageNeed(p.setFloat("sedimentLoad", 0.48));
    stageNeed(p.setFloat("floodPlucking", 0.66));
    stageNeed(p.setFloat("pluckingBlockScale", 0.12));
    stageNeed(p.setFloat("constrictionScour", 0.78));
    stageNeed(p.setFloat("knickpointErosion", 0.74));
    stageNeed(p.setFloat("streamBedKarren", 0.72));
    stageNeed(p.setFloat("eddyPotholes", 0.70));
    stageNeed(p.setFloat("potholeGravelSize", 0.42));
    stageNeed(p.setFloat("breakdownScour", 0.68));
    stageNeed(p.setFloat("hydraulicGradient", 0.52));
    stageNeed(p.setFloat("recharge", 0.78));
    stageNeed(p.setFloat("flowFocusing", 0.82));
    stageNeed(p.setFloat("damkohler", 0.0035));
    stageNeed(p.setFloat("transportG", 1.6));
    stageNeed(p.setFloat("microstructure", 0.78));
    stageNeed(p.setFloat("microporosityAccess", microporosity));
    stageNeed(p.setFloat("permeabilityContrast", permeabilityContrast));
    stageNeed(p.setInt("fractureCount", 7));
    stageNeed(p.setInt("cupolas", 7));
    stageNeed(p.setInt("feeders", 4));
    stageNeed(p.setInt("dripstones", 4));
    stageNeed(p.setFloat("dripstoneScale", 0.90));
    stageNeed(p.setString("stalagmiteShape", "conical"));
    stageNeed(p.setFloat("normalSmoothing", 0.86));
    stageNeed(p.setString("surfaceNormalMode", "densityGradient"));
    stageNeed(p.setInt("wetnessRefinement", 1));
    stageNeed(p.setFloat("boundaryClosure", 1.0));
    stageNeed(p.setFloat("condensationCorrosion", 0.72));
    stageNeed(p.setFloat("biogenicCorrosion", 0.68));
    stageNeed(p.setFloat("mineralArmoring", 0.52));
    stageNeed(p.setFloat("condensationFaceting", 0.62));
    stageNeed(p.setFloat("differentialVeinErosion", 0.58));
    stageNeed(p.setFloat("breakdown", 0.68));
    stageNeed(p.setInt("breakdownEvents", 5));
    stageNeed(p.setFloat("sedimentDeposition", 0.64));
    stageNeed(p.setFloat("paragenesis", 0.7));
    stageNeed(p.setInt("sedimentBars", 5));
    stageNeed(p.setInt("isosurfaceSampling", 2));
    stageNeed(p.setInt("surfaceRefinement", 2));
    stageNeed(p.setFloat("refinementThreshold", 0.0015));
    stageNeed(p.setInt("flowstones", 2));
    stageNeed(p.setInt("curtains", 1));
    stageNeed(p.setFloat("flowstoneScale", 1.0));
    local generation = eve.CaveGeneration();
    stageNeed(generation.configure(p));
    stageNeed(generation.buildSkeleton());
    // generation.setChamber(...) can replace individual chambers before volume sampling.
    stageNeed(generation.sampleField());
    // generation.field() returns an owning ScalarField3D. Edit a snapshot, then replaceField().
    if (cavePipeline.extraChambers.len() > 0) {
        local field = stageNeed(generation.field());
        foreach (shape in cavePipeline.extraChambers)
            stageNeed(field.carveEllipsoid(shape.x, shape.y, shape.z, shape.rx, shape.ry, shape.rz));
        stageNeed(generation.replaceField(field));
    }
    foreach (operation in cavePipeline.surface) stageNeed(generation.evolveSurface(operation));
    if (cavePipeline.deposits) stageNeed(generation.deposit());
    return stageNeed(generation.buildMesh());
}
