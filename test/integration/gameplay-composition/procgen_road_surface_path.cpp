#include <algorithm>
#include <array>
#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;
namespace {
using Point = std::array<float, 3>;

eve::Result<RoadNetwork> curve(RoadSidePathSampling policy) {
    RoadNetwork network;
    auto        a = network.addNode(0, 0, 0), b = network.addNode(0, 0, 30);
    if (!a.ok()) return eve::Result<RoadNetwork>::failure(a.status());
    if (!b.ok()) return eve::Result<RoadNetwork>::failure(b.status());
    RoadStyle style;
    style.laneWidth             = 3;
    style.automaticStructures   = false;
    style.crossSection          = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    style.samplingSpacingMeters = 0;
    style.sidePlacement         = {RoadSideReference::Center, 0, .02f, 0, policy};
    auto added = network.addEdge(a.value(), b.value(), {{0, 0, 0}, {6, 0, 15}, {0, 0, 30}}, 1, 1, style);
    if (!added.ok()) return eve::Result<RoadNetwork>::failure(added.status());
    return eve::Result<RoadNetwork>::success(std::move(network));
}

}  // namespace

TEST_CASE("procgen.road.surfacePath.retainsPresetHistoryAndMigratesMissingPolicy") {
    auto made = curve(RoadSidePathSampling::SurfaceSegments);
    REQUIRE(made.ok());
    auto network = std::move(made).takeValue();
    auto saved   = captureRoadNetwork(network);
    REQUIRE(saved.ok());
    auto  legacy    = saved.value();
    auto& placement = *legacy.getIf<eve::Value::Object>()
                           ->at("edges")
                           .getIf<eve::Value::Array>()
                           ->at(0)
                           .getIf<eve::Value::Object>()
                           ->at("style")
                           .getIf<eve::Value::Object>()
                           ->at("sidePlacement")
                           .getIf<eve::Value::Object>();
    placement.erase("pathSampling");
    auto restored = restoreRoadNetwork(legacy);
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].style.sidePlacement.pathSampling == RoadSidePathSampling::Analytic);
    placement.emplace("pathSampling", eve::Value(std::int64_t{2}));
    REQUIRE(!restoreRoadNetwork(legacy).ok());
    auto invalid                       = network.edges()[0].style;
    invalid.sidePlacement.pathSampling = static_cast<RoadSidePathSampling>(2);
    REQUIRE(!network.setEdgeStyle(1, invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == saved.value());
    RoadPresetDraft draft;
    draft.source = network;
    draft.types  = {{"root", "", {}}};
    draft.bindings.push_back({1, "root", {}});
    draft.types[0].overrides.sidePlacement = network.edges()[0].style.sidePlacement;
    auto document                          = RoadPresetDocument::create(draft);
    REQUIRE(document.ok());
    auto edit                                           = document.value()->beginEdit();
    edit.types[0].overrides.sidePlacement->pathSampling = RoadSidePathSampling::Analytic;
    auto prepared                                       = document.value()->prepare(std::move(edit));
    REQUIRE(prepared.ok());
    REQUIRE(document.value()->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(document.value()->undo().ok());
    REQUIRE(document.value()->network().edges()[0].style.sidePlacement.pathSampling ==
            RoadSidePathSampling::SurfaceSegments);
    auto history = document.value()->capture();
    REQUIRE(history.ok());
    auto restore = document.value()->prepareRestore(history.value());
    REQUIRE(restore.ok());
    REQUIRE(document.value()->commit(std::move(restore).takeValue()).ok());
    REQUIRE(document.value()->redo().ok());
    REQUIRE(document.value()->network().edges()[0].style.sidePlacement.pathSampling == RoadSidePathSampling::Analytic);
}
