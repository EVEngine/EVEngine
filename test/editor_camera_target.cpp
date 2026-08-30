#include "editor/EditorCameraTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
void apply(CameraDocumentTarget& target, EditorResult<DomainOperation> operation) {
    REQUIRE(operation.value);
    REQUIRE(target.applyDomainOperation(*operation.value).isAccepted());
}
SelectionSnapshot select(const CameraDocumentTarget& target, const char* id) {
    SelectionSnapshot selection;
    selection.channel = "camera";
    selection.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), "camera.rig"});
    return selection;
}
CameraRigValue rig(const char* id, const char* mode = "follow") {
    CameraRigValue value;
    value.id = ObjectId(id); value.name = id; value.mode = mode;
    return value;
}
}

TEST_CASE("editor.camera.rigs_are_property_editable_reversible_and_persistent") {
    CameraDocumentTarget target("shot");
    apply(target, target.makeCreateRig(rig("hero", "orbit")));
    const auto selection = select(target, "hero");
    auto edit = target.makeSet(selection, PropertyPath("rig.fov"), 72.0, PropertySetMode::Absolute);
    REQUIRE(edit.value); REQUIRE(target.applyDomainOperation(*edit.value).isAccepted());
    CHECK_EQ(*target.read(selection, PropertyPath("rig.fov")).value.getIf<double>(), 72.0);
    DomainOperation undo = *edit.value; undo.payload = edit.value->inverse;
    REQUIRE(target.applyDomainOperation(undo).isAccepted());
    CHECK_EQ(*target.read(selection, PropertyPath("rig.fov")).value.getIf<double>(), 60.0);
    CameraDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).isAccepted());
    CHECK_EQ(restored.rigs().front().id.value(), std::string("hero"));
}

TEST_CASE("editor.camera.timeline_has_stable_keys_reference_safety_and_scrub") {
    CameraDocumentTarget target("shot");
    auto hero = rig("hero", "follow"); hero.offset = {0, 3, 8};
    apply(target, target.makeCreateRig(hero));
    CameraTimelineKeyValue cut; cut.id=ObjectId("cut-1"); cut.kind="cut"; cut.time=1; cut.rig=ObjectId("hero"); cut.blend=.5f;
    apply(target, target.makeCreateKey(cut));
    CameraTimelineKeyValue fov0; fov0.id=ObjectId("fov-0"); fov0.kind="float"; fov0.time=1; fov0.property="fov"; fov0.value=60;
    apply(target, target.makeCreateKey(fov0));
    CameraTimelineKeyValue fov; fov.id=ObjectId("fov-1"); fov.kind="float"; fov.time=3; fov.property="fov"; fov.value=80;
    apply(target, target.makeCreateKey(fov));
    CHECK_EQ(static_cast<int>(target.makeDeleteRig(ObjectId("hero")).status), static_cast<int>(EditorStatus::Conflict));
    CameraPreview preview; auto pose=preview.evaluate(target,2); REQUIRE(pose.value);
    CHECK_EQ(pose.value->rig.value(),std::string("hero")); CHECK_EQ(pose.value->fov,70.f);
    auto overlay=preview.gizmo(target,2); REQUIRE(overlay.value); CHECK_EQ(overlay.value->primitives.size(),static_cast<std::size_t>(3));
}

TEST_CASE("editor.camera.invalid_snapshot_and_runtime_generation_are_atomic") {
    CameraDocumentTarget target("shot"); apply(target,target.makeCreateRig(rig("hero")));
    const auto before=target.snapshotValue(); EditorValue invalid=before;
    auto* root=invalid.getIf<EditorValue::Object>(); auto* content=(*root)["content"].getIf<EditorValue::Object>();
    auto* rigs=(*content)["rigs"].getIf<EditorValue::Array>(); auto* first=(*rigs)[0].getIf<EditorValue::Object>(); (*first)["fov"]=200.0;
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status),static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(),before);
    CameraDocumentRuntime runtime; REQUIRE(runtime.publish(target,nullptr).isAccepted());
    auto* generation=runtime.controller(); const auto revision=runtime.revision();
    CameraDocumentTarget empty("empty"); REQUIRE(runtime.publish(empty,nullptr).isAccepted());
    CHECK(runtime.controller()!=generation); CHECK(runtime.revision()!=revision);
}
