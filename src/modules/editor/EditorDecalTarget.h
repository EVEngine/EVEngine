#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTarget.h"
#include "editor/EditorGizmoPreview.h"

#include <map>
#include <string>
#include <utility>

namespace eve::graphics { class Texture; }
namespace eve::decal { class DecalManager; }

namespace eve::editor {

/** @brief Stable, serializable authoring document for one projected decal. */
class DecalDocumentTarget final : public virtual IEditableTarget,
                                  public IDomainOperationTarget,
                                  public IDomainOperationTargetStaging,
                                  public IPropertyProvider {
public:
    explicit DecalDocumentTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    /** @brief Read a renderer-neutral property by stable path. @return Borrowed pointer into this target, or null. @lifetime Valid until the target is mutated or destroyed. */
    const EditorValue* value(const std::string& path) const;
    /** @brief Validate projection geometry, fades, UVs and texture-channel rules. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Capture schema-version-one decal content. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load and validate persisted decal content. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
private:
    bool matches(const SelectionSnapshot& selection) const;
    static PropertySchema decalSchema();
    static std::map<std::string,EditorValue> defaults();
    std::string id_; Revision revision_=1; EditRegion dirty_;
    std::map<std::string,EditorValue> values_;
};

/** @brief Resolves every decal texture before a live generation is replaced. */
class IDecalRuntimeAssetResolver {
public:
    virtual ~IDecalRuntimeAssetResolver() = default;
    /** @brief Resolve a borrowed texture, returning NotFound for missing assets. */
    virtual EditorResult<graphics::Texture*> texture(const std::string& asset) const = 0;
};

/** @brief Atomic publication boundary for a complete decal candidate. */
class IDecalRuntimeSink {
public:
    virtual ~IDecalRuntimeSink() = default;
    /** @brief Publish a candidate while preserving runtime state on rejection. */
    virtual EditorResult<void> publish(const DecalDocumentTarget& document) = 0;
};

/** @brief Candidate-first binding from one stable decal document to DecalManager. */
class DecalRuntimeBinding final : public IDecalRuntimeSink {
public:
    DecalRuntimeBinding(decal::DecalManager* manager, const IDecalRuntimeAssetResolver* assets)
        : manager_(manager), assets_(assets) {}
    /** @brief Publish a complete replacement; failure preserves the previous generation. */
    EditorResult<void> publish(const DecalDocumentTarget& document) override;
    /** @brief Remove the currently published generation, if any. */
    EditorResult<void> clear();
    /** @brief Runtime integer ID for diagnostics only. */
    int runtimeId() const { return runtimeId_; }
private:
    decal::DecalManager* manager_=nullptr; const IDecalRuntimeAssetResolver* assets_=nullptr;
    int runtimeId_=0;
};

/** @brief Candidate-first operation target synchronizing author state and a live decal. */
class DecalPublishingTarget final : public IDomainOperationTarget,
                                    public IDomainOperationTargetStaging {
public:
    DecalPublishingTarget(std::string id, IDecalRuntimeSink* sink)
        : document_(std::move(id)), sink_(sink) {}
    const std::string& targetId() const override { return document_.targetId(); }
    unsigned long long revision() const override { return document_.revision(); }
    EditRegion dirtyRegion() const override { return document_.dirtyRegion(); }
    void clearDirtyRegion() override { document_.clearDirtyRegion(); }
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Mutable document used by the Inspector to plan operations. */
    DecalDocumentTarget& authoringTarget() { return document_; }
    /** @brief Immutable document used by previews and persistence. */
    const DecalDocumentTarget& authoringTarget() const { return document_; }
private:
    DecalDocumentTarget document_; IDecalRuntimeSink* sink_=nullptr; bool staging_=false;
};

/** @brief Builds projection-box and normal overlays for decal placement tools. */
class DecalGizmoPreviewService {
public:
    /** @brief Create a revision-bound projection volume and direction arrow. */
    EditorGizmoSnapshot build(const DecalDocumentTarget& document) const;
};

}  // namespace eve::editor
