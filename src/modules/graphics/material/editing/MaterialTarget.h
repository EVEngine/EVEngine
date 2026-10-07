#pragma once
#include "common/Export.h"


#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"
#include "editing/EditableTarget.h"

#include <map>
#include <string>
#include <vector>

namespace eve::material_editing {

using editing::CapabilityId; using editing::DiagnosticSeverity; using editing::DomainOperation;
using editing::EditRegion; using editing::IDomainOperationTarget; using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget; using editing::IPropertyProvider; using editing::PropertyDescriptor;
using editing::PropertyFlag; using editing::PropertyPath; using editing::PropertyReadResult;
using editing::PropertyReadState; using editing::PropertySchema; using editing::PropertySetMode;
using editing::PropertyType; using editing::RuleId; using editing::SelectionSnapshot; using editing::TargetDescriptor;
using editing::TargetId;
template <class T> using EditorResult = editing::Result<T>;
using EditorStatus = editing::Status; using EditorValue = editing::Value; using EditorDiagnostic = editing::Diagnostic;

/** @brief UI-neutral, serializable material authoring target. */
class EVENGINE_API_BACKENDS MaterialDocumentTarget final : public ::eve::editing::EditableTargetState,
                                                           public virtual IEditableTarget,
                                                           public IDomainOperationTarget,
                                                           public IDomainOperationTargetStaging,
                                                           public eve::editing::IEditingSnapshotProvider,
                                                           public IPropertyProvider {
public:
    /** @brief Material document target. */
    explicit MaterialDocumentTarget(std::string id);

    /** @brief Target id. */
    TargetId         targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;

    /** @brief Current revision. */
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    /** @brief Schema. */
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    /** @brief Reads . */
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    /** @brief Make set. */
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    /** @brief Make reset. */
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

    /** @brief Capture deterministic content suitable for DocumentService persistence. */
    EditorValue snapshotValue() const override;
    /** @brief Replace content when opening a persisted material document. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Validate cross-field authoring rules without mutating the target. */
    std::vector<EditorDiagnostic> validate() const;

private:
    /** @brief Material schema. */
    static PropertySchema materialSchema();
    static std::map<std::string, EditorValue> defaults();
    /** @brief Validate assignment. */
    static EditorResult<void> validateAssignment(const PropertyDescriptor& descriptor,
                                                 const EditorValue& value);
    /** @brief Selection matches. */
    bool selectionMatches(const SelectionSnapshot& selection) const;

    std::string                        id_;
    std::map<std::string, EditorValue> values_;
};

/** @brief Atomic runtime publication boundary for a complete material candidate. */
class IMaterialRuntimeSink {
public:
    /** @brief Releases IMaterialRuntimeSink resources. */
    virtual ~IMaterialRuntimeSink() = default;
    /** @brief Publish a candidate; failure must leave the runtime material unchanged. */
    virtual EditorResult<void> publish(const MaterialDocumentTarget& candidate) = 0;
};

/** @brief Candidate-first material operation target with live commit/undo publication. */
class EVENGINE_API_BACKENDS MaterialPublishingTarget final : public IDomainOperationTarget,
                                                             public IDomainOperationTargetStaging {
public:
    /** @brief Create an owned material document bound to a non-owning runtime sink. */
    MaterialPublishingTarget(std::string id, IMaterialRuntimeSink* sink);
    /** @brief Target id. */
    TargetId targetId() const override { return TargetId(document_.targetId()); }
    /** @brief Revision. */
    std::uint64_t revision() const override { return document_.revision(); }
    /** @brief Dirty region. */
    EditRegion dirtyRegion() const override { return document_.dirtyRegion(); }
    /** @brief Clears dirty region. */
    void clearDirtyRegion() override { document_.clearDirtyRegion(); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;
    /**
     * @brief Forward property and snapshot capabilities from the authoring document.
     * @ownership Borrowed from this target; callers must not delete the returned capability.
     * @lifetime Valid until this target is destroyed or its document is replaced.
     */
    void* queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Mutable authoring document used by property inspectors. */
    MaterialDocumentTarget& authoringTarget() { return document_; }
    /** @brief Immutable authoring document used by validators and previews. */
    const MaterialDocumentTarget& authoringTarget() const { return document_; }
    /** @brief Atomically parse and publish a persisted material snapshot.
     * @param snapshot Detached persisted value borrowed for this call.
     * @return Applied only after validation and runtime publication both succeed; failure preserves authoring and
     * runtime state.
     * @remarks Render/editor thread only, synchronous, and may call the configured runtime sink without holding a
     * lock.
     */
    [[nodiscard]] EditorResult<void> reloadSnapshot(const EditorValue& snapshot);

private:
    MaterialDocumentTarget document_;
    IMaterialRuntimeSink* sink_ = nullptr;
    bool staging_ = false;
};

}  // namespace eve::material_editing

namespace eve::graphics {
class Renderable3D;
class Texture;
class Shader;
}

namespace eve::material_editing {

/** @brief Resolves authoring asset references before mutating a Renderable3D. */
class IMaterialRuntimeAssetResolver {
public:
    /** @brief Releases IMaterialRuntimeAssetResolver resources. */
    virtual ~IMaterialRuntimeAssetResolver() = default;
    /** @brief Resolve a texture asset reference to a borrowed live texture. */
    virtual EditorResult<graphics::Texture*> resolveTexture(const std::string& asset) const = 0;
    /** @brief Resolve a shader asset reference to a borrowed live shader. */
    virtual EditorResult<graphics::Shader*> resolveShader(const std::string& asset) const = 0;
};

/** @brief Built-in legacy-material publisher for one borrowed Renderable3D. */
class EVENGINE_API_WORLD Renderable3DMaterialRuntimeSink final : public IMaterialRuntimeSink {
public:
    /** @brief Bind a live renderable and asset resolver; both must outlive the sink. */
    Renderable3DMaterialRuntimeSink(graphics::Renderable3D* renderable,
                                    const IMaterialRuntimeAssetResolver* assets);
    /** @brief Renderable 3 d material runtime sink. */
    ~Renderable3DMaterialRuntimeSink() override;
    /** @brief Publish. */
    EditorResult<void> publish(const MaterialDocumentTarget& candidate) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::material_editing
