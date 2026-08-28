#include "editor/EditorSceneImportTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::editor;
namespace{SelectionSnapshot select(const SceneImportTarget&t){SelectionSnapshot s;s.channel="scene-import";s.items.push_back({SelectionDomain::Asset,TargetId(t.targetId()),StableId(t.targetId()),"scene-import"});return s;}void apply(SceneImportTarget&t,EditorResult<DomainOperation>op){REQUIRE(op.value);REQUIRE(t.applyDomainOperation(*op.value).accepted());}}
TEST_CASE("editor.scene_import.presets_expand_and_custom_edits_are_reversible"){
 SceneImportTarget target("castle.import");const auto selection=select(target);apply(target,target.makeSet(selection,PropertyPath("source"),"castle.glb",PropertySetMode::Absolute));auto mobile=target.makeSet(selection,PropertyPath("preset"),"mobile",PropertySetMode::Absolute);REQUIRE(mobile.value);REQUIRE(target.applyDomainOperation(*mobile.value).accepted());CHECK(!target.value().importAnimations);CHECK(!target.value().importLights);CHECK(target.value().mipmaps);
 auto camera=target.makeSet(selection,PropertyPath("importCameras"),true,PropertySetMode::Absolute);REQUIRE(camera.value);REQUIRE(target.applyDomainOperation(*camera.value).accepted());CHECK_EQ(target.value().preset,std::string("custom"));DomainOperation undo=*camera.value;undo.payload=camera.value->inverse;REQUIRE(target.applyDomainOperation(undo).accepted());CHECK_EQ(target.value().preset,std::string("mobile"));CHECK(!target.value().importCameras);
}
TEST_CASE("editor.scene_import.snapshot_load_is_atomic_and_preflight_requires_loader"){
 SceneImportTarget target("castle.import");const auto selection=select(target);apply(target,target.makeSet(selection,PropertyPath("source"),"castle.glb",PropertySetMode::Absolute));const auto before=target.snapshotValue();EditorValue invalid=before;auto*root=invalid.getIf<EditorValue::Object>();auto*content=(*root)["content"].getIf<EditorValue::Object>();(*content)["preset"]="unknown";CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status),static_cast<int>(EditorStatus::Rejected));CHECK_EQ(target.snapshotValue(),before);SceneImportPreflightRuntime runtime;CHECK_EQ(static_cast<int>(runtime.inspect(target,nullptr).status),static_cast<int>(EditorStatus::Rejected));
}
