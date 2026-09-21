#pragma once

#include "graphics/material/editing/MaterialTarget.h"

#include <span>

namespace eve::material_editing {

/** @brief Atomic publication boundary for a complete multi-material candidate. */
class IMaterialBatchRuntimeSink {
public:
    virtual ~IMaterialBatchRuntimeSink() = default;
    /** @brief Publish every candidate as one transaction.
     * @param candidates Borrowed immutable documents valid only for this synchronous call.
     * @return Applied only when all runtime materials were replaced; failure preserves every prior material.
     * @remarks Main/render thread only. Implementations must not invoke unknown callbacks while holding locks.
     */
    [[nodiscard]] virtual EditorResult<void> publish(std::span<const MaterialDocumentTarget> candidates) = 0;
};

/** @brief One editor transaction target for mixed-value, multi-material property editing.
 * Owns all authoring documents and publishes a complete candidate set through one atomic sink call.
 */
class EVENGINE_API_BACKENDS MaterialBatchTarget final : public ::eve::editing::EditableTargetState,
                                                        public virtual IEditableTarget,
                                                        public IDomainOperationTarget,
                                                        public IDomainOperationTargetStaging,
                                                        public IPropertyProvider,
                                                        public editing::IEditingSnapshotProvider {
public:
    /** @brief Construct a batch from uniquely identified owning documents.
     * @param sink Borrowed optional runtime sink that must outlive this target.
     */
    MaterialBatchTarget(std::string id, std::vector<MaterialDocumentTarget> materials,
                        IMaterialBatchRuntimeSink* sink = nullptr);

    TargetId         targetId() const override { return TargetId(id_); }
    TargetDescriptor describe() const override;
    /** @brief Query property or snapshot capabilities.
     * @return Borrowed pointer owned by this target, or null.
     * @lifetime Valid until this target is destroyed.
     */
    void*                            queryCapability(const CapabilityId& capability) override;
    [[nodiscard]] EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    [[nodiscard]] eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema                           schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    [[nodiscard]] EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                                        const EditorValue& value, PropertySetMode mode) const override;
    [[nodiscard]] EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                                          const PropertyPath&      path) const override;
    EditorValue                                 snapshotValue() const override;

    /** @brief Return owning material IDs in batch order. */
    std::vector<editing::StableId> materialIds() const;

private:
    [[nodiscard]] EditorResult<std::vector<std::size_t>> selectedIndices(const SelectionSnapshot& selection) const;
    [[nodiscard]] EditorResult<DomainOperation>          replacement(std::vector<MaterialDocumentTarget> candidates,
                                                                     const PropertyPath&                 path) const;
    [[nodiscard]] EditorResult<void> publishAndAdopt(std::vector<MaterialDocumentTarget> candidates,
                                                     editing::Revision candidateRevision,
                                                     const EditRegion& candidateDirty);

    std::string                         id_;
    std::vector<MaterialDocumentTarget> materials_;
    IMaterialBatchRuntimeSink*          sink_ = nullptr;
};

}  // namespace eve::material_editing
