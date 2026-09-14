#include "editing/EditingValueJson.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
#include "scene/editing/SceneTarget.h"
#include "scene/editor/SceneEditorSession.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("scene.editor.live_session_preserves_links_rejects_divergence_and_stale_host") {
    using namespace eve::editing;
    auto created = eve::scene::SceneHost::createHost("scene-session-live");
    REQUIRE(created.ok());
    auto* host = created.value();
    host->setTree(eve::scene::node("root", {eve::scene::node("child")}));
    int external = 42;
    host->findById("child").value()->links.push_back({777, &external, 0});
    eve::scene_editor::SceneEditorSession session(
        std::make_unique<eve::scene_editing::SceneHostEditorTarget>("live", host));
    REQUIRE(session
                .execute("scene.object.update.v1",
                         Value::Object{{"object", "child"}, {"name", "Changed"}, {"position", Value::Array{2, 3, 4}}})
                .ok());
    REQUIRE_EQ(host->findById("child").value()->x, 2.f);
    REQUIRE_EQ(host->findById("child").value()->links.at(0).target, &external);
    REQUIRE(!session.execute("scene.object.delete.v1", Value::Object{{"object", "child"}}).ok());
    REQUIRE(host->hasNode("child"));
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(host->findById("child").value()->x, 0.f);
    REQUIRE_EQ(host->findById("child").value()->name, std::string("child"));
    const auto before = session.saveJson();
    REQUIRE(host->appendNode(eve::scene::SceneNode{.id = "outside", .name = "Outside"}, "root") ==
            eve::scene::SceneMutationStatus::Applied);
    REQUIRE(!session.execute("scene.object.rename.v1", Value::Object{{"object", "child"}, {"name", "Refused"}}).ok());
    REQUIRE_EQ(session.saveJson(), before);
    ecs::DestroyEntity(host);
    REQUIRE(!session.execute("scene.object.rename.v1", Value::Object{{"object", "child"}, {"name", "Stale"}}).ok());
}

TEST_CASE("scene.editor.live_restore_reorders_hierarchy_atomically_and_can_reopen") {
    using namespace eve::editing;
    auto created = eve::scene::SceneHost::createHost("scene-session-restore");
    REQUIRE(created.ok());
    auto* host = created.value();
    host->setTree(eve::scene::node("z-root", {eve::scene::node("a-child")}));
    eve::scene_editor::SceneEditorSession session(
        std::make_unique<eve::scene_editing::SceneHostEditorTarget>("live", host));
    const auto before = session.saveJson();
    REQUIRE(session.restoreJson(before).ok());
    // Published arena is sorted by id, so the importer must not assume parent-first storage.
    eve::scene_editor::SceneEditorSession reopened(
        std::make_unique<eve::scene_editing::SceneHostEditorTarget>("live-again", host));
    REQUIRE_EQ(reopened.saveJson(), before);
    auto  changed = session.snapshot();
    auto& objects = *changed.getIf<Value::Object>()->at("objects").getIf<Value::Array>();
    for (auto& entry : objects) {
        auto& object     = *entry.getIf<Value::Object>();
        object["parent"] = *object.at("id").getIf<std::string>() == "a-child" ? "" : "a-child";
    }
    REQUIRE(session.restoreJson(editorValueToJson(changed)).ok());
    REQUIRE_EQ(host->getParentById("z-root")->id, std::string("a-child"));
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), before);
}

TEST_CASE("scene.editor.detached_root_keeps_world_transform") {
    auto created = eve::scene::SceneHost::createHost("scene-detached-root");
    REQUIRE(created.ok());
    auto* host = created.value();
    host->setTree(eve::scene::node("root", {eve::scene::node("child")}));
    eve::scene_editor::SceneEditorSession session(
        std::make_unique<eve::scene_editing::SceneHostEditorTarget>("live", host));
    REQUIRE(session
                .execute("scene.object.update.v1",
                         eve::editing::Value::Object{
                             {"object", "child"}, {"parent", ""}, {"position", eve::editing::Value::Array{3, 4, 5}}})
                .ok());
    eve::scene::TransformSystem::updateHost(host);
    REQUIRE_EQ(host->findById("child").value()->world[3][0], 3.f);
    REQUIRE_EQ(host->findById("child").value()->world[3][1], 4.f);
}
