#pragma once

/**
 * @brief 武器模块入口（eve.Weapon）：定义注册 / 实体工厂 / 挂点操作 / 帧调度。
 * 设计文档：docs/dev/通用载具系统设计.md
 */

#include "common/ECS.h"
#include "common/Module.h"
#include "weapon/WeaponLogic.h"
#include "weapon/WeaponTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::weapon {

/** @brief 武器模块（eve.Weapon）。 */
class Weapon : public Module {
public:
    Module_REG(Weapon);
    Weapon();
    ~Weapon() override;

    /** @brief 从 JSON 注册武器模板；返回成功注册数量。 */
    int registerWeaponsFromJson(const std::string& json);
    /** @brief 清空全部武器模板。 */
    void clearWeaponDefinitions();
    /** @brief 已注册武器模板数量。 */
    int getWeaponDefinitionCount();
    /** @brief 武器模板查询。 */
    bool        hasWeaponDefinition(const std::string& id);
    std::string getWeaponDefinitionLogic(const std::string& id);
    float       getWeaponDefinitionDamage(const std::string& id);
    float       getWeaponDefinitionRange(const std::string& id);

    /** @brief 注册武器逻辑（C++ 插件/游戏侧扩展点；同名替换）。 */
    static void registerLogic(IWeaponLogic* logic);
    /** @brief 已注册逻辑数量（内置 hitscan / projectile 起步）。 */
    static int getLogicCount();

    /** @brief 工厂：对象由 ECS 表持有，脚本持有的是非拥有句柄。 */
    WeaponEntity* newWeapon(const std::string& defId);
    /** @brief 创建一个挂点（炮塔/机枪座等）。 */
    WeaponMountEntity* newMount(const std::string& id, const std::string& type = "turret");

    /** @brief 挂点操作：挂武器 / 取武器 / 限位 / 瞄准 / 击毁。 */
    bool          mountAttachWeapon(WeaponMountEntity* m, WeaponEntity* w);
    WeaponEntity* mountGetWeapon(WeaponMountEntity* m);
    void          mountSetLimits(WeaponMountEntity* m, float yawMin, float yawMax, float pitchMin, float pitchMax,
                                 float rotSpeed, float firingArc);
    /** @brief 按限位夹取目标角并写入挂载武器的瞄准目标（角度，度）。 */
    void mountAimAt(WeaponMountEntity* m, float yaw, float pitch);
    /** @brief 标记挂点被击毁（炮塔被打掉）；已毁挂点不再瞄准/开火。 */
    void mountDestroy(WeaponMountEntity* m);

    /** @brief 武器操作（脚本便捷入口，转发 WeaponSystem）。 */
    bool fire(WeaponEntity* w, const FireRequest& req);
    /** @brief 向目标点开火（自动生成 FireRequest）。 */
    bool fireAt(WeaponEntity* w, float x, float y, float z, int shooterId = 0);
    bool canFire(WeaponEntity* w);
    void startReload(WeaponEntity* w);
    void cancelReload(WeaponEntity* w);
    void setAim(WeaponEntity* w, float yaw, float pitch);

    /** @brief 每帧推进全部武器与挂点。 */
    void update(float dt);

    /** @brief 事件队列（fire/reload/empty，上一次 update 产生）。 */
    void        clearEvents();
    int         getEventCount() const;
    std::string getEventType(int index) const;
    std::string getEventWeaponId(int index) const;
    std::string getEventDefId(int index) const;
    std::string getEventMountId(int index) const;
    int         getEventAmmoLeft(int index) const;

private:
    const WeaponDefinition* findDef(const std::string& id) const;

    std::unordered_map<std::string, WeaponDefinition> defs_;
    std::vector<ecs::EntityHandle>                    weapons_;
    std::vector<ecs::EntityHandle>                    mounts_;
    std::vector<WeaponEvent>                          events_;
    int                                               nextInstance_ = 1;
};

}  // namespace eve::weapon
