#pragma once

#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"
#include "graphics/VegetationField.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::graphics_editing {

/** @brief Owning editor document whose runtime projection is one VegetationField.
 * Element IDs remain stable while priority changes reorder the runtime projection.
 * The live field is borrowed and must outlive this main-thread-affine target. No callbacks are invoked.
 */
class VegetationFieldTarget final : public virtual editing::IEditableTarget,
                                    public editing::IDomainOperationTarget,
                                    public editing::IDomainOperationTargetStaging,
                                    public editing::IPropertyProvider {
public:
    /** @brief Import a complete runtime field into a new authoring target.
     * @return Owning target, or a structured failure without modifying the field.
     */
    [[nodiscard]] static editing::Result<std::unique_ptr<VegetationFieldTarget>> create(
        std::string id, graphics::VegetationField& field);

    editing::TargetId         targetId() const override { return editing::TargetId(id_); }
    std::uint64_t             revision() const override { return revision_; }
    editing::EditRegion       dirtyRegion() const override { return dirty_; }
    void                      clearDirtyRegion() override { dirty_.clear(); }
    editing::TargetDescriptor describe() const override;
    /** @brief Query the vegetation property capability.
     * @return Borrowed pointer owned by this target, or null for an unknown capability.
     * @lifetime Valid until this target is destroyed.
     */
    void*                               queryCapability(const editing::CapabilityId& capability) override;
    [[nodiscard]] editing::Result<void> applyDomainOperation(const editing::DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<editing::IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] editing::Result<void>                            commitDomainState(
        std::unique_ptr<editing::IDomainOperationTarget> candidate) override;
    [[nodiscard]] eve::Result<eve::Revision> currentRevision(
        const editing::SelectionSnapshot& selection) const override;
    editing::PropertySchema     schema(const editing::SelectionSnapshot& selection) const override;
    editing::PropertyReadResult read(const editing::SelectionSnapshot& selection,
                                     const editing::PropertyPath&      path) const override;
    [[nodiscard]] editing::Result<editing::DomainOperation> makeSet(const editing::SelectionSnapshot& selection,
                                                                    const editing::PropertyPath&      path,
                                                                    const editing::Value&             value,
                                                                    editing::PropertySetMode mode) const override;
    [[nodiscard]] editing::Result<editing::DomainOperation> makeReset(const editing::SelectionSnapshot& selection,
                                                                      const editing::PropertyPath& path) const override;

    /** @brief Build one atomic operation from a transform gizmo's final state.
     * @param center World-space center.
     * @param extents Positive local half-size.
     * @param yaw Radians around positive Y.
     */
    [[nodiscard]] editing::Result<editing::DomainOperation> makeTransform(const editing::SelectionSnapshot& selection,
                                                                          glm::vec3 center, glm::vec3 extents,
                                                                          float yaw) const;
    /** @brief Stable selection IDs in authoring order. */
    std::vector<editing::StableId> elementIds() const;

private:
    struct Entry {
        editing::StableId           id;
        graphics::VegetationElement value;
    };
    /** @brief Construct a document with an optional borrowed runtime projection.
     * @param field Borrowed field that must outlive this target, or null for a detached staging clone.
     */
    VegetationFieldTarget(std::string id, graphics::VegetationField* field, graphics::VegetationGlobals globals,
                          std::vector<Entry> entries, std::uint64_t fieldRevision);
    /** @brief Resolve one selected entry without retaining selection storage.
     * @return Borrowed entry owned by this target, or null.
     * @lifetime Valid until the target is mutated or destroyed.
     */
    const Entry* selected(const editing::SelectionSnapshot& selection) const;
    /** @brief Resolve an entry by stable identity.
     * @return Borrowed mutable entry owned by this target, or null.
     * @lifetime Valid until the target is mutated or destroyed.
     */
    Entry*                                                  selected(const std::string& id);
    [[nodiscard]] editing::Result<void>                     validateAndPublish(const std::vector<Entry>& entries);
    [[nodiscard]] editing::Result<editing::DomainOperation> replacement(const Entry& before, const Entry& after,
                                                                        std::string property) const;

    std::string                 id_;
    graphics::VegetationField*  field_ = nullptr;
    graphics::VegetationGlobals globals_;
    std::vector<Entry>          entries_;
    std::uint64_t               fieldRevision_ = 0;
    editing::Revision           revision_      = 1;
    editing::EditRegion         dirty_;
};

}  // namespace eve::graphics_editing
