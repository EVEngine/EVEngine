#pragma once
#include "common/Export.h"

#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "gpuagents/EffectProfile.h"

#include <string>
#include <vector>

namespace eve::gpuagents {
class EffectBackend;
class GpuAgentWorld;
}  // namespace eve::gpuagents

namespace eve::gpuagents_editing {

using CapabilityId       = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation    = editing::DomainOperation;
using EditorDiagnostic   = editing::Diagnostic;
template <class T>
using EditorResult           = editing::Result<T>;
using EditorStatus           = editing::Status;
using EditorValue            = editing::Value;
using IDomainOperationTarget = editing::IDomainOperationTarget;
using IEditableTarget        = editing::IEditableTarget;
using IPropertyProvider      = editing::IPropertyProvider;
using PropertyPath           = editing::PropertyPath;
using PropertyReadResult     = editing::PropertyReadResult;
using PropertySchema         = editing::PropertySchema;
using PropertySetMode        = editing::PropertySetMode;
using Revision               = editing::Revision;
using RuleId                 = editing::RuleId;
using SelectionSnapshot      = editing::SelectionSnapshot;
using TargetDescriptor       = editing::TargetDescriptor;
using TargetId               = editing::TargetId;

/** @brief Authoring values for one GPU Agents effect document. */
struct EVENGINE_API_ORCHESTRATION GpuAgentsSettings {
    std::string  kind                = "Fish";  ///< Fish | LifeNetwork | Bird | Petal
    std::int64_t maxAgents           = 128;
    double       fixedDt             = 1.0 / 60.0;
    double       agentRadius         = 0.25;
    double       obstaclePredictTime = 0.35;
    std::int64_t seed                = 1;

    // Shared boid / life motion knobs (kind-specific consumers ignore unused fields).
    double       separationRadius  = 1.5;
    double       cohesionRadius    = 4.0;
    double       alignmentRadius   = 3.0;
    double       separationWeight  = 1.4;
    double       cohesionWeight    = 0.8;
    double       alignmentWeight   = 1.0;
    double       maxSpeed          = 4.0;
    double       moveSpeed         = 1.8;
    double       depositStrength   = 0.35;
    double       trailDecayRate    = 0.15;
    double       diffusionRate     = 0.08;
    double       trailFollow       = 1.2;
    std::int64_t fieldResolution   = 64;
    double       worldSize         = 32.0;
    double       originX           = -16.0;
    double       originY           = 0.0;
    double       originZ           = -16.0;
    double       minSurfaceNormalZ = 0.35;

    // LifeField material binding labels.
    std::string lifeFieldSampler   = "LifeFieldTexture";
    std::string surfaceDataSampler = "SurfaceDataTexture";

    auto operator<=>(const GpuAgentsSettings&) const = default;
};

/** @brief Build the UI-independent PropertySchema for gpuagents:effect-document v1. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION PropertySchema gpuAgentsEffectSchema();

/** @brief Convert authoring settings into a runtime EffectProfile. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION gpuagents::EffectProfile toEffectProfile(const GpuAgentsSettings& settings);

/** @brief Reversible document for GPU Agents effect authoring. */
class EVENGINE_API_ORCHESTRATION GpuAgentsDocumentTarget final : public ::eve::editing::EditableTargetState,
                                                                 public virtual IEditableTarget,
                                                                 public IDomainOperationTarget,
                                                                 public IPropertyProvider {
public:
    explicit GpuAgentsDocumentTarget(std::string id);
    TargetId         targetId() const override { return TargetId(id_); }
    TargetDescriptor describe() const override;
    /**
     * @brief Query an optional target capability.
     * @ownership Borrowed; owned by this target.
     * @lifetime Valid until this target is destroyed or mutated.
     * @nullable Yes when the capability is unsupported.
     */
    void*                         queryCapability(const CapabilityId& capability) override;
    EditorResult<void>            applyDomainOperation(const DomainOperation& operation) override;
    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;

    /** @brief Immutable authored settings. */
    GpuAgentsSettings settings() const { return settings_; }
    /** @brief Structured validation diagnostics. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Capture schema-version-one settings. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load and validate a persisted snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    bool              matches(const SelectionSnapshot& selection) const;
    std::string       id_;
    GpuAgentsSettings settings_;
};

/**
 * @brief Optional bridge projecting a validated document into live World/Backend.
 * @ownership Borrowed world/backend; does not take ownership.
 */
class EVENGINE_API_ORCHESTRATION GpuAgentsRuntimeApplier {
public:
    /**
     * @brief Configure backend from document and optionally re-init the world surface domain.
     * @param target Authoring document.
     * @param backend Live backend to reconfigure.
     * @param world Optional world whose surface origin/size/resolution is updated for LifeNetwork.
     * @ownership Borrowed pointers.
     * @lifetime Valid for the duration of the call.
     */
    [[nodiscard("check runtime apply")]] EditorResult<void> apply(const GpuAgentsDocumentTarget& target,
                                                                  gpuagents::EffectBackend*      backend,
                                                                  gpuagents::GpuAgentWorld*      world) const;
};

}  // namespace eve::gpuagents_editing
