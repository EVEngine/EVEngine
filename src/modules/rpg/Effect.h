#pragma once

// 效果系统：数据驱动的 Effect "定义/模板"（类似 GAS 的 GameplayEffect）。
//
// 一个 EffectDefinition 描述"施加后会发生什么"，本身不含运行时状态；
// 施加到具体 actor 后由 StatusSystem 产生 StatusInstance（见 StatusTypes.h）。
//
// 灵活性/可定制点：
//  - 完全数据驱动：C++ 侧 registerEffect() 或脚本侧 loadFromJson() 均可定义新效果，
//    无需修改引擎代码；effect id、属性名、tag、op 全部是字符串。
//  - durationPolicy / stackPolicy 同样是字符串，方便未来扩展新策略而不破坏 ABI。
//  - period > 0 的周期效果不会自动改属性，而是产生 tick 事件交给上层处理，
//    从而可以让周期伤害/治疗完整走 Settlement 结算流水线（见 Settlement.h）。

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** 效果里携带的一条属性修改规格（应用到 AttributeSystem::addModifier）。 */
struct EffectModifierSpec {
    std::string attribute;
    std::string op = "add";
    double value = 0.0;
    int priority = 0;
};

struct EffectDefinition {
    std::string id;

    /** "instant" | "duration" | "infinite"（未知值按 "instant" 处理）。 */
    std::string durationPolicy = "instant";
    float duration = 0.f;  ///< durationPolicy == "duration" 时使用（秒）

    /** > 0 时为周期效果：每隔 period 秒产生一次 StatusTickEvent。 */
    float period = 0.f;

    /** "none" | "refresh" | "extend" | "stack"（未知值按 "none" 处理）。 */
    std::string stackPolicy = "none";
    int maxStacks = 1;

    /** durationPolicy 为 duration/infinite 且 period<=0 时，apply 会直接写入这些修改器。 */
    std::vector<EffectModifierSpec> modifiers;

    std::vector<std::string> tags;

    bool hasTag(const std::string &tag) const;
};

/** 全局效果定义表：进程级单例，供任意模块 / 脚本按 id 引用。 */
class EffectRegistry {
public:
    static void registerEffect(const EffectDefinition &def);
    static const EffectDefinition *find(const std::string &id);
    static bool remove(const std::string &id);
    static void clear();
    static int count();

    /**
     * 从 JSON 数组批量注册效果定义，返回成功注册的数量。
     * 每个元素形如：
     * {
     *   "id": "poison", "durationPolicy": "duration", "duration": 5,
     *   "period": 1, "stackPolicy": "stack", "maxStacks": 5,
     *   "modifiers": [{"attribute":"health","op":"add","value":-5}],
     *   "tags": ["debuff", "poison"]
     * }
     */
    static int loadFromJson(const std::string &json, std::string *error = nullptr);

private:
    static std::unordered_map<std::string, EffectDefinition> &table();
};

}  // namespace eve::rpg
