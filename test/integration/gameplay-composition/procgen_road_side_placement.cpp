// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "modules": [
    "procgen",
    "procgen_editing"
  ],
  "origins": [
    "procgen_road_side_placement.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include <cmath>
#include <limits>
#include "common/ProcgenWorldQuery.h"
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;
namespace {
RoadNetwork placementNetwork(RoadStyle style) {
    style.laneWidth           = 3;
    style.automaticStructures = false;
    RoadNetwork network;
    auto        a = network.addNode(0, 0, 0), b = network.addNode(0, 0, 30);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(network.addEdge(a.value(), b.value(), {{0, 0, 0}, {0, 0, 15}, {0, 0, 30}}, 1, 1, style).ok());
    return network;
}
}  // namespace

TEST_CASE("procgen.road.authoring.sidePlacementPresetHistoryMigrationAndAtomicFailure") {
    RoadStyle style;
    style.sidePlacement = {RoadSideReference::Center, 2, 1.02f, 45};
    auto network        = placementNetwork(style);
    auto encoded        = captureRoadNetwork(network);
    REQUIRE(encoded.ok());
    auto restored = restoreRoadNetwork(encoded.value());
    REQUIRE(restored.ok());
    REQUIRE_EQ(restored.value().edges()[0].style.sidePlacement.yawDegrees, 45);
    auto  legacy = encoded.value();
    auto& fields = *legacy.getIf<eve::Value::Object>()
                        ->at("edges")
                        .getIf<eve::Value::Array>()
                        ->at(0)
                        .getIf<eve::Value::Object>()
                        ->at("style")
                        .getIf<eve::Value::Object>();
    fields.erase("sidePlacement");
    auto migrated = restoreRoadNetwork(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().edges()[0].style.sidePlacement.reference == RoadSideReference::LegacyOutside);
    REQUIRE_EQ(migrated.value().edges()[0].style.sidePlacement.verticalOffset, 0);
    const auto before = captureRoadNetwork(network).value();
    for (int variant = 0; variant < 3; ++variant) {
        auto bad = style;
        if (variant == 0) bad.sidePlacement.reference = static_cast<RoadSideReference>(3);
        if (variant == 1) bad.sidePlacement.lateralOffset = std::numeric_limits<float>::infinity();
        if (variant == 2) bad.sidePlacement.yawDegrees = 361;
        REQUIRE(!network.setEdgeStyle(network.edges()[0].id, bad).ok());
        REQUIRE(captureRoadNetwork(network).value() == before);
    }
    RoadPresetDraft draft;
    draft.source                           = network;
    draft.types                            = {{"root", "", {}}};
    draft.types[0].overrides.sidePlacement = style.sidePlacement;
    draft.bindings.push_back({network.edges()[0].id, "root", {}});
    auto document = RoadPresetDocument::create(draft);
    REQUIRE(document.ok());
    auto edit                             = document.value()->beginEdit();
    edit.types[0].overrides.sidePlacement = RoadSidePlacement{RoadSideReference::AsphaltEdges, -1, .02f, -30};
    auto prepared                         = document.value()->prepare(std::move(edit));
    REQUIRE(prepared.ok());
    REQUIRE(document.value()->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(document.value()->undo().ok());
    REQUIRE_EQ(document.value()->network().edges()[0].style.sidePlacement.lateralOffset, 2);
    auto save = document.value()->capture();
    REQUIRE(save.ok());
    auto restore = document.value()->prepareRestore(save.value());
    REQUIRE(restore.ok());
    REQUIRE(document.value()->commit(std::move(restore).takeValue()).ok());
    REQUIRE(document.value()->redo().ok());
    const auto& result = document.value()->network().edges()[0].style.sidePlacement;
    REQUIRE(result.reference == RoadSideReference::AsphaltEdges);
    REQUIRE_EQ(result.lateralOffset, -1);
    REQUIRE_EQ(result.yawDegrees, -30);
}

TEST_CASE("procgen.road.authoring.sidePlacementRotatesTerrainFootprintBeforeWorldUpOffset") {
    struct Plane final : eve::IProcgenWorldQuery {
        int                                 calls = 0;
        eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float, float, std::uint64_t) override {
            ++calls;
            eve::ProcgenSurfaceHit hit;
            hit.hit = true;
            hit.x   = x;
            hit.z   = z;
            hit.y   = x + 2 * z;
            return eve::Result<eve::ProcgenSurfaceHit>::success(hit);
        }
    } query;
    RoadStyle style;
    style.sidePlacement     = {RoadSideReference::Center, 2, 1.02f, 90};
    style.sideTerrain       = {RoadSideTerrainMode::Highest, -1, 1, -2, 2, 3};
    auto            network = placementNetwork(style);
    RoadBakeOptions options;
    options.placementWorldQuery = &query;
    options.sideObjectClearance = 0;
    options.sideObjectSpacing   = 5;
    options.includeNavigation = options.includeMarkings = false;
    auto baked                                          = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int sides = 0;
    for (int i = 0; i < baked.value().placements.getCount(); ++i) {
        const auto& points = baked.value().placements;
        if (points.getStringAttribute(i, "road_role", "").find("side.") != 0) continue;
        ++sides;
        REQUIRE(std::fabs(points.points()[i].yaw - 90) < 1e-4f);
        REQUIRE(std::fabs(points.points()[i].y - (points.points()[i].x + 2 * points.points()[i].z + 4 + 1.02f)) <
                1e-4f);
    }
    REQUIRE_EQ(sides, 7);
    REQUIRE_EQ(query.calls, 63);
    options.maximumPlacementQueries = 62;
    REQUIRE(!bakeRoadNetwork(network, options).ok());
}
