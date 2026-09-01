#pragma once
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "editing/EditableTarget.h"
#include <array>
#include <memory>
#include <string>
namespace eve::graphics{class Graphics;class Texture;class Camera3D;}
namespace eve::hd2d{class Hd2D;class Sprite3D;class TileMap3D;}
namespace eve::hd2d_editing{
using CapabilityId=editing::CapabilityId;using DiagnosticSeverity=editing::DiagnosticSeverity;using DomainOperation=editing::DomainOperation;
using EditRegion=editing::EditRegion;using EditorDiagnostic=editing::Diagnostic;template<class T>using EditorResult=editing::Result<T>;
using EditorStatus=editing::Status;using EditorValue=editing::Value;using IDomainOperationTarget=editing::IDomainOperationTarget;
using IDomainOperationTargetStaging=editing::IDomainOperationTargetStaging;using IEditableTarget=editing::IEditableTarget;
using IPropertyProvider=editing::IPropertyProvider;using PropertyDescriptor=editing::PropertyDescriptor;using PropertyFlag=editing::PropertyFlag;
using PropertyPath=editing::PropertyPath;using PropertyReadResult=editing::PropertyReadResult;using PropertyReadState=editing::PropertyReadState;
using PropertySchema=editing::PropertySchema;using PropertySetMode=editing::PropertySetMode;using PropertyType=editing::PropertyType;
using Revision=editing::Revision;using RuleId=editing::RuleId;using SelectionSnapshot=editing::SelectionSnapshot;
using TargetDescriptor=editing::TargetDescriptor;using TargetId=editing::TargetId;using editing::validatePropertyValue;
/** @brief Authored HD-2D sprite-sheet and tile-extrusion preset. */
struct Hd2dAssetValue{std::string kind="sprite";std::string sourceAsset;int columns=1,rows=1,frame=0,animStart=0,animEnd=0;float fps=12;bool flipX=false,flipY=false,visible=true;std::array<float,2>size{1,1};std::array<float,4>tint{1,1,1,1};float sideDepth=6,heightScale=1;std::array<float,4>wallUv{0,0,.05f,.05f};};
/** @brief Revisioned HD-2D sprite/tilemap presentation asset. */
class Hd2dDocumentTarget final:public virtual IEditableTarget,public IDomainOperationTarget,public IDomainOperationTargetStaging,public IPropertyProvider{
public:explicit Hd2dDocumentTarget(std::string id);const std::string&targetId()const override{return id_;}unsigned long long revision()const override{return revision_;}EditRegion dirtyRegion()const override{return dirty_;}void clearDirtyRegion()override{dirty_.clear();}TargetDescriptor describe()const override;void*queryCapability(const CapabilityId&)override;EditorResult<void>applyDomainOperation(const DomainOperation&)override;std::unique_ptr<IDomainOperationTarget>cloneDomainState()const override;EditorResult<void>commitDomainState(std::unique_ptr<IDomainOperationTarget>)override;eve::Result<eve::Revision>currentRevision(const SelectionSnapshot&)const override;PropertySchema schema(const SelectionSnapshot&)const override;PropertyReadResult read(const SelectionSnapshot&,const PropertyPath&)const override;EditorResult<DomainOperation>makeSet(const SelectionSnapshot&,const PropertyPath&,const EditorValue&,PropertySetMode)const override;EditorResult<DomainOperation>makeReset(const SelectionSnapshot&,const PropertyPath&)const override;const Hd2dAssetValue&value()const{return value_;}std::vector<EditorDiagnostic>validate()const;EditorValue snapshotValue()const;EditorResult<void>loadSnapshot(const EditorValue&);
private:bool matches(const SelectionSnapshot&)const;EditorValue contentValue()const;EditorResult<DomainOperation>replacement(EditorValue,std::string)const;std::string id_;Revision revision_=1;EditRegion dirty_;Hd2dAssetValue value_;};
/** @brief Deterministically sampled sprite-sheet frame and UV rectangle. */
struct Hd2dFramePreview{int frame=0;std::array<float,4>uv{0,0,1,1};};
/** @brief Pure sprite animation scrub evaluator. */
class Hd2dFramePreviewService{public:EditorResult<Hd2dFramePreview>evaluate(const Hd2dDocumentTarget&,float time)const;};
/** @brief Resolves an HD-2D sprite texture. */
class IHd2dTextureResolver{public:virtual~IHd2dTextureResolver()=default;virtual EditorResult<graphics::Texture*>texture(const std::string&)const=0;};
/** @brief Candidate-first live Sprite3D/TileMap3D preset publication. */
class Hd2dDocumentRuntime{public:Hd2dDocumentRuntime();~Hd2dDocumentRuntime();EditorResult<hd2d::Sprite3D*>publishSprite(const Hd2dDocumentTarget&,hd2d::Hd2D*,graphics::Graphics*,graphics::Camera3D*,const IHd2dTextureResolver&);EditorResult<hd2d::TileMap3D*>publishTileMap(const Hd2dDocumentTarget&,hd2d::Hd2D*);Revision revision()const{return revision_;}private:std::unique_ptr<hd2d::Sprite3D>sprite_;std::unique_ptr<hd2d::TileMap3D>tilemap_;Revision revision_=0;};
} // namespace eve::hd2d_editing
