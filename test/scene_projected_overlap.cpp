#include <memory>
#include "common/Capability.h"
#include "common/SceneVisibilityEvidence.h"
#include "editing/EditingProperty.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editing/SceneRegions.h"
#include "scene/editor/SceneEditorSession.h"
#include "scene/editor/SceneVisualRelationEvidence.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
using namespace eve::editing;
std::unique_ptr<eve::scene_editor::SceneEditorSession> makeOverlapSession() {
    auto target = std::make_unique<eve::scene_editor::SceneEditorSession>(
        std::make_unique<eve::scene_editing::SceneAuthoringDocumentTarget>("overlap"));
    for (const auto* id : {"a", "b"})
        REQUIRE(target
                    ->execute("scene.authoring.spawn.v1",
                              Value::Object{{"object", id}, {"source", Value::Object{{"kind", "box"}}}})
                    .ok());
    return target;
}
Value intent() {
    return Value::Object{{"id", "compose"}, {"type", "projected_overlap"},
                         {"subject", "a"},  {"reference", "b"},
                         {"minimum", 0.4},  {"maximum", 0.6}};
}
Value evidence(double fraction) {
    Value pair   = Value::Object{{"object", "a"},          {"status", "Measured"},
                                 {"fraction", fraction},   {"intersectionPixels", std::int64_t(fraction * 100)},
                                 {"referencePixels", 100}, {"subjectPixels", 100}};
    Value target = Value::Object{{"object", "b"},
                                 {"status", "AmbiguousDepth"},
                                 {"visibleFraction", 0.0},
                                 {"occluders", Value::Array{}},
                                 {"projectedOverlaps", Value::Array{pair}}};
    return Value::Object{{"targets", Value::Array{target}}};
}
}  // namespace
TEST_CASE("scene.projected_overlap.schema_v4_migration_roundtrip_delete_and_invalid_restore") {
    auto target                                       = makeOverlapSession();
    auto legacy                                       = target->snapshot();
    (*legacy.getIf<Value::Object>())["schemaVersion"] = 3;
    REQUIRE(target->execute("scene.snapshot.restore.v1", legacy).ok());
    REQUIRE_EQ(*target->snapshot().getIf<Value::Object>()->at("schemaVersion").getIf<std::int64_t>(), 4);
    REQUIRE(target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{intent()}}}).ok());
    const auto saved = target->saveJson();
    REQUIRE(target->restoreJson(saved).ok());
    REQUIRE_EQ(target->saveJson(), saved);
    for (int version : {1, 2, 3}) {
        auto bad                                       = target->snapshot();
        (*bad.getIf<Value::Object>())["schemaVersion"] = version;
        REQUIRE(!target->execute("scene.snapshot.restore.v1", bad).ok());
        REQUIRE_EQ(target->saveJson(), saved);
    }
    auto bad                                   = intent();
    (*bad.getIf<Value::Object>())["tolerance"] = 0.1;
    REQUIRE(!target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{bad}}}).ok());
    REQUIRE_EQ(target->saveJson(), saved);
    REQUIRE(target->execute("scene.object.delete.v1", Value::Object{{"object", "a"}}).ok());
    REQUIRE(target->snapshot().getIf<Value::Object>()->at("relations").getIf<Value::Array>()->empty());
    REQUIRE(target->undo().ok());
    REQUIRE_EQ(target->saveJson(), saved);
}
TEST_CASE("scene.projected_overlap.target_planning_includes_partner_and_reference_region") {
    auto target   = makeOverlapSession();
    auto revision = target->geometryRevision(ObjectId("b"));
    REQUIRE(revision.ok());
    auto relation                                = intent();
    (*relation.getIf<Value::Object>())["region"] = Value::Object{{"schemaId", "eve.scene.local-region"},
                                                                 {"schemaVersion", 1},
                                                                 {"kind", "local_box"},
                                                                 {"id", "lower"},
                                                                 {"geometryRevision", revision.value()},
                                                                 {"minimum", Value::Array{-0.5, -0.5, -0.6}},
                                                                 {"maximum", Value::Array{0.5, 0, 0.6}}};
    REQUIRE(target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{relation}}}).ok());
    auto                                    snapshot = target->snapshot();
    eve::scene_editing::SceneDocumentTarget hierarchy("parse");
    auto restore = hierarchy.makeRestore(snapshot.getIf<Value::Object>()->at("hierarchy"));
    REQUIRE(restore.ok());
    REQUIRE(hierarchy.applyDomainOperation(restore.value()).ok());
    auto parsed = eve::scene_editing::parseSceneRelations(snapshot.getIf<Value::Object>()->at("relations"), hierarchy);
    REQUIRE(parsed.ok());
    auto planned = eve::scene_editing::sceneVisibilityTargets(parsed.value(), snapshot);
    REQUIRE(planned.ok());
    REQUIRE_EQ(planned.value().size(), size_t(2));
    REQUIRE_EQ(*planned.value()[0].getIf<std::string>(), std::string("a"));
    REQUIRE_EQ(*planned.value()[1].getIf<Value::Object>()->at("object").getIf<std::string>(), std::string("b"));
}
TEST_CASE("scene.projected_overlap_uses_pair_fraction_independent_of_visibility_and_rejects_bad_evidence") {
    eve::scene_editing::SceneRelation relation;
    relation.id        = "compose";
    relation.kind      = eve::scene_editing::SceneRelationKind::ProjectedOverlap;
    relation.subject   = ObjectId("a");
    relation.reference = ObjectId("b");
    relation.minimum   = 0.4;
    relation.maximum   = 0.6;
    eve::scene_editor::PhysicsPlacementFrame  frame;
    eve::scene_editor::PhysicsPlacementObject selected;
    selected.object   = ObjectId("unrelated");
    selected.selected = true;
    frame.objects     = {selected};
    auto physical     = eve::scene_editor::evaluateSceneRelations({relation}, frame);
    REQUIRE(physical.ok());
    for (double fraction : {0.0, 0.5, 1.0}) {
        auto report =
            eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, evidence(fraction), physical.value());
        REQUIRE(report.ok());
        REQUIRE_EQ(*report.value().getIf<Value::Object>()->at("hardSatisfied").getIf<bool>(), fraction == 0.5);
    }
    auto bad = evidence(1.2);
    REQUIRE(!eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value()).ok());
    bad         = evidence(0.5);
    auto& pairs = *bad.getIf<Value::Object>()
                       ->at("targets")
                       .getIf<Value::Array>()
                       ->front()
                       .getIf<Value::Object>()
                       ->at("projectedOverlaps")
                       .getIf<Value::Array>();
    (*pairs.front().getIf<Value::Object>())["referencePixels"] = 0;
    REQUIRE(!eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value()).ok());
    (*pairs.front().getIf<Value::Object>())["referencePixels"] = 100;
    pairs.push_back(pairs.front());
    REQUIRE(!eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value()).ok());
    pairs.clear();
    auto missing = eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value());
    REQUIRE(missing.ok());
    REQUIRE(!*missing.value().getIf<Value::Object>()->at("hardSatisfied").getIf<bool>());
    relation.hard = false;
    auto soft     = eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value());
    REQUIRE(soft.ok());
    REQUIRE(*soft.value().getIf<Value::Object>()->at("hardSatisfied").getIf<bool>());
}

TEST_CASE("scene.projected_overlap.provider_absent_rejects_measure_and_commit_without_document_mutation") {
    auto* previous = eve::cap::query<eve::ISceneVisibilityEvidence>();
    if (previous) eve::cap::revoke<eve::ISceneVisibilityEvidence>(previous);
    auto target = makeOverlapSession();
    REQUIRE(target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{intent()}}}).ok());
    eve::scene_editor::PhysicsPlacementRequest request;
    eve::scene_editor::PhysicsPlacementObject  a, b;
    a.object                  = ObjectId("a");
    a.selected                = true;
    b.object                  = ObjectId("b");
    request.objects           = {a, b};
    request.settings.gravityY = 0;
    REQUIRE(target->beginRelationPlacement(request).ok());
    const auto saved   = target->saveJson();
    auto       missing = target->prepareRelationVisibility("host", Value::Object{});
    REQUIRE(!missing.ok());
    REQUIRE_EQ(static_cast<int>(missing.code()), static_cast<int>(Status::Unsupported));
    REQUIRE(!target->checkRelationPlacement().ok());
    REQUIRE(!target->commitPhysicsPlacement().ok());
    REQUIRE_EQ(target->saveJson(), saved);
    REQUIRE(target->cancelPhysicsPlacement().ok());
    if (previous) eve::cap::provide<eve::ISceneVisibilityEvidence>(previous);
}
