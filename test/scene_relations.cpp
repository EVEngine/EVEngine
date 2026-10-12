#include "common/Capability.h"
#include "common/SceneVisibilityEvidence.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editor/SceneEditorSession.h"
#include "scene/editor/SceneRelationEvidence.h"
#include "scene/editor/SceneVisualRelationEvidence.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>

namespace {
using namespace eve::editing;
using namespace eve::scene_editor;
std::unique_ptr<SceneEditorSession> relationSession() {
    auto session = std::make_unique<SceneEditorSession>(
        std::make_unique<eve::scene_editing::SceneAuthoringDocumentTarget>("relations"));
    for (const auto* id : {"floor", "crate", "wall"}) {
        auto created =
            session->execute("scene.object.create.v1",
                             Value::Object{{"object", id},
                                           {"position", Value::Array{std::string(id) == "wall" ? 10.0 : 0.0,
                                                                     std::string(id) == "floor" ? -0.5 : 3.0, 0.0}}});
        REQUIRE(created.ok());
    }
    return session;
}
Value relation(const char* id, const char* type, const char* reference) {
    return Value::Object{{"id", id},     {"type", type},     {"subject", "crate"}, {"reference", reference},
                         {"hard", true}, {"tolerance", 0.06}};
}
PhysicsPlacementRequest request() {
    PhysicsPlacementRequest request;
    request.settings.mode            = PhysicsPlacementMode::Fall;
    request.settings.freezeRotationX = request.settings.freezeRotationY = request.settings.freezeRotationZ = true;
    PhysicsPlacementObject floor, crate;
    floor.object      = ObjectId("floor");
    floor.locked      = true;
    floor.halfExtentX = floor.halfExtentZ = 5;
    floor.halfExtentY                     = 0.5;
    crate.object                          = ObjectId("crate");
    crate.selected                        = true;
    request.objects                       = {floor, crate};
    return request;
}
bool satisfied(const Value& report) {
    const auto& fields = *report.getIf<Value::Object>();
    REQUIRE_EQ(*fields.at("schemaId").getIf<std::string>(), std::string("eve.scene.relation-report"));
    REQUIRE_EQ(*fields.at("schemaVersion").getIf<std::int64_t>(), 1);
    return *fields.at("hardSatisfied").getIf<bool>();
}
const Value::Array& relations(const Value& snapshot) {
    return *snapshot.getIf<Value::Object>()->at("relations").getIf<Value::Array>();
}
}  // namespace

TEST_CASE("scene.relations.roundtrip_delete_undo_and_v1_migration_are_atomic") {
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{relation("on-floor", "supported_on", "floor")}}})
                .ok());
    const auto saved = session->saveJson();
    REQUIRE(session->execute("scene.object.delete.v1", Value::Object{{"object", "crate"}}).ok());
    REQUIRE(relations(session->snapshot()).empty());
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), saved);
    REQUIRE(session->redo().ok());
    REQUIRE(relations(session->snapshot()).empty());
    REQUIRE(session->restoreJson(saved).ok());
    REQUIRE_EQ(session->saveJson(), saved);
    auto  legacy = session->snapshot();
    auto& root   = *legacy.getIf<Value::Object>();
    root.erase("relations");
    root["schemaVersion"] = 1;
    REQUIRE(session->execute("scene.snapshot.restore.v1", legacy).ok());
    REQUIRE(relations(session->snapshot()).empty());
    REQUIRE_EQ(*session->snapshot().getIf<Value::Object>()->at("schemaVersion").getIf<std::int64_t>(), 4);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), saved);
}

TEST_CASE("scene.relations.reject_unknown_missing_and_duplicate_without_mutation") {
    auto       session = relationSession();
    const auto saved   = session->saveJson();
    for (auto bad : {relation("r", "unknown-type", "floor"), relation("r", "supported_on", "missing")}) {
        REQUIRE(
            !session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{bad}}}).ok());
        REQUIRE_EQ(session->saveJson(), saved);
    }
    auto r = relation("r", "supported_on", "floor");
    REQUIRE(!session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{r, r}}}).ok());
    (*r.getIf<Value::Object>())["tolerance"] = -1.0;
    REQUIRE(!session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{r}}}).ok());
    REQUIRE_EQ(session->saveJson(), saved);
    SceneEditorSession plain("plain");
    REQUIRE_EQ(static_cast<int>(
                   plain.execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{}}}).code()),
               static_cast<int>(Status::Unsupported));
}

TEST_CASE("scene.relations.contact_evidence_distinguishes_support_penetration_and_unmeasurable") {
    eve::scene_editing::SceneRelation r;
    r.id        = "r";
    r.subject   = ObjectId("crate");
    r.reference = ObjectId("floor");
    PhysicsPlacementFrame frame;
    frame.tick             = 3;
    frame.contactsComplete = true;
    PhysicsPlacementObject crate, floor;
    crate.object   = r.subject;
    crate.selected = true;
    floor.object   = r.reference;
    frame.objects  = {crate, floor};
    frame.contacts.push_back({r.subject, r.reference, 0, 0, 0, 0, -1, 0, -0.005});
    auto good = evaluateSceneRelations({r}, frame);
    REQUIRE(good.ok());
    REQUIRE(satisfied(good.value()));
    frame.contacts[0].separation = -0.2;
    auto penetrating             = evaluateSceneRelations({r}, frame);
    REQUIRE(penetrating.ok());
    REQUIRE(!satisfied(penetrating.value()));
    frame.contacts[0].separation = 0;
    frame.contacts[0].normalY    = 0;
    auto side                    = evaluateSceneRelations({r}, frame);
    REQUIRE(side.ok());
    REQUIRE(!satisfied(side.value()));
    frame.contacts.clear();
    r.kind         = eve::scene_editing::SceneRelationKind::Nonpenetrating;
    auto separated = evaluateSceneRelations({r}, frame);
    REQUIRE(separated.ok());
    REQUIRE(satisfied(separated.value()));
    frame.contactsComplete = false;
    auto incomplete        = evaluateSceneRelations({r}, frame);
    REQUIRE(incomplete.ok());
    REQUIRE(!satisfied(incomplete.value()));
    frame.contactsComplete    = true;
    frame.objects[1].selected = true;
    auto disabledPair         = evaluateSceneRelations({r}, frame);
    REQUIRE(disabledPair.ok());
    REQUIRE(!satisfied(disabledPair.value()));
}

TEST_CASE("scene.relations.native_drop_preserves_preview_and_commits_with_locked_floor") {
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{relation("on-floor", "supported_on", "floor")}}})
                .ok());
    const auto before = session->saveJson();
    REQUIRE(session->beginRelationPlacement(request()).ok());
    auto unstepped = session->checkRelationPlacement();
    REQUIRE(unstepped.ok());
    REQUIRE(!satisfied(unstepped.value()));
    REQUIRE(!session->commitPhysicsPlacement().ok());
    REQUIRE_EQ(session->saveJson(), before);
    for (int i = 0; i < 240; ++i) REQUIRE(session->updatePhysicsPlacement(0, 3, 0).ok());
    auto report = session->checkRelationPlacement();
    REQUIRE(report.ok());
    REQUIRE(satisfied(report.value()));
    REQUIRE_EQ(session->saveJson(), before);
    REQUIRE(session->commitPhysicsPlacement().ok());
    REQUIRE(session->saveJson() != before);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), before);
    REQUIRE(session->redo().ok());
}

TEST_CASE("scene.relations.reject_wrong_support_missing_participant_locked_selection_and_stale_preview") {
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{relation("wrong-support", "supported_on", "wall")}}})
                .ok());
    REQUIRE(!session->beginRelationPlacement(request()).ok());
    auto                   candidate = request();
    PhysicsPlacementObject wall;
    wall.object = ObjectId("wall");
    wall.locked = true;
    candidate.objects.push_back(wall);
    candidate.objects[1].locked = true;
    REQUIRE(!session->beginRelationPlacement(candidate).ok());
    candidate.objects[1].locked = false;
    REQUIRE(session->beginRelationPlacement(candidate).ok());
    for (int i = 0; i < 200; ++i) REQUIRE(session->updatePhysicsPlacement(0, 3, 0).ok());
    auto report = session->checkRelationPlacement();
    REQUIRE(report.ok());
    REQUIRE(!satisfied(report.value()));
    REQUIRE(!session->commitPhysicsPlacement().ok());
    REQUIRE(session->cancelPhysicsPlacement().ok());
    REQUIRE(session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{}}}).ok());
    REQUIRE(session->beginRelationPlacement(request()).ok());
    REQUIRE(session
                ->execute("scene.transform.set.v1",
                          Value::Object{{"object", "floor"}, {"position", Value::Array{0.0, -1.0, 0.0}}})
                .ok());
    REQUIRE_EQ(static_cast<int>(session->checkRelationPlacement().code()), static_cast<int>(Status::Conflict));
    REQUIRE_EQ(static_cast<int>(session->commitPhysicsPlacement().code()), static_cast<int>(Status::Conflict));
    REQUIRE(session->cancelPhysicsPlacement().ok());
}

TEST_CASE("scene.relations.physics_roundtrip_preserves_scene_yxz_rotation") {
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.transform.set.v1",
                          Value::Object{{"object", "crate"}, {"rotation", Value::Array{0.31, 0.52, -0.23}}})
                .ok());
    REQUIRE(session->beginRelationPlacement(request()).ok());
    auto frame = session->updatePhysicsPlacement(0, 3, 0);
    REQUIRE(frame.ok());
    const auto& rotation = frame.value().objects[1].transform;
    CHECK(std::abs(rotation.rotationX - 0.31) < 1e-5);
    CHECK(std::abs(rotation.rotationY - 0.52) < 1e-5);
    CHECK(std::abs(rotation.rotationZ + 0.23) < 1e-5);
    REQUIRE(session->commitPhysicsPlacement().ok());
    const auto  snapshot  = session->snapshot();
    const auto& hierarchy = *snapshot.getIf<Value::Object>()->at("hierarchy").getIf<Value::Object>();
    for (const auto& object : *hierarchy.at("objects").getIf<Value::Array>()) {
        const auto& fields = *object.getIf<Value::Object>();
        if (*fields.at("id").getIf<std::string>() != "crate") continue;
        const auto& t = *fields.at("transform").getIf<Value::Object>();
        CHECK(std::abs(*t.at("rotationX").getIf<double>() - 0.31) < 1e-5);
        CHECK(std::abs(*t.at("rotationY").getIf<double>() - 0.52) < 1e-5);
        CHECK(std::abs(*t.at("rotationZ").getIf<double>() + 0.23) < 1e-5);
    }
}

TEST_CASE("scene.relations.visual_roundtrip_delete_and_invalid_intervals_are_atomic") {
    auto  session = relationSession();
    Value visible =
        Value::Object{{"id", "keep-visible"}, {"type", "visible_fraction"}, {"subject", "crate"}, {"minimum", 0.75}};
    Value occludes = Value::Object{{"id", "cover"},       {"type", "occludes"}, {"subject", "crate"},
                                   {"reference", "wall"}, {"minimum", 0.2},     {"maximum", 0.5}};
    REQUIRE(
        session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{visible, occludes}}})
            .ok());
    const auto saved = session->saveJson();
    REQUIRE(session->restoreJson(saved).ok());
    REQUIRE_EQ(session->saveJson(), saved);
    REQUIRE(session->execute("scene.object.delete.v1", Value::Object{{"object", "crate"}}).ok());
    REQUIRE(relations(session->snapshot()).empty());
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), saved);
    for (const auto& fields :
         {Value::Object{{"minimum", 0.8}, {"maximum", 0.4}}, Value::Object{{"minimum", -0.1}},
          Value::Object{{"minimum", 1.1}}, Value::Object{{"reference", "floor"}}, Value::Object{{"tolerance", 0.01}}}) {
        auto bad = visible;
        for (const auto& [key, value] : fields) (*bad.getIf<Value::Object>())[key] = value;
        REQUIRE(
            !session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{bad}}}).ok());
        REQUIRE_EQ(session->saveJson(), saved);
    }
}
TEST_CASE("scene.relations.visual_evidence_uses_directional_occluder_and_rejects_ambiguity") {
    eve::scene_editing::SceneRelation visible, cover;
    visible.id      = "visible";
    visible.kind    = eve::scene_editing::SceneRelationKind::VisibleFraction;
    visible.subject = ObjectId("crate");
    visible.minimum = 0.75;
    cover.id        = "cover";
    cover.kind      = eve::scene_editing::SceneRelationKind::Occludes;
    cover.subject   = ObjectId("crate");
    cover.reference = ObjectId("wall");
    cover.minimum   = 0.2;
    cover.maximum   = 0.3;
    PhysicsPlacementFrame  frame;
    PhysicsPlacementObject crate;
    crate.object   = visible.subject;
    crate.selected = true;
    frame.objects  = {crate};
    Value::Array targets;
    targets.emplace_back(Value::Object{
        {"object", "crate"}, {"status", "Measured"}, {"visibleFraction", 0.8}, {"occluders", Value::Array{}}});
    targets.emplace_back(
        Value::Object{{"object", "wall"},
                      {"status", "Measured"},
                      {"visibleFraction", 0.5},
                      {"occluders", Value::Array{Value::Object{{"object", "crate"}, {"fraction", 0.25}}}}});
    Value                                                evidence = Value::Object{{"targets", std::move(targets)}};
    const std::vector<eve::scene_editing::SceneRelation> intents{visible, cover};
    auto                                                 physical = evaluateSceneRelations(intents, frame);
    REQUIRE(physical.ok());
    auto measured = evaluateSceneVisualRelations(intents, frame, evidence, physical.value());
    REQUIRE(measured.ok());
    REQUIRE(satisfied(measured.value()));
    auto& target = *(*evidence.getIf<Value::Object>()->at("targets").getIf<Value::Array>())[1].getIf<Value::Object>();
    target["occluders"] = Value::Array{Value::Object{{"object", "floor"}, {"fraction", 0.25}}};
    auto unrelated      = evaluateSceneVisualRelations(intents, frame, evidence, physical.value());
    REQUIRE(unrelated.ok());
    REQUIRE(!satisfied(unrelated.value()));
    target["status"] = "AmbiguousDepth";
    auto ambiguous   = evaluateSceneVisualRelations(intents, frame, evidence, physical.value());
    REQUIRE(ambiguous.ok());
    REQUIRE(!satisfied(ambiguous.value()));
}
TEST_CASE("scene.relations.visual_provider_absent_cannot_commit_but_physics_still_works") {
    auto* previous = eve::cap::query<eve::ISceneVisibilityEvidence>();
    if (previous) eve::cap::revoke<eve::ISceneVisibilityEvidence>(previous);
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{Value::Object{{"id", "visible"},
                                                                             {"type", "visible_fraction"},
                                                                             {"subject", "crate"},
                                                                             {"minimum", 0.5}}}}})
                .ok());
    REQUIRE(session->beginRelationPlacement(request()).ok());
    REQUIRE(!session->prepareRelationVisibility("host", Value::Object{}).ok());
    const auto saved = session->saveJson();
    REQUIRE(!session->commitPhysicsPlacement().ok());
    REQUIRE_EQ(session->saveJson(), saved);
    REQUIRE(session->physicsPlacementActive());
    REQUIRE(session->cancelPhysicsPlacement().ok());
    REQUIRE(session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{}}}).ok());
    REQUIRE(session->beginRelationPlacement(request()).ok());
    auto report = session->checkRelationPlacement();
    REQUIRE(report.ok());
    REQUIRE(satisfied(report.value()));
    REQUIRE(session->cancelPhysicsPlacement().ok());
    if (previous) eve::cap::provide<eve::ISceneVisibilityEvidence>(previous);
}

TEST_CASE("scene.relations.moving_unrelated_occluder_still_checks_all_visual_intent") {
    eve::scene_editing::SceneRelation visible;
    visible.id      = "visible";
    visible.kind    = eve::scene_editing::SceneRelationKind::VisibleFraction;
    visible.subject = ObjectId("target");
    visible.minimum = 0.8;
    PhysicsPlacementFrame  frame;
    PhysicsPlacementObject blocker;
    blocker.object   = ObjectId("blocker");
    blocker.selected = true;
    frame.objects    = {blocker};
    Value evidence   = Value::Object{
          {"targets",
           Value::Array{Value::Object{
               {"object", "target"}, {"status", "Measured"}, {"visibleFraction", 0.2}, {"occluders", Value::Array{}}}}}};
    auto physical = evaluateSceneRelations({visible}, frame);
    REQUIRE(physical.ok());
    auto evaluated = evaluateSceneVisualRelations({visible}, frame, evidence, physical.value());
    REQUIRE(evaluated.ok());
    REQUIRE(!satisfied(evaluated.value()));
    visible.hard = false;
    auto soft    = evaluateSceneVisualRelations({visible}, frame, evidence, physical.value());
    REQUIRE(soft.ok());
    REQUIRE(satisfied(soft.value()));
    const auto& item =
        *soft.value().getIf<Value::Object>()->at("relations").getIf<Value::Array>()->front().getIf<Value::Object>();
    REQUIRE_EQ(*item.at("status").getIf<std::string>(), std::string("Violated"));
}

TEST_CASE("scene.relations.malformed_visual_provider_evidence_returns_failure") {
    PhysicsPlacementFrame  frame;
    PhysicsPlacementObject object;
    object.object   = ObjectId("target");
    object.selected = true;
    frame.objects   = {object};
    eve::scene_editing::SceneRelation visible;
    visible.kind    = eve::scene_editing::SceneRelationKind::VisibleFraction;
    visible.subject = object.object;
    auto report     = evaluateSceneRelations({visible}, frame);
    REQUIRE(report.ok());
    for (const auto& broken :
         {Value::Object{{"object", "target"}},
          Value::Object{
              {"object", "target"}, {"status", "Measured"}, {"visibleFraction", 2.0}, {"occluders", Value::Array{}}},
          Value::Object{{"object", "target"},
                        {"status", "Measured"},
                        {"visibleFraction", 1.0},
                        {"occluders", Value::Array{Value::Object{{"object", "blocker"}}}}}}) {
        auto bad = evaluateSceneVisualRelations({visible}, frame, Value::Object{{"targets", Value::Array{broken}}},
                                                report.value());
        REQUIRE(!bad.ok());
    }
}
