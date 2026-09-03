#pragma once

/**
 * @brief 武器模块数据模型：武器模板 / 武器实体 / 挂点实体 / 手持位实体。
 *
 * 与引擎其他玩法模块一致（参考 card / rpg）：组件是纯数据结构体，
 * 行为全部放在 WeaponSystem（静态类）与 IWeaponLogic（可插拔逻辑）里。
 *
 * v2 设计（见 docs/dev/通用武器系统设计.md）：用统一的「形态 kind + 资源 +
 * 阶段机」泛化触发管线，让热武器 / 冷兵器 / 法杖 / 导弹共用同一套机制：
 *  - WeaponKind 声明武器形态（melee / ranged / magic / missile），
 *  - AttackResource 表达触发消耗（弹药 / 法力 / 充能 / 体力 / 无），
 *  - AttackStage 阶段机（Idle → Windup → Active → Recover）替代硬编码的装填/连发。
 *
 * 挂点（WeaponMountEntity）独立成实体，可挂在载具 / 建筑 / 炮台上，
 * 炮塔被打掉 = 挂点标记 destroyed，不需要改动载具实体。
 * 手持位（WeaponRigEntity）是轻量持有位，面向 ARPG/RPG 角色手持武器。
 */

#include "attributes/AttributeProjection.h"
#include "common/ECS.h"
#include "common/definitions/DefinitionRuntime.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::weapon {

/** @brief 武器形态。 */
enum class WeaponKind : uint8_t { Melee, Ranged, Magic, Missile, Custom };

/**
 * @brief 武器形态的字符串名（"melee" | "ranged" | "magic" | "missile" | "custom"）。
 * @return Borrowed non-null pointer to immutable process-lifetime storage.
 * @ownership The returned static text is not caller-owned and must not be freed.
 * @lifetime Valid for the process lifetime; copy it when storing it.
 * @thread Thread-safe because the characters are immutable.
 * @reentrancy Does not invoke callbacks.
 */
const char* weaponKindName(WeaponKind kind);

/** @brief 从字符串解析武器形态；未知名字回退 Ranged。 */
WeaponKind weaponKindFromName(const std::string& name);

/** @brief 触发资源的类型。 */
enum class ResourceKind : uint8_t { None, Ammo, Mana, Charges, Stamina };

/** @brief 触发资源模板（武器模板的一部分，用于运行时扣费 / 回复）。 */
struct AttackResource {
    ResourceKind kind    = ResourceKind::None;
    float        max     = 0.f;
    float        regen   = 0.f;  // >0 时每秒回复（法力 / 充能）
    float        cost    = 0.f;  // 每次触发消耗
    bool         infinite = false;  // 无消耗（近战默认）
};

/** @brief 攻击阶段。 */
enum class AttackStage : uint8_t { Idle, Windup, Active, Recover };

/**
 * @brief 攻击阶段的字符串名（"idle" | "windup" | "active" | "recover"）。
 * @return Borrowed non-null pointer to immutable process-lifetime storage.
 * @ownership The returned static text is not caller-owned and must not be freed.
 * @lifetime Valid for the process lifetime; copy it when storing it.
 * @thread Thread-safe because the characters are immutable.
 * @reentrancy Does not invoke callbacks.
 */
const char* attackStageName(AttackStage stage);

/** @brief 阶段时长（秒）；各形态按需配置。 */
struct AttackStageSpec {
    float windupTime  = 0.f;  // 前摇（挥剑起手 / 施法抬手 / 导弹锁定）
    float activeTime  = 0.f;  // 生效期（命中帧 / 引导节拍 / 发射时刻）
    float recoverTime = 0.f;  // 后摇（收刀 / 施法硬直 / 再装填窗口）
};

/** @brief 开火模式（热武器保留字段）。 */
enum class FireMode : uint8_t { Single, Burst, Auto };

/**
 * @brief 开火模式的字符串名（"single" | "burst" | "auto"）。
 * @return Borrowed non-null pointer to immutable process-lifetime storage.
 * @ownership The returned static text is not caller-owned and must not be freed.
 * @lifetime Valid for the process lifetime; copy it when storing it.
 * @thread Thread-safe because the characters are immutable.
 * @reentrancy Does not invoke callbacks.
 */
const char* fireModeName(FireMode mode);

/** @brief 从字符串解析开火模式；未知名字回退 Single。 */
FireMode fireModeFromName(const std::string& name);

/** @brief 武器事件类型。 */
enum class WeaponEventType : uint8_t {
    Fire,
    ReloadStart,
    ReloadEnd,
    Empty,
    WindupStart,
    AttackEnd,
    AimIn,
    AimOut
};

/**
 * @brief 事件类型的字符串名。
 * @return Borrowed non-null pointer to immutable process-lifetime storage.
 * @ownership The returned static text is not caller-owned and must not be freed.
 * @lifetime Valid for the process lifetime; copy it when storing it.
 * @thread Thread-safe because the characters are immutable.
 * @reentrancy Does not invoke callbacks.
 */
const char* weaponEventTypeName(WeaponEventType type);

/** @brief 投射物参数（武器模板的一部分，供游戏侧投射物服务使用）。 */
struct ProjectileSpec {
    std::string type    = "shell";
    float       speed   = 0.f;
    float       gravity = 0.f;
    float       aoe     = 0.f;
    /** @brief 单次触发弹丸数（霰弹 >1）；1 = 单弹丸。 */
    int   pelletCount   = 1;
    /** @brief 每粒相对中心的散布（度），仅 pelletCount>1 时生效。 */
    float pelletSpread  = 0.f;
};

/** @brief 武器模板（registerWeaponsFromJson 注册，进程级注册表）。 */
struct WeaponDefinition {
    std::string id;
    /** @brief 形态（决定阶段机如何跑）。默认 Ranged。 */
    WeaponKind kind = WeaponKind::Ranged;
    /** @brief IWeaponLogic 注册名；默认 "projectile"。 */
    std::string logic = "projectile";
    float       damage = 0.f;
    float       penetration = 0.f;
    float       range  = 0.f;
    /** @brief Linear range falloff begins here and reaches minimumDamageFactor at range. */
    float falloffStart = 0.f;
    float minimumDamageFactor = 1.f;
    /** @brief Minimum radial multiplier at the outer edge of projectile area damage. */
    float splashMinimumDamageFactor = 0.1f;
    /** @brief Probability of a shot retaining its intended target. */
    float accuracy = 1.0f;
    /** @brief Maximum world-space miss displacement for an inaccurate shot. */
    float scatterRadius = 0.0f;
    /** @brief 散布（度）。 */
    float    spread   = 0.f;
    /** @brief 散布 bloom：连续射击精度下降，停火回稳。 */
    float spreadMin     = 0.f;  // 静止散布（度）
    float spreadMax     = 0.f;  // 连射上限散布（度）
    float spreadPerShot = 0.f;  // 每发增量（度）
    float spreadRecover = 0.f;  // 每秒回稳速度（度/秒）
    /** @brief 后坐力模型：每发累积，游戏侧读事件做相机/枪口表现。 */
    float recoilPitch = 0.f;    // 每发上抬（度）
    float recoilYaw   = 0.f;    // 每发水平（度）
    float recoilRecover = 0.f;  // 每秒回正速度（度/秒）
    /** @brief 伤害类型 / 元素（游戏侧结算用，引擎只透传）。 */
    std::string damageType;
    std::string element;
    /** @brief Shared target-domain and gameplay-tag acceptance policy. */
    bool targetsGround = true;
    bool targetsAir = true;
    bool friendlyFire = false;
    bool blockedByObstacles = false;
    std::vector<std::string> requiredTargetTags;
    std::vector<std::string> excludedTargetTags;
    /** @brief Soft automatic-target role hints; explicit attacks remain authoritative. */
    std::vector<std::string> preferredTargetTags;
    float preferredTargetBonus = 1.0f;
    /** @brief 运行时射击模式（safe/semi/auto 切换）。 */
    std::vector<FireMode> selectableModes;  // 空 = 固定 fireMode
    /** @brief 开镜缩放 FOV（0 = 无缩放）；`State.aiming` 时生效。 */
    float zoomFov = 0.f;
    FireMode fireMode = FireMode::Single;
    /** @brief 单次开火后的冷却（秒）；热武器保留字段。 */
    float cooldown = 0.f;
    /** @brief burst 模式的连发数与发间间隔（秒）。 */
    int   burstSize     = 1;
    float burstInterval = 0.f;
    /** @brief 触发资源（弹药 / 法力 / 充能 / 体力 / 无）。 */
    AttackResource resource;
    /** @brief 阶段机时长（秒）。 */
    AttackStageSpec stages;
    /** @brief 近战挥击命中弧（度）。 */
    float arc = 0.f;
    /** @brief 弹匣容量 / 备用弹药（<0 = 无限备用，0 = 无备用）。热武器保留字段。 */
    int            magSize     = 1;
    int            reserveSize = 0;
    float          reloadTime  = 0.f;
    ProjectileSpec projectile;
    /** @brief 特效/音效资源 id（引擎不播放，游戏侧读事件播放）。 */
    std::string effectMuzzle;
    std::string effectSound;
};

/**
 * @brief 一次攻击请求（C++ 系统 / 脚本发起）。角度均为度。
 * 旧名 FireRequest 保留为别名，向后兼容。
 */
struct AttackRequest {
    float targetX = 0.f;
    float targetY = 0.f;
    float targetZ = 0.f;
    bool  hasTarget = false;
    /** @brief 可选目标实体（导弹锁定 / 法杖单目标）。 */
    ecs::EntityHandle targetHandle;
    /** @brief 炮口世界位置（未提供时游戏侧自行推断）。 */
    float muzzleX = 0.f;
    float muzzleY = 0.f;
    float muzzleZ = 0.f;
    /** @brief 发射方向（度）。 */
    float yaw   = 0.f;
    float pitch = 0.f;
    /** @brief 游戏侧实体/玩家 id。 */
    int shooterId = 0;
    /** @brief 近战挥击命中弧（度）。 */
    float arcAngle = 0.f;
    /** @brief 法术/导弹爆炸半径。 */
    float aoeRadius = 0.f;
    /** @brief 本发实际散布（度，散布 bloom 后的当前值）。 */
    float spread = 0.f;
    /** @brief 多弹丸：本粒序号 / 总粒数（1 = 单弹丸）。 */
    int pelletIndex = 0;
    int pelletCount = 1;
};

/** @brief 兼容别名：IProjectileService 等旧接口继续用 FireRequest 名。 */
using FireRequest = AttackRequest;

/** @brief 武器事件（帧缓存，脚本轮询，与 building/rpg 风格一致）。 */
struct WeaponEvent {
    WeaponEventType type = WeaponEventType::Fire;
    std::string     weaponId;
    std::string     defId;
    std::string     mountId;
    int             ammoLeft = 0;
    /** @brief 发射位置（仅 Fire 事件填写）。 */
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    /** @brief 近战命中弧（仅 Fire 事件填写）。 */
    float arc = 0.f;
    /** @brief 法术/导弹爆炸半径（仅 Fire 事件填写）。 */
    float aoe = 0.f;
    /** @brief 本发当前散布（度，仅 Fire 事件）。 */
    float spread = 0.f;
    /** @brief 本发后坐（度，仅 Fire 事件）。 */
    float recoilPitch = 0.f;
    float recoilYaw   = 0.f;
    /** @brief 本发弹丸数（霰弹 >1）。 */
    int pellets = 1;
    /** @brief 伤害类型 / 元素（游戏侧结算用）。 */
    std::string damageType;
    std::string element;
};

/** @brief 运行时触发资源（实体组件）。 */
struct Resource {
    ResourceKind kind       = ResourceKind::None;
    float        value      = 0.f;  // 当前值（弹匣剩余 / 法力 / 充能 / 体力）
    float        max        = 0.f;
    float        regen      = 0.f;
    float        cost       = 0.f;
    bool         infinite   = false;
    /** @brief 备用弹药（热武器；<0 = 无限）。 */
    int          reserve    = 0;
    bool         reloading  = false;
    float        reloadProgress = 0.f;
};

/** @brief 武器实体：数据全部在组件里，行为在 WeaponSystem。 */
class WeaponEntity : public ecs::Entity {
public:
    ENTITY(WeaponEntity, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    /** @brief 稳定实例 id / 模板 id / 游戏侧拥有者。 */
    struct Identity {
        std::string id;
        std::string defId;
        int         ownerId = 0;
    };

    /**
     * @brief Typed definition projection used by legacy WeaponSystem code.
     * @ownership Borrowed through `def` when `owned` is empty. When `owned`
     *            is present, this component owns the immutable projection and
     *            `def` points into it.
     * @lifetime The borrowed pointer remains valid until this component is
     *           replaced or its owner is destroyed. Callers must not retain it
     *           across a definition projection swap.
     */
    struct Definition {
        const WeaponDefinition* def = nullptr;
        /** @brief Immutable typed projection owner installed by checked adapters. */
        std::shared_ptr<const WeaponDefinition> owned;
    };

    /**
     * @brief Canonical definition identity and reload policy projected to ECS.
     * @ownership The common DefinitionRegistry owns definition data; this
     *            component only stores identity and policy.
     * @lifetime The identity is valid with this entity and becomes stale when
     *           the registry generation is replaced or removed.
     */
    struct DefinitionBinding {
        eve::definition::InstanceIdentity identity;
        eve::definition::ReloadPolicy     reloadPolicy = eve::definition::ReloadPolicy::RebuildInstance;
        bool                              active       = true;
    };

    /** @brief 触发资源 / 冷却 / 装填 / 阶段机运行时状态。 */
    struct State {
        Resource  resource;
        float     cooldown = 0.f;  // 热武器额外冷却
        int       burstRemaining = 0;
        float     burstTimer     = 0.f;
        bool      jammed         = false;
        AttackStage stage         = AttackStage::Idle;
        float     stageTimer     = 0.f;
        /** @brief 阶段期间保存的攻击请求（供 Active/channel 使用）。 */
        AttackRequest lastRequest;
        /** @brief 阶段机时长（模板持有，实体只存指针）。 */
        const AttackStageSpec* stages = nullptr;
        /** @brief 散布 bloom：当前散布（度），随射击增大、停火回稳。 */
        float currentSpread = 0.f;
        /** @brief 未回正的后坐（度）。 */
        float recoilPitch = 0.f;
        float recoilYaw   = 0.f;
        /** @brief 运行时射击模式（safe/semi/auto；需在模板 selectableModes 内）。 */
        FireMode selector = FireMode::Single;
        /** @brief 是否开镜（ADS）。 */
        bool aiming = false;
        /** @brief 可选的共享弹药池（不拥有，模块管理生命周期）。 */
        class AmmoPoolEntity* ammoPool = nullptr;
    };

    /**
     * @brief Canonical optional mana/stamina state for non-ammo weapons.
     *
     * `State::Resource` remains a one-way compatibility projection for Mana
     * and Stamina. Ammo, charges, reload progress and cooldown stay owned by
     * the weapon resource/phase state and are not converted to attributes.
     */
    struct Attributes {
        eve::attributes::AttributeProjection values;
    };

    /** @brief 炮口朝向（度）。turnSpeed 0 = 立即到位。 */
    struct Aim {
        float yaw          = 0.f;
        float pitch        = 0.f;
        float desiredYaw   = 0.f;
        float desiredPitch = 0.f;
        float turnSpeed    = 0.f;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Definition, definition)
    COMPONENT(DefinitionBinding, definitionBinding)
    COMPONENT(State, state)
    COMPONENT(Attributes, attributes)
    COMPONENT(Aim, aim)

    /**
     * @brief 创建并触摸全部组件。
     * @return Borrowed nullable pointer to an ECS-owned weapon.
     * @ownership The ECS world owns the entity; callers must release through ECS and never delete it.
     * @lifetime Valid until entity/world destruction; retain the generation-qualified handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy Creation invokes no user callbacks; do not re-enter structural ECS mutation.
     */
    static WeaponEntity* createWeapon();
};

/** @brief 武器挂点实体：可持有武器并限制旋转（炮塔 / 机枪座 / 固定挂架）。 */
class WeaponMountEntity : public ecs::Entity {
public:
    ENTITY(WeaponMountEntity, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    /** @brief 挂点 id / 场景节点路径 / 类型（"turret" | "pintle" | "fixed"）。 */
    struct Identity {
        std::string id;
        std::string nodePath;
        std::string type = "turret";
    };

    /** @brief 旋转限位与射界（度）。 */
    struct Limits {
        float yawMin   = -180.f;
        float yawMax   = 180.f;
        float pitchMin = -45.f;
        float pitchMax = 45.f;
        /** @brief 旋转速度（度/秒）；0 = 立即。 */
        float rotSpeed = 0.f;
        /** @brief RTS 射界（度，0 = 360° 全向）。 */
        float firingArc = 0.f;
    };

    /** @brief 当前朝向（度）与挂载的武器（不拥有，模块负责两者生命周期）。 */
    struct State {
        WeaponEntity* weapon    = nullptr;
        float         yaw       = 0.f;
        float         pitch     = 0.f;
        bool          destroyed = false;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Limits, limits)
    COMPONENT(State, state)

    /**
     * @brief 创建并触摸全部组件。
     * @return Borrowed nullable pointer to an ECS-owned mount.
     * @ownership The ECS world owns the entity; callers must release through ECS and never delete it.
     * @lifetime Valid until entity/world destruction; retain the generation-qualified handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy Creation invokes no user callbacks; do not re-enter structural ECS mutation.
     */
    static WeaponMountEntity* createMount();
};

/** @brief 手持位实体：面向 ARPG/RPG 角色手持武器（剑/法杖/步枪），无炮塔限位。 */
class WeaponRigEntity : public ecs::Entity {
public:
    ENTITY(WeaponRigEntity, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    /** @brief 手持位 id / 场景节点路径 / 握持类型。 */
    struct Identity {
        std::string id;
        std::string nodePath;
        std::string wield = "right_hand";
    };

    /** @brief 持有位姿偏移（挂到角色手持节点的相对位置/旋转）。 */
    struct Held {
        float posX = 0.f;
        float posY = 0.f;
        float posZ = 0.f;
        float rotX = 0.f;
        float rotY = 0.f;
        float rotZ = 0.f;
    };

    /** @brief 当前持有的武器（不拥有，模块负责生命周期）。 */
    struct State {
        WeaponEntity* weapon = nullptr;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Held, held)
    COMPONENT(State, state)

    /**
     * @brief 创建并触摸全部组件。
     * @return Borrowed nullable pointer to an ECS-owned rig.
     * @ownership The ECS world owns the entity; callers must release through ECS and never delete it.
     * @lifetime Valid until entity/world destruction; retain the generation-qualified handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy Creation invokes no user callbacks; do not re-enter structural ECS mutation.
     */
    static WeaponRigEntity* createRig();
};

/** @brief 共享弹药池实体：多把武器共用的备用弹药，装填时从池中取。 */
class AmmoPoolEntity : public ecs::Entity {
public:
    ENTITY(AmmoPoolEntity, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    /** @brief 弹药池 id / 弹药类型（"pistol" | "rifle" | "shell"…）。 */
    struct Identity {
        std::string id;
        std::string ammoType;
    };

    /** @brief 当前存量 / 上限（<0 = 无限）。 */
    struct State {
        int count = 0;
        int max   = -1;
    };

    COMPONENT(Identity, identity)
    COMPONENT(State, state)

    /**
     * @brief 创建并触摸全部组件。
     * @return Borrowed nullable pointer to an ECS-owned ammunition pool.
     * @ownership The ECS world owns the entity; callers must release through ECS and never delete it.
     * @lifetime Valid until entity/world destruction; retain the generation-qualified handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy Creation invokes no user callbacks; do not re-enter structural ECS mutation.
     */
    static AmmoPoolEntity* createPool();
};

}  // namespace eve::weapon
