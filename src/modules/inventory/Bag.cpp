#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"

#include <algorithm>

namespace eve::inventory {

Bag::Bag(int slotCount) {
    if (slotCount < 0) slotCount = 0;
    slots_.assign(size_t(slotCount), ItemStack{});
}

void Bag::destroy() { delete this; }

void Bag::setSlotCount(int slotCount) {
    if (slotCount < 0) slotCount = 0;
    slots_.resize(size_t(slotCount));
}

void Bag::clearAcceptTags() { acceptTags_.clear(); }

void Bag::addAcceptTag(const std::string &tag) {
    if (tag.empty()) return;
    if (std::find(acceptTags_.begin(), acceptTags_.end(), tag) == acceptTags_.end())
        acceptTags_.push_back(tag);
}

int Bag::getAcceptTagCount() const { return int(acceptTags_.size()); }

std::string Bag::getAcceptTag(int index) const {
    if (index < 0 || index >= int(acceptTags_.size())) return {};
    return acceptTags_[size_t(index)];
}

void Bag::clearRejectTags() { rejectTags_.clear(); }

void Bag::addRejectTag(const std::string &tag) {
    if (tag.empty()) return;
    if (std::find(rejectTags_.begin(), rejectTags_.end(), tag) == rejectTags_.end())
        rejectTags_.push_back(tag);
}

int Bag::getRejectTagCount() const { return int(rejectTags_.size()); }

std::string Bag::getRejectTag(int index) const {
    if (index < 0 || index >= int(rejectTags_.size())) return {};
    return rejectTags_[size_t(index)];
}

void Bag::setExtra(const std::string &key, const std::string &value) { extra_[key] = value; }

std::string Bag::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra_.find(key);
    return it == extra_.end() ? fallback : it->second;
}

bool Bag::canAddItem(const std::string &itemId, int quantity) {
    return InventorySystem::canAdd(this, itemId, quantity, nullptr);
}

std::string Bag::canAddItemReason(const std::string &itemId, int quantity) {
    std::string reason;
    InventorySystem::canAdd(this, itemId, quantity, &reason);
    return reason;
}

int Bag::addItem(const std::string &itemId, int quantity) {
    return InventorySystem::addItem(this, itemId, quantity);
}

int Bag::removeItem(const std::string &itemId, int quantity) {
    return InventorySystem::removeItem(this, itemId, quantity);
}

int Bag::removeAt(int slot, int quantity) { return InventorySystem::removeAt(this, slot, quantity); }

bool Bag::swapSlots(int slotA, int slotB) { return InventorySystem::swapSlots(this, slotA, slotB); }

bool Bag::moveSlot(int fromSlot, int toSlot) {
    return InventorySystem::moveSlot(this, fromSlot, toSlot);
}

bool Bag::splitStack(int slot, int quantity, int toSlot) {
    return InventorySystem::splitStack(this, slot, quantity, toSlot);
}

int Bag::countItem(const std::string &itemId) const {
    return InventorySystem::countItem(this, itemId);
}

int Bag::findItem(const std::string &itemId) const {
    return InventorySystem::findItem(this, itemId);
}

int Bag::findItemByTag(const std::string &tag) const {
    return InventorySystem::findItemByTag(this, tag);
}

float Bag::getUsedWeight() const { return InventorySystem::usedWeight(this); }

float Bag::getUsedVolume() const { return InventorySystem::usedVolume(this); }

int Bag::getUsedSlotCount() const { return InventorySystem::usedSlotCount(this); }

void Bag::clear() { InventorySystem::clearBag(this); }

bool Bag::isSlotEmpty(int slot) const {
    if (slot < 0 || slot >= getSlotCount()) return true;
    return slots_[size_t(slot)].empty();
}

std::string Bag::getSlotItemId(int slot) const {
    if (slot < 0 || slot >= getSlotCount()) return {};
    return slots_[size_t(slot)].itemId;
}

int Bag::getSlotQuantity(int slot) const {
    if (slot < 0 || slot >= getSlotCount()) return 0;
    return slots_[size_t(slot)].quantity;
}

int Bag::getSlotInstanceId(int slot) const {
    if (slot < 0 || slot >= getSlotCount()) return 0;
    return slots_[size_t(slot)].instanceId;
}

float Bag::getSlotDurability(int slot) const {
    if (slot < 0 || slot >= getSlotCount()) return -1.f;
    return slots_[size_t(slot)].durability;
}

void Bag::setSlotDurability(int slot, float durability) {
    if (slot < 0 || slot >= getSlotCount()) return;
    if (!slots_[size_t(slot)].empty()) slots_[size_t(slot)].durability = durability;
}

std::string Bag::getSlotProp(int slot, const std::string &key, const std::string &fallback) const {
    if (slot < 0 || slot >= getSlotCount()) return fallback;
    return slots_[size_t(slot)].getProp(key, fallback);
}

void Bag::setSlotProp(int slot, const std::string &key, const std::string &value) {
    if (slot < 0 || slot >= getSlotCount()) return;
    if (!slots_[size_t(slot)].empty()) slots_[size_t(slot)].setProp(key, value);
}

bool Bag::slotHasTag(int slot, const std::string &tag) const {
    if (slot < 0 || slot >= getSlotCount()) return false;
    return slots_[size_t(slot)].hasTag(tag);
}

void Bag::addSlotTag(int slot, const std::string &tag) {
    if (slot < 0 || slot >= getSlotCount() || tag.empty()) return;
    auto &s = slots_[size_t(slot)];
    if (s.empty()) return;
    if (!s.hasTag(tag)) s.tags.push_back(tag);
}

}  // namespace eve::inventory
