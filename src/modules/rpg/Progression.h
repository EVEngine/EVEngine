#pragma once

#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** @brief 一次升级事件。 */
struct LevelUpEvent {
    RPGActor* actor         = nullptr;
    int       oldLevel      = 1;
    int       newLevel      = 1;
    int       pointsGranted = 0;
};

/** @brief 等级/经验成长规则与运行时操作。 */
class ProgressionSystem {
public:
    /** @brief 初始化并重置角色成长运行时。 */
    static void configure(RPGActor* actor, int level, int maxLevel, double baseXp, double growth, int pointsPerLevel);
    /** @brief 增加经验并连续升级。 @return 本次提升的等级数。 */
    static int gainExperience(RPGActor* actor, double amount);
    /** @brief 返回当前等级升到下一级所需经验；满级返回 0。 */
    static double experienceForNextLevel(RPGActor* actor);
    /** @brief 消费未分配点数，余额不足时不修改并返回 false。 */
    static bool spendPoints(RPGActor* actor, int amount);
    /** @brief 移出并清空升级事件。 */
    static void pollEvents(std::vector<LevelUpEvent>& out);
};

}  // namespace eve::rpg
