#pragma once

/**
 * @file QuestTypes.h
 * @brief RPG 目标追踪器（任务/成就/教程）的纯数据类型。
 *
 * 与 Effect/Skill 同构：定义是进程级模板（QuestDefinition），运行时进度由
 * 独立 Tracker 对象持有。目标匹配只比较字符串 (topic, target) 并计数，
 * 引擎不解释击杀、拾取、对话等语义。
 */

#include <string>
#include <vector>

namespace eve::rpg {

/** @brief 一条目标：听哪个 notify、要多少次。target 空 = 只匹配 topic。 */
struct QuestObjective {
    std::string id;
    std::string topic;
    std::string target;
    int count = 1;  ///< <=0 按 1 处理
    /**
     * @brief 可选目标：计入进度与 UI，但不参与完成判定（objectivePolicy=all/ordered）。
     * @remarks objectivePolicy=any 时，可选目标也不算作「任一完成」的候选；仅非 optional 计入。
     */
    bool optional = false;
};

/** @brief 奖励规格（定义侧数据）。引擎不发奖，仅提供查询。 */
struct RewardSpec {
    std::string type;
    std::string id;
    double amount = 0.0;
};

/** @brief 单条目标的运行时进度。 */
struct ObjectiveRuntime {
    std::string id;
    int current = 0;
    int count = 1;
    bool done = false;
};

/** @brief 一条任务在本 Tracker 上的运行时状态。 */
struct QuestRuntime {
    std::string id;
    /** @brief locked | inactive | active | ready | completed | failed */
    std::string state;
    std::vector<ObjectiveRuntime> objectives;
    /**
     * @brief 绝对截止时刻（与 Tracker::applyClock 使用同一时间轴）；0 表示未武装或无限时。
     * @remarks 进入 active 后由 applyClock 按定义 timeLimitSeconds 武装；过期 fail 的 reason 为 expired。
     */
    double deadlineAt = 0.0;
    /** @brief 最近一次 refreshByKey 写入的 stamp；与定义 refreshKey 配对做日刷/赛季刷。 */
    std::string refreshStamp;
};

/**
 * @brief 追踪器事件：activate | progress | ready | complete | fail | reset | reject。
 * 无关字段为空 / 0。
 */
struct QuestEvent {
    std::string entryId;
    std::string objectiveId;
    std::string action;
    std::string topic;
    std::string target;
    int amount = 0;
    std::string reason;
};

}  // namespace eve::rpg