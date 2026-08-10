#pragma once

// 运行时容器：固定格数的背包 / 箱子 / 商店栏等。
// 行为由 InventorySystem 提供；本类暴露便于脚本绑定的薄封装方法。

#include "inventory/ItemTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::inventory {

class Bag {
public:
    Bag(int slotCount);
    ~Bag() = default;

    Bag(const Bag &) = delete;
    Bag &operator=(const Bag &) = delete;

    void destroy();

    std::string getId() const { return id_; }
    void setId(const std::string &id) { id_ = id; }

    std::string getKind() const { return kind_; }
    void setKind(const std::string &kind) { kind_ = kind; }

    int getSlotCount() const { return int(slots_.size()); }
    /** 调整格数：扩容追加空槽；缩容会丢弃被裁掉的槽（不自动转移）。 */
    void setSlotCount(int slotCount);

    float getMaxWeight() const { return maxWeight_; }
    void setMaxWeight(float w) { maxWeight_ = w; }
    float getMaxVolume() const { return maxVolume_; }
    void setMaxVolume(float v) { maxVolume_ = v; }

    std::string getAcceptRule() const { return acceptRule_; }
    void setAcceptRule(const std::string &name) { acceptRule_ = name; }
    std::string getCapacityPolicy() const { return capacityPolicy_; }
    void setCapacityPolicy(const std::string &name) { capacityPolicy_ = name; }
    std::string getStackRule() const { return stackRule_; }
    void setStackRule(const std::string &name) { stackRule_ = name; }

    void clearAcceptTags();
    void addAcceptTag(const std::string &tag);
    int getAcceptTagCount() const;
    std::string getAcceptTag(int index) const;

    void clearRejectTags();
    void addRejectTag(const std::string &tag);
    int getRejectTagCount() const;
    std::string getRejectTag(int index) const;

    void setExtra(const std::string &key, const std::string &value);
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;

    // ---- 便捷操作（转发 InventorySystem）----
    bool canAddItem(const std::string &itemId, int quantity);
    std::string canAddItemReason(const std::string &itemId, int quantity);
    int addItem(const std::string &itemId, int quantity);
    int removeItem(const std::string &itemId, int quantity);
    int removeAt(int slot, int quantity);
    bool swapSlots(int slotA, int slotB);
    bool moveSlot(int fromSlot, int toSlot);
    bool splitStack(int slot, int quantity, int toSlot);
    int countItem(const std::string &itemId) const;
    int findItem(const std::string &itemId) const;
    int findItemByTag(const std::string &tag) const;
    float getUsedWeight() const;
    float getUsedVolume() const;
    int getUsedSlotCount() const;
    void clear();

    // ---- 槽位查询 ----
    bool isSlotEmpty(int slot) const;
    std::string getSlotItemId(int slot) const;
    int getSlotQuantity(int slot) const;
    int getSlotInstanceId(int slot) const;
    float getSlotDurability(int slot) const;
    void setSlotDurability(int slot, float durability);
    std::string getSlotProp(int slot, const std::string &key, const std::string &fallback = {}) const;
    void setSlotProp(int slot, const std::string &key, const std::string &value);
    bool slotHasTag(int slot, const std::string &tag) const;
    void addSlotTag(int slot, const std::string &tag);

    // ---- 供 System 直接访问 ----
    const std::vector<ItemStack> &slots() const { return slots_; }
    std::vector<ItemStack> &slots() { return slots_; }
    const std::vector<std::string> &acceptTags() const { return acceptTags_; }
    const std::vector<std::string> &rejectTags() const { return rejectTags_; }

private:
    friend class InventorySystem;

    std::string id_;
    std::string kind_ = "backpack";
    float maxWeight_ = 0.f;   ///< <=0 表示该维度不限制（仍受 capacityPolicy 控制）
    float maxVolume_ = 0.f;
    std::string acceptRule_ = "default";
    std::string capacityPolicy_ = "slotsAndWeight";
    std::string stackRule_ = "sameItem";
    std::vector<std::string> acceptTags_;
    std::vector<std::string> rejectTags_;
    std::vector<ItemStack> slots_;
    std::unordered_map<std::string, std::string> extra_;
};

}  // namespace eve::inventory
