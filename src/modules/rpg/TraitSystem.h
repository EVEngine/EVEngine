#pragma once

/**
 * @file TraitSystem.h
 * @brief 特征系统：施加/移除运行时特征，并提供耐性/特殊参数/攻击附加等查询。
 *
 * 特征定义见 Trait.h（TraitRegistry）。paramRate 会在施加时把倍率转为
 * 属性修改器（source="trait:<instanceId>"）落到对应参数；其它 kind 按需计算：
 * elementRate/stateRate 取倍率乘积，exParam 取加值，attackElement/attackState
 * 返回标签列表，stateResist 判定免疫。战斗/结算系统通过这些查询读取特征效果。
 */

#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/**
 * @brief 特征系统。
 * @thread 调用线程应与 RPGActor 的 ECS 线程一致。
 */
class TraitSystem {
public:
    /**
     * @brief 施加一个特征。
     * @return 适配器分配的实例 id（>0）；未知特征 / 空 actor 返回 0。
     */
    static int apply(RPGActor *actor, const std::string &traitId, const std::string &source = "");
    /** @brief 按实例 id 移除。 */
    static bool remove(RPGActor *actor, int instanceId);
    /** @brief 移除该 actor 上所有来自 source 的特征；返回移除数。 */
    static int removeBySource(RPGActor *actor, const std::string &source);
    /** @brief 移除该 actor 上所有 traitId 匹配的特征；返回移除数。 */
    static int removeByTrait(RPGActor *actor, const std::string &traitId);
    /** @brief 移除全部特征。 */
    static void removeAll(RPGActor *actor);
    /** @brief 是否已施加某特征。 */
    static bool hasTrait(RPGActor *actor, const std::string &traitId);

    static int getCount(RPGActor *actor);
    static int getInstanceIdAt(RPGActor *actor, int index);
    static std::string getTraitIdAt(RPGActor *actor, int index);
    static std::string getSourceAt(RPGActor *actor, int index);

    /** @brief 参数倍率乘积（如 attack → 1.5 表示 +50%）。 */
    static double getParamRate(RPGActor *actor, const std::string &param);
    /** @brief 元素耐性乘积（1 = 无修正，0.5 = 减半）。 */
    static double getElementRate(RPGActor *actor, const std::string &element);
    /** @brief 状态施加率乘积（1 = 正常）。 */
    static double getStateRate(RPGActor *actor, const std::string &stateId);
    /** @brief 是否免疫某状态（stateResist 命中；target 空 = 全部免疫）。 */
    static bool isStateResist(RPGActor *actor, const std::string &stateId);
    /** @brief 特殊参数加值（hit/evasion/critRate/critEvade/magicEvade/magicReflect/...）。 */
    static double getExParam(RPGActor *actor, const std::string &exParam);

    /** @brief 攻击附加元素列表。 */
    static std::vector<std::string> getAttackElements(RPGActor *actor);
    /** @brief 攻击附加状态列表。 */
    static std::vector<std::string> getAttackStates(RPGActor *actor);
    /** @brief 攻击速度修正（加值）。 */
    static double getAttackSpeed(RPGActor *actor);
    /** @brief 攻击次数附加（整数加值）。 */
    static int getAttackTimesAdd(RPGActor *actor);
};

}  // namespace eve::rpg