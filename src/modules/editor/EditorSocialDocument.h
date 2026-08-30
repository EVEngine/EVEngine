#pragma once

#include "editor/EditorAuthority.h"
#include "editing/EditableTarget.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::social { class SocialGraph; }

namespace eve::editor {

/** @brief Stable social-graph entity metadata for graph presenters. */
struct SocialEntityRecord { StableId id; std::string label; std::string category; };

/** @brief Directed ownership, control, assignment or weighted relation edge. */
struct SocialEdgeRecord {
    StableId id;
    StableId source;
    StableId target;
    std::string kind = "relation";
    std::string type;
    double weight = 1.0;
};

/** @brief UI-neutral revisioned social graph authoring document. */
class SocialDocumentTarget final : public virtual IEditableTarget,
                                   public IDomainOperationTarget,
                                   public IDomainOperationTargetStaging {
public:
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.social-graph"); }
    explicit SocialDocumentTarget(std::string id);
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
    /** @brief Enumerate entities in stable order. */
    std::vector<SocialEntityRecord> entities() const;
    /** @brief Enumerate edges in stable order. */
    std::vector<SocialEdgeRecord> edges() const;
    /** @brief Plan reversible entity creation or replacement. */
    EditorResult<DomainOperation> makeSetEntity(const SocialEntityRecord& entity) const;
    /** @brief Plan deletion of an entity and reject it while edges reference it. */
    EditorResult<DomainOperation> makeDeleteEntity(const StableId& id) const;
    /** @brief Plan reversible edge creation or replacement. */
    EditorResult<DomainOperation> makeSetEdge(const SocialEdgeRecord& edge) const;
    /** @brief Plan reversible edge deletion. */
    EditorResult<DomainOperation> makeDeleteEdge(const StableId& id) const;
    /** @brief Validate references, uniqueness constraints and finite weights. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Capture deterministic schema-version-one graph content. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a validated graph snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    std::string id_; Revision revision_ = 1; EditRegion dirty_;
    std::map<StableId, SocialEntityRecord> entities_;
    std::map<StableId, SocialEdgeRecord> edges_;
};

/** @brief Publishes a complete validated social document to a runtime SocialGraph. */
class SocialRuntimeApplier {
public:
    EditorResult<void> apply(const SocialDocumentTarget& document, social::SocialGraph* runtime) const;
};

}  // namespace eve::editor
