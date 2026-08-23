#pragma once

/**
 * @brief 载具模块数据模型：载具模板 / 命令 / 载具实体。
 *
 * 与引擎其他玩法模块一致（参考 card / weapon / rpg）：组件是纯数据结构体，
 * 行为放在 VehicleSystem（静态类）与 IVehicleMobility（可插拔移动模型）里。
 * 载具可以声明武器挂点（MountDef），运行时由 Vehicle 模块通过 weapon 模块
 * 创建 WeaponMountEntity 并挂载武器——RTS / 坦克 / FPS 的区别只体现在
 * 命令队列 / 驾驶输入 / 座位（Phase 4）这些可选面上。
 */

#include "common/ECS.h"
#include "weapon/WeaponTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::vehicle {

/** @brief RTS 命令类型。 */
enum class VehicleOrderType : uint8_t { Move, AttackMove, Attack, Stop, Hold };

/** @brief 命令类型的字符串名（"move" | "attack_move" | ...）。 */
const char* vehicleOrderTypeName(VehicleOrderType type);

/** @brief 一条命令（队列由 OrderSystem 逐条执行）。 */
struct VehicleOrder {
    VehicleOrderType type         = VehicleOrderType::Move;
    float            x            = 0.f;
    float            y            = 0.f;
    int              targetId     = 0;
    float            arriveRadius = 12.f;
};

/** @brief 每帧驾驶输入（由 PlayerDriver / OrderSystem / 脚本写入）。 */
struct VehicleInput {
    float throttle  = 0.f;  // -1..1
    float steer     = 0.f;  // -1..1
    float brake     = 0.f;  // 0..1
    bool  handbrake = false;
};

/** @brief 装甲区（Phase 4 伤害管线使用；Phase 2 仅作为定义数据保留）。 */
struct ArmorZone {
    std::string name;
    float       mult = 1.f;
    std::string node;
};

/** @brief 武器挂点声明（载具模板的一部分）。 */
struct MountDef {
    std::string name;
    std::string weapon;  // weapon 模板 id
    std::string type      = "turret";
    float       yawMin    = -180.f;
    float       yawMax    = 180.f;
    float       pitchMin  = -45.f;
    float       pitchMax  = 45.f;
    float       rotSpeed  = 0.f;
    float       firingArc = 0.f;
    std::string aimMode   = "auto";  // "auto" | "manual"
};

/** @brief 载具模板（registerVehiclesFromJson 注册，进程级注册表）。 */
struct VehicleDefinition {
    std::string id;
    std::string category = "vehicle";
    /** @brief IVehicleMobility 注册名；默认 "kinematic"。 */
    std::string              mobility  = "kinematic";
    float                    maxSpeed  = 120.f;  // 像素/秒
    float                    accel     = 80.f;   // 像素/秒²
    float                    turnRate  = 90.f;   // 度/秒
    float                    radius    = 16.f;   // 选择/碰撞半径
    float                    maxHealth = 100.f;
    std::vector<ArmorZone>   armorZones;
    std::vector<MountDef>    mounts;
    std::vector<std::string> tags;
};

/** @brief 载具事件类型。 */
enum class VehicleEventType : uint8_t { OrderCompleted, Damaged, Destroyed };

/** @brief 事件类型的字符串名。 */
const char* vehicleEventTypeName(VehicleEventType type);

/** @brief 载具事件（帧缓存，脚本轮询）。 */
struct VehicleEvent {
    VehicleEventType type = VehicleEventType::OrderCompleted;
    std::string      vehicleId;
    std::string      defId;
    std::string      orderType;
    float            x = 0.f;
    float            y = 0.f;
};

/** @brief 载具实体：数据全部在组件里，行为在 VehicleSystem。 */
class VehicleEntity : public ecs::Entity {
public:
    ENTITY(VehicleEntity, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    /** @brief 稳定实例 id / 模板 id / 阵营。 */
    struct Identity {
        std::string id;
        std::string defId;
        std::string faction;
    };

    /** @brief 模板指针（Vehicle 模块注册表持有，实体不拥有）。 */
    struct Definition {
        const VehicleDefinition* def = nullptr;
    };

    /** @brief 每帧输入（命令系统 / 驾驶者写入，移动模型消费）。 */
    struct Input {
        float throttle  = 0.f;
        float steer     = 0.f;
        float brake     = 0.f;
        bool  handbrake = false;
    };

    /** @brief 2D 运动状态（kinematic 直接持有；物理模式由 body 回写）。 */
    struct Motion {
        float x       = 0.f;
        float y       = 0.f;
        float heading = 0.f;  // 度，0 = +X
        float speed   = 0.f;  // 像素/秒
        bool  arrived = false;
    };

    /** @brief 生命值（Phase 4 伤害管线增强）。 */
    struct Health {
        float hp    = 0.f;
        float maxHp = 0.f;
    };

    /** @brief 命令队列（OrderSystem 逐条执行）。 */
    struct Orders {
        std::vector<VehicleOrder> queue;
        int                       current = -1;
    };

    /** @brief 一个挂点槽位：挂点实体 + 瞄准模式。 */
    struct MountSlot {
        eve::weapon::WeaponMountEntity* mount   = nullptr;
        std::string                     aimMode = "auto";  // "auto" | "manual"
    };

    /** @brief 武器挂点槽位列表（不拥有挂点；weapon 模块管理生命周期）。 */
    struct Mounts {
        std::vector<MountSlot> list;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Definition, definition)
    COMPONENT(Input, input)
    COMPONENT(Motion, motion)
    COMPONENT(Health, health)
    COMPONENT(Orders, orders)
    COMPONENT(Mounts, mounts)

    /** @brief 创建并触摸全部组件。 */
    static VehicleEntity* createVehicle();
};

}  // namespace eve::vehicle
