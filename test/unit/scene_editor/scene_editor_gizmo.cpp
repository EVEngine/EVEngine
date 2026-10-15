#include <cmath>
#include "editor/TransformGizmo.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("scene.editor.gizmo_plane_scale_changes_only_selected_axes") {
    eve::editor::TransformGizmo g;
    g.setMode("scale");
    g.setScale(2, 3, 4);
    REQUIRE_EQ(g.pick(.2f, .2f, 5, 0, 0, -1), std::string("xy"));
    REQUIRE(g.beginDrag("xy", .2f, .2f, 5, 0, 0, -1));
    REQUIRE(g.updateDrag(.7f, .45f, 5, 0, 0, -1));
    REQUIRE(std::abs(g.getScaleX() - 3.f) < .001f);
    REQUIRE(std::abs(g.getScaleY() - 3.75f) < .001f);
    REQUIRE_EQ(g.getScaleZ(), 4.f);
}

TEST_CASE("scene.editor.gizmo_uniform_scale_preserves_proportions_and_ignores_previous_axis") {
    eve::editor::TransformGizmo g;
    g.setMode("scale");
    REQUIRE(g.beginDrag("z", 0, 5, 0, 0, -1, 0));
    g.endDrag();
    g.setScale(2, 3, 4);
    REQUIRE_EQ(g.pick(0, 0, 5, 0, 0, -1), std::string("xyz"));
    REQUIRE(g.beginDrag("xyz", 0, 0, 5, 0, 0, -1));
    REQUIRE(g.updateDrag(0, 1, 5, 0, 0, -1));
    REQUIRE_EQ(g.getScaleX(), 4.f);
    REQUIRE_EQ(g.getScaleY(), 6.f);
    REQUIRE_EQ(g.getScaleZ(), 8.f);
}

TEST_CASE("scene.editor.gizmo_plane_translate_keeps_locked_axis") {
    eve::editor::TransformGizmo g;
    g.setPosition(0, 0, .13f);
    g.setSnapTranslate(.25f, .25f, .25f);
    REQUIRE_EQ(g.pick(.2f, .2f, 5, 0, 0, -1), std::string("xy"));
    REQUIRE(g.beginDrag("xy", .2f, .2f, 5, 0, 0, -1));
    REQUIRE(g.updateDrag(.7f, .95f, 5, 0, 0, -1));
    REQUIRE_EQ(g.getPositionX(), .5f);
    REQUIRE_EQ(g.getPositionY(), .75f);
    REQUIRE_EQ(g.getPositionZ(), .13f);
}
