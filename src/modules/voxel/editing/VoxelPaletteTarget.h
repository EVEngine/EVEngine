#pragma once
#include "common/Export.h"


#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "voxel/CubeType.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::voxel {
class CubeTypeRegistry;
}
namespace eve::voxel_editing {

using CapabilityId       = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation    = editing::DomainOperation;
using EditRegion         = editing::EditRegion;
using EditorDiagnostic   = editing::Diagnostic;
template <class T>
using EditorResult                  = editing::Result<T>;
using EditorStatus                  = editing::Status;
using EditorValue                   = editing::Value;
using IDomainOperationTarget        = editing::IDomainOperationTarget;
using IDomainOperationTargetStaging = editing::IDomainOperationTargetStaging;
using IEditableTarget               = editing::IEditableTarget;
using IPropertyProvider             = editing::IPropertyProvider;
using ObjectId                      = editing::ObjectId;
using PropertyDescriptor            = editing::PropertyDescriptor;
using PropertyFlag                  = editing::PropertyFlag;
using PropertyPath                  = editing::PropertyPath;
using PropertyReadResult            = editing::PropertyReadResult;
using PropertyReadState             = editing::PropertyReadState;
using PropertySchema                = editing::PropertySchema;
using PropertySetMode               = editing::PropertySetMode;
using PropertyType                  = editing::PropertyType;
using Revision                      = editing::Revision;
using RuleId                        = editing::RuleId;
using SelectionSnapshot             = editing::SelectionSnapshot;
using TargetDescriptor              = editing::TargetDescriptor;
using TargetId                      = editing::TargetId;
using editing::validatePropertyValue;

/** @brief Stable authored entry in a Voxel cube palette. */
struct VoxelPaletteEntryValue {
    ObjectId        id;
    voxel::CubeType type;
};

/** @brief Revisioned Voxel CubeType palette with face-material Inspector. */
class EVENGINE_API_ORCHESTRATION VoxelPaletteTarget final : public ::eve::editing::EditableTargetState,
                                                            public virtual IEditableTarget,
                                                            public IDomainOperationTarget,
                                                            public IDomainOperationTargetStaging,
                                                            public IPropertyProvider {
public:
    /** @brief Voxel palette target. */
    explicit VoxelPaletteTarget(std::string id);
    /** @brief Target id. */
    TargetId                                targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor                        describe() const override;
    /**
     * @brief Query an optional stable editing capability.
     * @return Borrowed pointer owned by this target, or null when unsupported.
     * @lifetime Valid until this target is destroyed or the capability is explicitly invalidated.
     */
    void*                                   queryCapability(const CapabilityId&) override;
    /** @brief Applies domain operation. */
    EditorResult<void>                      applyDomainOperation(const DomainOperation&) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    EditorResult<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    /** @brief Schema. */
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    /** @brief Reads . */
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Make set. */
    EditorResult<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    /** @brief Make reset. */
    EditorResult<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Add one unique named cube type. */ EditorResult<DomainOperation> makeCreate(
        const VoxelPaletteEntryValue&) const;
    /** @brief Delete one cube type by stable editor ID. */ EditorResult<DomainOperation> makeDelete(
        const ObjectId&) const;
    /** @brief Entries. */
    const std::vector<VoxelPaletteEntryValue>& entries() const { return entries_; }
    /** @brief Validate. */
    std::vector<EditorDiagnostic>              validate() const;
    /** @brief Snapshot value. */
    EditorValue                                snapshotValue() const;
    /** @brief Loads snapshot. */
    EditorResult<void>                         loadSnapshot(const EditorValue&);

private:
    /** @brief Matches. */
    bool                                matches(const SelectionSnapshot&) const;
    /** @brief Content value. */
    EditorValue                         contentValue() const;
    /** @brief Replacement. */
    EditorResult<DomainOperation>       replacement(EditorValue, std::string = {}) const;
    std::string                         id_;
    std::vector<VoxelPaletteEntryValue> entries_;
};

/** @brief Stable base/variant ID mapping produced by palette publication. */
struct VoxelPalettePublishedEntry {
    ObjectId    id;
    std::string name;
    int         baseId   = 0;
    int         variants = 0;
};

/** @brief Candidate-first CubeTypeRegistry publication. */
class EVENGINE_API_ORCHESTRATION VoxelPaletteRuntime {
public:
    /** @brief Voxel palette runtime. */
    VoxelPaletteRuntime();
    /** @brief Voxel palette runtime. */
    ~VoxelPaletteRuntime();
    /** @brief Build a complete registry before replacing the active generation. */
    EditorResult<std::vector<VoxelPalettePublishedEntry>> publish(const VoxelPaletteTarget&);
    /** @brief Access the published registry. @return Borrowed pointer owned by this runtime, or null. @lifetime Valid
     * until the next publish or runtime destruction. */
    const voxel::CubeTypeRegistry* registry() const { return registry_.get(); }
    /** @brief Revision. */
    Revision                       revision() const { return revision_; }

private:
    std::unique_ptr<voxel::CubeTypeRegistry> registry_;
    Revision                                 revision_ = 0;
};
}  // namespace eve::voxel_editing
