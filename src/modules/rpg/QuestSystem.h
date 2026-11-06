#pragma once

/**
 * @file QuestSystem.h
 * @brief 目标追踪器的状态机与匹配行为。
 *
 * Tracker 的写操作薄转发到这里；事件入队到 Tracker 自己的队列（pending）。
 * 进度由 notify 推进；限时/日刷由 applyClock / refreshByKey 显式喂入外部时钟与日历，
 * 不接入 rpg.update(dt)。
 */

#include "rpg/QuestTypes.h"

#include <string>
#include <vector>

#include "common/Export.h"

namespace eve::rpg {

class Tracker;
struct QuestDefinition;

/** @brief EVENGINE_API_PLATFORM public API. */
class EVENGINE_API_PLATFORM QuestSystem {
public:
    /** @brief 补建 Registry 里尚不在 Tracker 上的条目，并解开前置已齐的 locked 条目。 */
    static void syncAuto(Tracker *t);

    /** @brief def 的全部 requires 在本 Tracker 上均已 completed（谓词查询）。 */
    static bool hasMetRequirements(const Tracker &t, const QuestDefinition &def);

    /** @brief 解锁后的落点：startPolicy==auto → "active"，否则 "inactive"。 */
    static std::string stateForUnlocked(const QuestDefinition &def);

    /** @brief 激活：仅 inactive 且前置已齐时成功；否则 push reject 并返回 false（compatibility facade (脚本兼容门面)）。 */
    static bool activate(Tracker *t, const std::string &id, std::string *reason = nullptr);
    /** @brief 是否可激活（谓词查询，可返回 reason）。 */
    static bool canActivate(Tracker *t, const std::string &id, std::string *reason = nullptr);
    /** @brief 不可激活的原因（"" 可激活，否则 unknown/locked/alreadyActive/alreadyCompleted/failed/ready）。 */
    static std::string canActivateReason(Tracker *t, const std::string &id);

    /** @brief 报告事实 (topic, target, amount)；只推进 state==active 的条目。 */
    static void notify(Tracker *t, const std::string &topic, const std::string &target, int amount);

    /** @brief 仅 state==ready 时成功 → completed 并触发解锁；否则 reject（compatibility facade (脚本兼容门面)）。 */
    static bool claim(Tracker *t, const std::string &id, std::string *reason = nullptr);

    /** @brief 进度清零并按当前 requires 回落 locked/inactive/active；不级联（compatibility facade (脚本兼容门面)）。 */
    static bool reset(Tracker *t, const std::string &id);
    /** @brief abandon/fail → failed；对 completed/failed/locked/未知 id 为 reject（compatibility facade (脚本兼容门面)）。 */
    static bool abandon(Tracker *t, const std::string &id);
    /** @brief Fail. */
    static bool fail(Tracker *t, const std::string &id, const std::string &reason);

    /**
     * @brief 按定义的 objectivePolicy / optional 判定条目目标是否已满足完成条件。
     * @remarks 空目标列表视为已满足。unknown id / 无定义时回退为「全部 ObjectiveRuntime.done」。
     */
    static bool isObjectivesSatisfied(const Tracker &t, const std::string &id);

    /**
     * @brief 将 Tracker 状态映射为对话相位后缀：offer|active|turnin|completed|failed。
     * @remarks locked/inactive → offer；active → active；ready → turnin；其余原样。
     * 未知 id 返回空串。与 rpg-classic quest_npc 的 dialogueBase.phase 约定一致。
     */
    static std::string dialoguePhase(const Tracker &t, const std::string &questId);

    /**
     * @brief 拼接 dialogueBase + "." + dialoguePhase；相位或 base 为空时返回空串。
     */
    static std::string dialogueConversationId(const Tracker &t, const std::string &questId,
                                              const std::string &dialogueBase);

    /**
     * @brief 武装未设 deadline 的限时条目，并 fail 已过期的 active/ready（reason=expired）。
     * @param nowSeconds 游戏提供的单调或墙钟秒；与 deadlineAt 同一时间轴。
     * @return 因过期而失败的条目数。
     * @cost Low — linear in Tracker entry count; call on a coarse cadence (seconds), not every frame.
     */
    static int applyClock(Tracker *t, double nowSeconds);

    /**
     * @brief 对 refreshKey 匹配的条目，若 stamp 与 refreshStamp 不同则 reset 并写入新 stamp。
     * @return 实际 reset 的条目数。
     * @cost Low — linear in Tracker entry count per calendar-key change.
     */
    static int refreshByKey(Tracker *t, const std::string &refreshKey, const std::string &stamp);

    /** @brief 把 Tracker 的 pending 移动并清空到 out。 */
    static void pollEvents(Tracker *t, std::vector<QuestEvent> &out);
};

}  // namespace eve::rpg
