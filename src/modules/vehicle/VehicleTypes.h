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

#include <cstdint>
#include <string>
#include <vector>

// 只持有指针，不需要完整类型；physics 开启时才包含对应头文件。
namespace eve::physics {
class Body;
class Body3D;
}  // namespace eve::physics

namespace eve::weapon {
class WeaponMountEntity;
}  // namespace eve::weapon

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
    bool  fire      = false;  // 武器座开火请求
    float aimYaw    = 0.f;    // 武器座瞄准（度）
    float aimPitch  = 0.f;
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

/** @brief 3D 悬架车轮配置（raycast 悬架用）。 */
struct SuspensionWheel {
    /** @brief 相对车体原点的局部偏移（米，+Y 向上）。 */
    float x          = 0.f;
    float y          = 0.f;
    float z          = 0.f;
    float radius     = 0.3f;
    float restLength = 0.4f;  // 悬架静止长度
    float stiffness  = 60.f;  // 弹簧刚度
    float damping    = 8.f;   // 阻尼
    bool  drive      = true;
    bool  steer      = true;
};

/** @brief 3D 悬架参数（车辆模板的一部分）。 */
struct SuspensionConfig {
    /** @brief 悬架最大行程（米）。 */
    float maxTravel = 0.3f;
    /** @brief 驱动轮满油门牵引力（牛顿）。 */
    float driveForce = 2000.f;
    /** @brief 侧向抓地系数（越大越不容易侧滑）。 */
    float                        lateralGrip = 12.f;
    std::vector<SuspensionWheel> wheels;
};

/** @brief 座位声明（载具模板的一部分，FPS 面）。 */
struct SeatDef {
    std::string name       = "passenger";
    std::string driver     = "player";  // IVehicleDriver 注册名
    std::string cameraMode = "third";   // "first" | "third" | "orbit"
    int         mountIndex = -1;        // 该座位控制的挂点下标（-1 = 无武器）
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
    SuspensionConfig         suspension;
    std::vector<SeatDef>     seats;
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
        bool  fire      = false;
        float aimYaw    = 0.f;
        float aimPitch  = 0.f;
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

    /** @brief 物理刚体（不拥有；physics 模块管理生命周期）。 */
    struct PhysicsBody {
        eve::physics::Body*   body2d = nullptr;
        eve::physics::Body3D* body3d = nullptr;
        std::string           space;  // "2d" | "3d" | ""
    };

    /** @brief 悬架运行时状态（与模板 wheels 一一对应）。 */
    struct SuspensionState {
        struct WheelState {
            float prevCompression = 0.f;
            bool  grounded        = false;
        };
        std::vector<WheelState> wheels;
    };

    /** @brief 座位槽位（运行时，与模板 seats 一一对应）。 */
    struct SeatSlot {
        std::string name;
        std::string driver     = "player";
        std::string cameraMode = "third";
        int         mountIndex = -1;
        int         occupant   = 0;  // 玩家/实体 id，0 = 空
        bool        occupied   = false;
    };

    /** @brief 座位列表（FPS 面）。 */
    struct Seats {
        std::vector<SeatSlot> list;
    };

    /** @brief 通用标志位。 */
    struct Flags {
        bool destroyed = false;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Definition, definition)
    COMPONENT(Input, input)
    COMPONENT(Motion, motion)
    COMPONENT(Health, health)
    COMPONENT(Orders, orders)
    COMPONENT(Mounts, mounts)
    COMPONENT(PhysicsBody, physicsBody)
    COMPONENT(SuspensionState, suspension)
    COMPONENT(Seats, seats)
    COMPONENT(Flags, stateFlags)

    /** @brief 创建并触摸全部组件。 */
    static VehicleEntity* createVehicle();
};

/** @brief 伤害修饰器（游戏侧注册，多重监听，按优先级调用）。 */
class IVehicleDamageModifier {
public:
    static constexpr const char* capabilityName = "IVehicleDamageModifier";

    virtual ~IVehicleDamageModifier() = default;

    /** @brief 返回修正后的伤害（默认原值；装甲区倍率在修饰器之后应用）。 */
    virtual float modifyDamage(VehicleEntity& v, float amount, const std::string& zone, int sourceId) = 0;
};

}  // namespace eve::vehicle
