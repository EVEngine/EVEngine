#pragma once

/**
 * @file ContainerAdapters.h
 * @brief Inventory adapters for generic Bag and EquipmentSet containers.
 */

#include "common/Container.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"

namespace eve::inventory {

enum class InventoryContainerKind : std::uint8_t { Bag, Equipment };

/** @brief Owning transfer payload for one inventory stack. */
struct InventoryContainerObject final : eve::container::ContainerObjectPayload {
    ItemStack stack;
    std::string equipmentSlot;
};

/**
 * @brief Adapts one Bag or EquipmentSet without changing their legacy APIs.
 *
 * The Bag or EquipmentSet is borrowed and remains the sole owner of its slot
 * data. Callers must serialize access to the owner for the full synchronous
 * snapshot/prepare/commit operation; a prepared state must be committed or
 * rolled back before the owner is destroyed. Restore recreates the Bag or
 * EquipmentSet first, then binds a fresh adapter. The adapter never retains
 * temporary pointers in a snapshot and never deletes inventory items.
 */
class InventoryContainerAdapter final : public eve::container::IContainer {
public:
    /**
     * @brief Bind a borrowed bag.
     * @param id Stable container identity.
     * @param bag Borrowed authoritative bag storage.
     * @param capacity Optional capacity; an unlimited value uses bag slots.
     */
    InventoryContainerAdapter(eve::container::ContainerId id, Bag* bag,
                              eve::container::Capacity capacity = eve::container::Capacity::unlimited());
    /**
     * @brief Bind a borrowed equipment set.
     * @param id Stable container identity.
     * @param equipment Borrowed authoritative equipment-slot storage.
     */
    InventoryContainerAdapter(eve::container::ContainerId id, EquipmentSet* equipment);

    [[nodiscard]] const eve::container::ContainerDescriptor& descriptor() const noexcept override {
        return descriptor_;
    }
    [[nodiscard]] eve::Result<eve::container::ContainerSnapshot> snapshot() const override;
    [[nodiscard]] eve::Result<void> validateInsert(
        const eve::container::ContainerObject& object,
        std::optional<eve::container::SlotIndex> destination,
        std::optional<eve::container::MembershipId> ignoredObject = std::nullopt) const override;
    /** @copydoc eve::container::IContainer::prepare */
    [[nodiscard]] eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>> prepare(
        const eve::container::ContainerSnapshot& expected,
        const eve::container::ContainerSnapshot& candidate) override;

    /** @brief Return the adapter revision for optimistic transfer requests. */
    [[nodiscard]] eve::Revision revision() const noexcept { return revision_; }

private:
    class PreparedState;

    [[nodiscard]] static eve::container::MembershipId objectId(const ItemStack& stack);
    [[nodiscard]] static eve::container::ContainerObject describe(const ItemStack& stack,
                                                                    std::string equipmentSlot = {});
    /**
     * @brief Resolve one authoritative inventory slot for a synchronous operation.
     * @ownership Borrowed from the bound Bag or EquipmentSet; the adapter never deletes it.
     * @nullable Null for an invalid/unbound/out-of-range slot.
     * @lifetime Until the bound owner is destroyed or its slot storage is replaced;
     *            callers must not retain the pointer across mutations.
     * @thread Inventory-owner thread unless externally synchronized.
     * @reentrancy No callbacks are made while the pointer is held.
     */
    [[nodiscard]] const ItemStack* stackAt(eve::container::SlotIndex slot) const;
    /** @copydoc InventoryContainerAdapter::stackAt(eve::container::SlotIndex) */
    [[nodiscard]] ItemStack* stackAt(eve::container::SlotIndex slot);
    [[nodiscard]] std::string slotName(eve::container::SlotIndex slot) const;
    [[nodiscard]] eve::Result<void> validateObject(const InventoryContainerObject& object) const;

    eve::container::ContainerDescriptor descriptor_;
    InventoryContainerKind kind_;
    Bag* bag_ = nullptr;
    EquipmentSet* equipment_ = nullptr;
    eve::Revision revision_ = eve::Revision(0);
};

}  // namespace eve::inventory
