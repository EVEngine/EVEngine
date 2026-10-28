#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::stylize {
struct MeshVfxAsset;
class MeshVfxAssetInstance;
}

namespace eve::stylize_editing {

using editing::CapabilityId;
using editing::DomainOperation;
using editing::EditRegion;
using editing::IDomainOperationTarget;
using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget;
using editing::IPropertyProvider;
using editing::PropertyPath;
using editing::PropertyReadResult;
using editing::PropertySchema;
using editing::PropertySetMode;
using editing::Revision;
using editing::SelectionSnapshot;
using editing::TargetDescriptor;
using editing::TargetId;
using EditorValue = editing::Value;
using editing::Result;

/**
 * @brief Transactional editor target for one canonical MeshVfxAsset document.
 * @ownership Owns the authoritative asset snapshot; returned references are borrowed until mutation.
 * @thread Editor-thread affine and not internally synchronized.
 * @reentrancy Does not invoke callbacks.
 */
class EVENGINE_API_DOMAINS MeshVfxAssetTarget final : public ::eve::editing::EditableTargetState,
                                                      public virtual IEditableTarget,
                                                      public IDomainOperationTarget,
                                                      public IDomainOperationTargetStaging,
                                                      public IPropertyProvider {
public:
    /** @brief Construct a target containing a valid one-layer default asset. */
    explicit MeshVfxAssetTarget(std::string id);
    /** @brief Mesh vfx asset target. */
    ~MeshVfxAssetTarget();
    /** @brief Mesh vfx asset target. */
    MeshVfxAssetTarget(const MeshVfxAssetTarget& other);
    /** @brief Operator =. */
    MeshVfxAssetTarget& operator=(const MeshVfxAssetTarget& other);

    /** @brief Target id. */
    TargetId                                targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;
    /** @brief Queries capability. */
    void* queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    Result<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    /** @brief Schema. */
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    /** @brief Reads . */
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    /** @brief Make set. */
    Result<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    /** @brief Make reset. */
    Result<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

    /** @brief Return the authoritative parsed asset. */
    [[nodiscard]] const stylize::MeshVfxAsset& asset() const noexcept;
    /** @brief Capture schema-version-one editor persistence data. */
    [[nodiscard]] EditorValue snapshotValue() const;
    /** @brief Atomically load a persisted target snapshot. */
    [[nodiscard]] Result<void> loadSnapshot(const EditorValue& snapshot);

private:
    /** @brief Matches. */
    bool matches(const SelectionSnapshot& selection) const;
    /** @brief Canonical json. */
    std::string canonicalJson() const;

    std::string                            id_;
    std::unique_ptr<stylize::MeshVfxAsset> asset_;
};

/**
 * @brief Candidate-first live preview generation for a MeshVfxAssetTarget.
 * A rejected publication preserves the previous instance and revision.
 */
class EVENGINE_API_DOMAINS MeshVfxPreviewRuntime {
public:
    /** @brief Mesh vfx preview runtime. */
    MeshVfxPreviewRuntime();
    /** @brief Mesh vfx preview runtime. */
    ~MeshVfxPreviewRuntime();
    /** @brief Build every runtime layer before atomically replacing the active preview. */
    [[nodiscard]] Result<void> publish(const MeshVfxAssetTarget& document);
    /** @brief Return the active preview instance, or null before publication. */
    [[nodiscard]] stylize::MeshVfxAssetInstance* instance() noexcept { return instance_.get(); }
    /** @brief Return the active document revision, or zero before publication. */
    [[nodiscard]] Revision revision() const noexcept { return revision_; }

private:
    std::unique_ptr<stylize::MeshVfxAssetInstance> instance_;
    Revision revision_ = 0;
};

}  // namespace eve::stylize_editing
