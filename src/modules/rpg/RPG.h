#pragma once

/**
 * @brief RPG 模块入口：属性 / 效果 / 状态 / 技能 / 结算五套系统的脚本绑定与帧调度点。
 *
 * 各系统的完整设计说明见对应头文件：
 *   AttributeSystem.h / AttributeTypes.h — 属性
 *   Effect.h                             — 效果（数据驱动定义）
 *   StatusSystem.h / StatusTypes.h       — 状态（运行时实例，buff/debuff）
 *   Skill.h / SkillSystem.h / SkillTypes.h — 技能
 *   Settlement.h                         — 结算流水线
 * 以及总体设计文档：docs/2026-08-08-rpg-module-design.md
 */

#include "common/Module.h"
#include "rpg/RPGActor.h"
#include "rpg/StatusTypes.h"
#include "rpg/SkillTypes.h"

#include <string>
#include <vector>

namespace eve::rpg {

class SettlementContext;

/** @brief RPG 模块（eve.RPG）：Actor 工厂 + 定义注册 + 帧调度 + 事件缓存。 */
class RPG : public Module {
public:
    Module_REG(RPG);
    RPG() = default;
    ~RPG() override = default;

    /** @brief 创建一个空白 RPGActor。 */
    RPGActor *newActor();

    /** @brief 效果定义注册（数据驱动，进程级注册表）。 */
    int registerEffectsFromJson(const std::string &json);
    void clearEffectDefinitions();
    int getEffectDefinitionCount();

    /** @brief 技能定义注册。 */
    int registerSkillsFromJson(const std::string &json);
    void clearSkillDefinitions();
    int getSkillDefinitionCount();

    /** @brief 推进 StatusSystem / SkillSystem，并刷新本帧的 tick / change / cast 事件缓存。 */
    void update(float dt);

    /** @brief 周期状态 tick 事件（上一次 update() 产生的，供脚本轮询）。 */
    int getTickEventCount() const;
    RPGActor *getTickEventActor(int index) const;
    int getTickEventInstanceId(int index) const;
    std::string getTickEventEffectId(int index) const;
    std::string getTickEventSource(int index) const;
    int getTickEventStacks(int index) const;

    /** @brief 状态生命周期变更事件（apply/refresh/extend/stack/remove/expire/reject）。 */
    int getStatusChangeEventCount() const;
    RPGActor *getStatusChangeEventActor(int index) const;
    int getStatusChangeEventInstanceId(int index) const;
    std::string getStatusChangeEventEffectId(int index) const;
    std::string getStatusChangeEventSource(int index) const;
    std::string getStatusChangeEventAction(int index) const;
    int getStatusChangeEventStacks(int index) const;
    std::string getStatusChangeEventReason(int index) const;

    /** @brief 技能释放结算事件。 */
    int getCastEventCount() const;
    RPGActor *getCastEventCaster(int index) const;
    RPGActor *getCastEventTarget(int index) const;
    std::string getCastEventSkillId(int index) const;

    /** @brief 结算：新建上下文 / 运行流水线 / 配置阶段。 */
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
};

}  // namespace eve::rpg
