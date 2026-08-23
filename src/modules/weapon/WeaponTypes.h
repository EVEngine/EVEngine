#pragma once

/**
 * @brief 武器模块数据模型：武器模板 / 武器实体 / 挂点实体。
 *
 * 与引擎其他玩法模块一致（参考 card / rpg）：组件是纯数据结构体，
 * 行为全部放在 WeaponSystem（静态类）与 IWeaponLogic（可插拔逻辑）里。
 * 挂点（WeaponMountEntity）独立成实体，可以挂在载具 / 建筑 / 炮台上，
 * 炮塔被打掉 = 挂点标记 destroyed，不需要改动载具实体。
 */

#include "common/ECS.h"

#include <cstdint>
#include <string>

namespace eve::weapon {

/** @brief 开火模式。 */
enum class FireMode : uint8_t { Single, Burst, Auto };

/** @brief 开火模式的字符串名（"single" | "burst" | "auto"）。 */
const char* fireModeName(FireMode mode);

/** @brief 从字符串解析开火模式；未知名字回退 Single。 */
FireMode fireModeFromName(const std::string& name);

/** @brief 武器事件类型。 */
enum class WeaponEventType : uint8_t { Fire, ReloadStart, ReloadEnd, Empty };

/** @brief 事件类型的字符串名。 */
const char* weaponEventTypeName(WeaponEventType type);

/** @brief 投射物参数（武器模板的一部分，供游戏侧投射物服务使用）。 */
struct ProjectileSpec {
    std::string type    = "shell";
    float       speed   = 0.f;
    float       gravity = 0.f;
    float       aoe     = 0.f;
};

/** @brief 武器模板（registerWeaponsFromJson 注册，进程级注册表）。 */
struct WeaponDefinition {
    std::string id;
    /** @brief IWeaponLogic 注册名；默认 "projectile"。 */
    std::string logic       = "projectile";
    float       damage      = 0.f;
    float       penetration = 0.f;
    float       range       = 0.f;
    /** @brief 散布（度）。 */
    float    spread   = 0.f;
    FireMode fireMode = FireMode::Single;
    /** @brief 单次开火后的冷却（秒）。 */
    float cooldown = 0.f;
    /** @brief burst 模式的连发数与发间间隔（秒）。 */
    int   burstSize     = 1;
    float burstInterval = 0.f;
    /** @brief 弹匣容量 / 备用弹药（<0 = 无限备用，0 = 无备用）。 */
    int            magSize     = 1;
    int            reserveSize = 0;
    float          reloadTime  = 0.f;
    ProjectileSpec projectile;
    /** @brief 特效/音效资源 id（引擎不播放，游戏侧读事件播放）。 */
    std::string effectMuzzle;
    std::string effectSound;
};

/** @brief 一次开火请求（C++ 系统 / 脚本发起）。角度均为度。 */
struct FireRequest {
    float targetX   = 0.f;
    float targetY   = 0.f;
    float targetZ   = 0.f;
    bool  hasTarget = false;
    /** @brief 炮口世界位置（未提供时游戏侧自行推断）。 */
    float muzzleX = 0.f;
    float muzzleY = 0.f;
    float muzzleZ = 0.f;
    /** @brief 发射方向（度）。 */
    float yaw   = 0.f;
    float pitch = 0.f;
    /** @brief 游戏侧实体/玩家 id。 */
    int shooterId = 0;
};

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

    /** @brief 模板指针（Weapon 模块注册表持有，实体不拥有）。 */
    struct Definition {
        const WeaponDefinition* def = nullptr;
    };

    /** @brief 弹药 / 冷却 / 装填 / 连发运行时状态。 */
    struct State {
        int   magAmmo        = 0;
        int   reserveAmmo    = 0;
        float cooldown       = 0.f;
        float reloadProgress = 0.f;
        bool  reloading      = false;
        int   burstRemaining = 0;
        float burstTimer     = 0.f;
        bool  jammed         = false;
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
    COMPONENT(State, state)
    COMPONENT(Aim, aim)

    /** @brief 创建并触摸全部组件。 */
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

    /** @brief 创建并触摸全部组件。 */
    static WeaponMountEntity* createMount();
};

}  // namespace eve::weapon
