#pragma once

// 属性系统：面向 RPGActor 的操作入口 + 自定义 op 注册表。
//
// 灵活性/可定制点：
//  - 属性名是任意字符串，无需预先声明。
//  - 内置 op（add / mulAdd / mulMul / override / clampMin / clampMax）覆盖常见需求；
//    通过 registerOp() 可以在 C++ 侧注册任意自定义运算（例如"取当前生命百分比的平方"），
//    脚本侧只需按约定的字符串名字引用即可，无需重新编译引擎。

#include "rpg/AttributeTypes.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace eve::rpg {

class RPGActor;

/** 自定义 op 注册表：opName -> f(currentResult, modifierValue) -> newResult。 */
class AttributeOpTable {
public:
    using Fn = std::function<double(double current, double value)>;

    void registerOp(const std::string &name, Fn fn);
    void unregisterOp(const std::string &name);
    bool has(const std::string &name) const;
    double apply(const std::string &name, double current, double value) const;

private:
    std::unordered_map<std::string, Fn> ops_;
};

/**
 * 面向 RPGActor 的属性操作静态入口。数据本身存放在 RPGActor::Attributes 组件里，
 * 这里只是围绕组件的一组无状态操作函数（+ 一张全局自定义 op 表）。
 */
class AttributeSystem {
public:
    /** 全局自定义 op 注册表（进程生命周期内有效）。 */
    static AttributeOpTable &customOps();

    /** 设置基础值（不存在则创建该属性）。 */
    static void setBase(RPGActor *actor, const std::string &attribute, double value);
    static double getBase(RPGActor *actor, const std::string &attribute);

    /** 增量修改基础值（如自然回蓝、升级加点）。 */
    static void modifyBase(RPGActor *actor, const std::string &attribute, double delta);

    static bool hasAttribute(RPGActor *actor, const std::string &attribute);

    /**
     * 添加一条修改器，返回自动生成的唯一 id（用于之后精确移除）。
     * 空字符串 id 表示失败（actor 为空）。
     */
    static std::string addModifier(RPGActor *actor, const std::string &attribute,
                                    const std::string &source, const std::string &op,
                                    double value, int priority = 0);

    /** 按 id 精确移除；返回是否命中。 */
    static bool removeModifier(RPGActor *actor, const std::string &attribute,
                                const std::string &modifierId);

    /** 按来源标签批量移除某个属性上的修改器；返回移除数量。 */
    static int removeModifiersBySource(RPGActor *actor, const std::string &attribute,
                                        const std::string &source);

    /** 按来源标签批量移除该 actor 所有属性上的修改器；返回移除数量。 */
    static int removeAllModifiersBySource(RPGActor *actor, const std::string &source);

    /** 计算并返回最终值（内部带缓存，属性未变化时不会重复计算）。 */
    static double getFinal(RPGActor *actor, const std::string &attribute);

    /** 标记某个属性需要在下次 getFinal 时重新计算（内部使用；一般不需要手动调用）。 */
    static void invalidate(RPGActor *actor, const std::string &attribute);

private:
    static std::string nextModifierId();
};

}  // namespace eve::rpg
