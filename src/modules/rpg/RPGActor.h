#pragma once

/**
 * @brief RPG 模块的基础实体：把属性 / 状态 / 技能三张表挂到一个 ECS 实体上。
 *
 * 与引擎其他模块一致（参考 particles/ParticleEmitter、map/TileLayer），
 * 组件是纯数据结构体，行为都放在对应的 *System 静态类里。
 *
 * 可扩展性：游戏可以直接使用 RPGActor，也可以仿照
 * docs/游戏模型设计.md 的示例，从 RPGActor 派生出自己的实体类型
 * （例如 Player : public RPGActor / Monster : public RPGActor），
 * 在子类上追加自己的组件，同时天然复用属性/状态/技能三大组件与配套 System
 * （View<RPGActor, RPGActor::Attributes> 等能看到所有派生实体）。
 */

#include "common/ECS.h"
#include "rpg/AttributeTypes.h"
#include "rpg/StatusTypes.h"
#include "rpg/SkillTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief 属性 / 状态 / 技能三表合一的 ECS 实体。 */
class RPGActor : public ecs::Entity {
public:
    ENTITY(RPGActor, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    /** @brief 属性表：任意字符串 -> AttributeValue（base + modifiers）。 */
    struct Attributes {
        std::unordered_map<std::string, AttributeValue> values;
    };

    /** @brief 状态（buff/debuff）运行时列表 —— ECS 组件，游戏侧常称 Buffs。 */
    struct Statuses {
        std::vector<StatusInstance> active;
        int nextInstanceId = 1;
    };

    /** @brief 已学会技能的冷却状态 + 当前读条状态。 */
    struct Skills {
        std::unordered_map<std::string, SkillRuntime> known;
        CastingState casting;
    };

    COMPONENT(Attributes, attributes)
    COMPONENT(Statuses, statuses)
    COMPONENT(Skills, skills)

    /** @brief 创建一个空白 actor（三张表均为空，按需惰性写入），并纳入 liveActors() 跟踪。 */
    static RPGActor *createActor();

    /**
     * @brief 便捷成员方法（转发到 AttributeSystem / StatusSystem / SkillSystem）。
     * 提供这一层薄封装主要是为了让脚本可以写 `actor.setBaseAttribute(...)`，
     * 与引擎其他模块（TileLayer / ParticleEmitter 等）的绑定风格保持一致；
     * C++ 侧仍然可以直接使用对应的 *System 静态类。
     */

    /** @brief 属性访问。 */
    void setBaseAttribute(const std::string &attribute, double value);
    double getBaseAttribute(const std::string &attribute);
    void modifyBaseAttribute(const std::string &attribute, double delta);
    bool hasAttribute(const std::string &attribute);
    std::string addAttributeModifier(const std::string &attribute, const std::string &source,
                                      const std::string &op, double value, int priority = 0);
    bool removeAttributeModifier(const std::string &attribute, const std::string &modifierId);
    int removeAttributeModifiersBySource(const std::string &attribute, const std::string &source);
    int removeAllAttributeModifiersBySource(const std::string &source);
    double getFinalAttribute(const std::string &attribute);

    /** @brief 状态（buff/debuff）—— Statuses ECS 组件的薄封装。 */
    int applyEffect(const std::string &effectId, const std::string &source = "");
    bool removeStatus(int instanceId);
    int removeStatusByEffect(const std::string &effectId);
    int removeStatusBySource(const std::string &source);
    int removeStatusByTag(const std::string &tag);
    bool hasEffect(const std::string &effectId);
    bool hasStatusTag(const std::string &tag);
    int getStatusCount();
    std::string getStatusEffectId(int index);
    int getStatusStacks(int index);
    float getStatusRemaining(int index);
    int getStatusInstanceId(int index);
    std::string getStatusSource(int index);
    std::string getStatusProp(int instanceId, const std::string &key,
                              const std::string &fallback = {});
    bool setStatusProp(int instanceId, const std::string &key, const std::string &value);

    /** @brief Buff 别名（与 Status API 等价，便于游戏侧按习惯命名）。 */
    int applyBuff(const std::string &effectId, const std::string &source = "");
    bool removeBuff(int instanceId);
    int removeBuffByEffect(const std::string &effectId);
    int removeBuffBySource(const std::string &source);
    int removeBuffByTag(const std::string &tag);
    bool hasBuff(const std::string &effectId);
    bool hasBuffTag(const std::string &tag);
    int getBuffCount();
    std::string getBuffEffectId(int index);
    int getBuffStacks(int index);
    float getBuffRemaining(int index);
    int getBuffInstanceId(int index);
    std::string getBuffSource(int index);
    std::string getBuffProp(int instanceId, const std::string &key,
                            const std::string &fallback = {});
    bool setBuffProp(int instanceId, const std::string &key, const std::string &value);

    /** @brief 技能：学习 / 查询 / 冷却。 */
    void learnSkill(const std::string &skillId);
    bool knowsSkill(const std::string &skillId);
    bool forgetSkill(const std::string &skillId);
    float getSkillCooldown(const std::string &skillId);
    void setSkillCooldown(const std::string &skillId, float seconds);
    bool canCastSkill(const std::string &skillId);
    /** @brief 不能释放的原因（"" = 可以）。 */
    std::string canCastSkillReason(const std::string &skillId);
    /** @brief 开始读条释放；false 表示不满足条件。 */
    bool beginCastSkill(const std::string &skillId, RPGActor *target = nullptr);
    /** @brief 取消读条。 */
    void cancelCastSkill();
    /** @brief 读条状态查询。 */
    bool isCastingSkill();
    std::string getCastingSkillId();
    float getCastProgress();

    /**
     * @brief 返回所有通过 createActor() 创建、且当前仍存活的 actor。
     * StatusSystem::update / SkillSystem::update 用它遍历所有 actor 逐帧推进。
     * 直接调用继承自 ENTITY 宏的 RPGActor::create() 不会被跟踪——推荐总是用 createActor()。
     *
     * 实现细节：内部按 ecs::EntityHandle（而非裸指针）跟踪，每次调用通过
     * ecs::try_get 按 (id, generation) 校验是否仍是同一个存活实体——
     * 这样当一个 actor 被销毁、其 id 槽位被新 actor 复用时，不会把新实体
     * 误判为旧的已销毁实体（裸指针 + is_entity_visible 无法区分这种复用）。
     */
    static const std::vector<RPGActor *> &liveActors();
};

}  // namespace eve::rpg
