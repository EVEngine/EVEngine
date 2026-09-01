#include "procgen/algorithms/CaveMesh.h"

#include "procgen/algorithms/CaveMeshInternal.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <utility>

namespace eve::procgen {

void registerCaveMeshRecipe(MeshRecipeRegistry& registry) {
    RecipeDescriptor cave{"mesh.cave", "Limestone Cave", "Mesh", {}};
    cave.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
    cave.params.push_back(ParamDescriptor::choice("style", "Cave Style", "mixed",
                                                  {"cavern", "tunnels", "vertical", "labyrinth", "mixed"}));
    cave.params.push_back(
        ParamDescriptor::choice("genesis", "Cave Genesis", "epigene", {"epigene", "hypogene", "mixed"}));
    cave.params.push_back(ParamDescriptor::integer("resolution", "Resolution", 40, 12, 128));
    cave.params.push_back(ParamDescriptor::floating("width", "World Width", 30.f, 1.f, 1000.f, 0.5f));
    cave.params.push_back(ParamDescriptor::floating("height", "World Height", 12.f, 1.f, 1000.f, 0.5f));
    cave.params.push_back(ParamDescriptor::floating("depth", "World Depth", 24.f, 1.f, 1000.f, 0.5f));
    cave.params.push_back(ParamDescriptor::integer("chambers", "Chambers", 7, 1, 64));
    cave.params.push_back(ParamDescriptor::integer("branches", "Branches", 4, 0, 32));
    cave.params.push_back(ParamDescriptor::floating("tunnelRadius", "Tunnel Radius", 0.16f, 0.04f, 0.4f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("chamberScale", "Chamber Scale", 1.f, 0.35f, 2.5f, 0.05f));
    cave.params.push_back(
        ParamDescriptor::floating("chamberHierarchy", "Chamber Size Hierarchy", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("passageVariation", "Passage Scale Variation", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("chamberIrregularity", "Chamber Low-frequency Relief", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("roughness", "Wall Roughness", 0.12f, 0.f, 0.45f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("multiscaleRoughness", "Multi-scale Wall Relief", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("erosion", "Karst Erosion", 0.55f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("bedding", "Bedding Dissolution", 0.6f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("fractureDissolution", "Joint Dissolution", 0.55f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("vadoseIncision", "Stream Incision", 0.35f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("waterTableCorrosion", "Water-table Corrosion", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("scallopErosion", "Flow Scallops", 0.45f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("scallopScale", "Scallop Scale", 0.12f, 0.04f, 0.32f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("scallopHydraulicScaling", "Flow-scaled Scallops", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("scallopScaleVariability", "Scallop Scale Variability", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("scallopFlowSeparation", "Scallop Flow Separation", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("scallopFlowHistory", "Multi-stage Scallop Overprint", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("hydraulicErosion", "Hydraulic Erosion", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("mixingCorrosion", "Confluence Mixing Corrosion", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("lithologicHeterogeneity", "Lithologic Selective Erosion", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("floodAbrasion", "Sediment-laden Flood Abrasion", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("floodPlucking", "Fracture-controlled Flood Plucking", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(
        ParamDescriptor::floating("constrictionScour", "Constriction Pool Scour", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("knickpointErosion", "Knickpoint Erosion", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("streamBedKarren", "Stream-bed Karren", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("eddyPotholes", "Eddy Potholes", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("potholeGravelSize", "Pothole Gravel Size", 0.5f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::floating("breakdownScour", "Breakdown-block Scour", 0.f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::integer("dripstones", "Dripstone Pairs", 12, 0, 64));
    cave.params.push_back(ParamDescriptor::floating("dripstoneScale", "Dripstone Scale", 0.7f, 0.25f, 1.5f, 0.05f));
    cave.params.push_back(ParamDescriptor::choice("stalagmiteShape", "Stalagmite Shape", "mixed",
                                                  {"conical", "columnar", "flatTop", "mixed"}));
    cave.params.push_back(ParamDescriptor::floating("normalSmoothing", "Normal Smoothing", 0.78f, 0.f, 1.f, 0.01f));
    cave.params.push_back(ParamDescriptor::integer("flowstones", "Flowstone Patches", 7, 0, 32));
    cave.params.push_back(ParamDescriptor::integer("curtains", "Cave Curtains", 5, 0, 32));
    cave.params.push_back(ParamDescriptor::floating("flowstoneScale", "Flowstone Scale", 0.75f, 0.25f, 1.5f, 0.05f));
    cave.params.push_back(ParamDescriptor::integer("surfaceRefinement", "Surface Refinement", 0, 0, 2));
    cave.params.push_back(
        ParamDescriptor::floating("refinementThreshold", "Refinement Error", 0.0015f, 0.0001f, 0.02f, 0.0001f));
    auto advanced = [&cave](ParamDescriptor param) {
        param.advanced = true;
        cave.params.push_back(std::move(param));
    };
    advanced(ParamDescriptor::integer("nx", "X Resolution", 40, 8, 128));
    advanced(ParamDescriptor::integer("ny", "Y Resolution", 24, 8, 128));
    advanced(ParamDescriptor::integer("nz", "Z Resolution", 40, 8, 128));
    advanced(ParamDescriptor::integer("fractureCount", "Joint Planes", 5, 0, 24));
    advanced(ParamDescriptor::floating("waterTableLevel", "Highest Water-table Level", 0.18f, -0.8f, 0.8f, 0.01f));
    advanced(ParamDescriptor::integer("waterTableStages", "Water-table Stages", 1, 1, 4));
    advanced(ParamDescriptor::floating("waterTableDrop", "Water-table Stage Drop", 0.18f, 0.05f, 0.5f, 0.01f));
    advanced(ParamDescriptor::floating("waterTableFluctuation", "Water-table Fluctuation", 0.35f, 0.f, 1.f, 0.01f));
    advanced(
        ParamDescriptor::floating("fractureApertureVariability", "Joint Aperture Variability", 0.f, 0.f, 1.f, 0.01f));
    advanced(
        ParamDescriptor::floating("fractureStressControl", "Stress-controlled Joint Branching", 0.f, 0.f, 1.f, 0.01f));
    advanced(
        ParamDescriptor::floating("fractureFlowFeedback", "Aperture-flow Joint Channelization", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("hydraulicGradient", "Hydraulic Gradient", 0.35f, 0.01f, 2.f, 0.01f));
    advanced(ParamDescriptor::floating("recharge", "Recharge", 0.65f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("flowFocusing", "Flow Focusing", 0.7f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("damkohler", "Effective Damkohler", 0.002f, 0.00005f, 0.05f, 0.00005f));
    advanced(ParamDescriptor::floating("transportG", "Transverse Transport G", 1.f, 0.1f, 5.f, 0.1f));
    advanced(ParamDescriptor::floating("sedimentLoad", "Flood Sediment Load", 0.55f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("pluckingBlockScale", "Plucked Block Scale", 0.1f, 0.04f, 0.2f, 0.01f));
    advanced(ParamDescriptor::floating("microstructure", "Rock Microstructure", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("roughnessFlowCoupling", "Rough-wall Mass Transfer", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("surfaceSlopeReactivity", "Geometry-controlled Surface Reactivity", 0.f, 0.f,
                                       1.f, 0.01f));
    advanced(ParamDescriptor::floating("reactivePatchiness", "Correlated Reactive Patches", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("condensationCorrosion", "Condensation Corrosion", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("biogenicCorrosion", "Biogenic Corrosion", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("mineralArmoring", "Secondary-mineral Armoring", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("condensationFaceting", "Condensation Faceting", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("differentialVeinErosion", "Differential Vein Erosion", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("breakdown", "Breakdown", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::integer("breakdownEvents", "Breakdown Events", 4, 0, 16));
    advanced(ParamDescriptor::floating("sedimentDeposition", "Sediment Deposition", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("paragenesis", "Sediment-driven Paragenesis", 0.f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::integer("sedimentBars", "Sediment Bars", 4, 0, 16));
    advanced(ParamDescriptor::floating("microporosityAccess", "Accessible Microporosity", 0.55f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::floating("permeabilityContrast", "Permeability Contrast", 0.65f, 0.f, 1.f, 0.01f));
    advanced(ParamDescriptor::integer("cupolas", "Ceiling Cupolas", 6, 0, 32));
    advanced(ParamDescriptor::integer("feeders", "Rising Feeders", 4, 0, 24));
    registry.registerRecipe(std::move(cave), caveMeshGenerator());
}

}  // namespace eve::procgen
