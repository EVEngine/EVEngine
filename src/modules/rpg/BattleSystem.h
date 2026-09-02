#pragma once

/**
 * @file BattleSystem.h
 * @brief 伤害公式 DSL 与技能伤害注册。
 *
 * 提供 RPG Maker 式伤害公式求值器（如 "a.atk * 4 - b.def * 2"，`a`=攻击方、
 * `b`=目标、参数名取 actor 的最终属性），并按技能 id 注册伤害规格（类型/公式/
 * 元素/暴击/命中）。暴击与元素耐性通过 TraitSystem 查询。
 */

#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** @brief 一次技能命中的伤害规格。 */
struct SkillDamageSpec {
    /** @brief "hp" | "hpHeal" | "mp" | "mpHeal"。 */
    std::string damageType = "hp";
    /** @brief 公式字符串，如 "a.atk * 4 - b.def * 2"；空则对 hp 用默认物理公式。 */
    std::string formula;
    /** @brief 元素（"" = 无）；伤害乘目标对应元素耐性。 */
    std::string element;
    double critChance = 0.0;  ///< 追加到攻击方 critRate 的暴击率
    int hitChance = 100;      ///< 命中率（百分比）
};

/** @brief 一次命中结算结果。 */
struct DamageResult {
    double amount = 0.0;
    bool crit = false;
    bool hit = true;
    double elementRate = 1.0;
};

/**
 * @brief 战斗数值：公式求值 + 技能伤害注册 + 结算。
 * @thread 纯计算，可在任意线程执行。
 */
class BattleSystem {
public:
    /**
     * @brief Validate and publish one skill damage definition atomically.
     * @param skillId Non-empty definition id.
     * @param spec Candidate definition; a non-empty formula must satisfy the formula grammar.
     * @return Success after publication, or a structured diagnostic without mutating the registry.
     * @thread Call on the owning RPG definition thread; registry mutation is not synchronized.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] static eve::Result<void> registerSkillDamageChecked(const std::string &skillId,
                                                                      const SkillDamageSpec &spec);
    /** @brief Compatibility facade; invalid definitions are ignored. */
    static void registerSkillDamage(const std::string &skillId, const SkillDamageSpec &spec);
    static const SkillDamageSpec *findSkillDamage(const std::string &skillId);
    static void clearSkillDamage();

    /**
     * @brief 求值伤害公式。
     * @return 公式结果；未知参数/语法按 0 处理（不抛）。
     */
    static double evaluateFormula(const std::string &formula, RPGActor *attacker, RPGActor *target);

    /**
     * @brief Strictly parse and evaluate a damage formula.
     * @return The finite result, or ParseError for malformed input and InvalidArgument for division by zero.
     * @thread Pure synchronous evaluation; actor reads must obey the caller's simulation-thread contract.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] static eve::Result<double> evaluateFormulaChecked(const std::string &formula,
                                                                     RPGActor *attacker, RPGActor *target);

    /**
     * @brief 结算一次命中：命中判定、公式、元素耐性、暴击。
     * @param rng 注入的随机数流（命中/暴击判定用）。
     */
    static DamageResult resolveHit(RPGActor *attacker, RPGActor *target,
                                   const SkillDamageSpec &spec, unsigned seed);
};

}  // namespace eve::rpg
