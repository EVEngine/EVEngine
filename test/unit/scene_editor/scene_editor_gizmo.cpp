// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "scene_editor_gizmo.cpp"
  ]
}
*/
// clang-format on

#include "editor/TransformGizmo.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>

namespace {
void near(float actual, float expected) { CHECK(std::abs(actual - expected) < 0.001f); }
}  // namespace

TEST_CASE("scene.editor.gizmo.dragLifecycleAndTransformModesContract") {
    SUB_CASE("drag lifecycle rejects updates without an active handle") {
        eve::editor::TransformGizmo gizmo;
        CHECK(!gizmo.isDragging());
        CHECK(!gizmo.updateDrag(0, 0, 5, 0, 0, -1));
        CHECK(!gizmo.beginDrag("", 0, 0, 5, 0, 0, -1));
        CHECK(!gizmo.isDragging());

        REQUIRE(gizmo.beginDrag("xy", .2f, .2f, 5, 0, 0, -1));
        CHECK(gizmo.isDragging());
        CHECK_EQ(gizmo.getActiveAxis(), std::string("xy"));
        gizmo.endDrag();
        CHECK(!gizmo.isDragging());
        CHECK_EQ(gizmo.getActiveAxis(), std::string(""));
        CHECK(!gizmo.updateDrag(.7f, .9f, 5, 0, 0, -1));
    };
    SUB_CASE("XY translation snaps selected axes only") {
        eve::editor::TransformGizmo gizmo;
        gizmo.setPosition(0, 0, .13f);
        gizmo.setSnapTranslate(.25f, .25f, .25f);
        REQUIRE_EQ(gizmo.pick(.2f, .2f, 5, 0, 0, -1), std::string("xy"));
        REQUIRE(gizmo.beginDrag("xy", .2f, .2f, 5, 0, 0, -1));
        REQUIRE(gizmo.updateDrag(.7f, .95f, 5, 0, 0, -1));
        near(gizmo.getPositionX(), .5f);
        near(gizmo.getPositionY(), .75f);
        near(gizmo.getPositionZ(), .13f);
    };
    SUB_CASE("XY scale changes selected axes only") {
        eve::editor::TransformGizmo gizmo;
        gizmo.setMode("scale");
        gizmo.setScale(2, 3, 4);
        REQUIRE_EQ(gizmo.pick(.2f, .2f, 5, 0, 0, -1), std::string("xy"));
        REQUIRE(gizmo.beginDrag("xy", .2f, .2f, 5, 0, 0, -1));
        REQUIRE(gizmo.updateDrag(.7f, .45f, 5, 0, 0, -1));
        near(gizmo.getScaleX(), 3.f);
        near(gizmo.getScaleY(), 3.75f);
        near(gizmo.getScaleZ(), 4.f);
    };
    SUB_CASE("uniform scale preserves ratios") {
        eve::editor::TransformGizmo gizmo;
        gizmo.setMode("scale");
        REQUIRE(gizmo.beginDrag("z", 0, 5, 0, 0, -1, 0));
        gizmo.endDrag();
        gizmo.setScale(2, 3, 4);
        REQUIRE_EQ(gizmo.pick(0, 0, 5, 0, 0, -1), std::string("xyz"));
        REQUIRE(gizmo.beginDrag("xyz", 0, 0, 5, 0, 0, -1));
        REQUIRE(gizmo.updateDrag(0, 1, 5, 0, 0, -1));
        near(gizmo.getScaleX(), 4.f);
        near(gizmo.getScaleY(), 6.f);
        near(gizmo.getScaleZ(), 8.f);
    };
}
