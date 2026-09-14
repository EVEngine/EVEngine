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
template <class T>
using EditorResult = editing::Result<T>;

/**
 * @brief Transactional editor target for one canonical MeshVfxAsset document.
 * @ownership Owns the authoritative asset snapshot; returned references are borrowed until mutation.
 * @thread Editor-thread affine and not internally synchronized.
 * @reentrancy Does not invoke callbacks.
 */
class MeshVfxAssetTarget final : public virtual IEditableTarget,
                                 public IDomainOperationTarget,
                                 public IDomainOperationTargetStaging,
                                 public IPropertyProvider {
public:
    /** @brief Construct a target containing a valid one-layer default asset. */
    explicit MeshVfxAssetTarget(std::string id);
    ~MeshVfxAssetTarget();
    MeshVfxAssetTarget(const MeshVfxAssetTarget& other);
    MeshVfxAssetTarget& operator=(const MeshVfxAssetTarget& other);

    TargetId targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
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

    /** @brief Return the authoritative parsed asset. */
    [[nodiscard]] const stylize::MeshVfxAsset& asset() const noexcept;
    /** @brief Capture schema-version-one editor persistence data. */
    [[nodiscard]] EditorValue snapshotValue() const;
    /** @brief Atomically load a persisted target snapshot. */
    [[nodiscard]] EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    bool matches(const SelectionSnapshot& selection) const;
    std::string canonicalJson() const;

    std::string id_;
    Revision revision_ = 1;
    EditRegion dirty_;
    std::unique_ptr<stylize::MeshVfxAsset> asset_;
};

/**
 * @brief Candidate-first live preview generation for a MeshVfxAssetTarget.
 * A rejected publication preserves the previous instance and revision.
 */
class MeshVfxPreviewRuntime {
public:
    MeshVfxPreviewRuntime();
    ~MeshVfxPreviewRuntime();
    /** @brief Build every runtime layer before atomically replacing the active preview. */
    [[nodiscard]] EditorResult<void> publish(const MeshVfxAssetTarget& document);
    /** @brief Return the active preview instance, or null before publication. */
    [[nodiscard]] stylize::MeshVfxAssetInstance* instance() noexcept { return instance_.get(); }
    /** @brief Return the active document revision, or zero before publication. */
    [[nodiscard]] Revision revision() const noexcept { return revision_; }

private:
    std::unique_ptr<stylize::MeshVfxAssetInstance> instance_;
    Revision revision_ = 0;
};

}  // namespace eve::stylize_editing
