#include "avatar_editing/AvatarTarget.h"

#include "avatar/AvatarInstance.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::avatar_editing;
using namespace eve::editing;

namespace {
void apply(AvatarDocumentTarget&target,EditorResult<DomainOperation>operation){REQUIRE(operation.value);REQUIRE(target.applyDomainOperation(*operation.value).isAccepted());}
SelectionSnapshot select(const AvatarDocumentTarget&target,const char*id,const char*type){SelectionSnapshot s;s.channel="avatar";s.items.push_back({SelectionDomain::Asset,TargetId(target.targetId()),StableId(id),type});return s;}
class MissingTextures final:public IAvatarTextureResolver{public:EditorResult<eve::graphics::Texture*>texture(const std::string&)const override{return EditorResult<eve::graphics::Texture*>::error(EditorStatus::NotFound,RuleId("test.texture"),"missing texture");}};
}

TEST_CASE("editor.avatar.layers_and_parameters_have_dynamic_reversible_inspectors") {
 AvatarDocumentTarget target("hero");AvatarLayerValue layer;layer.id=ObjectId("eyes-id");layer.name="eyes";layer.textureAsset="eyes.png";apply(target,target.makeCreateLayer(layer));
 AvatarParameterValue parameter;parameter.id=ObjectId("mouth-id");parameter.name="mouth";parameter.maximum=2;parameter.value=.5f;apply(target,target.makeCreateParameter(parameter));
 const auto layerSelection=select(target,"eyes-id","avatar.layer");CHECK(target.schema(layerSelection).find(PropertyPath("layer.texture"))!=nullptr);auto edit=target.makeSet(layerSelection,PropertyPath("layer.z"),int64_t{7},PropertySetMode::Absolute);REQUIRE(edit.value);REQUIRE(target.applyDomainOperation(*edit.value).isAccepted());CHECK_EQ(target.layers().front().zIndex,7);DomainOperation undo=*edit.value;undo.payload=edit.value->inverse;REQUIRE(target.applyDomainOperation(undo).isAccepted());CHECK_EQ(target.layers().front().zIndex,0);
 const auto parameterSelection=select(target,"mouth-id","avatar.parameter");CHECK(target.schema(parameterSelection).find(PropertyPath("parameter.value"))!=nullptr);apply(target,target.makeSet(parameterSelection,PropertyPath("parameter.value"),1.5,PropertySetMode::Absolute));CHECK_EQ(target.parameters().front().value,1.5f);
}

TEST_CASE("editor.avatar.expressions_protect_referenced_channels_and_snapshot_is_atomic") {
 AvatarDocumentTarget target("hero");AvatarParameterValue parameter;parameter.id=ObjectId("mouth-id");parameter.name="mouth";apply(target,target.makeCreateParameter(parameter));AvatarExpressionValue expression;expression.id=ObjectId("smile-id");expression.name="smile";expression.channels["mouth"]=1;apply(target,target.makeCreateExpression(expression));CHECK_EQ(static_cast<int>(target.makeDeleteParameter(ObjectId("mouth-id")).status),static_cast<int>(EditorStatus::Conflict));
 const auto before=target.snapshotValue();EditorValue invalid=before;auto*root=invalid.getIf<EditorValue::Object>();auto*content=(*root)["content"].getIf<EditorValue::Object>();auto*parameters=(*content)["parameters"].getIf<EditorValue::Array>();auto*first=(*parameters)[0].getIf<EditorValue::Object>();(*first)["maximum"]=-1.0;CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status),static_cast<int>(EditorStatus::Rejected));CHECK_EQ(target.snapshotValue(),before);
}

TEST_CASE("editor.avatar.runtime_publishes_metadata_and_preserves_generation_on_asset_failure") {
 AvatarDocumentTarget target("hero");AvatarParameterValue parameter;parameter.id=ObjectId("mouth-id");parameter.name="mouth";parameter.value=.75f;apply(target,target.makeCreateParameter(parameter));AvatarExpressionValue expression;expression.id=ObjectId("smile-id");expression.name="smile";expression.channels["mouth"]=1;apply(target,target.makeCreateExpression(expression));AvatarDocumentRuntime runtime;REQUIRE(runtime.publish(target).isAccepted());REQUIRE(runtime.instance()!=nullptr);CHECK(runtime.instance()->hasParameter("mouth"));CHECK_EQ(runtime.instance()->getParameter("mouth"),.75f);const auto*generation=runtime.instance();const auto revision=runtime.revision();
 AvatarLayerValue layer;layer.id=ObjectId("eyes-id");layer.name="eyes";layer.textureAsset="missing.png";apply(target,target.makeCreateLayer(layer));MissingTextures textures;CHECK_EQ(static_cast<int>(runtime.publish(target,&textures).status),static_cast<int>(EditorStatus::NotFound));CHECK(runtime.instance()==generation);CHECK_EQ(runtime.revision(),revision);
}
