#pragma once

// 库存操作静态入口 + 可插拔接纳 / 容量 / 堆叠 / 变更钩子。
//
// C++ 侧通过 register* 扩展；脚本侧通过 Bag 上的策略名字符串选用已注册规则。

#include "inventory/ItemTypes.h"
#include "common/Result.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::inventory {

class Bag;
class EquipmentSet;
class InventoryResourceAccount;
class InventorySaveSession;

/** @brief One exact item quantity requested by an atomic inventory batch. */
struct InventoryItemGrant {
    std::string itemId;
    int         quantity = 0;
};

/**
 * @brief Move-only, validated inventory state prepared for one Bag.
 * @remarks Preparation does not mutate the Bag or publish events. The object is
 * bound to the Bag state observed during preparation and may be committed once.
 */
class PreparedInventoryAdd {
public:
    PreparedInventoryAdd();
    ~PreparedInventoryAdd();
    PreparedInventoryAdd(PreparedInventoryAdd &&) noexcept;
    PreparedInventoryAdd &operator=(PreparedInventoryAdd &&) noexcept;
    PreparedInventoryAdd(const PreparedInventoryAdd &) = delete;
    PreparedInventoryAdd &operator=(const PreparedInventoryAdd &) = delete;

private:
    struct State;
    std::unique_ptr<State> state_;
    friend class InventorySystem;
};

/**
 * @brief Move-only, validated inventory removal prepared for one Bag.
 * @remarks Preparation neither mutates the Bag nor publishes events. Commit
 * rejects a Bag that no longer matches the observed baseline.
 */
class PreparedInventoryRemove {
public:
    PreparedInventoryRemove();
    ~PreparedInventoryRemove();
    PreparedInventoryRemove(PreparedInventoryRemove &&) noexcept;
    PreparedInventoryRemove &operator=(PreparedInventoryRemove &&) noexcept;
    PreparedInventoryRemove(const PreparedInventoryRemove &) = delete;
    PreparedInventoryRemove &operator=(const PreparedInventoryRemove &) = delete;

private:
    struct State;
    std::unique_ptr<State> state_;
    friend class InventorySystem;
};

class InventorySystem {
public:
    using AcceptFn =
        std::function<bool(const Bag &bag, const ItemDefinition &def, int quantity, std::string *reason)>;
    using CapacityFn =
        std::function<bool(const Bag &bag, const ItemDefinition &def, int quantity, std::string *reason)>;
    using StackFn =
        std::function<bool(const ItemStack &a, const ItemStack &b, const ItemDefinition &def)>;
    using ChangeHook = std::function<void(const InventoryChangeEvent &ev)>;

    static void registerAcceptRule(const std::string &name, AcceptFn fn);
    static void unregisterAcceptRule(const std::string &name);
    static bool hasAcceptRule(const std::string &name);

    static void registerCapacityPolicy(const std::string &name, CapacityFn fn);
    static void unregisterCapacityPolicy(const std::string &name);
    static bool hasCapacityPolicy(const std::string &name);

    static void registerStackRule(const std::string &name, StackFn fn);
    static void unregisterStackRule(const std::string &name);
    static bool hasStackRule(const std::string &name);

    static void registerChangeHook(const std::string &name, ChangeHook fn);
    static void unregisterChangeHook(const std::string &name);
    static bool hasChangeHook(const std::string &name);

    /** @brief 确保内置规则已注册（模块首次使用时自动调用）。 */
    static void ensureBuiltins();

    static bool canAdd(Bag *bag, const std::string &itemId, int quantity, std::string *reason = nullptr);
    /** @brief 返回实际放入数量（可能部分成功）。 */
    static int addItem(Bag *bag, const std::string &itemId, int quantity);
    /**
     * @brief Validate a multi-item addition against a private candidate Bag.
     * @param bag Borrowed Bag that must remain alive and unmodified until commit.
     * @param grants Exact positive quantities; duplicate item ids are allowed.
     * @return A move-only prepared mutation, or a structured failure without mutation/events.
     * @thread Call on the Bag owning simulation thread.
     * @reentrancy No change hooks are invoked during preparation.
     */
    [[nodiscard]] static eve::Result<PreparedInventoryAdd>
    prepareAddBatch(Bag *bag, const std::vector<InventoryItemGrant> &grants);
    /**
     * @brief Commit one prepared addition and then publish its change events.
     * @param prepared Prepared state returned by prepareAddBatch().
     * @return Exact added quantity, or Conflict if the Bag changed before commit.
     * @remarks The slot swap is the no-fail commit boundary. Hooks observe only
     * the final Bag state; hook exceptions are isolated from authoritative state.
     * @thread Call on the same simulation thread used for preparation.
     */
    [[nodiscard]] static eve::Result<int> commitAddBatch(PreparedInventoryAdd prepared);
    /**
     * @brief Prepare removal of one exact item quantity without mutation or events.
     * @return Pending mutation, or structured failure when the quantity is unavailable.
     * @thread Call on the Bag owning simulation thread.
     */
    [[nodiscard]] static eve::Result<PreparedInventoryRemove>
    prepareRemove(Bag *bag, const std::string &itemId, int quantity);
    /**
     * @brief Commit a prepared removal and publish its event after the no-fail slot swap.
     * @return Exact removed quantity, or Conflict when the Bag changed after preparation.
     * @thread Call on the same simulation thread used for preparation.
     */
    [[nodiscard]] static eve::Result<int> commitRemove(PreparedInventoryRemove prepared);
    static int removeItem(Bag *bag, const std::string &itemId, int quantity);
    static int removeAt(Bag *bag, int slot, int quantity);
    static bool swapSlots(Bag *bag, int slotA, int slotB);
    static bool moveSlot(Bag *bag, int fromSlot, int toSlot);
    static bool splitStack(Bag *bag, int slot, int quantity, int toSlot);
    static int transfer(Bag *from, Bag *to, const std::string &itemId, int quantity);
    static int transferSlot(Bag *from, int fromSlot, Bag *to, int quantity);

    static int countItem(const Bag *bag, const std::string &itemId);
    static int findItem(const Bag *bag, const std::string &itemId);
    static int findItemByTag(const Bag *bag, const std::string &tag);
    static float usedWeight(const Bag *bag);
    static float usedVolume(const Bag *bag);
    static int usedSlotCount(const Bag *bag);
    static void clearBag(Bag *bag);

    static bool equipFromBag(EquipmentSet *eq, const std::string &equipSlot, Bag *bag, int bagSlot);
    static bool unequipToBag(EquipmentSet *eq, const std::string &equipSlot, Bag *bag);

    static void pushEvent(InventoryChangeEvent ev);
    static void pollEvents(std::vector<InventoryChangeEvent> &out);
    static void clearEvents();
    static const std::vector<InventoryChangeEvent> &events();

    static int nextInstanceId();

private:
    friend class InventoryResourceAccount;
    friend class InventorySaveSession;

    static void ensureNextInstanceIdAbove(int usedInstanceId) noexcept;

    static bool canStackTogether(const Bag &bag, const ItemStack &a, const ItemStack &b,
                                 const ItemDefinition &def);
    static bool checkAccept(const Bag &bag, const ItemDefinition &def, int quantity,
                            std::string *reason);
    static bool checkCapacity(const Bag &bag, const ItemDefinition &def, int quantity,
                              std::string *reason);
    static int freeSpaceInSlot(const Bag &bag, int slot, const ItemDefinition &def);
    static void emit(InventoryChangeEvent ev);
    static int addItemImpl(Bag *bag, const std::string &itemId, int quantity, bool publish);

    static std::unordered_map<std::string, AcceptFn> &acceptRules();
    static std::unordered_map<std::string, CapacityFn> &capacityPolicies();
    static std::unordered_map<std::string, StackFn> &stackRules();
    static std::unordered_map<std::string, ChangeHook> &changeHooks();
    static std::vector<InventoryChangeEvent> &eventQueue();
    static int &instanceCounter();
    static bool &builtinsReady();
    static bool                                        &changeHooksSuppressed();
    static std::unique_ptr<Bag> cloneBag(const Bag &source);
    static bool bagsEqual(const Bag &target, const Bag &baseline);
};

}  // namespace eve::inventory
