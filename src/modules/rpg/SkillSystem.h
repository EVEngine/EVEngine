#pragma once
#include "common/Export.h"
#include "common/Result.h"


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

/** @brief EVENGINE_API_PLATFORM public API. */
class EVENGINE_API_PLATFORM SkillSystem {
public:
    /** @brief 返回 false 表示禁止释放；通过 outReason 说明原因。 */
    using CastCondition =
        /** @brief Bool. */
        std::function<bool(RPGActor *actor, const SkillDefinition &def, std::string &outReason)>;

    /** @brief Reject global legacy cast callbacks without a versioned persistence contract.
     * @return Applied when none are registered, otherwise Unsupported; no callbacks or mutation.
     * @thread RPG owner thread; check at capture/admission and before publication. */
    [[nodiscard]] static Result<void> validatePersistenceContext();
    /** @brief Registers cast condition. */
    static void registerCastCondition(const std::string &name, CastCondition fn);
    /** @brief Unregisters cast condition. */
    static void unregisterCastCondition(const std::string &name);
    /** @brief Clears cast conditions. */
    static void clearCastConditions();

    /** @brief Learn. */
    static void learn(RPGActor *actor, const std::string &skillId);
    /** @brief Knows. */
    static bool knows(RPGActor *actor, const std::string &skillId);
    /** @brief Forget. */
    static bool forget(RPGActor *actor, const std::string &skillId);

    /** @brief 技能的目标类型字符串（"" 表示未学会/未知）。 */
    static std::string getTargetType(RPGActor *actor, const std::string &skillId);

    /** @brief Returns the cooldown remaining. */
    static float getCooldownRemaining(RPGActor *actor, const std::string &skillId);
    /** @brief Sets the cooldown remaining. */
    static void setCooldownRemaining(RPGActor *actor, const std::string &skillId, float seconds);

    /** @brief 检查冷却/消耗/学会状态/自定义条件，不产生任何副作用。 */
    static bool canCast(RPGActor *actor, const std::string &skillId, std::string *reason = nullptr);

    /**
     * @brief 尝试释放技能：通过 canCast 检查后立即扣除消耗并进入冷却；
     * castTime<=0 时立即结算（授予效果 + 产生 SkillCastEvent），
     * 否则进入读条状态，由 update() 在读条结束时结算。
     * @remarks Target must be in the caster's physical Table. Timed casts retain a generation-qualified link;
     * retirement/reuse cancels resolution without redirecting effects to the caster. The Table owner outlives calls.
     * No target means default self resolution; an explicit self target retains its event projection.
     * Cross-world rejection occurs before cost/cooldown mutation.
     */
    static bool beginCast(RPGActor *actor, const std::string &skillId, RPGActor *target = nullptr,
                           std::string *reason = nullptr);

    /** @brief 打断当前读条（不退还已扣除的消耗/冷却）。 */
    static void cancelCast(RPGActor *actor);

    /** @brief True when casting. */
    static bool isCasting(RPGActor *actor);
    /** @brief Returns the casting skill id. */
    static std::string getCastingSkillId(RPGActor *actor);
    /** @brief Returns the cast progress. */
    static float getCastProgress(RPGActor *actor);  ///< 0..1，非读条状态返回 0

    /** @brief Advance current-world RPG skill clocks, then resolve completed casts outside Views.
     * @param dt Injected seconds; invalid deltas produce no mutation or callbacks.
     * @cost Scan actors/known skills plus O(K log K) ordering for K keyed actors per phase;
     * effect work is delegated per completed cast.
     * @thread Simulation owner, outside caller Views; the owning Table remains alive through callbacks.
     * @remarks Nonfinite, zero or negative dt leaves all state unchanged. Cooldowns clamp at zero;
     * completed casts clear the full casting state before effect callbacks. */
    static void update(float dt);

    /** @brief Append cast facts to out and drain the active simulation queue; self-output is a no-op.
     * @ownership Event values own weak actor references, never the actor or its Table.
     * @throws std::bad_alloc or std::length_error Before either vector's event facts change.
     * @cost One move per queued event; output growth is amortized over the caller-owned vector. */
    static void pollCastEvents(std::vector<SkillCastEvent> &out);
};

}  // namespace eve::rpg
