#pragma once

/**
 * @brief ARPG 模块入口：角色、效果、技能、战斗、成长、任务、掉落、AI 与存档。
 *
 * 各系统的完整设计说明见对应头文件：
 *   AttributeSystem.h / AttributeTypes.h — 属性
 *   Effect.h                             — 效果（数据驱动定义）
 *   StatusSystem.h / StatusTypes.h       — 状态（运行时实例，buff/debuff）
 *   Skill.h / SkillSystem.h / SkillTypes.h — 技能
 *   Settlement.h                         — 结算流水线
 *   Combat.h / Progression.h             — 标准战斗与成长
 *   Quest.h / Tracker.h                  — 任务定义与运行时
 *   Loot.h / UtilityAI.h / StateCodec.h  — 掉落、AI 与存档
 * 以及总体设计文档：docs/dev/ARPG框架设计.md
 */

#include "common/Module.h"
#include "rpg/Combat.h"
#include "rpg/Progression.h"
#include "rpg/RPGActor.h"
#include "rpg/SkillTypes.h"
#include "rpg/StatusTypes.h"

#include <string>
#include <vector>

namespace eve::rpg {

class SettlementContext;
class Tracker;
class LootRoll;
class UtilityBrain;

/** @brief RPG 模块（eve.RPG）：Actor 工厂 + 定义注册 + 帧调度 + 事件缓存。 */
class RPG : public Module {
public:
    Module_REG(RPG);
    RPG()           = default;
    ~RPG() override = default;

    /** @brief 创建一个空白 RPGActor。 */
    RPGActor* newActor();

    /** @brief 效果定义注册（数据驱动，进程级注册表）。 */
    int  registerEffectsFromJson(const std::string& json);
    void clearEffectDefinitions();
    int  getEffectDefinitionCount();

    /** @brief 技能定义注册。 */
    int  registerSkillsFromJson(const std::string& json);
    void clearSkillDefinitions();
    int  getSkillDefinitionCount();

    /** @brief 任务定义与每玩家独立任务追踪器。 */
    int      registerQuestsFromJson(const std::string& json);
    void     clearQuestDefinitions();
    int      getQuestDefinitionCount();
    Tracker* newTracker();

    /** @brief 注册并按确定性种子抽取战利品表。 */
    int       registerLootTablesFromJson(const std::string& json);
    void      clearLootTableDefinitions();
    int       getLootTableDefinitionCount();
    void      setLootRandomSeed(int seed);
    LootRoll* rollLoot(const std::string& tableId, float luck);

    /** @brief 注册 Utility AI 决策集并创建独立黑板。 */
    int           registerAIDecisionSetsFromJson(const std::string& json);
    void          clearAIDecisionSetDefinitions();
    int           getAIDecisionSetDefinitionCount();
    UtilityBrain* newUtilityBrain();

    /** @brief 角色与任务运行时 JSON 快照。 */
    std::string serializeActor(RPGActor* actor);
    bool        deserializeActor(RPGActor* actor, const std::string& json);
    std::string serializeTracker(Tracker* tracker);
    bool        deserializeTracker(Tracker* tracker, const std::string& json);

    /** @brief 标准战斗结算与确定性随机种子。 */
    void        setCombatRandomSeed(int seed);
    float       dealDamage(RPGActor* source, RPGActor* target, float power, const std::string& damageType);
    float       heal(RPGActor* source, RPGActor* target, float amount);
    int         getCombatEventCount() const;
    RPGActor*   getCombatEventSource(int index) const;
    RPGActor*   getCombatEventTarget(int index) const;
    std::string getCombatEventAction(int index) const;
    std::string getCombatEventDamageType(int index) const;
    float       getCombatEventAmount(int index) const;
    bool        getCombatEventCritical(int index) const;
    bool        getCombatEventKilled(int index) const;

    /** @brief 本帧升级事件。 */
    int       getLevelEventCount() const;
    RPGActor* getLevelEventActor(int index) const;
    int       getLevelEventOldLevel(int index) const;
    int       getLevelEventNewLevel(int index) const;
    int       getLevelEventPointsGranted(int index) const;

    /** @brief 推进 StatusSystem / SkillSystem，并刷新本帧的 tick / change / cast 事件缓存。 */
    void update(float dt);

    /** @brief 周期状态 tick 事件（上一次 update() 产生的，供脚本轮询）。 */
    int         getTickEventCount() const;
    RPGActor*   getTickEventActor(int index) const;
    int         getTickEventInstanceId(int index) const;
    std::string getTickEventEffectId(int index) const;
    std::string getTickEventSource(int index) const;
    int         getTickEventStacks(int index) const;

    /** @brief 状态生命周期变更事件（apply/refresh/extend/stack/remove/expire/reject）。 */
    int         getStatusChangeEventCount() const;
    RPGActor*   getStatusChangeEventActor(int index) const;
    int         getStatusChangeEventInstanceId(int index) const;
    std::string getStatusChangeEventEffectId(int index) const;
    std::string getStatusChangeEventSource(int index) const;
    std::string getStatusChangeEventAction(int index) const;
    int         getStatusChangeEventStacks(int index) const;
    std::string getStatusChangeEventReason(int index) const;

    /** @brief 技能释放结算事件。 */
    int         getCastEventCount() const;
    RPGActor*   getCastEventCaster(int index) const;
    RPGActor*   getCastEventTarget(int index) const;
    std::string getCastEventSkillId(int index) const;

    /** @brief 结算：新建上下文 / 运行流水线 / 配置阶段。 */
    SettlementContext* newSettlementContext();
    void               runSettlement(const std::string& pipeline, SettlementContext* ctx);
    int                getSettlementStageCount(const std::string& pipeline);
    bool               hasSettlementStage(const std::string& pipeline, const std::string& stage);
    bool               setSettlementStageEnabled(const std::string& pipeline, const std::string& stage, bool enabled);
    bool               setSettlementStagePriority(const std::string& pipeline, const std::string& stage, int priority);
    bool               removeSettlementStage(const std::string& pipeline, const std::string& stage);
    void               clearSettlementPipeline(const std::string& pipeline);

private:
    std::vector<StatusTickEvent>   ticks_;
    std::vector<StatusChangeEvent> changes_;
    std::vector<SkillCastEvent>    casts_;
    std::vector<CombatEvent>       combatEvents_;
    std::vector<LevelUpEvent>      levelEvents_;
};

}  // namespace eve::rpg
