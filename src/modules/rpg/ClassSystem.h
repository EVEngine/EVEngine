#pragma once

/**
 * @file ClassSystem.h
 * @brief 职业系统：设定职业、施加职业基础特征、按等级自动学习技能。
 *
 * setClass 会撤销旧职业特征（source="class:<旧id>"）并施加新职业特征，
 * 然后补学当前等级已解锁的技能。升级后调用 checkLevelSkills 学习新增技能。
 */

#include <string>

namespace eve::rpg {

class RPGActor;

/**
 * @brief 职业系统。
 * @thread 调用线程应与 RPGActor 的 ECS 线程一致。
 */
class ClassSystem {
public:
    /**
     * @brief 设定职业并施加职业基础特征。
     * @return 未知职业 id 返回 false。
     */
    static bool setClass(RPGActor *actor, const std::string &classId);
    /** @brief 当前职业 id（无则空）。 */
    static std::string getClassId(RPGActor *actor);
    /** @brief 是否已设定指定职业。 */
    static bool hasClass(RPGActor *actor, const std::string &classId);

    /**
     * @brief 按当前等级补学尚未学过的升级技能。
     * @return 本次新学到的技能数。
     */
    static int checkLevelSkills(RPGActor *actor);

    /** @brief 职业定义中可学技能总数。 */
    static int getLearnCount(RPGActor *actor);
    static std::string getLearnSkillIdAt(RPGActor *actor, int index);
    static int getLearnLevelAt(RPGActor *actor, int index);
};

}  // namespace eve::rpg