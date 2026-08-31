#pragma once

/**
 * @brief RPG 模块入口：属性 / 效果 / 状态 / 技能 / 结算 / 任务 / 成长 / 生命 的
 * 脚本绑定与帧调度点。
 *
 * 各系统的完整设计说明见对应头文件：
 *   AttributeSystem.h / AttributeTypes.h — 属性
 *   Effect.h                             — 效果（数据驱动定义）
 *   StatusSystem.h / StatusTypes.h       — 状态（运行时实例，buff/debuff）
 *   Skill.h / SkillSystem.h / SkillTypes.h — 技能
 *   Settlement.h                         — 结算流水线
 *   Quest.h / Tracker.h / QuestSystem.h  — 任务/成就/教程追踪器
 *   LevelSystem.h                        — 等级 / 经验
 *   VitalsSystem.h                       — 当前资源 / 受伤 / 死亡
 * 以及总体设计文档：docs/dev/RPG系统设计.md
 */

#include "common/Module.h"
#include "rpg/LevelSystem.h"
#include "rpg/BattleVictory.h"
#include "rpg/RPGActor.h"
#include "rpg/StatusTypes.h"
#include "rpg/SkillTypes.h"
#include "rpg/VitalsSystem.h"

#include <string>
#include <vector>

namespace eve {
namespace inventory {
class Bag;
class EquipmentSet;
}
}  // namespace eve

namespace eve::rpg {

struct SettlementContext;
class Tracker;
class Battle;
class Party;
class GameState;
class WorldState;
class StoryEventSession;
struct WorldLootRequest;

/** @brief RPG 模块（eve.RPG）：Actor 工厂 + 定义注册 + 帧调度 + 事件缓存。 */
class RPG : public Module {
public:
    Module_REG(RPG);
    RPG() = default;
    ~RPG() override = default;

    /**
     * @brief 创建一个空白 RPGActor。
     * @return Borrowed nullable pointer to an ECS-owned actor; null means creation failed.
     * @ownership The ECS world owns the actor; callers must release it through ECS and never delete it.
     * @lifetime Valid until actor/world destruction; retain the generation-qualified EntityHandle across frames.
     * @thread Call on the RPG module's owning ECS thread.
     * @reentrancy The factory invokes no user callbacks; do not re-enter structural ECS mutation while using the
     * result.
     */
    RPGActor *newActor();
    /**
     * @brief Create an empty generation-safe party roster.
     * @return Owned nullable party; the caller is responsible for destruction.
     * @ownership The caller owns the Party, which never owns its linked actors.
     * @lifetime Valid until caller destruction; linked actors may independently become stale.
     * @thread Call and use on the RPG simulation thread.
     */
    Party *newParty();

    /** @brief 效果定义注册（数据驱动，进程级注册表）。 */
    int registerEffectsFromJson(const std::string &json);
    void clearEffectDefinitions();
    int getEffectDefinitionCount();

    /** @brief 技能定义注册。 */
    int registerSkillsFromJson(const std::string &json);
    void clearSkillDefinitions();
    int getSkillDefinitionCount();

    /** @brief 任务定义注册。 */
    int registerQuestsFromJson(const std::string &json);
    /**
     * @brief Strictly validate and atomically replace the process-local quest catalogue.
     * @return Committed definition count or a structured failure; the previous catalogue survives failure.
     * @thread Call on the owning simulation/content thread.
     * @reentrancy Does not invoke script callbacks.
     */
    [[nodiscard]] eve::Result<int> replaceQuestsFromJson(const std::string &json);
    void clearQuestDefinitions();
    int getQuestDefinitionCount();
    /** @brief Return whether one exact quest definition is present in the current catalogue. */
    bool hasQuestDefinition(const std::string &id) const;
    /**
     * @brief Atomically complete one ready quest and apply all item/attribute rewards.
     * @return Committed reward-entry count, or a structured failure without observable mutation.
     * @thread Call on the authoritative state owners' simulation thread.
     * @reentrancy Inventory observers run only after the complete state is committed.
     */
    [[nodiscard]] eve::Result<int> claimQuestRewards(Tracker *tracker, GameState *gameState,
                                                      inventory::Bag *bag, const std::string &questId);
    /**
     * @brief Atomically settle one persistent loot object across world, quest, state and inventory owners.
     * @return Committed-effect count, or a structured failure without observable mutation.
     * @thread Call on the authoritative owners' simulation thread.
     * @reentrancy Inventory observers run only after all owners are committed.
     */
    [[nodiscard]] eve::Result<int> collectWorldLoot(Tracker *tracker, GameState *gameState,
                                                     inventory::Bag *bag,
                                                     const WorldLootRequest &request);
    /**
     * @brief Atomically settle one victorious persistent encounter.
     * @return Levels and learned skills after the complete settlement, or structured failure.
     * @thread Call on the authoritative owners' simulation thread.
     * @reentrancy No callbacks are invoked; level events are poll-only.
     */
    [[nodiscard]] eve::Result<BattleVictoryReceipt>
    settleBattleVictory(RPGActor *actor, Tracker *tracker, GameState *gameState,
                        const BattleVictoryRequest &request);
    /** @brief Atomically debit GameState currency and add purchased items to Bag. */
    [[nodiscard]] eve::Result<int> buyFromShop(GameState *gameState, inventory::Bag *bag,
                                                const std::string &currencyId,
                                                const std::string &itemId, int quantity,
                                                double unitPrice);
    /** @brief Atomically remove sold items from Bag and credit GameState currency. */
    [[nodiscard]] eve::Result<int> sellToShop(GameState *gameState, inventory::Bag *bag,
                                               const std::string &currencyId,
                                               const std::string &itemId, int quantity,
                                               double unitPrice);
    /** @brief Strictly validate and atomically replace the authoritative shop catalogue. */
    [[nodiscard]] eve::Result<int> replaceShopOffersFromJson(const std::string &json);
    /** @brief Return the committed shop-offer count. */
    int getShopOfferCount() const;
    /** @brief Clear the shop catalogue for teardown or failed initial publication rollback. */
    void clearShopOffers();
    /** @brief Return whether an exact shop offer id exists. */
    bool hasShopOffer(const std::string &offerId) const;
    std::string getShopOfferId(int index) const;
    std::string getShopOfferItemId(int index) const;
    std::string getShopOfferName(int index) const;
    std::string getShopOfferDescription(int index) const;
    int getShopOfferBuyPrice(int index) const;
    int getShopOfferSellPrice(int index) const;
    /** @brief Buy using the item and price from the authoritative shop catalogue. */
    [[nodiscard]] eve::Result<int> buyShopOffer(GameState *gameState, inventory::Bag *bag,
                                                const std::string &currencyId,
                                                const std::string &offerId, int quantity);
    /** @brief Sell using the item and price from the authoritative shop catalogue. */
    [[nodiscard]] eve::Result<int> sellShopOffer(GameState *gameState, inventory::Bag *bag,
                                                 const std::string &currencyId,
                                                 const std::string &offerId, int quantity);
    /** @brief Strictly replace the data-driven encounter catalogue. */
    [[nodiscard]] eve::Result<int> replaceEncountersFromJson(const std::string &json);
    void clearEncounters();
    bool hasEncounter(const std::string &encounterId) const;
    int getEncounterMemberCount(const std::string &encounterId) const;
    /**
     * @brief Create the first member through the single-enemy compatibility facade.
     * @return Borrowed nullable actor owned by the RPG ECS world.
     * @ownership The RPG ECS world owns the actor; callers release it through RPGActor::release().
     * @lifetime Valid until release, RPG teardown, or owning ECS world reset.
     * @thread Call on the owning simulation thread.
     * @reentrancy Does not invoke callbacks.
     */
    RPGActor *newEncounterActor(const std::string &encounterId);
    /**
     * @brief Create one indexed member from an encounter composition.
     * @return Borrowed nullable actor owned by the RPG ECS world.
     * @ownership The RPG ECS world owns the actor; callers release it through RPGActor::release().
     * @lifetime Valid until release, RPG teardown, or owning ECS world reset.
     * @thread Call on the owning simulation thread.
     * @reentrancy Does not invoke callbacks.
     */
    RPGActor *newEncounterMemberActor(const std::string &encounterId, int memberIndex);
    std::string getEncounterDisplayName(const std::string &encounterId) const;
    double getEncounterMaxHp(const std::string &encounterId) const;
    std::string getEncounterMemberDisplayName(const std::string &encounterId, int memberIndex) const;
    double getEncounterMemberMaxHp(const std::string &encounterId, int memberIndex) const;
    /** @brief Strictly replace ordered companion/AI battle tactics. */
    [[nodiscard]] eve::Result<int> replaceBattleTacticsFromJson(const std::string &json);
    void clearBattleTactics();
    /** @brief Select and queue one action from a registered tactics definition. */
    [[nodiscard]] eve::Result<std::string> queueBattleTactic(Battle *battle, RPGActor *actor,
                                                             const std::string &tacticsId);
    /** @brief Strictly validate and atomically replace versioned story-event content. */
    [[nodiscard]] eve::Result<int> replaceStoryEventsFromJson(const std::string &json);
    /** @brief Clear story-event content during teardown or content-package rollback. */
    void clearStoryEvents();
    /** @brief Return the number of committed story-event definitions. */
    int getStoryEventCount() const;
    /** @brief Return whether an exact story-event id exists. */
    bool hasStoryEvent(const std::string &eventId) const;
    /**
     * @brief Create an empty caller-owned story-event session.
     * @return Owned nullable session; caller destroys it after use.
     * @ownership The caller owns the session; it owns no GameState or presentation object.
     * @thread Create and use on the RPG simulation thread.
     * @reentrancy Construction invokes no callbacks.
     */
    StoryEventSession *newStoryEventSession();
    /** @brief Settle a victory using only rewards declared by the encounter catalogue. */
    [[nodiscard]] eve::Result<BattleVictoryReceipt>
    settleEncounterVictory(RPGActor *actor, Tracker *tracker, GameState *gameState,
                           const std::string &encounterId, const std::string &mapId,
                           const std::string &objectId);
    /**
     * @brief 创建一个新的目标追踪器（任务日志 / 成就 / 教程各建一份）。
     * @return Owned nullable tracker created with new; the caller must destroy it.
     * @ownership The caller owns the returned tracker and is responsible for its destruction.
     * @lifetime Valid until the caller destroys it; not retained by RPG.
     * @thread Create and use on the owning simulation thread.
     * @reentrancy Construction calls syncAuto which emits activation events; poll before reading.
     */
    Tracker *newTracker();

    /** @brief 升级事件缓存（上一次 update() 产生的，供脚本轮询）。 */
    int getLevelUpEventCount() const;
    RPGActor *getLevelUpEventActor(int index) const;
    int getLevelUpEventPreviousLevel(int index) const;
    int getLevelUpEventNewLevel(int index) const;

    /** @brief 生命等资源事件缓存（上一次 update() 产生的，供脚本轮询）。 */
    int getVitalsEventCount() const;
    RPGActor *getVitalsEventActor(int index) const;
    std::string getVitalsEventResource(int index) const;
    std::string getVitalsEventAction(int index) const;
    double getVitalsEventAmount(int index) const;
    std::string getVitalsEventSource(int index) const;
    double getVitalsEventCurrent(int index) const;
    double getVitalsEventMax(int index) const;

    /** @brief 装备加成（物品 id → 属性加成）注册与同步。 */
    int registerItemStatsFromJson(const std::string &itemId, const std::string &json);
    void clearItemStats(const std::string &itemId);
    void clearAllItemStats();
    int getItemStatCount();
    /** @brief 把装备栏当前装备同步为 actor 属性修改器；返回施加条数。 */
    int syncEquipModifiers(RPGActor *actor, inventory::EquipmentSet *equip);

    /** @brief 掉落表注册与 roll。 */
    int registerLootTablesFromJson(const std::string &json);
    void clearLootTables();
    int getLootTableCount();
    /** @brief 用种子随机流 roll 掉落表，把命中物品加入背包；返回掉落物品种类数。 */
    int rollLoot(const std::string &tableId, inventory::Bag *bag, int seed);

    /** @brief 特征定义注册。 */
    int registerTraitsFromJson(const std::string &json);
    void clearTraitDefinitions();
    int getTraitDefinitionCount();

    /** @brief 职业定义注册。 */
    int registerClassesFromJson(const std::string &json);
    void clearClassDefinitions();
    int getClassDefinitionCount();

    /** @brief 技能伤害注册（战斗用）与战斗对象。 */
    int registerSkillDamage(const std::string &skillId, const std::string &damageType,
                            const std::string &formula, const std::string &element,
                            double critChance, int hitChance);
    void clearSkillDamage();
    /**
     * @brief 创建一场新的回合制战斗。
     * @return Owned nullable Battle; the caller must destroy it after the battle ends.
     * @ownership The caller owns the returned battle and is responsible for its destruction.
     * @lifetime Valid until the caller destroys it; not retained by RPG.
     * @thread Create and drive on the owning simulation thread.
     */
    Battle *newBattle();

    /**
     * @brief 创建一份新的游戏状态（开关/变量）。
     * @return Owned nullable GameState; the caller must destroy it.
     * @ownership The caller owns the returned state and is responsible for its destruction.
     * @lifetime Valid until the caller destroys it; not retained by RPG.
     */
    GameState *newGameState();

    /**
     * @brief Create a typed per-map object-state adapter over one GameState owner.
     * @param gameState Borrowed authoritative state that must outlive the returned adapter.
     * @return Caller-owned adapter, or null when gameState is null.
     * @ownership Caller destroys the adapter; it never owns GameState.
     * @thread Create and use on the GameState owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    WorldState *newWorldState(GameState *gameState);
    /**
     * @brief 进程级全局游戏状态（借用，不销毁）。
     * @return Borrowed nullable global GameState owned by the process.
     */
    GameState *globalGameState();

    /** @brief 推进 StatusSystem / SkillSystem，并刷新本帧的 tick / change / cast 事件缓存。 */
    void update(float dt);

    /** @brief 周期状态 tick 事件（上一次 update() 产生的，供脚本轮询）。 */
    int getTickEventCount() const;
    /**
     * @brief Returns the actor referenced by a retained tick event, or null for an invalid index.
     * @return Borrowed nullable ECS actor pointer from the event projection.
     * @ownership The ECS world owns the actor; the event cache never transfers ownership.
     * @lifetime Valid until actor/world destruction; use the actor's persistent identity or handle after the poll.
     * @thread Call on the RPG simulation thread before the next event-producing update.
     * @reentrancy Do not retain across callbacks or event-cache refresh.
     */
    RPGActor *getTickEventActor(int index) const;
    int getTickEventInstanceId(int index) const;
    std::string getTickEventEffectId(int index) const;
    std::string getTickEventSource(int index) const;
    int getTickEventStacks(int index) const;

    /** @brief 状态生命周期变更事件（apply/refresh/extend/stack/remove/expire/reject）。 */
    int getStatusChangeEventCount() const;
    /**
     * @brief Returns the actor referenced by a status event, or null for an invalid index.
     * @return Borrowed nullable ECS actor pointer from the event projection.
     * @ownership The ECS world owns the actor; the event cache never transfers ownership.
     * @lifetime Valid until actor/world destruction; use a persistent identity or handle after polling.
     * @thread Call on the RPG simulation thread before the next event-producing update.
     * @reentrancy Do not retain across callbacks or event-cache refresh.
     */
    RPGActor *getStatusChangeEventActor(int index) const;
    int getStatusChangeEventInstanceId(int index) const;
    std::string getStatusChangeEventEffectId(int index) const;
    std::string getStatusChangeEventSource(int index) const;
    std::string getStatusChangeEventAction(int index) const;
    int getStatusChangeEventStacks(int index) const;
    std::string getStatusChangeEventReason(int index) const;

    /** @brief 技能释放结算事件。 */
    int getCastEventCount() const;
    /**
     * @brief Returns the caster referenced by a cast event, or null for an invalid index.
     * @return Borrowed nullable ECS actor pointer from the event projection.
     * @ownership The ECS world owns the actor; the event cache never transfers ownership.
     * @lifetime Valid until actor/world destruction; use a persistent identity or handle after polling.
     * @thread Call on the RPG simulation thread before the next event-producing update.
     * @reentrancy Do not retain across callbacks or event-cache refresh.
     */
    RPGActor *getCastEventCaster(int index) const;
    /**
     * @brief Returns the target referenced by a cast event, or null for an invalid index.
     * @return Borrowed nullable ECS actor pointer from the event projection.
     * @ownership The ECS world owns the actor; the event cache never transfers ownership.
     * @lifetime Valid until actor/world destruction; use a persistent identity or handle after polling.
     * @thread Call on the RPG simulation thread before the next event-producing update.
     * @reentrancy Do not retain across callbacks or event-cache refresh.
     */
    RPGActor *getCastEventTarget(int index) const;
    std::string getCastEventSkillId(int index) const;

    /**
     * @brief 结算：新建上下文 / 运行流水线 / 配置阶段。
     * @return Owned nullable heap context; the caller must destroy it after the synchronous pipeline call.
     * @ownership The caller owns the returned context and is responsible for its explicit destruction.
     * @lifetime Valid until the caller destroys it; it is not retained by RPG or the settlement registry.
     * @thread Create and use on the owning simulation thread.
     * @reentrancy Construction invokes no external callbacks; do not pass the context to re-entrant mutation.
     */
    SettlementContext *newSettlementContext();
    void runSettlement(const std::string &pipeline, SettlementContext *ctx);
    int getSettlementStageCount(const std::string &pipeline);
    bool hasSettlementStage(const std::string &pipeline, const std::string &stage);
    bool setSettlementStageEnabled(const std::string &pipeline, const std::string &stage,
                                    bool enabled);
    bool setSettlementStagePriority(const std::string &pipeline, const std::string &stage,
                                     int priority);
    bool removeSettlementStage(const std::string &pipeline, const std::string &stage);
    void clearSettlementPipeline(const std::string &pipeline);

private:
    std::vector<StatusTickEvent> ticks_;
    std::vector<StatusChangeEvent> changes_;
    std::vector<SkillCastEvent> casts_;
    std::vector<LevelUpEvent> levelUps_;
    std::vector<VitalsEvent> vitals_;
};

}  // namespace eve::rpg
