#include "editor/EditorCurveDocument.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.curve.timeline_is_reversible_sampled_and_serializable") {
    EditorCurveDocument curve("particle-size-color");
    auto start = curve.makeSetKey({StableId("start"), 0.0, 0.0, 0.0, 0.0, "linear"});
    auto end = curve.makeSetKey({StableId("end"), 1.0, 2.0, 0.0, 0.0, "linear"});
    REQUIRE(start.ok()); REQUIRE(end.ok()); CHECK(curve.applyDomainOperation(start.value()).ok()); CHECK(curve.applyDomainOperation(end.value()).ok());
    auto black = curve.makeSetStop({StableId("black"), 0.0, {0.0, 0.0, 0.0, 1.0}});
    auto white = curve.makeSetStop({StableId("white"), 1.0, {1.0, 1.0, 1.0, 1.0}});
    REQUIRE(black.ok()); REQUIRE(white.ok()); CHECK(curve.applyDomainOperation(black.value()).ok()); CHECK(curve.applyDomainOperation(white.value()).ok());
    CHECK_EQ(curve.sampleCurve(0.5), 1.0); CHECK_EQ(curve.sampleGradient(0.5)[0], 0.5);
    const auto preview = curve.preview(16);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied)); CHECK_EQ(preview.curveSamples.size(), 16U);
    auto remove = curve.makeDeleteKey(StableId("end")); REQUIRE(remove.ok()); CHECK(curve.applyDomainOperation(remove.value()).ok());
    DomainOperation undo = remove.value(); undo.type = remove.value().inverseType; undo.payload = remove.value().inverse;
    CHECK(curve.applyDomainOperation(undo).ok()); CHECK_EQ(curve.keys().size(), 2U);
    EditorCurveDocument restored("particle-size-color"); CHECK(restored.loadSnapshot(curve.snapshotValue()).ok());
    CHECK_EQ(restored.snapshotValue(), curve.snapshotValue());
    CHECK_EQ(static_cast<int>(curve.preview(5000, 4096).status), static_cast<int>(EditorStatus::Rejected));
}
