#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "common/Export.h"
#include "common/Uri.h"
#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"

namespace eve::procgen {
class SpatialData;
class BiomeRules;
class PointSet;
}  // namespace eve::procgen
namespace eve::biome_editing {
using CapabilityId       = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation    = editing::DomainOperation;
using EditRegion         = editing::EditRegion;
using EditorDiagnostic   = editing::Diagnostic;
using editing::Result;
using EditorStatus                  = editing::Status;
using EditorValue                   = editing::Value;
using IDomainOperationTarget        = editing::IDomainOperationTarget;
using IDomainOperationTargetStaging = editing::IDomainOperationTargetStaging;
using IEditableTarget               = editing::IEditableTarget;
using IEditingSnapshotProvider      = editing::IEditingSnapshotProvider;
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
/** @brief Fixed domain slots; each URI names a scheme owned by that domain. */
enum class BiomeSchemeKind { Monsters, Animals, Plants, Surface, Weather, Fog };
/** @brief Canonical codec/property names in BiomeSchemeKind order. */
inline constexpr std::array<std::string_view, 6> biomeSchemeNames{"monsters", "animals", "plants",
                                                                  "surface",  "weather", "fog"};
/** @brief Owning references, not copies of mutable domain configurations; null means unassigned. */
using BiomeSchemes = std::array<std::optional<eve::ResourceUri>, 6>;

struct BiomePublication;
/** @brief Owning region configuration at one published biome revision. */
struct BiomeRegionConfiguration {
    ObjectId         region;
    TargetId         biome;
    eve::ResourceUri spatial;
    std::uint32_t    seed;
    Revision         revision;
    BiomeSchemes     schemes;
};
/**
 * @brief Region-owned weak binding to one biome runtime, automatically following successful publications.
 * @ownership Owns only region identity, spatial URI and seed; never keeps the biome runtime alive.
 * @lifetime Runtime destruction invalidates the binding; recreating the runtime requires explicit restoration.
 * @thread Owner thread only, including destruction; no callbacks or ECS mutations.
 */
class EVENGINE_API_ORCHESTRATION BiomeRegionBinding {
public:
    /** @brief Return an owning current configuration, or NotFound after runtime destruction.
     * @cost Copies six optional URI values per explicit inspection, not a per-frame lookup.
     */
    [[nodiscard]] Result<BiomeRegionConfiguration> configuration() const;
    /** @brief Save persistent binding data only; no runtime handle or duplicated scheme configuration.
     * @cost Copies binding identity and URI once per save; fails if the source runtime has expired.
     */
    [[nodiscard]] Result<EditorValue> snapshotValue() const;

private:
    friend class BiomeDocumentRuntime;
    BiomeRegionBinding(ObjectId, eve::ResourceUri, std::uint32_t, std::weak_ptr<BiomePublication>);
    ObjectId                        region_;
    eve::ResourceUri                spatial_;
    std::uint32_t                   seed_;
    std::weak_ptr<BiomePublication> publication_;
};
/** @brief Stable weighted asset entry in one biome layer. */ struct BiomeAssetValue {
    ObjectId    id;
    std::string asset;
    float       weight = 1, minScale = 1, maxScale = 1;
    bool        randomYaw = true;
};
/** @brief Stable authored biome layer referencing a spatial asset. */ struct BiomeLayerValue {
    ObjectId                     id;
    std::string                  name, spatialAsset;
    int                          priority = 0;
    float                        density  = 1;
    std::vector<BiomeAssetValue> assets;
};
/** @brief Revisioned BiomeRules asset. */
class EVENGINE_API_ORCHESTRATION BiomeDocumentTarget final : public ::eve::editing::EditableTargetState,
                                                             public virtual IEditableTarget,
                                                             public IDomainOperationTarget,
                                                             public IDomainOperationTargetStaging,
                                                             public IPropertyProvider,
                                                             public IEditingSnapshotProvider {
public:
    /** @brief Biome document target. */
    explicit BiomeDocumentTarget(std::string id);
    /** @brief Capability identity published by describe() for Inspector property editing. */
    static CapabilityId                     propertyCapabilityId() {
        /** @brief Capability id. */
        return CapabilityId("eve.editor.target.biome-properties");
    }
    /** @brief Target id. */
    TargetId                                targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor                        describe() const override;
    /**
     * @brief Query Inspector property and snapshot capabilities.
     * @ownership Borrowed from this target; callers must not delete the pointer.
     * @lifetime Valid until this target is destroyed or replaced by commitDomainState.
     * @thread Owner-thread only.
     */
    void*                                   queryCapability(const CapabilityId&) override;
    /** @brief Applies domain operation. */
    Result<void>                      applyDomainOperation(const DomainOperation&) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    Result<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    /** @brief Schema. */
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    /** @brief Reads . */
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Make set. */
    Result<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    /** @brief Make reset. */
    Result<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Make create layer. */
    Result<DomainOperation>           makeCreateLayer(const BiomeLayerValue&) const;
    /** @brief Make delete layer. */
    Result<DomainOperation>           makeDeleteLayer(const ObjectId&) const;
    /** @brief Make create asset. */
    Result<DomainOperation>           makeCreateAsset(const ObjectId& layer, const BiomeAssetValue&) const;
    /** @brief Make delete asset. */
    Result<DomainOperation>           makeDeleteAsset(const ObjectId&) const;
    /** @brief Make set exclusions. */
    Result<DomainOperation>           makeSetExclusions(std::vector<std::string>) const;
    /** @brief Plan a reversible scheme association; empty URI clears the slot.
     * @thread Owner thread; input copied, no callbacks or resource loading.
     * @cost Copies the document once per authored edit, amortized by the editing transaction.
     */
    [[nodiscard]] Result<DomainOperation> makeSetScheme(BiomeSchemeKind, std::string_view uri) const;
    /** @brief Borrow scheme references until document mutation/destruction; owner thread only. */
    const BiomeSchemes& schemes() const noexcept { return schemes_; }
    /** @brief Layers. */
    const std::vector<BiomeLayerValue>&     layers() const { return layers_; }
    /** @brief Exclusions. */
    const std::vector<std::string>&         exclusions() const { return exclusions_; }
    /** @brief Validate. */
    std::vector<EditorDiagnostic>           validate() const;
    /** @brief Snapshot value. */
    EditorValue                             snapshotValue() const override;
    /** @brief Loads snapshot. */
    Result<void>                      loadSnapshot(const EditorValue&);

private:
    /** @brief Matches. */
    bool                          matches(const SelectionSnapshot&) const;
    /** @brief Content value. */
    EditorValue                   contentValue() const;
    /** @brief Replacement. */
    Result<DomainOperation> replacement(EditorValue, std::string = {}) const;
    std::string                   id_;
    std::vector<BiomeLayerValue>  layers_;
    std::vector<std::string>      exclusions_;
    BiomeSchemes                  schemes_;
};
/** @brief Resolves copied spatial domains for BiomeRules publication. */
class IBiomeSpatialResolver {
public:
    /** @brief Releases IBiomeSpatialResolver resources. */
    virtual ~IBiomeSpatialResolver() = default;
    /**
     * @brief Resolve one authored spatial asset to a generation domain.
     * @ownership Borrowed; the resolver retains the SpatialData.
     * @lifetime Valid for the duration of publish(); must not be retained afterward.
     * @thread Owner-thread only.
     */
    virtual Result<procgen::SpatialData*> resolve(const std::string&) const = 0;
};
/** @brief Candidate-first BiomeRules generation. */
class EVENGINE_API_ORCHESTRATION BiomeDocumentRuntime {
public:
    /** @brief Biome document runtime. */
    BiomeDocumentRuntime();
    /** @brief Biome document runtime. */
    ~BiomeDocumentRuntime();
    BiomeDocumentRuntime(const BiomeDocumentRuntime&)            = delete;
    BiomeDocumentRuntime& operator=(const BiomeDocumentRuntime&) = delete;
    /** @brief Bind an independently owned region to the published biome without copying its schemes.
     * @thread Owner thread; no callbacks. Binding destruction has no effect on the runtime.
     * @return Owning weak binding, or Rejected/NotFound for invalid identity/URI or unpublished runtime.
     */
    [[nodiscard]] Result<BiomeRegionBinding> bindRegion(ObjectId region, std::string_view spatial,
                                                        std::uint32_t seed) const;
    /** @brief Restore a saved binding against the same logical biome; unknown fields/versions are rejected.
     * @thread Owner thread, no callbacks; input borrowed during call, failure changes nothing.
     * @cost Parses bounded binding metadata once per region load.
     */
    [[nodiscard]] Result<BiomeRegionBinding> restoreRegion(const EditorValue&) const;
    /** @brief Generate using the region's domain, seed and current published rules.
     * @ownership Resolver and binding borrowed only during call; output belongs to caller.
     * @thread Owner thread; resolver must not reenter/mutate this runtime.
     * @cost Spatial sampling and asset selection per explicit generation, amortized per region rebuild.
     */
    [[nodiscard]] Result<std::unique_ptr<procgen::PointSet>> previewRegion(const BiomeRegionBinding&,
                                                                           const IBiomeSpatialResolver&, float spacing,
                                                                           float jitter);
    /** @brief Publish rules and scheme references together; failure preserves all bound regions' previous state.
     * @ownership Document and resolver borrowed during this call; only owning projections are retained.
     * @thread Owner thread; resolver must not reenter or mutate the document/runtime.
     * @cost Copies and resolves all authored layers once per explicit document publication.
     */
    [[nodiscard]] Result<void> publish(const BiomeDocumentTarget&, const IBiomeSpatialResolver&);
    /**
     * @brief Generate a PointSet from a previously published candidate.
     * @param domain Borrowed spatial query domain; must outlive this call.
     * @ownership Success transfers the PointSet to the caller.
     * @thread Owner-thread only.
     */
    Result<std::unique_ptr<procgen::PointSet>> preview(procgen::SpatialData* domain, float spacing,
                                                             std::uint32_t seed, float jitter,
                                                             Revision expectedRevision);
    /**
     * @brief Published BiomeRules, or null before the first successful publish.
     * @ownership Borrowed from this runtime; callers must not delete it.
     * @lifetime Valid until the next publish() or this runtime is destroyed.
     * @thread Owner-thread only.
     */
    procgen::BiomeRules* rules() const { return rules_.get(); }
    /** @brief Revision. */
    Revision                                         revision() const { return revision_; }

private:
    std::unique_ptr<procgen::BiomeRules> rules_;
    Revision                             revision_ = 0;
    std::shared_ptr<BiomePublication>    publication_;
};
}  // namespace eve::biome_editing
