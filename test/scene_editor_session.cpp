#include "scene/editor/SceneEditorSession.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

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

TEST_CASE("scene.editor.physics_placement_commits_selected_objects_as_one_undo_step") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement");
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{{"object", "wall"}, {"position", Value::Array{2.0, 0.5, 0.0}}})
                .ok());
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    const auto              before = session.saveJson();
    PhysicsPlacementRequest request;
    request.objects.push_back({ObjectId("wall"), {}, 0.5, 2.0, 2.0, false});
    request.objects.push_back({ObjectId("crate"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    REQUIRE(session.physicsPlacementActive());
    for (int step = 0; step < 90; ++step) REQUIRE(session.updatePhysicsPlacement(4.0, 0.5, 0.0).ok());
    REQUIRE_EQ(session.saveJson(), before);
    REQUIRE(session.commitPhysicsPlacement().ok());
    REQUIRE(!session.physicsPlacementActive());
    REQUIRE(session.saveJson() != before);
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), before);
}

TEST_CASE("scene.editor.physics_placement_cancel_is_non_destructive") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-cancel");
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    const auto              before = session.saveJson();
    PhysicsPlacementRequest request;
    request.objects.push_back({ObjectId("crate"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    REQUIRE(session.updatePhysicsPlacement(3.0, 0.0, 0.0).ok());
    REQUIRE(session.cancelPhysicsPlacement().ok());
    REQUIRE_EQ(session.saveJson(), before);
}

TEST_CASE("scene.editor.physics_placement_transform_commits_scale_and_undoes_atomically") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-scale");
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    const auto              before = session.saveJson();
    PhysicsPlacementRequest request;
    request.primaryObject = ObjectId("crate");
    request.objects.push_back({ObjectId("crate"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    auto frame = session.updatePhysicsPlacementTransform(1.0, 0.0, 0.0, 0.0, 0.0, 0.25, 2.0, 1.5, 0.75);
    REQUIRE(frame.ok());
    CHECK(std::abs(frame.value().objects[0].transform.scaleX - 2.0) < 0.001);
    REQUIRE(session.commitPhysicsPlacement().ok());
    REQUIRE(session.saveJson() != before);
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), before);
}

TEST_CASE("scene.editor.physics_placement_auto_hull_cache_is_explicit_and_session_owned") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-auto-hull");
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "mesh"}}).ok());
    PhysicsPlacementRequest missing;
    auto                    object = PhysicsPlacementObject{ObjectId("mesh"), {}, 0.5, 0.5, 0.5, true};
    object.shape                   = PhysicsPlacementShape::Auto;
    missing.objects.push_back(object);
    const auto missingResult = session.beginPhysicsPlacement(std::move(missing));
    REQUIRE(missingResult.code() == Status::NotFound);
    const std::vector<float> cube = {-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,
                                     -0.5f, -0.5f, 0.5f,  0.5f, -0.5f, 0.5f,  -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f};
    REQUIRE(session.cachePhysicsPlacementHull(ObjectId("mesh"), cube, 32).ok());
    const auto invalidCache = session.cachePhysicsPlacementHull(
        ObjectId("mesh"), {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f}, 32);
    REQUIRE(invalidCache.code() == Status::Rejected);
    PhysicsPlacementRequest cached;
    cached.objects.push_back(object);
    REQUIRE(session.beginPhysicsPlacement(std::move(cached)).ok());
    REQUIRE(session.cancelPhysicsPlacement().ok());
    REQUIRE(session.removePhysicsPlacementHull(ObjectId("mesh")).ok());
    const auto missingRemoval = session.removePhysicsPlacementHull(ObjectId("mesh"));
    REQUIRE(missingRemoval.code() == Status::NoOp);
}

TEST_CASE("scene.editor.physics_placement_compound_cache_round_trips_and_detects_stale_resources") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession       source("physics-placement-compound-cache-source");
    PhysicsPlacementCollider left;
    left.halfExtentX               = 0.25;
    left.halfExtentY               = 1.0;
    left.halfExtentZ               = 0.5;
    left.localX                    = -0.75;
    PhysicsPlacementCollider right = left;
    right.localX                   = 0.75;
    REQUIRE(source.cachePhysicsPlacementCompound(ObjectId("arch"), "mesh-hash-1", {left, right}).ok());
    const auto json = source.savePhysicsPlacementColliderCacheJson();

    SceneEditorSession restored("physics-placement-compound-cache-restored");
    REQUIRE(restored.execute("scene.object.create.v1", Value::Object{{"object", "arch"}}).ok());
    REQUIRE(restored.restorePhysicsPlacementColliderCacheJson(json).ok());
    PhysicsPlacementObject object{ObjectId("arch"), {}, 1.0, 1.0, 0.5, true};
    object.shape               = PhysicsPlacementShape::Auto;
    object.colliderResourceKey = "mesh-hash-1";
    PhysicsPlacementRequest request;
    request.objects.push_back(object);
    REQUIRE(restored.beginPhysicsPlacement(std::move(request)).ok());
    REQUIRE(restored.cancelPhysicsPlacement().ok());

    object.colliderResourceKey = "mesh-hash-2";
    PhysicsPlacementRequest stale;
    stale.objects.push_back(object);
    CHECK_EQ(static_cast<int>(restored.beginPhysicsPlacement(std::move(stale)).code()),
             static_cast<int>(Status::Conflict));
    const auto before = restored.savePhysicsPlacementColliderCacheJson();
    CHECK_EQ(static_cast<int>(restored.restorePhysicsPlacementColliderCacheJson("{}").code()),
             static_cast<int>(Status::Rejected));
    REQUIRE_EQ(restored.savePhysicsPlacementColliderCacheJson(), before);
}

TEST_CASE("scene.editor.physics_placement_detects_scene_changes_before_commit") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-conflict");
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    PhysicsPlacementRequest request;
    request.objects.push_back({ObjectId("crate"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    REQUIRE(session.execute("scene.object.rename.v1", Value::Object{{"object", "crate"}, {"name", "Changed"}}).ok());
    const auto committed = session.commitPhysicsPlacement();
    REQUIRE_EQ(static_cast<int>(committed.code()), static_cast<int>(Status::Conflict));
    REQUIRE(session.physicsPlacementActive());
    REQUIRE(session.cancelPhysicsPlacement().ok());
}

TEST_CASE("scene.editor.physics_placement_converts_mixed_parent_world_preview_back_to_local_trs") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-parents");
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{{"object", "left-parent"}, {"position", Value::Array{10.0, 0.0, 0.0}}})
                .ok());
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{{"object", "right-parent"}, {"position", Value::Array{-10.0, 0.0, 0.0}}})
                .ok());
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{
                             {"object", "left"}, {"parent", "left-parent"}, {"position", Value::Array{1.0, 0.0, 0.0}}})
                .ok());
    REQUIRE(session
                .execute("scene.object.create.v1", Value::Object{{"object", "right"},
                                                                 {"parent", "right-parent"},
                                                                 {"position", Value::Array{2.0, 0.0, 0.0}}})
                .ok());
    PhysicsPlacementRequest request;
    request.primaryObject = ObjectId("left");
    request.objects.push_back({ObjectId("left"), {}, 0.5, 0.5, 0.5, true});
    request.objects.push_back({ObjectId("right"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    for (int step = 0; step < 30; ++step) REQUIRE(session.updatePhysicsPlacement(3.5, 0.0, 0.0).ok());
    REQUIRE(session.commitPhysicsPlacement().ok());
    const auto  snapshot = session.snapshot();
    const auto* root     = snapshot.getIf<Value::Object>();
    REQUIRE(root != nullptr);
    const auto* objects = root->at("objects").getIf<Value::Array>();
    REQUIRE(objects != nullptr);
    const auto localX = [&](const std::string& id) {
        for (const auto& entry : *objects) {
            const auto* fields = entry.getIf<Value::Object>();
            if (!fields) continue;
            const auto* objectId = fields->at("id").getIf<std::string>();
            if (objectId && *objectId == id) {
                const auto* transform = fields->at("transform").getIf<Value::Object>();
                return *transform->at("x").getIf<double>();
            }
        }
        return -1000.0;
    };
    CHECK(std::abs(localX("left") - 3.0) < 0.08);
    CHECK(std::abs(localX("right") - 4.0) < 0.08);
}

TEST_CASE("scene.editor.physics_placement_selected_parent_uses_its_final_world_for_child_local_trs") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-selected-parent");
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{{"object", "parent"}, {"position", Value::Array{5.0, 0.0, 0.0}}})
                .ok());
    REQUIRE(session
                .execute(
                    "scene.object.create.v1",
                    Value::Object{{"object", "child"}, {"parent", "parent"}, {"position", Value::Array{1.0, 0.0, 0.0}}})
                .ok());
    PhysicsPlacementRequest request;
    request.primaryObject = ObjectId("parent");
    request.objects.push_back({ObjectId("parent"), {}, 0.4, 0.4, 0.4, true});
    request.objects.push_back({ObjectId("child"), {}, 0.4, 0.4, 0.4, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    for (int step = 0; step < 30; ++step) REQUIRE(session.updatePhysicsPlacement(7.5, 0.0, 0.0).ok());
    REQUIRE(session.commitPhysicsPlacement().ok());
    const auto  snapshot = session.snapshot();
    const auto* root     = snapshot.getIf<Value::Object>();
    REQUIRE(root != nullptr);
    const auto* objects = root->at("objects").getIf<Value::Array>();
    REQUIRE(objects != nullptr);
    const auto localX = [&](const std::string& id) {
        for (const auto& entry : *objects) {
            const auto* fields = entry.getIf<Value::Object>();
            if (!fields) continue;
            const auto* objectId = fields->at("id").getIf<std::string>();
            if (objectId && *objectId == id) {
                const auto* transform = fields->at("transform").getIf<Value::Object>();
                return *transform->at("x").getIf<double>();
            }
        }
        return -1000.0;
    };
    CHECK(std::abs(localX("parent") - 7.0) < 0.08);
    CHECK(std::abs(localX("child") - 1.0) < 0.08);
}
