#pragma once

#include <string>

namespace eve::rpg {

class Tracker;
struct QuestDefinition;
struct QuestRuntime;

/** @brief 通用任务状态机与 topic/target 事件计数器。 */
class QuestSystem {
public:
    /** @brief 补建新定义并解锁满足前置条件的自动任务。 */
    static void syncAuto(Tracker& tracker);
    /** @brief 手动激活 inactive 任务。 */
    static bool activate(Tracker& tracker, const std::string& id);
    /** @brief 返回激活失败原因；空字符串表示可以激活。 */
    static std::string canActivateReason(const Tracker& tracker, const std::string& id);
    /** @brief 向全部活跃任务广播一个计数事件。 */
    static void notify(Tracker& tracker, const std::string& topic, const std::string& target, int amount);
    /** @brief 领取 ready 任务并进入 completed。 */
    static bool claim(Tracker& tracker, const std::string& id);
    /** @brief 清空任务进度并按当前前置条件重置状态。 */
    static bool reset(Tracker& tracker, const std::string& id);
    /** @brief 将可失败状态转为 failed 并记录原因。 */
    static bool fail(Tracker& tracker, const std::string& id, const std::string& reason);

private:
    static void emit(Tracker& tracker, const std::string& id, const std::string& action,
                     const std::string& objective = {}, const std::string& topic = {}, const std::string& target = {},
                     int amount = 0, const std::string& reason = {});
    static bool requiresMet(const Tracker& tracker, const QuestDefinition& definition);
    static void createRuntime(Tracker& tracker, const QuestDefinition& definition);
    static void finishIfReady(Tracker& tracker, QuestRuntime& runtime, const QuestDefinition& definition);
    static void unlock(Tracker& tracker);
};

}  // namespace eve::rpg
