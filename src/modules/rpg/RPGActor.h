#pragma once
/** @file RPGActor.h
 * @brief rpg module-interface: canonical actor state and versioned checkpoint admission.
 * @remarks Data boundary includes cast clocks and coordinator-supplied typed target bindings. No new capability,
 * event, script or tool endpoint; existing script checkpoint facades use the same codec without a target context.
 * Subject registration is an external primary key: coordinators save it in actor records; body checkpoints
 * never clone or rename ActorIdentity.
 */
#include "common/Export.h"


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
#include "rpg/ActorExecutionOrder.h"
#include "rpg/ActorIdentity.h"
#include "rpg/AttributeTypes.h"
#include "rpg/SkillTypes.h"
#include "rpg/StatusTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::rpg {
class SkillConditionBindings;

class PreparedMemberActor;
class SkillContent;
class RPGSaveSession;
class Party;

/** @brief 属性 / 状态 / 技能三表合一的 ECS 实体。 */
class EVENGINE_API_PLATFORM RPGActor : public ecs::Entity {
public:
    /** @brief Entity. */
    ENTITY(RPGActor, ecs::Entity)

    /** @brief Release. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief 属性组件：AttributeSet 是属性状态与计算的唯一权威真源。 */
    struct Attributes {
        /** @brief Constructs a Attributes. */
        Attributes() : values(std::string{}) {}

        ::eve::attributes::AttributeSet values;
    };

    /**
     * @brief Status lifecycle component backed by the generic effects container.
     *
     * `container` is the only owner of active effect instances. The integer
     * maps and executor metadata are adapter state for legacy RPG projections;
     * they are keyed by the canonical string instance id and are copied with
     * the ECS component for staging/copy operations.
     */
    struct Statuses {
        ::eve::effects::EffectContainer                         container;
        std::unordered_map<std::string, StatusExecutorMetadata> metadata;
        std::unordered_map<std::string, int>                    legacyIdByEffect;
        std::unordered_map<int, std::string>                    effectByLegacyId;
        int nextInstanceId = 1;
    };

    /** @brief 已学会技能的冷却状态 + 当前读条状态。 */
    struct Skills {
        std::unordered_map<std::string, SkillRuntime> known;
        CastingState casting;
        /** @brief Actual immutable condition executor owner, shared by casting and spawn archive capture.
         * @remarks Process-local provider; never serialized as code. Restore must explicitly rebind matching revisions.
         */
        std::shared_ptr<const SkillConditionBindings> conditionBindings{};
    };

    /** @brief 成长进度：等级 / 经验 / 升级所需经验。 */
    struct Progression {
        int level = 1;
        double xp = 0.0;
        double xpToNext = 100.0;  ///< 当前等级的升级阈值
    };

    /**
     * @brief 当前资源（hp / mana ...）：current 是运行时瞬时值，上限取同名最终属性。
     * max 由 attributes 推导（getFinalAttribute(resource)），本组件只存 current。
     */
    struct Vitals {
        std::unordered_map<std::string, double> current;
    };

    /** @brief 已施加的运行时特征实例。 */
    struct TraitInstance {
        int instanceId = 0;
        std::string traitId;
        std::string source;
    };

    /** @brief 该 actor 的活动特征列表（TraitSystem 读写）。 */
    struct Traits {
        std::vector<TraitInstance> active;
        int nextInstanceId = 1;
    };

    /** @brief 职业：当前职业 id + 已同步学技能的等级上限。 */
    struct ClassInfo {
        std::string classId;
        int skillsSyncedUpTo = 0;  ///< 已处理到该等级的升级学技能
    };

    COMPONENT(Attributes, attributes)
    COMPONENT(Statuses, statuses)
    COMPONENT(Skills, skills)
    COMPONENT(Progression, progression)
    COMPONENT(Vitals, vitals)
    COMPONENT(Traits, traits)
    COMPONENT(ClassInfo, classInfo)
    /** @brief Canonical phase ordering fact; not an actor identity registry. */
    COMPONENT(ActorExecutionOrder, executionOrder)
    /** @brief Optional stable registration, independent of reusable checkpoint body state. */
    COMPONENT(ActorIdentity, identity)

    /**
     * @brief 创建一个空白 actor（三张表均为空，按需惰性写入）。
     * @return Borrowed nullable pointer to the ECS-owned actor; null means creation failed.
     * @ownership The ECS world owns the actor; callers must release it through ECS and never delete it.
     * @lifetime Valid until actor/world destruction; retain the generation-qualified EntityHandle across frames.
     * @thread Call on the RPG ECS thread.
     * @reentrancy The factory invokes no user callbacks; do not re-enter structural ECS mutation while using the
     * result.
     */
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
    /** @brief Canonical modifier insertion backed by AttributeSet. */
    [[nodiscard]] eve::Result<ModifierId> addAttributeModifier(AttributeModifier modifier);
    /** @brief Legacy string modifier insertion facade. */
    std::string addAttributeModifier(const std::string &attribute, const std::string &source, const std::string &op,
                                     double value, int priority = 0);
    /** @brief Canonical modifier removal backed by AttributeSet. */
    [[nodiscard]] eve::Result<void> removeAttributeModifier(const ModifierId &modifierId);
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

    /** @brief 成长进度（LevelSystem 的薄转发）。 */
    int getLevel();
    double getXp();
    double getXpToNext();
    void setXpToNext(double value);
    /**
     * @brief Validate and atomically restore this actor's progression checkpoint.
     * @param level Positive level.
     * @param xp Finite, non-negative progress below xpToNext.
     * @param xpToNext Finite positive threshold for the current level.
     * @return Applied state, or a structured failure without mutation.
     * @remarks The actor retains ownership of its progression component and no
     * level-up event or callback is emitted by restoration.
     * @thread Call on the actor's owning ECS simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> restoreProgression(int level, double xp, double xpToNext);
    /** @brief 增加经验并可能升级（compatibility facade (脚本兼容门面)；返回是否升级）。 */
    bool gainXp(double amount);

    /** @brief 生命等当前资源（VitalsSystem 的薄转发，max 取同名最终属性）。 */
    double getCurrent(const std::string &resource);
    double getMax(const std::string &resource);
    void setCurrent(const std::string &resource, double value);
    double takeDamage(const std::string &resource, double amount, const std::string &source = "");
    double heal(const std::string &resource, double amount);
    void revive(const std::string &resource, double amount = -1.0);
    bool isDead(const std::string &resource);

    /** @brief 特征（TraitSystem 的薄转发）。 */
    int applyTrait(const std::string &traitId, const std::string &source = "");
    /** @brief 移除一个特征（compatibility facade (脚本兼容门面)；成功返回 true）。 */
    bool removeTrait(int instanceId);
    int removeTraitsBySource(const std::string &source);
    int removeTraitsByTrait(const std::string &traitId);
    bool hasTrait(const std::string &traitId);
    int getTraitCount();
    int getTraitInstanceIdAt(int index);
    std::string getTraitIdAt(int index);
    std::string getTraitSourceAt(int index);
    double getParamRate(const std::string &param);
    double getElementRate(const std::string &element);
    double getStateRate(const std::string &stateId);
    bool isStateResist(const std::string &stateId);
    double getExParam(const std::string &exParam);
    double getAttackSpeed();
    int getAttackTimesAdd();
    std::vector<std::string> getAttackElements();
    std::vector<std::string> getAttackStates();

    /** @brief 职业（ClassSystem 的薄转发）。 */
    /** @brief 设定职业（compatibility facade (脚本兼容门面)；成功返回 true）。 */
    bool setClass(const std::string &classId);
    std::string getClassId();
    bool hasClass(const std::string &classId);
    int checkLevelSkills();
    int getClassLearnCount();
    std::string getClassLearnSkillIdAt(int index);
    int getClassLearnLevelAt(int index);

    /**
     * @brief Capture this actor at a persistent safe checkpoint.
     * @return Deterministic schema `eve.rpg.actor-checkpoint` version 5 JSON, or a structured failure.
     * @remarks The checkpoint stores complete canonical attributes, active lifecycle effects and RPG executor metadata,
     * progression, vitals, learned skill cooldowns, class and preconfigured trait identities. Version 4 adds active
     * cast clocks and explicit default-self, explicit-self, linked or retired intent. Version 5 adds executionOrder;
     * null preserves legacy iteration, otherwise it is a canonical non-nil ordering key. Table events remain separate.
     * @param target Optional coordinator-admitted stable identity for a live other-actor target; its handle must
     * equal the actual native link and its registered ActorIdentity. Register referenced actors before providing
     * this context; RPG never invents a target ID.
     * @thread Call on the actor's owning ECS simulation thread.
     * @reentrancy No callbacks or registry mutations are performed.
     * @ownership Returned text owns all facts; no actor, content pointer or callback is retained.
     * @remarks Custom vital-bound policies remain unsupported until explicit candidate validation is available.
     * @cost Copies and encodes every attribute, lifecycle effect and adapter row once per explicit checkpoint.
     * @throws std::bad_alloc Live state remains unchanged.
     */
    [[nodiscard]] eve::Result<std::string> checkpointJson(std::optional<CastTargetBinding> target = std::nullopt) const;

    /**
     * @brief Validate and atomically restore a safe checkpoint into this preconfigured actor.
     * @param json UTF-8 JSON produced by checkpointJson().
     * @return Applied state, or a structured failure without mutation.
     * @remarks The destination must have no active effect/cast and must use the same class,
     * trait identities; required destination attributes must be present. Skill/effect ids must exist in current
     * content. Versions 3/4 preserve modifiers, effect adapter watermarks and exact periodic progress, and reject
     * unknown fields. Versions 1/2 retain their base-layout and unknown-field policy, migrating absent effects to
     * empty; v1 also migrates missing cooldowns to zero. Versions 1–3 migrate absent casting to inactive; unknown
     * versions are rejected. v1–v4 preserve the destination order (or legacy null); coordinated spawn restore supplies
     * an explicit stable default for those older versions. Unknown v5 order fields/keys are rejected.
     * @param target Optional coordinator-admitted live target, required for saved linked-other casts. Identity,
     * registered ActorIdentity, generation, RPG family and same-world constraints are validated before publication.
     * Default-self/explicit-self/retired restore without this context, preserving their event/cancellation semantics.
     * @thread Call on the actor's owning ECS simulation thread.
     * @reentrancy No callbacks or gameplay events are emitted.
     * @ownership Borrows input only during this call; all candidate values are detached before publication.
     * @cost Parses and copies the complete actor state and modifier relationships per restore attempt.
     * @throws std::bad_alloc Live state remains unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreCheckpointJson(std::string_view                 json,
                                                          std::optional<CastTargetBinding> target = std::nullopt);

    /**
     * @brief 返回当前 Table 中所有存活的 RPGActor（含派生类）。
     * StatusSystem::update / SkillSystem::update 用它遍历所有 actor 逐帧推进。
     *
     * 实现细节：由 `ecs::View<RPGActor, Attributes>` + `ViewIterator::entity()`
     * 收集，不再维护第二份 handle 注册表。跨帧持有身份仍应使用 EntityHandle。
     * @remarks Unkeyed actors precede keyed actors; keyed actors sort by their execution value. Equal/absent
     * keys retain native View order. Replay coordinators must prove complete unique keys. No View escapes.
     * @cost Scan N actors and sort K keyed actors among them per system phase; temporary ordering storage
     * is proportional to N and the returned vector reuses its capacity between synchronous calls.
     * @thread RPG simulation owner thread only; no callbacks.
     * @throws std::bad_alloc Query storage allocation failed without changing actor facts.
     * @lifetime 返回的 vector 在下一次 `liveActors()` 调用前保持稳定；其中的指针
     * 仅在当前帧/当前 View 语义下借用，销毁后勿跨帧保留裸指针。
     */
    static const std::vector<RPGActor *> &liveActors();

private:
    struct CheckpointCandidate {
        ::eve::attributes::PreparedAttributeSet attributes;
        Skills skills;
        Progression progression;
        Vitals vitals;
        ClassInfo classInfo;
        Statuses                                statuses;
        std::optional<SubjectRef>               castTarget;
        ActorExecutionOrder                     executionOrder;
        std::uint8_t                            sourceVersion = 5;
    };

    [[nodiscard]] eve::Result<CheckpointCandidate> prepareCheckpointJson(
        std::string_view json, std::optional<CastTargetBinding> target = std::nullopt) const;
    /** @brief Shared canonical parser; skills is a nullable synchronous borrow, never retained.
     * @remarks Null selects live references; non-null selects exact admitted archive references.
     */
    [[nodiscard]] static eve::Result<CheckpointCandidate> prepareCheckpointState(
        std::string_view json, const ::eve::attributes::AttributeSet &baseAttributes, const Statuses &baseStatuses,
        const Traits &traits, const ClassInfo &baseClass, const SkillContent *skills);
    void commitCheckpoint(CheckpointCandidate candidate) noexcept;
    friend class PreparedMemberActor;
    friend class RPGSaveSession;
    friend class Party;
};

}  // namespace eve::rpg
