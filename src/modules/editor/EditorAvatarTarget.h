#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTargetV2.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::avatar { class AvatarInstance; }
namespace eve::graphics { class Texture; }

namespace eve::editor {

/** @brief Stable image layer in an Avatar asset. */
struct AvatarLayerValue {
    ObjectId id;std::string name,textureAsset;int zIndex=0;bool visible=true;
    std::array<float,2> offset{0,0},size{64,64};std::array<float,4> color{1,1,1,1};
};
/** @brief Reflected Avatar parameter and its initial value. */
struct AvatarParameterValue { ObjectId id;std::string name;float defaultValue=0,minimum=0,maximum=1,value=0; };
/** @brief Named expression containing layer/parameter channel targets. */
struct AvatarExpressionValue { ObjectId id;std::string name;std::map<std::string,float> channels; };

/** @brief Revisioned image/Live2D/VRoid Avatar authoring asset. */
class AvatarDocumentTarget final : public IEditableTargetV2,
                                   public IDomainOperationTarget,
                                   public IDomainOperationTargetStaging,
                                   public IPropertyProvider {
public:
    explicit AvatarDocumentTarget(std::string id);
    const std::string& targetId()const override{return id_;}unsigned long long revision()const override{return revision_;}
    EditRegion dirtyRegion()const override{return dirty_;}void clearDirtyRegion()override{dirty_.clear();}
    TargetDescriptor describe()const override;void*queryCapability(const CapabilityId&)override;
    EditorResult<void>applyDomainOperation(const DomainOperation&)override;
    std::unique_ptr<IDomainOperationTarget>cloneDomainState()const override;
    EditorResult<void>commitDomainState(std::unique_ptr<IDomainOperationTarget>)override;
    eve::Result<eve::Revision>currentRevision(const SelectionSnapshot&)const override;
    PropertySchema schema(const SelectionSnapshot&)const override;
    PropertyReadResult read(const SelectionSnapshot&,const PropertyPath&)const override;
    EditorResult<DomainOperation>makeSet(const SelectionSnapshot&,const PropertyPath&,const EditorValue&,PropertySetMode)const override;
    EditorResult<DomainOperation>makeReset(const SelectionSnapshot&,const PropertyPath&)const override;
    /** @brief Change backend kind and model source. */
    EditorResult<DomainOperation>makeSetSource(std::string kind,std::string asset)const;
    EditorResult<DomainOperation>makeCreateLayer(const AvatarLayerValue&)const;
    EditorResult<DomainOperation>makeDeleteLayer(const ObjectId&)const;
    EditorResult<DomainOperation>makeCreateParameter(const AvatarParameterValue&)const;
    EditorResult<DomainOperation>makeDeleteParameter(const ObjectId&)const;
    EditorResult<DomainOperation>makeCreateExpression(const AvatarExpressionValue&)const;
    EditorResult<DomainOperation>makeDeleteExpression(const ObjectId&)const;
    const std::string&kind()const{return kind_;}const std::string&sourceAsset()const{return sourceAsset_;}
    const std::vector<AvatarLayerValue>&layers()const{return layers_;}const std::vector<AvatarParameterValue>&parameters()const{return parameters_;}
    const std::vector<AvatarExpressionValue>&expressions()const{return expressions_;}
    std::vector<EditorDiagnostic>validate()const;EditorValue snapshotValue()const;EditorResult<void>loadSnapshot(const EditorValue&);
private:
    bool matches(const SelectionSnapshot&)const;EditorValue contentValue()const;EditorResult<DomainOperation>replacement(EditorValue,std::string={})const;
    std::string id_,kind_="image",sourceAsset_;Revision revision_=1;EditRegion dirty_;
    std::vector<AvatarLayerValue>layers_;std::vector<AvatarParameterValue>parameters_;std::vector<AvatarExpressionValue>expressions_;
};

/** @brief Resolves image-layer textures for candidate publication. */
class IAvatarTextureResolver { public:virtual~IAvatarTextureResolver()=default;virtual EditorResult<graphics::Texture*>texture(const std::string&)const=0; };

/** @brief Candidate-first live AvatarInstance generation. */
class AvatarDocumentRuntime {
public:
    AvatarDocumentRuntime();~AvatarDocumentRuntime();
    /** @brief Build all layers, metadata and expressions before replacing the live generation.
     * @param textures Optional borrowed resolver; it is used only during this call and is never retained.
     * @lifetime The caller must keep textures alive for the duration of publish.
     */
    EditorResult<void>publish(const AvatarDocumentTarget&,const IAvatarTextureResolver* textures=nullptr);
    avatar::AvatarInstance*instance()const{return instance_.get();}Revision revision()const{return revision_;}
private:std::unique_ptr<avatar::AvatarInstance>instance_;Revision revision_=0;
};

} // namespace eve::editor
