#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** @brief ARPG 常用伤害规则，可由项目统一调参。 */
struct CombatRules {
    double      armorConstant           = 100.0;
    double      maxResistance           = 0.75;
    double      minDamage               = 1.0;
    std::string healthAttribute         = "health";
    std::string attackAttribute         = "attack";
    std::string armorAttribute          = "armor";
    std::string critChanceAttribute     = "critChance";
    std::string critMultiplierAttribute = "critMultiplier";
};

/** @brief 一次伤害或治疗结算的不可变结果快照。 */
struct CombatEvent {
    RPGActor*   source = nullptr;
    RPGActor*   target = nullptr;
    std::string action;
    std::string damageType;
    double      amount       = 0.0;
    double      healthBefore = 0.0;
    double      healthAfter  = 0.0;
    bool        critical     = false;
    bool        killed       = false;
};

/** @brief 内建、可预测随机种子的标准 ARPG 战斗结算器。 */
class CombatSystem {
public:
    /** @brief 返回全局战斗规则，调用方可在加载内容时调参。 */
    static CombatRules& rules();
    /** @brief 重置战斗随机流，供测试、回放或服务器权威模拟使用。 */
    static void setRandomSeed(uint32_t seed);
    /** @brief 结算一次伤害并写入事件队列。 @return 最终伤害数值。 */
    static double dealDamage(RPGActor* source, RPGActor* target, double power, const std::string& damageType);
    /** @brief 结算一次治疗并写入事件队列。 @return 实际生命变化。 */
    static double heal(RPGActor* source, RPGActor* target, double amount);
    /** @brief 移出并清空尚未消费的战斗事件。 */
    static void pollEvents(std::vector<CombatEvent>& out);
};

}  // namespace eve::rpg
