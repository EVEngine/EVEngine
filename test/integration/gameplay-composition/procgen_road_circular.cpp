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
    "procgen_road_circular.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplineCircularArc.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.circularArcLimitsAndPresetUndo") {
    auto arc = detail::makeCircularArcXZ(0, 0, 8, 0, 1, 0, detail::CircularArcReference::Start);
    REQUIRE(arc.ok());
    REQUIRE(arc.value().line);
    REQUIRE(!detail::makeCircularArcXZ(0, 0, 8, 0, -1, 0, detail::CircularArcReference::Start).ok());
    REQUIRE(!detail::makeCircularArcXZ(0, 0, 0, 0, 1, 0, detail::CircularArcReference::Start).ok());
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    auto            source = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(source.ok());
    draft.source                             = std::move(source).takeValue();
    draft.types                              = {{"circle", "", {}}};
    draft.bindings                           = {{1, "circle", {}}};
    draft.types[0].pointDefaults.pathControl = RoadPathControl::Circular;
    auto created                             = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto doc                                    = std::move(created).takeValue();
    auto edit                                   = doc->beginEdit();
    edit.types[0].pointDefaults.circularFromEnd = true;
    auto token                                  = doc->prepare(std::move(edit));
    REQUIRE(token.ok());
    REQUIRE(doc->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc->undo().ok());
    auto saved = doc->capture();
    REQUIRE(saved.ok());
    auto restore = doc->prepareRestore(saved.value());
    REQUIRE(restore.ok());
    REQUIRE(doc->commit(std::move(restore).takeValue()).ok());
    REQUIRE(!doc->network().edges()[0].controlPoints[0].circularFromEnd);
    REQUIRE(doc->redo().ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].circularFromEnd);
}
