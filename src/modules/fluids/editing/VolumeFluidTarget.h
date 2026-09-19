#pragma once

#include "fluids/editing/FluidTarget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::fluids {
class VolumeFluid;
}

namespace eve::fluids_editing {

/** @brief Serializable authoring parameters for the bounded volume-fluid solver. */
struct VolumeFluidAuthoringSettings {
    std::uint32_t capacity         = 8192;
    std::uint32_t previewParticles = 640;
    double        spacing          = 0.1;
    double        gravityX = 0.0, gravityY = -9.81, gravityZ = 0.0;
    double        minimumX = -2.0, minimumY = 0.0, minimumZ = -1.0;
    double        maximumX = 2.0, maximumY = 3.0, maximumZ = 1.0;
    std::uint32_t iterations = 5;
};

/** @brief Bounded preview allocation/work estimate before constructing a solver. */
struct VolumeFluidAuthoringPreview {
    EditorStatus                  status                    = EditorStatus::Failed;
    Revision                      documentRevision          = 0;
    std::uint64_t                 estimatedBytes            = 0;
    std::uint64_t                 estimatedConstraintVisits = 0;
    std::vector<EditorDiagnostic> diagnostics;
};

/**
 * @brief Reversible schema-driven document for native volume-fluid solver settings.
 * @ownership Owns only authoring values; it never owns or retains a runtime solver.
 * @thread Editor-thread affine.
 * @reentrancy Does not invoke callbacks.
 */
class VolumeFluidTarget final : public ::eve::editing::EditableTargetState, public virtual IEditableTarget,
                                public IDomainOperationTarget,
                                public IPropertyProvider {
public:
    explicit VolumeFluidTarget(std::string id);
    TargetId         targetId() const override { return TargetId(id_); }
    TargetDescriptor describe() const override;
    /** @brief Query the property capability. @return Borrowed pointer owned by this target.
     * @lifetime Valid until target destruction. */
    void*                         queryCapability(const CapabilityId& capability) override;
    EditorResult<void>            applyDomainOperation(const DomainOperation& operation) override;
    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;
    /** @brief Return an immutable copy of current authoring settings. */
    VolumeFluidAuthoringSettings settings() const { return settings_; }
    /** @brief Validate numeric ranges and container clearance. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Estimate preview memory and constraint visits without allocation. */
    VolumeFluidAuthoringPreview previewBudget(std::uint64_t byteBudget  = 256ULL * 1024ULL * 1024ULL,
                                              std::uint64_t visitBudget = 100000000ULL) const;
    /** @brief Capture schema eve.volume-fluid-authoring version one. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a strict version-one authoring document. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    bool                         matches(const SelectionSnapshot& selection) const;
    std::string                  id_;
    VolumeFluidAuthoringSettings settings_;
};

/** @brief Atomic bridge from a validated authoring document to a live solver. */
class VolumeFluidRuntimeApplier {
public:
    /**
     * @brief Apply solver settings through the canonical snapshot restore transaction.
     * @param target Validated authoring document.
     * @param simulation Borrowed live solver; required and not retained.
     * @details Explicit editor publication only. Copies live snapshot state and may reallocate
     * solver storage, so callers must not invoke it from a per-frame preview or simulation loop.
     */
    EditorResult<void> apply(const VolumeFluidTarget& target, fluids::VolumeFluid* simulation) const;
};

}  // namespace eve::fluids_editing
