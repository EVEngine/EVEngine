#pragma once
#include <memory>
#include "editing/EditableTarget.h"
#include "editing/EditingTargetOperations.h"
namespace eve::building {
class PlacementWorld;
class PreparedAssembly;
}  // namespace eve::building
namespace eve::building_editing {
/**
 * @brief Whole-house transaction target sharing the canonical runtime world state.
 * @ownership Borrows a world; the host must destroy this target before the world.
 * @thread Owner/editor thread only; staging does not publish observer callbacks.
 * @lifetime Prepared operations own snapshots; they never retain the target/world.
 */
class BuildingAssemblyTarget final : public editing::EditableTargetState,
                                     public editing::IDomainOperationTarget,
                                     public editing::IDomainOperationTargetStaging {
public:
    /** @brief Bind a host-owned world that outlives this target. */
    BuildingAssemblyTarget(std::string id, building::PlacementWorld& world);
    ~BuildingAssemblyTarget() override;
    editing::TargetId         targetId() const override;
    editing::TargetDescriptor describe() const override;
    /**
     * @brief Consume a prepared house gesture into one reversible editor operation.
     * @cost Copies one candidate world per gesture; never invoke per preview frame.
     * @return Owning before/after operation; authoritative world remains unchanged.
     */
    [[nodiscard]] editing::Result<editing::DomainOperation> makeOperation(building::PreparedAssembly prepared) const;
    [[nodiscard]] editing::Result<void> applyDomainOperation(const editing::DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<editing::IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] editing::Result<void>                            commitDomainState(
                                   std::unique_ptr<editing::IDomainOperationTarget> candidate) override;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}  // namespace eve::building_editing
