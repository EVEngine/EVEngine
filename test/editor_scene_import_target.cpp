#include "sceneloader_editing/SceneImportTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::sceneloader_editing;
using namespace eve::editing;
namespace{SelectionSnapshot select(const SceneImportTarget&t){SelectionSnapshot s;s.channel="scene-import";s.items.push_back({SelectionDomain::Asset,TargetId(t.targetId()),StableId(t.targetId().value()),"scene-import"});return s;}void apply(SceneImportTarget&t,EditorResult<DomainOperation>op){REQUIRE(op.ok());REQUIRE(t.applyDomainOperation(op.value()).ok());}}
TEST_CASE("editor.scene_import.presets_expand_and_custom_edits_are_reversible"){
 SceneImportTarget target("castle.import");const auto selection=select(target);apply(target,target.makeSet(selection,PropertyPath("source"),"castle.glb",PropertySetMode::Absolute));auto mobile=target.makeSet(selection,PropertyPath("preset"),"mobile",PropertySetMode::Absolute);REQUIRE(mobile.ok());REQUIRE(target.applyDomainOperation(mobile.value()).ok());CHECK(!target.value().importAnimations);CHECK(!target.value().importLights);CHECK(target.value().mipmaps);
 auto camera=target.makeSet(selection,PropertyPath("importCameras"),true,PropertySetMode::Absolute);REQUIRE(camera.ok());REQUIRE(target.applyDomainOperation(camera.value()).ok());CHECK_EQ(target.value().preset,std::string("custom"));DomainOperation undo=camera.value();undo.payload=camera.value().inverse;REQUIRE(target.applyDomainOperation(undo).ok());CHECK_EQ(target.value().preset,std::string("mobile"));CHECK(!target.value().importCameras);
}
TEST_CASE("editor.scene_import.snapshot_load_is_atomic_and_preflight_requires_loader"){
 SceneImportTarget target("castle.import");const auto selection=select(target);apply(target,target.makeSet(selection,PropertyPath("source"),"castle.glb",PropertySetMode::Absolute));const auto before=target.snapshotValue();EditorValue invalid=before;auto*root=invalid.getIf<EditorValue::Object>();auto*content=(*root)["content"].getIf<EditorValue::Object>();(*content)["preset"]="unknown";CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()),static_cast<int>(EditorStatus::Rejected));CHECK_EQ(target.snapshotValue(),before);SceneImportPreflightRuntime runtime;CHECK_EQ(static_cast<int>(runtime.inspect(target,nullptr).code()),static_cast<int>(EditorStatus::Rejected));
}
