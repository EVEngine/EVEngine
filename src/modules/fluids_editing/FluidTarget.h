#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"

#include <string>
#include <vector>

namespace eve::fluids {
class FluidSimulation;
}

namespace eve::fluids_editing {

using CapabilityId       = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation    = editing::DomainOperation;
using EditRegion         = editing::EditRegion;
using EditorDiagnostic   = editing::Diagnostic;

template <class T>
using EditorResult           = editing::Result<T>;
using EditorStatus           = editing::Status;
using EditorValue            = editing::Value;
using IDomainOperationTarget = editing::IDomainOperationTarget;
using IEditableTarget        = editing::IEditableTarget;
using IPropertyProvider      = editing::IPropertyProvider;
using PropertyDescriptor     = editing::PropertyDescriptor;
using PropertyFlag           = editing::PropertyFlag;
using PropertyPath           = editing::PropertyPath;
using PropertyReadResult     = editing::PropertyReadResult;
using PropertyReadState      = editing::PropertyReadState;
using PropertySchema         = editing::PropertySchema;
using PropertySetMode        = editing::PropertySetMode;
using PropertyType           = editing::PropertyType;
using Revision               = editing::Revision;
using RuleId                 = editing::RuleId;
using SelectionSnapshot      = editing::SelectionSnapshot;
using TargetDescriptor       = editing::TargetDescriptor;
using TargetId               = editing::TargetId;

using editing::validatePropertyValue;

/** @brief Serializable parameters matching the CPU/GPU surface fluid solver. */
struct FluidSimulationSettings {
    int    maxParticles     = 10000;
    int    previewParticles = 1000;
    double particleRadius   = 0.05;
    double supportRadius    = 0.20;
    double restDensity      = 1.0;
    double gravityX = 0.0, gravityY = -9.8, gravityZ = 0.0;
    double viscosity       = 0.02;
    double yieldStress     = 0.0;
    double cohesion        = 0.0;
    double adhesion        = 0.0;
    double damping         = 0.0;
    double maximumVelocity = 20.0;
    int    iterations      = 1;
    int    pbfIterations   = 2;
};

/** @brief Cost estimate used before allocating or running a fluid preview. */
struct FluidSimulationPreview {
    EditorStatus                  status                  = EditorStatus::Failed;
    Revision                      documentRevision        = 0;
    std::uint64_t                 estimatedBytes          = 0;
    std::uint64_t                 estimatedNeighborChecks = 0;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Reversible property document for fluid simulation authoring. */
class FluidSimulationTarget final : public virtual IEditableTarget,
                                    public IDomainOperationTarget,
                                    public IPropertyProvider {
public:
    explicit FluidSimulationTarget(std::string id);
    TargetId         targetId() const override { return TargetId(id_); }
    std::uint64_t    revision() const override { return revision_; }
    EditRegion       dirtyRegion() const override { return dirty_; }
    void             clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime
     * Valid until this target is destroyed or mutated. */
    void*                         queryCapability(const CapabilityId& capability) override;
    EditorResult<void>            applyDomainOperation(const DomainOperation& operation) override;
    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;
    /** @brief Return an immutable settings copy. */
    FluidSimulationSettings settings() const { return settings_; }
    /** @brief Validate solver ranges and cross-property kernel constraints. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Estimate bounded memory and neighbor work for preview confirmation. */
    FluidSimulationPreview previewBudget(std::uint64_t byteBudget     = 256ULL * 1024ULL * 1024ULL,
                                         std::uint64_t neighborBudget = 100000000ULL) const;
    /** @brief Capture deterministic settings. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load schema-version-one settings. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    bool                    matches(const SelectionSnapshot& selection) const;
    std::string             id_;
    Revision                revision_ = 1;
    EditRegion              dirty_;
    FluidSimulationSettings settings_;
};

/** @brief Optional bridge applying authored tuning to a live FluidSimulation. */
class FluidSimulationRuntimeApplier {
public:
    /** @brief Apply validated mutable solver parameters; capacity remains runtime-owned. */
    EditorResult<void> apply(const FluidSimulationTarget& target, fluids::FluidSimulation* simulation) const;
};

}  // namespace eve::fluids_editing
