#pragma once

#include <memory>
#include <string>
#include <vector>
#include "common/Export.h"
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
template <class T>
using EditorResult                  = editing::Result<T>;
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
    /** @brief Make create layer. */
    EditorResult<DomainOperation>           makeCreateLayer(const BiomeLayerValue&) const;
    /** @brief Make delete layer. */
    EditorResult<DomainOperation>           makeDeleteLayer(const ObjectId&) const;
    /** @brief Make create asset. */
    EditorResult<DomainOperation>           makeCreateAsset(const ObjectId& layer, const BiomeAssetValue&) const;
    /** @brief Make delete asset. */
    EditorResult<DomainOperation>           makeDeleteAsset(const ObjectId&) const;
    /** @brief Make set exclusions. */
    EditorResult<DomainOperation>           makeSetExclusions(std::vector<std::string>) const;
    /** @brief Layers. */
    const std::vector<BiomeLayerValue>&     layers() const { return layers_; }
    /** @brief Exclusions. */
    const std::vector<std::string>&         exclusions() const { return exclusions_; }
    /** @brief Validate. */
    std::vector<EditorDiagnostic>           validate() const;
    /** @brief Snapshot value. */
    EditorValue                             snapshotValue() const override;
    /** @brief Loads snapshot. */
    EditorResult<void>                      loadSnapshot(const EditorValue&);

private:
    /** @brief Matches. */
    bool                          matches(const SelectionSnapshot&) const;
    /** @brief Content value. */
    EditorValue                   contentValue() const;
    /** @brief Replacement. */
    EditorResult<DomainOperation> replacement(EditorValue, std::string = {}) const;
    std::string                   id_;
    std::vector<BiomeLayerValue>  layers_;
    std::vector<std::string>      exclusions_;
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
    virtual EditorResult<procgen::SpatialData*> resolve(const std::string&) const = 0;
};
/** @brief Candidate-first BiomeRules generation. */
class EVENGINE_API_ORCHESTRATION BiomeDocumentRuntime {
public:
    /** @brief Biome document runtime. */
    BiomeDocumentRuntime();
    /** @brief Biome document runtime. */
    ~BiomeDocumentRuntime();
    /** @brief Publish. */
    EditorResult<void> publish(const BiomeDocumentTarget&, const IBiomeSpatialResolver&);
    /**
     * @brief Generate a PointSet from a previously published candidate.
     * @param domain Borrowed spatial query domain; must outlive this call.
     * @ownership Success transfers the PointSet to the caller.
     * @thread Owner-thread only.
     */
    EditorResult<std::unique_ptr<procgen::PointSet>> preview(procgen::SpatialData* domain, float spacing,
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
};
}  // namespace eve::biome_editing
