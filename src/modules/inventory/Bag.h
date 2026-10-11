#pragma once
#include "common/Export.h"


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
class EVENGINE_API_FOUNDATION Bag {
public:
    /** @brief 创建指定格数的空容器。 */
    Bag(int slotCount);
    /** @brief Bag. */
    ~Bag() = default;

    Bag(const Bag &) = delete;
    Bag &operator=(const Bag &) = delete;

    /** @brief 释放资源并使其失效。 */
    void destroy();

    /** @brief 容器 id（变更事件定位用）。 */
    std::string getId() const { return id_; }
    /** @brief Sets the id. */
    void setId(const std::string &id) { id_ = id; }

    /** @brief 容器种类（bag / chest / shop 等）。 */
    std::string getKind() const { return kind_; }
    /** @brief Sets the kind. */
    void setKind(const std::string &kind) { kind_ = kind; }

    /** @brief 格数。 */
    int getSlotCount() const { return int(slots_.size()); }
    /** @brief 调整格数：扩容追加空槽；缩容会丢弃被裁掉的槽（不自动转移）。 */
    void setSlotCount(int slotCount);

    /** @brief 重量 / 体积上限（容量策略用）。 */
    float getMaxWeight() const { return maxWeight_; }
    /** @brief Sets the max weight. */
    void setMaxWeight(float w) { maxWeight_ = w; }
    /** @brief Returns the max volume. */
    float getMaxVolume() const { return maxVolume_; }
    /** @brief Sets the max volume. */
    void setMaxVolume(float v) { maxVolume_ = v; }

    /** @brief 接受规则 / 容量策略 / 堆叠规则名。 */
    std::string getAcceptRule() const { return acceptRule_; }
    /** @brief Sets the accept rule. */
    void setAcceptRule(const std::string &name) { acceptRule_ = name; }
    /** @brief Returns the capacity policy. */
    std::string getCapacityPolicy() const { return capacityPolicy_; }
    /** @brief Sets the capacity policy. */
    void setCapacityPolicy(const std::string &name) { capacityPolicy_ = name; }
    /** @brief Returns the stack rule. */
    std::string getStackRule() const { return stackRule_; }
    /** @brief Sets the stack rule. */
    void setStackRule(const std::string &name) { stackRule_ = name; }

    /** @brief 可接受 / 拒绝的标签过滤。 */
    void clearAcceptTags();
    /** @brief Adds accept tag. */
    void addAcceptTag(const std::string &tag);
    /** @brief Returns the accept tag count. */
    int getAcceptTagCount() const;
    /** @brief Returns the accept tag. */
    std::string getAcceptTag(int index) const;

    /** @brief Clears reject tags. */
    void clearRejectTags();
    /** @brief Adds reject tag. */
    void addRejectTag(const std::string &tag);
    /** @brief Returns the reject tag count. */
    int getRejectTagCount() const;
    /** @brief Returns the reject tag. */
    std::string getRejectTag(int index) const;

    /** @brief 容器级额外属性（键值）。 */
    void setExtra(const std::string &key, const std::string &value);
    /** @brief Returns the extra. */
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;

    /** @brief 便捷操作（转发 InventorySystem）：增删 / 移动 / 查询。 */
    bool canAddItem(const std::string &itemId, int quantity);
    /** @brief Can add item reason. */
    std::string canAddItemReason(const std::string &itemId, int quantity);
    /** @brief Adds item. */
    int addItem(const std::string &itemId, int quantity);
    /** @brief Removes item. */
    int removeItem(const std::string &itemId, int quantity);
    /** @brief Removes at. */
    int removeAt(int slot, int quantity);
    /** @brief Swap slots. */
    bool swapSlots(int slotA, int slotB);
    /** @brief Moves slot. */
    bool moveSlot(int fromSlot, int toSlot);
    /** @brief Split stack. */
    bool splitStack(int slot, int quantity, int toSlot);
    /** @brief Returns the number of item. */
    int countItem(const std::string &itemId) const;
    /** @brief Finds item. */
    int findItem(const std::string &itemId) const;
    /** @brief Finds item by tag. */
    int findItemByTag(const std::string &tag) const;
    /** @brief Returns the used weight. */
    float getUsedWeight() const;
    /** @brief Returns the used volume. */
    float getUsedVolume() const;
    /** @brief Returns the used slot count. */
    int getUsedSlotCount() const;
    /** @brief Clears clear. */
    void clear();

    // ---- 槽位查询 ----
    /** @brief True when slot empty. */
    bool isSlotEmpty(int slot) const;
    /** @brief Returns the slot item id. */
    std::string getSlotItemId(int slot) const;
    /** @brief Returns the slot quantity. */
    int getSlotQuantity(int slot) const;
    /** @brief Returns the slot instance id. */
    int getSlotInstanceId(int slot) const;
    /** @brief Returns the slot durability. */
    float getSlotDurability(int slot) const;
    /** @brief Sets the slot durability. */
    void setSlotDurability(int slot, float durability);
    /** @brief Returns the slot prop. */
    std::string getSlotProp(int slot, const std::string &key, const std::string &fallback = {}) const;
    /** @brief Sets the slot prop. */
    void setSlotProp(int slot, const std::string &key, const std::string &value);
    /** @brief Slot has tag. */
    bool slotHasTag(int slot, const std::string &tag) const;
    /** @brief Adds slot tag. */
    void addSlotTag(int slot, const std::string &tag);

    // ---- 供 System 直接访问 ----
    /** @brief Slots. */
    const std::vector<ItemStack> &slots() const { return slots_; }
    /** @brief Slots. */
    std::vector<ItemStack> &slots() { return slots_; }
    /** @brief Accept tags. */
    const std::vector<std::string> &acceptTags() const { return acceptTags_; }
    /** @brief Reject tags. */
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
