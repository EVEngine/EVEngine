#include "housegen_editing/HouseGenTarget.h"

#include "housegen/HouseLayout.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::housegen_editing;
using namespace eve::editing;

namespace {
void apply(HouseGenDocumentTarget& target, EditorResult<DomainOperation> operation) {
    REQUIRE(operation.value); REQUIRE(target.applyDomainOperation(*operation.value).isAccepted());
}
HouseKitComponentValue component(const char* id, const char* category) {
    HouseKitComponentValue value; value.id=ObjectId(std::string("editor-")+id);
    value.component.id=id; value.component.modelPath=std::string("fixtures/")+id+".glb";
    value.component.category=category; return value;
}
void completeKit(HouseGenDocumentTarget& target) {
    apply(target,target.makeCreateComponent(component("foundation","foundation")));
    apply(target,target.makeCreateComponent(component("floor","floor")));
    apply(target,target.makeCreateComponent(component("wall","wall")));
    apply(target,target.makeCreateComponent(component("door","door")));
    apply(target,target.makeCreateComponent(component("roof","roof")));
}
SelectionSnapshot select(const HouseGenDocumentTarget& target,const char* id){SelectionSnapshot s;s.channel="housegen";s.items.push_back({SelectionDomain::Asset,TargetId(target.targetId()),StableId(id),"housegen.component"});return s;}
}

TEST_CASE("editor.housegen.kit_supports_incremental_authoring_properties_and_snapshot") {
    HouseGenDocumentTarget target("kit");
    apply(target,target.makeCreateComponent(component("wall","wall")));
    CHECK(!target.validate().empty());
    auto selection=select(target,"editor-wall");
    auto edit=target.makeSet(selection,PropertyPath("component.weight"),int64_t{5},PropertySetMode::Absolute);
    REQUIRE(edit.value);REQUIRE(target.applyDomainOperation(*edit.value).isAccepted());
    CHECK_EQ(*target.read(selection,PropertyPath("component.weight")).value.getIf<int64_t>(),int64_t{5});
    DomainOperation undo=*edit.value;undo.payload=edit.value->inverse;REQUIRE(target.applyDomainOperation(undo).isAccepted());
    CHECK_EQ(*target.read(selection,PropertyPath("component.weight")).value.getIf<int64_t>(),int64_t{1});
    HouseGenDocumentTarget restored("copy");REQUIRE(restored.loadSnapshot(target.snapshotValue()).isAccepted());
    CHECK_EQ(restored.components().size(),static_cast<std::size_t>(1));
}

TEST_CASE("editor.housegen.preview_is_deterministic_revision_safe_and_failure_atomic") {
    HouseGenDocumentTarget target("kit");completeKit(target);
    eve::housegen::HouseRequest request;request.seed=42;request.width=5;request.depth=4;request.floors=2;
    apply(target,target.makeSetRequest(request));
    HouseGenPreviewRuntime runtime;REQUIRE(runtime.publish(target).isAccepted());
    const std::string first=runtime.layout()->toJson();const auto revision=runtime.revision();
    REQUIRE(runtime.publish(target).isAccepted());CHECK_EQ(runtime.layout()->toJson(),first);
    auto overlay=runtime.gizmo(revision);REQUIRE(overlay.value);CHECK(!overlay.value->primitives.empty());
    apply(target,target.makeDeleteComponent(ObjectId("editor-roof")));
    CHECK_EQ(static_cast<int>(runtime.publish(target).status),static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(runtime.layout()->toJson(),first);CHECK_EQ(runtime.revision(),revision);
    CHECK_EQ(static_cast<int>(runtime.gizmo(target.revision()).status),static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.housegen.invalid_snapshot_does_not_partially_replace_document") {
    HouseGenDocumentTarget target("kit");completeKit(target);const auto before=target.snapshotValue();
    EditorValue invalid=before;auto* root=invalid.getIf<EditorValue::Object>();auto* content=(*root)["content"].getIf<EditorValue::Object>();auto* request=(*content)["request"].getIf<EditorValue::Object>();(*request)["floors"]=int64_t{1000};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status),static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(),before);
}
