#include "inventory/Equipment.h"
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"

#include <algorithm>

namespace eve::inventory {

void EquipmentSet::destroy() { delete this; }

void EquipmentSet::defineSlot(const std::string &slotName) {
    if (slotName.empty()) return;
    if (slots_.find(slotName) == slots_.end()) {
        slots_.emplace(slotName, Slot{});
        order_.push_back(slotName);
    }
}

void EquipmentSet::clearSlotAllowedTags(const std::string &slotName) {
    auto it = slots_.find(slotName);
    if (it == slots_.end()) return;
    it->second.allowedTags.clear();
}

void EquipmentSet::addSlotAllowedTag(const std::string &slotName, const std::string &tag) {
    defineSlot(slotName);
    if (tag.empty()) return;
    auto &tags = slots_[slotName].allowedTags;
    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) tags.push_back(tag);
}

bool EquipmentSet::hasSlot(const std::string &slotName) const {
    return slots_.find(slotName) != slots_.end();
}

int EquipmentSet::getSlotCount() const { return int(order_.size()); }

std::string EquipmentSet::getSlotName(int index) const {
    if (index < 0 || index >= int(order_.size())) return {};
    return order_[size_t(index)];
}

bool EquipmentSet::isSlotEmpty(const std::string &slotName) const {
    auto it = slots_.find(slotName);
    if (it == slots_.end()) return true;
    return it->second.stack.empty();
}

std::string EquipmentSet::getSlotItemId(const std::string &slotName) const {
    auto it = slots_.find(slotName);
    if (it == slots_.end()) return {};
    return it->second.stack.itemId;
}

int EquipmentSet::getSlotQuantity(const std::string &slotName) const {
    auto it = slots_.find(slotName);
    if (it == slots_.end()) return 0;
    return it->second.stack.quantity;
}

int EquipmentSet::getSlotInstanceId(const std::string &slotName) const {
    auto it = slots_.find(slotName);
    if (it == slots_.end()) return 0;
    return it->second.stack.instanceId;
}

const ItemStack *EquipmentSet::stackAt(const std::string &slotName) const {
    auto it = slots_.find(slotName);
    return it == slots_.end() ? nullptr : &it->second.stack;
}

ItemStack *EquipmentSet::stackAt(const std::string &slotName) {
    auto it = slots_.find(slotName);
    return it == slots_.end() ? nullptr : &it->second.stack;
}

const std::vector<std::string> *EquipmentSet::allowedTags(const std::string &slotName) const {
    auto it = slots_.find(slotName);
    return it == slots_.end() ? nullptr : &it->second.allowedTags;
}

bool EquipmentSet::canEquipFromBag(Bag *bag, int bagSlot, const std::string &equipSlot,
                                    std::string *reason) const {
    if (!bag || equipSlot.empty()) {
        if (reason) *reason = "invalid_args";
        return false;
    }
    auto it = slots_.find(equipSlot);
    if (it == slots_.end()) {
        if (reason) *reason = "unknown_slot";
        return false;
    }
    if (bagSlot < 0 || bagSlot >= bag->getSlotCount()) {
        if (reason) *reason = "bad_bag_slot";
        return false;
    }
    const auto &bs = bag->slots()[size_t(bagSlot)];
    if (bs.empty()) {
        if (reason) *reason = "empty_bag_slot";
        return false;
    }
    const ItemDefinition *def = ItemRegistry::find(bs.itemId);
    if (!def) {
        if (reason) *reason = "unknown_item";
        return false;
    }
    if (!def->equipSlot.empty() && def->equipSlot != equipSlot) {
        if (reason) *reason = "equip_slot_mismatch";
        return false;
    }
    const auto &allowed = it->second.allowedTags;
    if (!allowed.empty()) {
        bool ok = false;
        for (const auto &t : allowed) {
            if (def->hasTag(t) || bs.hasTag(t)) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            if (reason) *reason = "tag_mismatch";
            return false;
        }
    }
    // If current slot occupied, bagSlot will receive the old item (swap); bagSlot becomes empty
    // after take, so always OK capacity-wise for swap. If we'd need an extra empty slot we don't.
    return true;
}

bool EquipmentSet::equipFromBag(const std::string &equipSlot, Bag *bag, int bagSlot) {
    return InventorySystem::equipFromBag(this, equipSlot, bag, bagSlot);
}

bool EquipmentSet::unequipToBag(const std::string &equipSlot, Bag *bag) {
    return InventorySystem::unequipToBag(this, equipSlot, bag);
}

bool EquipmentSet::clearSlot(const std::string &slotName) {
    auto it = slots_.find(slotName);
    if (it == slots_.end() || it->second.stack.empty()) return false;
    std::string itemId = it->second.stack.itemId;
    int qty = it->second.stack.quantity;
    it->second.stack.clear();
    InventoryChangeEvent ev;
    ev.action = "unequip";
    ev.itemId = itemId;
    ev.quantity = qty;
    ev.equipSlot = slotName;
    InventorySystem::pushEvent(std::move(ev));
    return true;
}

}  // namespace eve::inventory
