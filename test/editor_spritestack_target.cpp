#include "editor/EditorSpriteStackTarget.h"

#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
SelectionSnapshot select(const SpriteStackDocumentTarget& target){SelectionSnapshot s;s.channel="spritestack";s.items.push_back({SelectionDomain::Asset,TargetId(target.targetId()),StableId(target.targetId()),"spritestack.asset"});return s;}
void apply(SpriteStackDocumentTarget& target,EditorResult<DomainOperation> operation){REQUIRE(operation.value);REQUIRE(target.applyDomainOperation(*operation.value).accepted());}
class MissingModelResolver final : public ISpriteStackModelResolver {
public:
    EditorResult<eve::model3d::ModelData*> resolveModel(const std::string&) const override {return EditorResult<eve::model3d::ModelData*>::error(EditorStatus::NotFound,RuleId("test.model"),"missing model");}
};
}

TEST_CASE("editor.spritestack.preset_is_reversible_and_atomic") {
    SpriteStackDocumentTarget target("stack");const auto selection=select(target);
    auto layers=target.makeSet(selection,PropertyPath("bake.layers"),int64_t{8},PropertySetMode::Absolute);REQUIRE(layers.value);REQUIRE(target.applyDomainOperation(*layers.value).accepted());CHECK_EQ(target.value().bake.layerCount,8);
    DomainOperation undo=*layers.value;undo.payload=layers.value->inverse;REQUIRE(target.applyDomainOperation(undo).accepted());CHECK_EQ(target.value().bake.layerCount,16);
    const auto before=target.snapshotValue();EditorValue invalid=before;auto*root=invalid.getIf<EditorValue::Object>();auto*content=(*root)["content"].getIf<EditorValue::Object>();(*content)["width"]=int64_t{100000};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status),static_cast<int>(EditorStatus::Rejected));CHECK_EQ(target.snapshotValue(),before);
}

TEST_CASE("editor.spritestack.bake_is_deterministic_and_reports_layer_artifacts") {
    SpriteStackDocumentTarget target("stack");const auto selection=select(target);apply(target,target.makeSet(selection,PropertyPath("bake.layers"),int64_t{6},PropertySetMode::Absolute));apply(target,target.makeSet(selection,PropertyPath("bake.width"),int64_t{32},PropertySetMode::Absolute));apply(target,target.makeSet(selection,PropertyPath("bake.height"),int64_t{24},PropertySetMode::Absolute));
    SpriteStackBakeRuntime runtime;auto first=runtime.bake(target);REQUIRE(first.value);CHECK_EQ(first.value->size(),static_cast<std::size_t>(6));for(const auto&layer:*first.value){CHECK_EQ(layer.width,32);CHECK_EQ(layer.height,24);CHECK(layer.checksum!=0);CHECK(layer.alphaCoverage>0);}const auto artifacts=*first.value;auto second=runtime.bake(target);REQUIRE(second.value);for(std::size_t i=0;i<artifacts.size();++i)CHECK_EQ((*second.value)[i].checksum,artifacts[i].checksum);
}

TEST_CASE("editor.spritestack.failed_rebake_preserves_generation_and_stale_publish_fails_first") {
    SpriteStackDocumentTarget target("stack");SpriteStackBakeRuntime runtime;auto baked=runtime.bake(target);REQUIRE(baked.value);const auto revision=runtime.revision();const auto checksum=baked.value->front().checksum;
    const auto selection=select(target);apply(target,target.makeSet(selection,PropertyPath("source.kind"),"model",PropertySetMode::Absolute));apply(target,target.makeSet(selection,PropertyPath("source.value"),"missing.glb",PropertySetMode::Absolute));MissingModelResolver resolver;CHECK_EQ(static_cast<int>(runtime.bake(target,&resolver).status),static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(runtime.revision(),revision);CHECK_EQ(runtime.layers().front()->getWidth(),128);CHECK(checksum!=0);CHECK_EQ(static_cast<int>(runtime.publish(nullptr,target.revision()).status),static_cast<int>(EditorStatus::Conflict));
}
