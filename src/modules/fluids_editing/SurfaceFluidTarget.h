#pragma once

#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "editing/EditableTarget.h"

#include <string>
#include <vector>

namespace eve::fluids {
class SurfaceDropletSimulation;
struct SurfaceFluidRenderParams;
struct SurfaceWetnessParams;
}

namespace eve::fluids_editing {

using CapabilityId=editing::CapabilityId;using DiagnosticSeverity=editing::DiagnosticSeverity;using DomainOperation=editing::DomainOperation;
using EditRegion=editing::EditRegion;using EditorDiagnostic=editing::Diagnostic;template<class T>using EditorResult=editing::Result<T>;
using EditorStatus=editing::Status;using EditorValue=editing::Value;using IDomainOperationTarget=editing::IDomainOperationTarget;
using IEditableTarget=editing::IEditableTarget;using IPropertyProvider=editing::IPropertyProvider;using PropertyDescriptor=editing::PropertyDescriptor;
using PropertyFlag=editing::PropertyFlag;using PropertyPath=editing::PropertyPath;using PropertyReadResult=editing::PropertyReadResult;
using PropertyReadState=editing::PropertyReadState;using PropertySchema=editing::PropertySchema;using PropertySetMode=editing::PropertySetMode;
using PropertyType=editing::PropertyType;using Revision=editing::Revision;using RuleId=editing::RuleId;
using SelectionSnapshot=editing::SelectionSnapshot;using TargetDescriptor=editing::TargetDescriptor;using TargetId=editing::TargetId;
using editing::validatePropertyValue;

/** @brief Renderer-neutral droplet motion, wet-film and wet-material authoring values. */
struct SurfaceFluidSettings {
    double gravityX = 0.0, gravityY = -9.8, gravityZ = 0.0;
    double friction = 1.5, maxSpeed = 12.0, adhesionAcceleration = 12.0;
    int maxCrossings = 16;
    double contactAngleDegrees = 72.0, mergeRadiusScale = 0.72, trailDeposition = 0.22;
    double airDrag = 0.08, reattachDistance = 0.035;
    double diffusion = 0.18, evaporation = 0.025, maxWetness = 1.0;
    double velocityStretch = 0.30, maxAspectRatio = 2.6, surfaceOffset = 0.001;
    double dryRoughness = 0.48, wetRoughness = 0.08;
    double drySpecular = 0.35, wetSpecular = 0.92;
    double wetDarkening = 0.12, normalStrength = 0.18;

    auto operator<=>(const SurfaceFluidSettings&) const = default;
};

/** @brief Reversible document for surface droplets, wet traces and material response. */
class SurfaceFluidTarget final : public virtual IEditableTarget,
                                 public IDomainOperationTarget,
                                 public IPropertyProvider {
public:
    explicit SurfaceFluidTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    /** @brief Return immutable authored values. */
    SurfaceFluidSettings settings() const { return settings_; }
    /** @brief Validate numerical safety and material response relationships. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Capture schema-version-one settings. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load and validate persisted settings. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    bool matches(const SelectionSnapshot& selection) const;
    std::string id_;
    Revision revision_ = 1;
    EditRegion dirty_;
    SurfaceFluidSettings settings_;
};

/** @brief Optional bridge copying a validated document into live fluid components. */
class SurfaceFluidRuntimeApplier {
public:
    /** @brief Apply droplet parameters and emit matching render/wetness parameters atomically. */
    EditorResult<void> apply(const SurfaceFluidTarget& target,
                             fluids::SurfaceDropletSimulation* simulation,
                             fluids::SurfaceFluidRenderParams* render,
                             fluids::SurfaceWetnessParams* wetness) const;
};

}  // namespace eve::fluids_editing
