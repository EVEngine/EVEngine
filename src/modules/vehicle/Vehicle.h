#pragma once

/**
 * @brief 载具模块入口（eve.Vehicle）：定义注册 / 实体工厂 / RTS 命令 / 帧调度。
 * 设计文档：docs/dev/通用载具系统设计.md
 */

#include "common/ECS.h"
#include "common/Module.h"
#include "vehicle/VehicleMobility.h"
#include "vehicle/VehicleTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::weapon {
class WeaponMountEntity;
}

namespace eve::physics {
class World;
class World3D;
}  // namespace eve::physics

namespace eve::vehicle {

/** @brief 载具模块（eve.Vehicle）。 */
class Vehicle : public Module {
public:
    Module_REG(Vehicle);
    Vehicle();
    ~Vehicle() override;

    /** @brief 从 JSON 注册载具模板；返回成功注册数量。 */
    int registerVehiclesFromJson(const std::string& json);
    /** @brief 清空全部载具模板。 */
    void clearVehicleDefinitions();
    /** @brief 已注册载具模板数量。 */
    int getVehicleDefinitionCount();
    /** @brief 载具模板查询。 */
    bool        hasVehicleDefinition(const std::string& id);
    std::string getVehicleDefinitionMobility(const std::string& id);
    float       getVehicleDefinitionMaxHealth(const std::string& id);

    /** @brief 注册移动模型（C++ 插件/游戏侧扩展点；同名替换）。 */
    static void registerMobility(IVehicleMobility* mobility);
    /** @brief 已注册移动模型数量（内置 kinematic 起步）。 */
    static int getMobilityCount();

    /** @brief 工厂：按模板创建载具并挂载武器挂点（ECS 表持有，脚本持有句柄）。 */
    VehicleEntity* newVehicle(const std::string& defId, float x, float y, float heading = 0.f,
                              const std::string& faction = "");

    /** @brief RTS 命令。 */
    void moveTo(VehicleEntity* v, float x, float y);
    void attackMove(VehicleEntity* v, float x, float y);
    void attack(VehicleEntity* v, float x, float y, int targetId = 0);
    void stop(VehicleEntity* v);
    void hold(VehicleEntity* v);
    void clearOrders(VehicleEntity* v);
    int  orderCount(VehicleEntity* v);
    /** @brief 当前命令类型名（"move" / "attack_move" / "attack" / "stop" / "hold" / "none"）。 */
    std::string getCurrentOrderType(VehicleEntity* v);

    /** @brief 直接驾驶输入（FPS / 脚本驱动；RTS 命令也会写 input）。 */
    void setInput(VehicleEntity* v, float throttle, float steer, float brake = 0.f, bool handbrake = false);

    /** @brief 状态查询 / 设置。 */
    float       getX(VehicleEntity* v);
    float       getY(VehicleEntity* v);
    float       getHeading(VehicleEntity* v);
    float       getSpeed(VehicleEntity* v);
    void        setPosition(VehicleEntity* v, float x, float y);
    void        setHeading(VehicleEntity* v, float deg);
    bool        isArrived(VehicleEntity* v);
    float       getHealth(VehicleEntity* v);
    void        setHealth(VehicleEntity* v, float hp);
    float       getMaxHealth(VehicleEntity* v);
    std::string getFaction(VehicleEntity* v);
    void        setFaction(VehicleEntity* v, const std::string& faction);

    /** @brief 物理绑定（physics 模块开启时可用；返回是否成功）。 */
    bool        attachPhysics2D(VehicleEntity* v, eve::physics::World* world);
    bool        attachPhysics3D(VehicleEntity* v, eve::physics::World3D* world, float heightY = 1.f);
    bool        detachPhysics(VehicleEntity* v);
    bool        hasPhysics(VehicleEntity* v);
    std::string getPhysicsSpace(VehicleEntity* v);
    /** @brief 3D 车身高度（无 3D 物理时返回 0）。 */
    float getHeight(VehicleEntity* v);

    /** @brief 伤害管线：修饰器 → 装甲区倍率 → 扣血 → 事件。 */
    void applyDamage(VehicleEntity* v, float amount, const std::string& zone = "", int sourceId = 0);
    /** @brief 装甲区倍率（未命中任何区返回 1）。 */
    float getArmorZoneMult(VehicleEntity* v, const std::string& zone);
    /** @brief 是否已摧毁。 */
    bool isDestroyed(VehicleEntity* v);

    /** @brief 座位（FPS 面）。 */
    int         getSeatCount(VehicleEntity* v);
    std::string getSeatName(VehicleEntity* v, int seatIndex);
    std::string getSeatCameraMode(VehicleEntity* v, int seatIndex);
    bool        isSeatOccupied(VehicleEntity* v, int seatIndex);
    int         getSeatOccupant(VehicleEntity* v, int seatIndex);
    /** @brief 座位绑定的武器挂点（mountIndex 无效返回 nullptr）。 */
    eve::weapon::WeaponMountEntity* getSeatMount(VehicleEntity* v, int seatIndex);
    bool                            enterSeat(VehicleEntity* v, int seatIndex, int playerId);
    bool                            exitSeat(VehicleEntity* v, int seatIndex);
    int                             exitSeatByPlayer(VehicleEntity* v, int playerId);

    /** @brief 写入玩家控制（归一化；角度为度）。 */
    void setPlayerControls(int playerId, float throttle, float steer, float brake, bool fire, float aimYaw,
                           float aimPitch);

    /** @brief 挂点查询（返回 weapon 模块的挂点实体）。 */
    int                             getMountCount(VehicleEntity* v);
    eve::weapon::WeaponMountEntity* getMount(VehicleEntity* v, int index);

    /** @brief 每帧推进全部载具（命令 → 移动 → RTS 自动瞄准）。 */
    void update(float dt);

    /** @brief 事件队列（order_completed 等，上一次 update 产生）。 */
    void        clearEvents();
    int         getEventCount() const;
    std::string getEventType(int index) const;
    std::string getEventVehicleId(int index) const;
    std::string getEventDefId(int index) const;
    std::string getEventOrderType(int index) const;
    float       getEventX(int index) const;
    float       getEventY(int index) const;

private:
    void                     autoAim(VehicleEntity& v);
    void                     updateSeats(VehicleEntity& v);
    const VehicleDefinition* findDef(const std::string& id) const;

    std::unordered_map<std::string, VehicleDefinition> defs_;
    std::vector<ecs::EntityHandle>                     vehicles_;
    std::vector<VehicleEvent>                          events_;
    int                                                nextInstance_ = 1;
};

}  // namespace eve::vehicle
