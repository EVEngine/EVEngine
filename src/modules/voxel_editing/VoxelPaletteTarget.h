#pragma once

#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "editing/EditableTarget.h"
#include "voxel/CubeType.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::voxel { class CubeTypeRegistry; }
namespace eve::voxel_editing {

using CapabilityId = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation = editing::DomainOperation;
using EditRegion = editing::EditRegion;
using EditorDiagnostic = editing::Diagnostic;
template<class T> using EditorResult = editing::Result<T>;
using EditorStatus = editing::Status;
using EditorValue = editing::Value;
using IDomainOperationTarget = editing::IDomainOperationTarget;
using IDomainOperationTargetStaging = editing::IDomainOperationTargetStaging;
using IEditableTarget = editing::IEditableTarget;
using IPropertyProvider = editing::IPropertyProvider;
using ObjectId = editing::ObjectId;
using PropertyDescriptor = editing::PropertyDescriptor;
using PropertyFlag = editing::PropertyFlag;
using PropertyPath = editing::PropertyPath;
using PropertyReadResult = editing::PropertyReadResult;
using PropertyReadState = editing::PropertyReadState;
using PropertySchema = editing::PropertySchema;
using PropertySetMode = editing::PropertySetMode;
using PropertyType = editing::PropertyType;
using Revision = editing::Revision;
using RuleId = editing::RuleId;
using SelectionSnapshot = editing::SelectionSnapshot;
using TargetDescriptor = editing::TargetDescriptor;
using TargetId = editing::TargetId;
using editing::validatePropertyValue;

/** @brief Stable authored entry in a Voxel cube palette. */
struct VoxelPaletteEntryValue { ObjectId id;voxel::CubeType type; };

/** @brief Revisioned Voxel CubeType palette with face-material Inspector. */
class VoxelPaletteTarget final:public virtual IEditableTarget,public IDomainOperationTarget,
 public IDomainOperationTargetStaging,public IPropertyProvider {
public:
 explicit VoxelPaletteTarget(std::string id);
 const std::string&targetId()const override{return id_;}unsigned long long revision()const override{return revision_;}
 EditRegion dirtyRegion()const override{return dirty_;}void clearDirtyRegion()override{dirty_.clear();}
 TargetDescriptor describe()const override;void*queryCapability(const CapabilityId&)override;
 EditorResult<void>applyDomainOperation(const DomainOperation&)override;std::unique_ptr<IDomainOperationTarget>cloneDomainState()const override;
 EditorResult<void>commitDomainState(std::unique_ptr<IDomainOperationTarget>)override;
 eve::Result<eve::Revision>currentRevision(const SelectionSnapshot&)const override;PropertySchema schema(const SelectionSnapshot&)const override;
 PropertyReadResult read(const SelectionSnapshot&,const PropertyPath&)const override;
 EditorResult<DomainOperation>makeSet(const SelectionSnapshot&,const PropertyPath&,const EditorValue&,PropertySetMode)const override;
 EditorResult<DomainOperation>makeReset(const SelectionSnapshot&,const PropertyPath&)const override;
 /** @brief Add one unique named cube type. */EditorResult<DomainOperation>makeCreate(const VoxelPaletteEntryValue&)const;
 /** @brief Delete one cube type by stable editor ID. */EditorResult<DomainOperation>makeDelete(const ObjectId&)const;
 const std::vector<VoxelPaletteEntryValue>&entries()const{return entries_;}
 std::vector<EditorDiagnostic>validate()const;EditorValue snapshotValue()const;EditorResult<void>loadSnapshot(const EditorValue&);
private:
 bool matches(const SelectionSnapshot&)const;EditorValue contentValue()const;EditorResult<DomainOperation>replacement(EditorValue,std::string={})const;
 std::string id_;Revision revision_=1;EditRegion dirty_;std::vector<VoxelPaletteEntryValue>entries_;
};

/** @brief Stable base/variant ID mapping produced by palette publication. */
struct VoxelPalettePublishedEntry { ObjectId id;std::string name;int baseId=0;int variants=0; };

/** @brief Candidate-first CubeTypeRegistry publication. */
class VoxelPaletteRuntime {
public:VoxelPaletteRuntime();~VoxelPaletteRuntime();
 /** @brief Build a complete registry before replacing the active generation. */
 EditorResult<std::vector<VoxelPalettePublishedEntry>>publish(const VoxelPaletteTarget&);
 /** @brief Access the published registry. @return Borrowed pointer owned by this runtime, or null. @lifetime Valid until the next publish or runtime destruction. */
 const voxel::CubeTypeRegistry*registry()const{return registry_.get();}Revision revision()const{return revision_;}
private:std::unique_ptr<voxel::CubeTypeRegistry>registry_;Revision revision_=0;
};
} // namespace eve::voxel_editing
