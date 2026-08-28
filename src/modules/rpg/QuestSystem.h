#pragma once

/**
 * @file QuestSystem.h
 * @brief 目标追踪器的状态机与匹配行为。
 *
 * Tracker 的写操作薄转发到这里；事件入队到 Tracker 自己的队列（pending）。
 * 本系统无 ECS 依赖、无 rpg.update(dt) 驱动——进度只由 notify 推进。
 */

#include "rpg/QuestTypes.h"

#include <string>
#include <vector>

namespace eve::rpg {

class Tracker;
struct QuestDefinition;

class QuestSystem {
public:
    /** @brief 补建 Registry 里尚不在 Tracker 上的条目，并解开前置已齐的 locked 条目。 */
    static void syncAuto(Tracker *t);

    /** @brief def 的全部 requires 在本 Tracker 上均已 completed。 */
    static bool requiresMet(const Tracker &t, const QuestDefinition &def);

    /** @brief 解锁后的落点：startPolicy==auto → "active"，否则 "inactive"。 */
    static std::string stateForUnlocked(const QuestDefinition &def);

    /** @brief 激活：仅 inactive 且前置已齐时成功；否则 push reject 并返回 false。 */
    static bool activate(Tracker *t, const std::string &id, std::string *reason = nullptr);
    static bool canActivate(Tracker *t, const std::string &id, std::string *reason = nullptr);
    /** @brief "" 可激活；否则 unknown/locked/alreadyActive/alreadyCompleted/failed/ready。 */
    static std::string canActivateReason(Tracker *t, const std::string &id);

    /** @brief 报告事实 (topic, target, amount)；只推进 state==active 的条目。 */
    static void notify(Tracker *t, const std::string &topic, const std::string &target, int amount);

    /** @brief 仅 state==ready 时成功 → completed 并触发解锁；否则 reject。 */
    static bool claim(Tracker *t, const std::string &id, std::string *reason = nullptr);

    /** @brief 进度清零并按当前 requires 回落 locked/inactive/active；不级联。 */
    static bool reset(Tracker *t, const std::string &id);
    /** @brief abandon/fail → failed；对 completed/failed/locked/未知 id 为 reject。 */
    static bool abandon(Tracker *t, const std::string &id);
    static bool fail(Tracker *t, const std::string &id, const std::string &reason);

    /** @brief 把 Tracker 的 pending 移动并清空到 out。 */
    static void pollEvents(Tracker *t, std::vector<QuestEvent> &out);
};

}  // namespace eve::rpg