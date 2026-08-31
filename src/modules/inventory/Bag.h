#pragma once

/**
 * @brief 运行时容器：固定格数的背包 / 箱子 / 商店栏等。
 * 行为由 InventorySystem 提供；本类暴露便于脚本绑定的薄封装方法。
 */

#include "inventory/ItemTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::inventory {

class InventorySaveSession;

/** @brief 格子型物品容器（脚本可直接操作）。 */
class Bag {
public:
    /** @brief 创建指定格数的空容器。 */
    Bag(int slotCount);
    ~Bag() = default;

    Bag(const Bag &) = delete;
    Bag &operator=(const Bag &) = delete;

    /** @brief 释放资源并使其失效。 */
    void destroy();

    /** @brief 容器 id（变更事件定位用）。 */
    std::string getId() const { return id_; }
    void setId(const std::string &id) { id_ = id; }

    /** @brief 容器种类（bag / chest / shop 等）。 */
    std::string getKind() const { return kind_; }
    void setKind(const std::string &kind) { kind_ = kind; }

    /** @brief 格数。 */
    int getSlotCount() const { return int(slots_.size()); }
    /** @brief 调整格数：扩容追加空槽；缩容会丢弃被裁掉的槽（不自动转移）。 */
    void setSlotCount(int slotCount);

    /** @brief 重量 / 体积上限（容量策略用）。 */
    float getMaxWeight() const { return maxWeight_; }
    void setMaxWeight(float w) { maxWeight_ = w; }
    float getMaxVolume() const { return maxVolume_; }
    void setMaxVolume(float v) { maxVolume_ = v; }

    /** @brief 接受规则 / 容量策略 / 堆叠规则名。 */
    std::string getAcceptRule() const { return acceptRule_; }
    void setAcceptRule(const std::string &name) { acceptRule_ = name; }
    std::string getCapacityPolicy() const { return capacityPolicy_; }
    void setCapacityPolicy(const std::string &name) { capacityPolicy_ = name; }
    std::string getStackRule() const { return stackRule_; }
    void setStackRule(const std::string &name) { stackRule_ = name; }

    /** @brief 可接受 / 拒绝的标签过滤。 */
    void clearAcceptTags();
    void addAcceptTag(const std::string &tag);
    int getAcceptTagCount() const;
    std::string getAcceptTag(int index) const;

    void clearRejectTags();
    void addRejectTag(const std::string &tag);
    int getRejectTagCount() const;
    std::string getRejectTag(int index) const;

    /** @brief 容器级额外属性（键值）。 */
    void setExtra(const std::string &key, const std::string &value);
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;

    /** @brief 便捷操作（转发 InventorySystem）：增删 / 移动 / 查询。 */
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
    friend class InventorySaveSession;

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
