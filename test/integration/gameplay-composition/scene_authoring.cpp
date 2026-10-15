#include <cmath>
#include <limits>
#include "scene/editing/SceneAuthoring.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editor/SceneEditorSession.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
using eve::editing::Value;
using eve::scene_editor::SceneEditorSession;
std::unique_ptr<SceneEditorSession> authoring() {
    return std::make_unique<SceneEditorSession>(
        std::make_unique<eve::scene_editing::SceneAuthoringDocumentTarget>("authoring-test"));
}
Value bounds() { return Value::Array{-0.5, -0.5, -0.5, 0.5, 0.5, 0.5}; }
Value camera() {
    return Value::Object{{"eye", Value::Array{0.0, 0.0, 10.0}},
                         {"target", Value::Array{0.0, 0.0, 0.0}},
                         {"up", Value::Array{0.0, 1.0, 0.0}},
                         {"fov", 60.0},
                         {"near", 0.1},
                         {"far", 100.0},
                         {"exposure", 0.0},
                         {"ambient", Value::Array{0.2, 0.2, 0.2}}};
}
eve::scene_editing::SceneAuthoringDocumentTarget copied(const SceneEditorSession& session) {
    eve::scene_editing::SceneAuthoringDocumentTarget target("inspect");
    auto                                             operation = target.makeRestore(session.snapshot());
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    return target;
}
double component(const Value& value, const char* key, std::size_t index) {
    return *value.getIf<Value::Object>()->at(key).getIf<Value::Array>()->at(index).getIf<double>();
}
}  // namespace

TEST_CASE("scene.authoring.document_atomic_spawn_presentation_roundtrip_history") {
    auto       session = authoring();
    const auto empty   = session->saveJson();
    REQUIRE(
        session
            ->execute("scene.authoring.spawn.v1", Value::Object{{"object", "crate"},
                                                                {"source", Value::Object{{"kind", "box"}, {"seed", 7}}},
                                                                {"position", Value::Array{0.0, 3.0, 0.0}}})
            .ok());
    const auto spawned = session->saveJson();
    REQUIRE(spawned != empty);
    REQUIRE(session->revision() > 0);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), empty);
    REQUIRE(session->redo().ok());
    REQUIRE_EQ(session->saveJson(), spawned);
    REQUIRE(session->execute("scene.authoring.camera.set.v1", Value::Object{{"value", camera()}}).ok());
    const auto cameraJson = session->saveJson();
    REQUIRE(session
                ->execute("scene.authoring.lighting.set.v1",
                          Value::Object{{"value", Value::Object{{"background", Value::Array{0.1, 0.2, 0.3}},
                                                                {"direction", Value::Array{-0.5, -1.0, -0.4}},
                                                                {"color", Value::Array{1.0, 1.0, 1.0}}}}})
                .ok());
    const auto complete = session->saveJson();
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), cameraJson);
    REQUIRE(session->redo().ok());
    REQUIRE_EQ(session->saveJson(), complete);
    auto restored = authoring();
    REQUIRE(restored->restoreJson(complete).ok());
    REQUIRE_EQ(restored->saveJson(), complete);
    REQUIRE(restored->undo().ok());
    REQUIRE_EQ(restored->saveJson(), empty);
}

TEST_CASE("scene.authoring.rejected_source_schema_camera_and_parent_preserve_state") {
    auto       session = authoring();
    const auto empty   = session->saveJson();
    REQUIRE(!session
                 ->execute("scene.authoring.spawn.v1",
                           Value::Object{{"object", "broken"},
                                         {"source", Value::Object{{"kind", "asset"}, {"path", "missing.glb"}}}})
                 .ok());
    REQUIRE_EQ(session->saveJson(), empty);
    REQUIRE(!session
                 ->execute("scene.authoring.spawn.v1",
                           Value::Object{
                               {"object", "broken"}, {"parent", "missing"}, {"source", Value::Object{{"kind", "box"}}}})
                 .ok());
    REQUIRE_EQ(session->saveJson(), empty);
    auto badCamera                            = camera();
    (*badCamera.getIf<Value::Object>())["up"] = Value::Array{0.0, 0.0, 1.0};
    REQUIRE(!session->execute("scene.authoring.camera.set.v1", Value::Object{{"value", badCamera}}).ok());
    REQUIRE_EQ(session->saveJson(), empty);
    auto document                                       = session->snapshot();
    (*document.getIf<Value::Object>())["schemaVersion"] = 99;
    REQUIRE(!session->execute("scene.snapshot.restore.v1", document).ok());
    REQUIRE_EQ(session->saveJson(), empty);
    (*document.getIf<Value::Object>())["schemaVersion"] = 2;
    (*document.getIf<Value::Object>())["unknown"]       = true;
    REQUIRE(!session->execute("scene.snapshot.restore.v1", document).ok());
    REQUIRE_EQ(session->saveJson(), empty);
}

TEST_CASE("scene.authoring.ground_and_gap_solve_world_geometry_under_reflected_parent") {
    auto session = authoring();
    REQUIRE(session
                ->execute("scene.object.create.v1", Value::Object{{"object", "parent"},
                                                                  {"position", Value::Array{3.0, 2.0, -1.0}},
                                                                  {"rotation", Value::Array{0.2, 0.7, 0.1}},
                                                                  {"scale", Value::Array{-2.0, 3.0, 0.5}}})
                .ok());
    REQUIRE(session
                ->execute(
                    "scene.object.create.v1",
                    Value::Object{{"object", "crate"}, {"parent", "parent"}, {"rotation", Value::Array{0.4, 0.5, 0.2}}})
                .ok());
    REQUIRE(session
                ->execute("scene.object.create.v1",
                          Value::Object{{"object", "reference"}, {"position", Value::Array{7.0, 0.0, 0.0}}})
                .ok());
    const auto before = session->saveJson();
    REQUIRE(session
                ->execute("scene.placement.ground.v1",
                          Value::Object{{"object", "crate"}, {"bounds", bounds()}, {"height", 0.0}})
                .ok());
    auto target   = copied(*session);
    auto geometry = eve::scene_editing::inspectAuthoringObject(target, eve::editing::ObjectId("crate"),
                                                               {-0.5, -0.5, -0.5, 0.5, 0.5, 0.5});
    REQUIRE(geometry.ok());
    REQUIRE(std::abs(component(geometry.value(), "worldMinimum", 1)) < 1e-9);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), before);
    REQUIRE(session
                ->execute("scene.placement.align.v1", Value::Object{{"object", "crate"},
                                                                    {"bounds", bounds()},
                                                                    {"reference", "reference"},
                                                                    {"referenceBounds", bounds()},
                                                                    {"axis", "x"},
                                                                    {"gap", 0.25}})
                .ok());
    target   = copied(*session);
    geometry = eve::scene_editing::inspectAuthoringObject(target, eve::editing::ObjectId("crate"),
                                                          {-0.5, -0.5, -0.5, 0.5, 0.5, 0.5});
    REQUIRE(geometry.ok());
    REQUIRE(std::abs(component(geometry.value(), "worldMinimum", 0) - 7.75) < 1e-9);
    const auto aligned = session->saveJson();
    REQUIRE(!session
                 ->execute("scene.placement.align.v1", Value::Object{{"object", "parent"},
                                                                     {"bounds", bounds()},
                                                                     {"reference", "crate"},
                                                                     {"referenceBounds", bounds()},
                                                                     {"axis", "x"},
                                                                     {"gap", 0.0}})
                 .ok());
    REQUIRE_EQ(session->saveJson(), aligned);
}

TEST_CASE("scene.authoring.screen_anchor_preserves_depth_and_rejects_singular_parent") {
    auto session = authoring();
    REQUIRE(session->execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    const auto before  = session->saveJson();
    Value      payload = Value::Object{{"object", "crate"},
                                       {"anchor", Value::Array{0.0, 0.0, 0.0}},
                                       {"eye", Value::Array{0.0, 0.0, 10.0}},
                                       {"target", Value::Array{0.0, 0.0, 0.0}},
                                       {"up", Value::Array{0.0, 1.0, 0.0}},
                                       {"fov", 60.0},
                                       {"aspect", 2.0},
                                       {"screen", Value::Array{0.75, 0.25}}};
    REQUIRE(session->execute("scene.placement.screen.v1", payload).ok());
    auto target    = copied(*session);
    auto transform = target.readTransform(eve::editing::ObjectId("crate"));
    REQUIRE(transform.ok());
    REQUIRE(std::abs(transform.value().x - 10.0 * std::tan(3.141592653589793 / 6.0)) < 1e-9);
    REQUIRE(std::abs(transform.value().y - 5.0 * std::tan(3.141592653589793 / 6.0)) < 1e-9);
    REQUIRE(std::abs(transform.value().z) < 1e-9);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), before);
    REQUIRE(session
                ->execute("scene.object.create.v1",
                          Value::Object{{"object", "flat"}, {"scale", Value::Array{0.000001, 0.000001, 0.000001}}})
                .ok());
    REQUIRE(session->execute("scene.object.reparent.v1", Value::Object{{"object", "crate"}, {"parent", "flat"}}).ok());
    const auto singular = session->saveJson();
    REQUIRE(!session->execute("scene.placement.screen.v1", payload).ok());
    REQUIRE_EQ(session->saveJson(), singular);
}
