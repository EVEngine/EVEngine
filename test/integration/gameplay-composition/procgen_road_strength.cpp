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
    "procgen_road_strength.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.splineStrengthPresetOverrideSaveAndUndo") {
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    auto            source = RoadNetwork::makeCurve(36.f, 2);
    REQUIRE(source.ok());
    draft.source                                = std::move(source).takeValue();
    draft.types                                 = {{"curve", "", {}}};
    draft.types[0].pointDefaults.splineStrength = .25f;
    draft.bindings                              = {{1, "curve", {}}};
    auto created                                = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto doc = std::move(created).takeValue();
    REQUIRE(doc->network().edges()[0].controlPoints[0].splineStrength == .25f);
    auto edit                                  = doc->beginEdit();
    edit.types[0].pointDefaults.splineStrength = 1.f;
    auto prepared                              = doc->prepare(std::move(edit));
    REQUIRE(prepared.ok());
    REQUIRE(doc->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(doc->undo().ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].splineStrength == .25f);
    auto state = doc->capture();
    REQUIRE(state.ok());
    auto token = doc->prepareRestore(state.value());
    REQUIRE(token.ok());
    REQUIRE(doc->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc->redo().ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].splineStrength == 1.f);
}
