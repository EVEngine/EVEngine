#include <algorithm>
#include <limits>
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
namespace eve::procgen::road {
namespace {

RoadBakeOptions bakeOptionsFromParams(const Params& params) {
    RoadBakeOptions options;
    options.maximumPlacementQueries = params.has("placementQueryBudget")
                                          ? static_cast<std::size_t>(params.getInt("placementQueryBudget", 0))
                                          : 65536u;
    options.placementQueryMinY      = params.has("placementQueryMinY")
                                          ? params.getFloat("placementQueryMinY", std::numeric_limits<float>::quiet_NaN())
                                          : -10000.f;
    options.placementQueryMaxY      = params.has("placementQueryMaxY")
                                          ? params.getFloat("placementQueryMaxY", std::numeric_limits<float>::quiet_NaN())
                                          : 10000.f;
    options.maximumSamplingEvaluations =
        params.has("samplingWorkBudget") ? static_cast<std::size_t>(params.getInt("samplingWorkBudget", 0)) : 65536u;
    options.samplingScale =
        params.has("samplingScale") ? params.getFloat("samplingScale", std::numeric_limits<float>::quiet_NaN()) : 1.f;
    options.pathSegmentsPerEdge = std::max(4, params.getInt("pathSegments", 48));
    options.turnSamples         = std::max(4, params.getInt("turnSamples", 12));
    options.includePiers        = params.getBool("piers", true);
    options.includeMarkings     = params.getBool("markings", true);
    options.includeNavigation   = params.getBool("navigation", true);
    options.includeJunctions    = params.getBool("junctions", true);
    options.includePlacements   = params.getBool("placements", true);
    options.junctionChordError  = params.getFloat("junctionChordError", 0.10f);
    options.sideObjectSpacing   = params.getFloat("sideObjectSpacing", 8.f);
    options.sideObjectOffset    = params.getFloat("sideObjectOffset", 0.5f);
    options.sideObjectClearance = params.getFloat("sideObjectClearance", 1.5f);
    return options;
}


}  // namespace

Result<RoadBakeResult> bakeRoadNetworkRecipe(const Params& params) {
    auto network = resolveRoadRecipeNetwork(params);
    if (!network.ok()) return Result<RoadBakeResult>::failure(network.status());
    const bool projectsTerrain =
        params.getBool("placements", true) &&
        std::any_of(network.value().edges().begin(), network.value().edges().end(), [](const RoadEdge& edge) {
            return (edge.style.sideTerrain.mode != RoadSideTerrainMode::None ||
                    edge.style.sideTerrain.alignment == RoadSideAlignment::TerrainNormal) &&
                   (edge.style.sideObjectsLeft || edge.style.sideObjectsRight);
        });
    if (projectsTerrain) {
        const auto revision = params.getString("placementWorldRevision", "");
        if (revision.empty() || revision.size() > 256 ||
            std::any_of(revision.begin(), revision.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            return Result<RoadBakeResult>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "terrain-snapped road recipes require the caller's stable placementWorldRevision",
                                  "placementWorldRevision"));
    }
    return bakeRoadNetwork(network.value(), bakeOptionsFromParams(params));
}

}  // namespace eve::procgen::road
