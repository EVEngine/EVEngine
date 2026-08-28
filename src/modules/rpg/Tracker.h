#pragma once

/**
 * @file Tracker.h
 * @brief 目标追踪器运行时实例（玩家日志 / 成就 / 教程可各建一份）。
 *
 * 不是 ECS 实体。定义见 QuestRegistry，进度与事件在本对象上。
 * 事件队列 poll 风格：pending 实时入队，pollEvents() 拷到 polled 并读。
 */

#include "rpg/QuestTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

class QuestSystem;

/** @brief 一份任务进度 + 本实例事件队列。 */
class Tracker {
public:
    Tracker();
    ~Tracker() = default;

    /** @brief 补建 Registry 里尚未出现的条目并按前置解锁。 */
    void syncAuto();

    bool activate(const std::string &id);
    bool canActivate(const std::string &id);
    std::string canActivateReason(const std::string &id);
    void notify(const std::string &topic, const std::string &target, int amount);
    bool claim(const std::string &id);
    bool reset(const std::string &id);
    bool abandon(const std::string &id);
    bool fail(const std::string &id, const std::string &reason);

    /** @brief 把 pending 移到 polled 并清空 pending；随后 getEvent* 读 polled。 */
    void pollEvents();

    int getCount() const;
    std::string getId(int index) const;
    std::string getState(const std::string &id) const;
    bool hasTag(const std::string &id, const std::string &tag) const;
    std::string getExtra(const std::string &id, const std::string &key) const;
    int getObjectiveCount(const std::string &id) const;
    std::string getObjectiveId(const std::string &id, int index) const;
    int getObjectiveCurrent(const std::string &id, int index) const;
    int getObjectiveCountRequired(const std::string &id, int index) const;
    bool isObjectiveDone(const std::string &id, int index) const;
    int getRewardCount(const std::string &id) const;
    std::string getRewardType(const std::string &id, int i) const;
    std::string getRewardId(const std::string &id, int i) const;
    double getRewardAmount(const std::string &id, int i) const;
    int getEventCount() const;
    std::string getEventEntryId(int i) const;
    std::string getEventObjectiveId(int i) const;
    std::string getEventAction(int i) const;
    std::string getEventTopic(int i) const;
    std::string getEventTarget(int i) const;
    int getEventAmount(int i) const;
    std::string getEventReason(int i) const;

    /** @brief 运行时条目（QuestSystem 读写；公开以便测试与脚本查询）。 */
    std::unordered_map<std::string, QuestRuntime> entries;
    std::vector<std::string>                      order;
    std::vector<QuestEvent>                       pending;
    std::vector<QuestEvent>                       polled;
};

}  // namespace eve::rpg