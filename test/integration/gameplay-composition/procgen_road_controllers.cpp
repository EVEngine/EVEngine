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
    "procgen_road_controllers.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.outgoingControllerPresetSaveAndUndo") {
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    auto            source = RoadNetwork::makeCurve(36.f, 2);
    REQUIRE(source.ok());
    draft.source                             = std::move(source).takeValue();
    draft.types                              = {{"type", "", {}}};
    draft.bindings                           = {{1, "type", {}}};
    draft.types[0].pointDefaults.pathControl = RoadPathControl::StraightXZ;
    auto created                             = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto doc                                = std::move(created).takeValue();
    auto edit                               = doc->beginEdit();
    edit.types[0].pointDefaults.pathControl = RoadPathControl::StraightXYZ;
    auto token                              = doc->prepare(std::move(edit));
    REQUIRE(token.ok());
    REQUIRE(doc->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc->undo().ok());
    auto state = doc->capture();
    REQUIRE(state.ok());
    auto restore = doc->prepareRestore(state.value());
    REQUIRE(restore.ok());
    REQUIRE(doc->commit(std::move(restore).takeValue()).ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].pathControl == RoadPathControl::StraightXZ);
    REQUIRE(doc->redo().ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].pathControl == RoadPathControl::StraightXYZ);
}
