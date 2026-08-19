#pragma once

// 属性系统的纯数据类型 + 纯函数计算核心。
//
// 设计要点（对应"高度灵活可定制化"目标）：
// 1. 属性名不是枚举，而是任意字符串 —— 新增属性无需修改引擎代码。
// 2. 修改器（modifier）的运算方式（op）同样是字符串：内置几种常见语义，
//    同时允许通过 AttributeSystem::registerOp() 注册任意自定义运算，
//    使得游戏可以在不修改引擎源码的情况下扩展全新的数值叠加规则。
// 3. computeAttributeValue() 是不依赖 ECS 的纯函数，方便单元测试与脚本外验证。

#include <string>
#include <vector>

namespace eve::rpg {

/**
 * @brief 单条修改器。附加在某个属性上，参与最终值计算。
 *
 * op 内置语义：
 *   "add"       — 所有 add 修改器直接求和后加到 base 上
 *   "mulAdd"    — 所有 mulAdd 修改器的 value 视为百分比，求和后一次性相乘：
 *                 result *= (1 + sum(value))
 *   "mulMul"    — 每条 mulMul 修改器独立相乘：result *= (1 + value)
 *   "override"  — 直接覆盖当前结果为 value（多条时取 priority 最大的一条生效）
 *   "clampMin"  — result = max(result, value)
 *   "clampMax"  — result = min(result, value)
 * 其余字符串会在 AttributeSystem 的自定义 op 注册表中查找；找不到则忽略该修改器。
 */
struct AttributeModifier {
    std::string id;      ///< 唯一 id（由 AttributeSystem 生成），用于精确移除
    std::string source;  ///< 来源标签（如 "effect:12" / "skill:fireball" / 自定义），用于批量移除
    std::string op = "add";
    double value = 0.0;
    int priority = 0;  ///< 计算顺序，数值小的先应用；同优先级按插入顺序
};

/** @brief 单个属性的完整状态：基础值 + 修改器列表 + 计算结果缓存。 */
struct AttributeValue {
    double base = 0.0;
    std::vector<AttributeModifier> modifiers;
    mutable double cached = 0.0;
    mutable bool dirty = true;
};

/**
 * @brief 纯函数：根据 base + modifiers 计算最终值。不依赖任何 ECS / 全局状态，
 * 自定义 op 通过 customOps 传入（AttributeSystem 内部会传入全局注册表）。
 */
double computeAttributeValue(const AttributeValue &attr,
                              const struct AttributeOpTable *customOps = nullptr);

}  // namespace eve::rpg
