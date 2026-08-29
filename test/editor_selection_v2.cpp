#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorSelection.h"

using namespace eve::editor;

TEST_CASE("editor.v2.selection_and_focus_are_presentation_neutral_channels") {
    EditorSelectionService selection;
    int                    notifications = 0;
    REQUIRE(selection.subscribe("park.hud", [&](const SelectionSnapshot&) { ++notifications; }).accepted());
    SelectionItem tree{SelectionDomain::Scene, TargetId("park"), StableId("tree-1"), "park.tree"};
    auto          selected = selection.set("world", {tree}, tree);
    REQUIRE(selected.accepted());
    CHECK_EQ(selected.value->sequence, static_cast<std::uint64_t>(1));
    CHECK(selection.snapshot("world").primary == tree);
    CHECK_EQ(notifications, 1);
    CHECK_EQ(static_cast<int>(selection.set("world", {}, tree).status), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(selection.clear("world").accepted());
    CHECK_EQ(notifications, 2);
    CHECK(selection.unsubscribe("park.hud"));

    EditorFocusService focus;
    auto               focused = focus.focus("world", StableId("viewport"), StableId("tree-1"));
    REQUIRE(focused.accepted());
    CHECK(focus.snapshot("world").surface == StableId("viewport"));
}
