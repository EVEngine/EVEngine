#pragma once

#include "rpg/QuestTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

class QuestSystem;
class StateCodec;

/** @brief 一名玩家/存档独立持有的任务追踪器。 */
class Tracker {
public:
    /** @brief 按当前 QuestRegistry 创建一份独立任务运行时。 */
    Tracker();
    /** @brief 释放脚本创建的追踪器。 */
    void destroy() { delete this; }

    /** @brief 同步注册表中新加入的定义并处理自动解锁。 */
    void syncAuto();
    /** @brief 手动激活任务。 */
    bool activate(const std::string& id);
    /** @brief 查询任务当前是否允许激活。 */
    bool canActivate(const std::string& id) const;
    /** @brief 返回不能激活的稳定原因码。 */
    std::string canActivateReason(const std::string& id) const;
    /** @brief 广播一条 topic/target 计数通知。 */
    void notify(const std::string& topic, const std::string& target, int amount);
    /** @brief 领取 ready 任务。 */
    bool claim(const std::string& id);
    /** @brief 重置指定任务。 */
    bool reset(const std::string& id);
    /** @brief 以 abandoned 原因放弃任务。 */
    bool abandon(const std::string& id);
    /** @brief 以指定原因令任务失败。 */
    bool fail(const std::string& id, const std::string& reason);

    /** @brief 返回追踪条目数量。 */
    int getCount() const;
    /** @brief 返回按稳定顺序排列的任务 id。 */
    std::string getId(int index) const;
    /** @brief 返回任务状态字符串。 */
    std::string getState(const std::string& id) const;
    /** @brief 查询任务定义标签。 */
    bool hasTag(const std::string& id, const std::string& tag) const;
    /** @brief 查询任务定义扩展字段。 */
    std::string getExtra(const std::string& id, const std::string& key) const;
    /** @brief 返回任务目标数。 */
    int getObjectiveCount(const std::string& id) const;
    /** @brief 返回指定目标 id。 */
    std::string getObjectiveId(const std::string& id, int index) const;
    /** @brief 返回指定目标当前进度。 */
    int getObjectiveCurrent(const std::string& id, int index) const;
    /** @brief 返回指定目标所需总量。 */
    int getObjectiveCountRequired(const std::string& id, int index) const;
    /** @brief 查询指定目标是否完成。 */
    bool isObjectiveDone(const std::string& id, int index) const;
    /** @brief 返回任务奖励描述数量。 */
    int getRewardCount(const std::string& id) const;
    /** @brief 返回奖励类型。 */
    std::string getRewardType(const std::string& id, int index) const;
    /** @brief 返回奖励内容 id。 */
    std::string getRewardId(const std::string& id, int index) const;
    /** @brief 返回奖励数量。 */
    float getRewardAmount(const std::string& id, int index) const;

    /** @brief 将待处理事件移动到脚本可读快照。 */
    void pollEvents();
    /** @brief 返回当前事件快照数量。 */
    int getEventCount() const;
    /** @brief 返回事件所属任务 id。 */
    std::string getEventEntryId(int index) const;
    /** @brief 返回事件所属目标 id。 */
    std::string getEventObjectiveId(int index) const;
    /** @brief 返回事件动作。 */
    std::string getEventAction(int index) const;
    /** @brief 返回事件 topic。 */
    std::string getEventTopic(int index) const;
    /** @brief 返回事件 target。 */
    std::string getEventTarget(int index) const;
    /** @brief 返回失败或拒绝原因。 */
    std::string getEventReason(int index) const;
    /** @brief 返回本次增加的计数。 */
    int getEventAmount(int index) const;

private:
    friend class QuestSystem;
    friend class StateCodec;
    std::unordered_map<std::string, QuestRuntime> entries_;
    std::vector<std::string>                      order_;
    std::vector<QuestEvent>                       pending_;
    std::vector<QuestEvent>                       polled_;
};

}  // namespace eve::rpg
