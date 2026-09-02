#include "decal_editing/DecalTarget.h"

#include "decal/DecalManager.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <map>

using namespace eve::decal_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot selection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset,TargetId(target),StableId("decal"),"decal.instance"});
    return value;
}
class Assets final : public IDecalRuntimeAssetResolver {
public:
    std::map<std::string,eve::graphics::Texture*> values;
    EditorResult<eve::graphics::Texture*> texture(const std::string& asset) const override {
        auto found=values.find(asset);if(found==values.end())return eve::editing::failed<eve::graphics::Texture*>(
            EditorStatus::NotFound,RuleId("test.decal.texture"),"missing texture");
        return eve::editing::applied<eve::graphics::Texture*>(found->second);
    }
};
EditorResult<void> set(DecalDocumentTarget&target,const char*path,EditorValue value){auto op=target.makeSet(selection(target.targetId().value().c_str()),PropertyPath(path),value,PropertySetMode::Absolute);if(!op.ok())return EditorResult<void>::failure(op.status());return target.applyDomainOperation(op.value());}
}

TEST_CASE("editor.decal.properties_are_reversible_persistent_and_gizmo_ready") {
    DecalDocumentTarget target("scorch");
    auto operation=target.makeSet(selection("scorch"),PropertyPath("projection.size"),2.5,PropertySetMode::Absolute);
    REQUIRE(operation.ok());REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(*target.value("projection.size")->getIf<double>(),2.5);
    DomainOperation undo=operation.value();undo.payload=operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());CHECK_EQ(*target.value("projection.size")->getIf<double>(),0.5);
    REQUIRE(set(target,"texture.albedo","textures/scorch.png").ok());
    auto gizmo=DecalGizmoPreviewService().build(target);CHECK_EQ(static_cast<int>(gizmo.status),static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(gizmo.primitives.size(),static_cast<std::size_t>(2));CHECK_EQ(gizmo.primitives[0].kind,std::string("oriented-wire-box"));
    DecalDocumentTarget restored("copy");REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(*restored.value("texture.albedo")->getIf<std::string>(),std::string("textures/scorch.png"));
}

TEST_CASE("editor.decal.snapshot_is_atomic_and_rejects_projection_rules") {
    DecalDocumentTarget target("scorch");const auto before=target.snapshotValue();
    EditorValue invalid=before;auto*root=invalid.getIf<EditorValue::Object>();auto*values=(*root)["values"].getIf<EditorValue::Object>();
    (*values)["transform.normal"]=EditorValue::Array{0.0,0.0,0.0};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()),static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(),before);
    CHECK_EQ(static_cast<int>(target.makeSet(selection("scorch"),PropertyPath("texture.uvRect"),
        EditorValue::Array{0.8,0.0,0.4,1.0},PropertySetMode::Absolute).code()),static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.decal.runtime_replacement_resolves_assets_before_atomic_generation_swap") {
    auto&manager=eve::decal::DecalManager::inst();manager.clearAll();manager.setLimit("scorch",1);
    DecalDocumentTarget target("scorch");REQUIRE(set(target,"decal.kind","scorch").ok());
    REQUIRE(set(target,"texture.albedo","textures/a.png").ok());
    Assets assets;auto*texture=reinterpret_cast<eve::graphics::Texture*>(0x1);assets.values["textures/a.png"]=texture;
    DecalRuntimeBinding binding(&manager,&assets);REQUIRE(binding.publish(target).ok());
    const int first=binding.runtimeId();REQUIRE(first>0);CHECK_EQ(manager.count(),1);
    REQUIRE(set(target,"texture.albedo","textures/missing.png").ok());
    CHECK_EQ(static_cast<int>(binding.publish(target).code()),static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(binding.runtimeId(),first);CHECK_EQ(manager.count(),1);CHECK_EQ(manager.instances()[0].id,first);
    REQUIRE(set(target,"texture.albedo","textures/a.png").ok());REQUIRE(set(target,"projection.depth",0.7).ok());
    REQUIRE(binding.publish(target).ok());CHECK(binding.runtimeId()!=first);CHECK_EQ(manager.count(),1);
    CHECK_EQ(manager.instances()[0].depth,0.7f);REQUIRE(binding.clear().ok());CHECK_EQ(manager.count(),0);
}

TEST_CASE("editor.decal.publishing_target_preserves_author_and_runtime_on_rejection") {
    auto&manager=eve::decal::DecalManager::inst();manager.clearAll();
    Assets assets;assets.values["textures/a.png"]=reinterpret_cast<eve::graphics::Texture*>(0x1);
    DecalRuntimeBinding runtime(&manager,&assets);DecalPublishingTarget target("live",&runtime);
    auto first=target.authoringTarget().makeSet(selection("live"),PropertyPath("texture.albedo"),
                                                "textures/a.png",PropertySetMode::Absolute);
    REQUIRE(first.ok());REQUIRE(target.applyDomainOperation(first.value()).ok());
    const int generation=runtime.runtimeId();REQUIRE(generation>0);
    auto missing=target.authoringTarget().makeSet(selection("live"),PropertyPath("texture.albedo"),
                                                  "textures/missing.png",PropertySetMode::Absolute);
    REQUIRE(missing.ok());CHECK_EQ(static_cast<int>(target.applyDomainOperation(missing.value()).code()),
                                    static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(*target.authoringTarget().value("texture.albedo")->getIf<std::string>(),
             std::string("textures/a.png"));
    CHECK_EQ(runtime.runtimeId(),generation);CHECK_EQ(manager.instances()[0].id,generation);
    REQUIRE(runtime.clear().ok());
}
