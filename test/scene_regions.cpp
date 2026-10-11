#include <memory>
#include "editing/EditingProperty.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editing/SceneRegions.h"
#include "scene/editor/SceneEditorSession.h"
#include "scene/editor/SceneVisualRelationEvidence.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
using namespace eve::editing;
std::unique_ptr<eve::scene_editor::SceneEditorSession> makeRegionSession() {
    auto result = std::make_unique<eve::scene_editor::SceneEditorSession>(
        std::make_unique<eve::scene_editing::SceneAuthoringDocumentTarget>("regions"));
    REQUIRE(
        result
            ->execute("scene.authoring.spawn.v1",
                      Value::Object{
                          {"object", "house"},
                          {"source", Value::Object{{"kind", "box"}, {"seed", 7}, {"mesh_params", Value::Object{}}}}})
            .ok());
    REQUIRE(result
                ->execute("scene.authoring.spawn.v1",
                          Value::Object{{"object", "tree"}, {"source", Value::Object{{"kind", "box"}}}})
                .ok());
    return result;
}
Value region(const std::string& revision) {
    return Value::Object{{"schemaId", "eve.scene.local-region"},
                         {"schemaVersion", 1},
                         {"kind", "local_box"},
                         {"id", "window"},
                         {"geometryRevision", revision},
                         {"minimum", Value::Array{-0.4, 0.0, -0.6}},
                         {"maximum", Value::Array{0.4, 0.4, 0.6}}};
}
Value intent(const std::string& revision) {
    return Value::Object{{"id", "keep-window"},
                         {"type", "visible_fraction"},
                         {"subject", "house"},
                         {"minimum", 0.95},
                         {"region", region(revision)}};
}
}  // namespace
TEST_CASE("scene.regions.geometry_revision_excludes_trs_and_material_but_invalidates_recipe") {
    auto target   = makeRegionSession();
    auto revision = target->geometryRevision(ObjectId("house"));
    REQUIRE(revision.ok());
    auto source = target->snapshot().getIf<Value::Object>()->at("sources").getIf<Value::Object>()->at("house");
    (*source.getIf<Value::Object>())["tint"] = Value::Array{0.1, 0.2, 0.3};
    REQUIRE(
        target->execute("scene.authoring.sources.set.v1", Value::Object{{"object", "house"}, {"value", source}}).ok());
    REQUIRE(
        target
            ->execute("scene.transform.set.v1", Value::Object{{"object", "house"}, {"position", Value::Array{2, 3, 4}}})
            .ok());
    auto same = target->geometryRevision(ObjectId("house"));
    REQUIRE(same.ok());
    REQUIRE_EQ(same.value(), revision.value());
    const auto relation = intent(revision.value());
    REQUIRE(target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{relation}}}).ok());
    auto hierarchy = eve::scene_editing::SceneDocumentTarget("check");
    auto snap      = target->snapshot();
    auto restore   = hierarchy.makeRestore(snap.getIf<Value::Object>()->at("hierarchy"));
    REQUIRE(restore.ok());
    REQUIRE(hierarchy.applyDomainOperation(restore.value()).ok());
    auto relations = eve::scene_editing::parseSceneRelations(snap.getIf<Value::Object>()->at("relations"), hierarchy);
    REQUIRE(relations.ok());
    REQUIRE(eve::scene_editing::sceneVisibilityTargets(relations.value(), snap).ok());
    (*source.getIf<Value::Object>())["seed"] = 8;
    REQUIRE(
        target->execute("scene.authoring.sources.set.v1", Value::Object{{"object", "house"}, {"value", source}}).ok());
    auto stale = eve::scene_editing::sceneVisibilityTargets(relations.value(), target->snapshot());
    REQUIRE(!stale.ok());
    REQUIRE_EQ(static_cast<int>(stale.code()), static_cast<int>(Status::Conflict));
    REQUIRE(target->undo().ok());
    REQUIRE(eve::scene_editing::sceneVisibilityTargets(relations.value(), target->snapshot()).ok());
}
TEST_CASE("scene.regions.roundtrip_delete_undo_and_v2_migration_preserve_intent") {
    auto target   = makeRegionSession();
    auto revision = target->geometryRevision(ObjectId("house"));
    REQUIRE(revision.ok());
    REQUIRE(target
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{intent(revision.value())}}})
                .ok());
    const auto saved = target->saveJson();
    REQUIRE(target->restoreJson(saved).ok());
    REQUIRE_EQ(target->saveJson(), saved);
    REQUIRE(target->execute("scene.object.delete.v1", Value::Object{{"object", "house"}}).ok());
    REQUIRE(target->snapshot().getIf<Value::Object>()->at("relations").getIf<Value::Array>()->empty());
    REQUIRE(target->undo().ok());
    REQUIRE_EQ(target->saveJson(), saved);
    auto legacy                                       = target->snapshot();
    (*legacy.getIf<Value::Object>())["schemaVersion"] = 2;
    REQUIRE(!target->execute("scene.snapshot.restore.v1", legacy).ok());
    REQUIRE_EQ(target->saveJson(), saved);
    auto& relation =
        *legacy.getIf<Value::Object>()->at("relations").getIf<Value::Array>()->front().getIf<Value::Object>();
    relation.erase("region");
    REQUIRE(target->execute("scene.snapshot.restore.v1", legacy).ok());
    REQUIRE_EQ(*target->snapshot().getIf<Value::Object>()->at("schemaVersion").getIf<std::int64_t>(), 4);
    REQUIRE(target->snapshot().getIf<Value::Object>()->at("relations") ==
            legacy.getIf<Value::Object>()->at("relations"));
}
TEST_CASE("scene.regions.invalid_bounds_unknown_fields_and_conflicting_identity_are_atomic") {
    auto target   = makeRegionSession();
    auto revision = target->geometryRevision(ObjectId("house"));
    REQUIRE(revision.ok());
    const auto saved = target->saveJson();
    for (auto bounds : {Value::Object{{"minimum", Value::Array{0, 0, 0}}, {"maximum", Value::Array{0, 1, 1}}},
                        Value::Object{{"kind", "screen_rectangle"}}, Value::Object{{"unknown", 1}},
                        Value::Object{{"schemaVersion", 99}}, Value::Object{{"geometryRevision", ""}}}) {
        auto  bad = intent(revision.value());
        auto& r   = *bad.getIf<Value::Object>()->at("region").getIf<Value::Object>();
        for (const auto& [key, value] : bounds) r[key] = value;
        REQUIRE(!target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{bad}}}).ok());
        REQUIRE_EQ(target->saveJson(), saved);
    }
    auto a = intent(revision.value()), b = a;
    (*b.getIf<Value::Object>())["id"]                                           = "another";
    (*b.getIf<Value::Object>()->at("region").getIf<Value::Object>())["maximum"] = Value::Array{0.3, 0.4, 0.6};
    REQUIRE(!target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{a, b}}}).ok());
    REQUIRE_EQ(target->saveJson(), saved);
}
TEST_CASE("scene.regions.evidence_is_matched_by_object_region_and_definition") {
    eve::scene_editing::SceneRelation relation;
    relation.id      = "window";
    relation.kind    = eve::scene_editing::SceneRelationKind::VisibleFraction;
    relation.subject = ObjectId("house");
    relation.minimum = 0.95;
    auto parsed      = eve::SceneLocalRegion::fromValue(toPresentationValue(region("revision")));
    REQUIRE(parsed.ok());
    relation.region = parsed.value();
    eve::scene_editor::PhysicsPlacementFrame  frame;
    eve::scene_editor::PhysicsPlacementObject tree;
    tree.object   = ObjectId("tree");
    tree.selected = true;
    frame.objects = {tree};
    auto physical = eve::scene_editor::evaluateSceneRelations({relation}, frame);
    REQUIRE(physical.ok());
    Value whole = Value::Object{
        {"object", "house"}, {"status", "Measured"}, {"visibleFraction", 1.0}, {"occluders", Value::Array{}}};
    Value window                                        = whole;
    (*window.getIf<Value::Object>())["region"]          = region("revision");
    (*window.getIf<Value::Object>())["visibleFraction"] = 0.2;
    Value evidence                                      = Value::Object{{"targets", Value::Array{whole, window}}};
    auto  measured = eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, evidence, physical.value());
    REQUIRE(measured.ok());
    REQUIRE(!*measured.value().getIf<Value::Object>()->at("hardSatisfied").getIf<bool>());
    (*window.getIf<Value::Object>()->at("region").getIf<Value::Object>())["id"] = "other";
    evidence   = Value::Object{{"targets", Value::Array{whole, window}}};
    auto wrong = eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, evidence, physical.value());
    REQUIRE(wrong.ok());
    REQUIRE_EQ(*wrong.value()
                    .getIf<Value::Object>()
                    ->at("relations")
                    .getIf<Value::Array>()
                    ->front()
                    .getIf<Value::Object>()
                    ->at("status")
                    .getIf<std::string>(),
               std::string("Unmeasurable"));
}
