#pragma once
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "editing/EditableTarget.h"
#include <memory>
#include <string>
#include <vector>
namespace eve::procgen{class SpatialData;class BiomeRules;class PointSet;}
namespace eve::biome_editing{
using CapabilityId=editing::CapabilityId;using DiagnosticSeverity=editing::DiagnosticSeverity;using DomainOperation=editing::DomainOperation;
using EditRegion=editing::EditRegion;using EditorDiagnostic=editing::Diagnostic;template<class T>using EditorResult=editing::Result<T>;
using EditorStatus=editing::Status;using EditorValue=editing::Value;using IDomainOperationTarget=editing::IDomainOperationTarget;
using IDomainOperationTargetStaging=editing::IDomainOperationTargetStaging;using IEditableTarget=editing::IEditableTarget;
using IPropertyProvider=editing::IPropertyProvider;using ObjectId=editing::ObjectId;using PropertyDescriptor=editing::PropertyDescriptor;
using PropertyFlag=editing::PropertyFlag;using PropertyPath=editing::PropertyPath;using PropertyReadResult=editing::PropertyReadResult;
using PropertyReadState=editing::PropertyReadState;using PropertySchema=editing::PropertySchema;using PropertySetMode=editing::PropertySetMode;
using PropertyType=editing::PropertyType;using Revision=editing::Revision;using RuleId=editing::RuleId;
using SelectionSnapshot=editing::SelectionSnapshot;using TargetDescriptor=editing::TargetDescriptor;using TargetId=editing::TargetId;
using editing::validatePropertyValue;
/** @brief Stable weighted asset entry in one biome layer. */struct BiomeAssetValue{ObjectId id;std::string asset;float weight=1,minScale=1,maxScale=1;bool randomYaw=true;};
/** @brief Stable authored biome layer referencing a spatial asset. */struct BiomeLayerValue{ObjectId id;std::string name,spatialAsset;int priority=0;float density=1;std::vector<BiomeAssetValue>assets;};
/** @brief Revisioned BiomeRules asset. */
class BiomeDocumentTarget final:public virtual IEditableTarget,public IDomainOperationTarget,public IDomainOperationTargetStaging,public IPropertyProvider{
public:explicit BiomeDocumentTarget(std::string id);const std::string&targetId()const override{return id_;}unsigned long long revision()const override{return revision_;}EditRegion dirtyRegion()const override{return dirty_;}void clearDirtyRegion()override{dirty_.clear();}TargetDescriptor describe()const override;void*queryCapability(const CapabilityId&)override;EditorResult<void>applyDomainOperation(const DomainOperation&)override;std::unique_ptr<IDomainOperationTarget>cloneDomainState()const override;EditorResult<void>commitDomainState(std::unique_ptr<IDomainOperationTarget>)override;eve::Result<eve::Revision>currentRevision(const SelectionSnapshot&)const override;PropertySchema schema(const SelectionSnapshot&)const override;PropertyReadResult read(const SelectionSnapshot&,const PropertyPath&)const override;EditorResult<DomainOperation>makeSet(const SelectionSnapshot&,const PropertyPath&,const EditorValue&,PropertySetMode)const override;EditorResult<DomainOperation>makeReset(const SelectionSnapshot&,const PropertyPath&)const override;EditorResult<DomainOperation>makeCreateLayer(const BiomeLayerValue&)const;EditorResult<DomainOperation>makeDeleteLayer(const ObjectId&)const;EditorResult<DomainOperation>makeCreateAsset(const ObjectId&layer,const BiomeAssetValue&)const;EditorResult<DomainOperation>makeDeleteAsset(const ObjectId&)const;EditorResult<DomainOperation>makeSetExclusions(std::vector<std::string>)const;const std::vector<BiomeLayerValue>&layers()const{return layers_;}const std::vector<std::string>&exclusions()const{return exclusions_;}std::vector<EditorDiagnostic>validate()const;EditorValue snapshotValue()const;EditorResult<void>loadSnapshot(const EditorValue&);
private:bool matches(const SelectionSnapshot&)const;EditorValue contentValue()const;EditorResult<DomainOperation>replacement(EditorValue,std::string={})const;std::string id_;Revision revision_=1;EditRegion dirty_;std::vector<BiomeLayerValue>layers_;std::vector<std::string>exclusions_;};
/** @brief Resolves copied spatial domains for BiomeRules publication. */class IBiomeSpatialResolver{public:virtual~IBiomeSpatialResolver()=default;virtual EditorResult<procgen::SpatialData*>resolve(const std::string&)const=0;};
/** @brief Candidate-first BiomeRules generation. */class BiomeDocumentRuntime{public:BiomeDocumentRuntime();~BiomeDocumentRuntime();EditorResult<void>publish(const BiomeDocumentTarget&,const IBiomeSpatialResolver&);EditorResult<std::unique_ptr<procgen::PointSet>>preview(procgen::SpatialData*domain,float spacing,std::uint32_t seed,float jitter,Revision expectedRevision);procgen::BiomeRules*rules()const{return rules_.get();}Revision revision()const{return revision_;}private:std::unique_ptr<procgen::BiomeRules>rules_;Revision revision_=0;};
} // namespace eve::biome_editing
