#pragma once

// 结算系统：把伤害/治疗/命中率/掉落……等一切"多方参与、需要按顺序层层计算"的
// 数值流程抽象成一条可插拔的流水线（SettlementPipeline），而不是把某一种
// 伤害公式写死在引擎里。
//
// 用法：
//   1. C++ 侧（游戏或插件代码）通过 registerStage() 往某个流水线（用字符串命名，
//      如 "damage"/"heal"，或游戏自定义的任意名字）里插入若干计算阶段；
//      每个阶段是 std::function<void(SettlementContext&)>，按 priority 升序执行。
//   2. 触发时（例如技能命中、周期性 DOT tick）构造一个 SettlementContext，
//      塞入 source/target/values/tags，调用 SettlementPipeline::run(name, ctx)；
//      各阶段依次读写 ctx.values，其中某一步可以设置 ctx.cancelled=true 提前终止
//      （例如"闪避判定"阶段判定闪避成功后取消后续所有伤害计算）。
//   3. 脚本侧虽然不能注册新的原生阶段（std::function 无法跨越 C++/Squirrel），
//      但可以：a) 直接读写 SettlementContext 的字段驱动纯脚本逻辑；
//              b) 通过 setStageEnabled / setStagePriority 开关或重排已注册的
//                 阶段，实现"选用哪些内建规则、以什么顺序生效"的定制。
//
// 这套机制与 AttributeSystem 的自定义 op、SkillSystem 的 cast condition 是同一种
// "C++ 提供可插拔扩展点，字符串作为跨语言、跨版本稳定的接口"的设计语言。

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** 结算上下文：通用的读写数据袋，字段完全由调用方与各阶段自行约定。 */
struct SettlementContext {
    RPGActor *source = nullptr;
    RPGActor *target = nullptr;
    std::string kind;  ///< 自由字符串，如 "damage" / "heal"，供阶段内部分支使用
    bool cancelled = false;

    std::unordered_map<std::string, double> values;
    std::vector<std::string> tags;

    double get(const std::string &key, double fallback = 0.0) const;
    void set(const std::string &key, double value);
    bool has(const std::string &key) const;

    void addTag(const std::string &tag);
    bool hasTag(const std::string &tag) const;
};

class SettlementPipeline {
public:
    using Stage = std::function<void(SettlementContext &)>;

    /** 同名 stage 已存在则整体覆盖（fn/priority），并重置为 enabled。 */
    static void registerStage(const std::string &pipeline, const std::string &stage, int priority,
                               Stage fn);
    static bool unregisterStage(const std::string &pipeline, const std::string &stage);
    static bool setStageEnabled(const std::string &pipeline, const std::string &stage, bool enabled);
    static bool setStagePriority(const std::string &pipeline, const std::string &stage, int priority);
    static bool hasStage(const std::string &pipeline, const std::string &stage);
    static int stageCount(const std::string &pipeline);
    static void clearPipeline(const std::string &pipeline);

    /** 按 priority 升序依次执行 pipeline 内已启用的阶段；ctx.cancelled 时提前终止。 */
    static void run(const std::string &pipeline, SettlementContext &ctx);
};

}  // namespace eve::rpg
