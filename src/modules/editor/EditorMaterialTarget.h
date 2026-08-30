#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editing/EditableTarget.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief UI-neutral, serializable material authoring target. */
class MaterialDocumentTarget final : public virtual IEditableTarget,
                                     public IDomainOperationTarget,
                                     public IDomainOperationTargetStaging,
                                     public eve::editing::IEditingSnapshotProvider,
                                     public IPropertyProvider {
public:
    explicit MaterialDocumentTarget(std::string id);

    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;

    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

    /** @brief Capture deterministic content suitable for DocumentService persistence. */
    EditorValue snapshotValue() const override;
    /** @brief Replace content when opening a persisted material document. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Validate cross-field authoring rules without mutating the target. */
    std::vector<EditorDiagnostic> validate() const;

private:
    static PropertySchema materialSchema();
    static std::map<std::string, EditorValue> defaults();
    static EditorResult<void> validateAssignment(const PropertyDescriptor& descriptor,
                                                 const EditorValue& value);
    bool selectionMatches(const SelectionSnapshot& selection) const;

    std::string id_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
    std::map<std::string, EditorValue> values_;
};

/** @brief Atomic runtime publication boundary for a complete material candidate. */
class IMaterialRuntimeSink {
public:
    virtual ~IMaterialRuntimeSink() = default;
    /** @brief Publish a candidate; failure must leave the runtime material unchanged. */
    virtual EditorResult<void> publish(const MaterialDocumentTarget& candidate) = 0;
};

/** @brief Candidate-first material operation target with live commit/undo publication. */
class MaterialPublishingTarget final : public IDomainOperationTarget,
                                       public IDomainOperationTargetStaging {
public:
    /** @brief Create an owned material document bound to a non-owning runtime sink. */
    MaterialPublishingTarget(std::string id, IMaterialRuntimeSink* sink);
    const std::string& targetId() const override { return document_.targetId(); }
    unsigned long long revision() const override { return document_.revision(); }
    EditRegion dirtyRegion() const override { return document_.dirtyRegion(); }
    void clearDirtyRegion() override { document_.clearDirtyRegion(); }
    TargetDescriptor describe() const override;
    /**
     * @brief Forward property and snapshot capabilities from the authoring document.
     * @ownership Borrowed from this target; callers must not delete the returned capability.
     * @lifetime Valid until this target is destroyed or its document is replaced.
     */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Mutable authoring document used by property inspectors. */
    MaterialDocumentTarget& authoringTarget() { return document_; }
    /** @brief Immutable authoring document used by validators and previews. */
    const MaterialDocumentTarget& authoringTarget() const { return document_; }

private:
    MaterialDocumentTarget document_;
    IMaterialRuntimeSink* sink_ = nullptr;
    bool staging_ = false;
};

}  // namespace eve::editor

namespace eve::graphics {
class Renderable3D;
class Texture;
class Shader;
}

namespace eve::editor {

/** @brief Resolves authoring asset references before mutating a Renderable3D. */
class IMaterialRuntimeAssetResolver {
public:
    virtual ~IMaterialRuntimeAssetResolver() = default;
    /** @brief Resolve a texture asset reference to a borrowed live texture. */
    virtual EditorResult<graphics::Texture*> resolveTexture(const std::string& asset) const = 0;
    /** @brief Resolve a shader asset reference to a borrowed live shader. */
    virtual EditorResult<graphics::Shader*> resolveShader(const std::string& asset) const = 0;
};

/** @brief Built-in legacy-material publisher for one borrowed Renderable3D. */
class Renderable3DMaterialRuntimeSink final : public IMaterialRuntimeSink {
public:
    /** @brief Bind a live renderable and asset resolver; both must outlive the sink. */
    Renderable3DMaterialRuntimeSink(graphics::Renderable3D* renderable,
                                    const IMaterialRuntimeAssetResolver* assets);
    ~Renderable3DMaterialRuntimeSink() override;
    EditorResult<void> publish(const MaterialDocumentTarget& candidate) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::editor
