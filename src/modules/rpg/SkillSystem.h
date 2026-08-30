#pragma once

// 技能释放流程：冷却 / 消耗 / 读条 / 授予效果。
//
// 可定制点：registerCastCondition() 允许 C++ 侧插入任意额外的"能否释放"判断
// （例如"沉默状态不能施法"），无需修改 SkillSystem 本身——引擎不内置任何
// 状态互斥概念（沉默/眩晕等完全是游戏自定义的 tag/effect）。

#include "rpg/SkillTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;
struct SkillDefinition;

class SkillSystem {
public:
    /** @brief 返回 false 表示禁止释放；通过 outReason 说明原因。 */
    using CastCondition =
        std::function<bool(RPGActor *actor, const SkillDefinition &def, std::string &outReason)>;

    static void registerCastCondition(const std::string &name, CastCondition fn);
    static void unregisterCastCondition(const std::string &name);
    static void clearCastConditions();

    static void learn(RPGActor *actor, const std::string &skillId);
    static bool knows(RPGActor *actor, const std::string &skillId);
    static bool forget(RPGActor *actor, const std::string &skillId);

    /** @brief 技能的目标类型字符串（"" 表示未学会/未知）。 */
    static std::string getTargetType(RPGActor *actor, const std::string &skillId);

    static float getCooldownRemaining(RPGActor *actor, const std::string &skillId);
    static void setCooldownRemaining(RPGActor *actor, const std::string &skillId, float seconds);

    /** @brief 检查冷却/消耗/学会状态/自定义条件，不产生任何副作用。 */
    static bool canCast(RPGActor *actor, const std::string &skillId, std::string *reason = nullptr);

    /**
     * @brief 尝试释放技能：通过 canCast 检查后立即扣除消耗并进入冷却；
     * castTime<=0 时立即结算（授予效果 + 产生 SkillCastEvent），
     * 否则进入读条状态，由 update() 在读条结束时结算。
     */
    static bool beginCast(RPGActor *actor, const std::string &skillId, RPGActor *target = nullptr,
                           std::string *reason = nullptr);

    /** @brief 打断当前读条（不退还已扣除的消耗/冷却）。 */
    static void cancelCast(RPGActor *actor);

    static bool isCasting(RPGActor *actor);
    static std::string getCastingSkillId(RPGActor *actor);
    static float getCastProgress(RPGActor *actor);  ///< 0..1，非读条状态返回 0

    /** @brief 遍历 RPGActor::liveActors()：冷却倒计时 + 读条推进/结算。 */
    static void update(float dt);

    static void pollCastEvents(std::vector<SkillCastEvent> &out);
};

}  // namespace eve::rpg
