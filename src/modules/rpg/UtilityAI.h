#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief Utility AI 的一个输入响应曲线。 */
struct AIConsideration {
    std::string input;
    std::string curve  = "linear";
    double      min    = 0.0;
    double      max    = 1.0;
    double      weight = 1.0;
};
/** @brief Utility AI 的一个候选动作。 */
struct AIActionDefinition {
    std::string                  id;
    double                       weight = 1.0;
    std::vector<AIConsideration> considerations;
};
/** @brief 一组可共同评分的动作定义。 */
struct AIDecisionSetDefinition {
    std::string                     id;
    std::vector<AIActionDefinition> actions;
};

/** @brief 进程级 AI 决策集注册表。 */
class AIDecisionRegistry {
public:
    /** @brief 注册或覆盖有效决策集。 */
    static bool registerSet(const AIDecisionSetDefinition& definition);
    /** @brief 按 id 查询决策集。 */
    static const AIDecisionSetDefinition* find(const std::string& id);
    /** @brief 清空决策集定义。 */
    static void clear();
    /** @brief 返回已注册决策集数量。 */
    static int count();
    /** @brief 从 JSON 对象或数组注册决策集。 */
    static int loadFromJson(const std::string& json, std::string* error = nullptr);
};

/** @brief 无世界依赖的 Utility AI 黑板；游戏负责把距离、血量等输入写入。 */
class UtilityBrain {
public:
    /** @brief 释放脚本创建的脑对象。 */
    void destroy() { delete this; }
    /** @brief 指定要使用的决策集 id。 */
    void setDecisionSet(const std::string& id) { decisionSet_ = id; }
    /** @brief 返回当前决策集 id。 */
    std::string getDecisionSet() const { return decisionSet_; }
    /** @brief 写入一个归一化前的黑板输入。 */
    void setInput(const std::string& name, float value) { inputs_[name] = value; }
    /** @brief 读取黑板输入，不存在时返回 fallback。 */
    float getInput(const std::string& name, float fallback) const;
    /** @brief 对当前决策集评分并返回最高分动作 id。 */
    std::string chooseAction();
    /** @brief 返回最近一次选择的最高效用分。 */
    float getLastScore() const { return float(lastScore_); }

private:
    std::string                             decisionSet_;
    std::unordered_map<std::string, double> inputs_;
    double                                  lastScore_ = 0.0;
};

}  // namespace eve::rpg
