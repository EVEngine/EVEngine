#pragma once

/**
 * @brief 武器行为接口与投射物服务接口。
 *
 * 新增武器类型 = 实现 IWeaponLogic 并按名字注册到 WeaponSystem
 * （registerLogic），不需要改引擎头文件。投射物/弹道表现由游戏侧
 * 实现 IProjectileService 并通过 eve::cap::provide 注册，weapon 模块
 * 不硬依赖 physics / graphics / particles。
 */

namespace eve::weapon {

struct FireRequest;
class WeaponEntity;

/** @brief 武器逻辑接口：注册到 WeaponSystem 的可插拔行为。 */
class IWeaponLogic {
public:
    virtual ~IWeaponLogic() = default;

    /** @brief 稳定名字（"hitscan" / "projectile" / 自定义名）。 */
    virtual const char* name() const = 0;

    /** @brief 逻辑自身的开火条件（弹药/冷却等由 WeaponSystem 统一检查）。 */
    virtual bool canFire(const WeaponEntity& w) const = 0;

    /** @brief 执行开火效果：生成投射物 / 命中判定等。弹药已由系统扣除。 */
    virtual void fire(WeaponEntity& w, const FireRequest& req) = 0;

    /** @brief 每帧推进逻辑自身状态。 */
    virtual void update(WeaponEntity& w, float dt) = 0;
};

/**
 * @brief 投射物服务：由游戏侧实现并注册为能力。
 * weapon 模块只认识这个接口，弹道/爆炸/受击由提供方负责。
 */
class IProjectileService {
public:
    static constexpr const char* capabilityName = "IProjectileService";

    virtual ~IProjectileService() = default;

    /** @brief 按武器模板的 projectile 参数生成一枚投射物。 */
    virtual void spawnProjectile(const WeaponEntity& w, const FireRequest& req) = 0;
};

}  // namespace eve::weapon
