#include "scene/editor/SceneEditorSession.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("scene.editor.session_create_restore_and_undo_share_one_history") {
    using namespace eve::editing;
    eve::scene_editor::SceneEditorSession session("level");
    const auto                            empty = session.saveJson();
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "box"}}).ok());
    const auto created = session.saveJson();
    REQUIRE(created != empty);
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), empty);
    REQUIRE(session.redo().ok());
    REQUIRE_EQ(session.saveJson(), created);
    REQUIRE(session.restoreJson(empty).ok());
    REQUIRE_EQ(session.saveJson(), empty);
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), created);
    REQUIRE(!session.restoreJson("{broken").ok());
    REQUIRE_EQ(session.saveJson(), created);
    REQUIRE(session.execute("scene.object.rename.v1", Value::Object{{"object", "box"}, {"name", "Crate"}}).ok());
    const auto renamed = session.saveJson();
    REQUIRE(!session
                 .execute("scene.object.update.v1", Value::Object{{"object", "box"},
                                                                  {"name", "Partial"},
                                                                  {"parent", "missing"},
                                                                  {"position", Value::Array{3, 4, 5}}})
                 .ok());
    REQUIRE_EQ(session.saveJson(), renamed);
}
