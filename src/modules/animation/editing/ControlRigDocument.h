#pragma once
#include "common/Export.h"
#include "editing/EditingTargetOperations.h"

namespace eve::animation {
struct RigDefinition;
}
namespace eve::editing {
struct GraphDocumentData;
}
namespace eve::animation_editing {
/** @brief Transactional rig document. Owns typed state; owner thread only; no callbacks. */
class EVENGINE_API_DOMAINS ControlRigDocument final : public editing::EditableTargetState,
                                                      public editing::IDomainOperationTarget,
                                                      public editing::IDomainOperationTargetStaging {
public:
    /** @brief Create an empty rig document with a stable target identity. */
    explicit ControlRigDocument(std::string id);
    /** @brief Destroy owned state. */
    ~ControlRigDocument();
    ControlRigDocument(const ControlRigDocument&)            = delete;
    ControlRigDocument& operator=(const ControlRigDocument&) = delete;
    /** @brief Return stable target identity by value. */
    editing::TargetId targetId() const override;
    /** @brief Describe this document for editor discovery. */
    editing::TargetDescriptor describe() const override;
    /** @brief Copy the authoritative rig definition; callers never mutate target state directly. */
    animation::RigDefinition definition() const;
    /** @brief Project solve nodes and dependency pins into the existing generic graph model. */
    [[nodiscard]] Result<editing::GraphDocumentData> graph() const;
    /** @brief Prepare a graph edit against its captured document revision; stale graphs are rejected. */
    [[nodiscard]] Result<editing::DomainOperation> replaceGraph(const editing::GraphDocumentData& graph) const;
    /** @brief Prepare a reversible replacement; validates before producing the operation.
     * @cost Document-sized encoding, amortized per committed user edit.
     */
    [[nodiscard]] Result<editing::DomainOperation> replace(const animation::RigDefinition& candidate) const;
    /** @brief Apply validated candidate atomically, checking target identity. */
    [[nodiscard]] Result<void> applyDomainOperation(const editing::DomainOperation& operation) override;
    /** @brief Produce detached state for existing editing transaction machinery. */
    [[nodiscard]] std::unique_ptr<editing::IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Publish a detached candidate for this target. */
    [[nodiscard]] Result<void> commitDomainState(std::unique_ptr<editing::IDomainOperationTarget> candidate) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::animation_editing
