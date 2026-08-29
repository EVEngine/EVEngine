#pragma once

/**
 * @brief 载具行为静态系统：移动模型注册表 + 命令队列 + 每帧推进。
 */

#include "vehicle/VehicleDriver.h"
#include "vehicle/VehicleTypes.h"

#include <functional>
#include <string>

namespace eve::vehicle {

class IVehicleMobility;
class IVehicleDriver;

/** @brief 事件汇：由 Vehicle 模块（或测试）注册，把事件写入自己的队列。 */
using VehicleEventSink = std::function<void(const VehicleEvent&)>;

/** @brief 载具系统：移动注册 + 命令处理 + 更新。 */
class VehicleSystem {
public:
    /** @brief 注册移动模型实现；同名替换。 */
    static void registerMobility(IVehicleMobility* mobility);
    /**
     * @brief 按名字取移动模型；未注册返回 nullptr。
     * @return Borrowed nullable implementation owned by the static registry.
     * @ownership VehicleSystem owns only the registry entry; the provider owns the implementation.
     * @lifetime Valid while registered and until replacement/unregister; do not retain across registry mutation.
     * @thread Call on the Vehicle registry thread.
     * @reentrancy The lookup does not invoke provider callbacks or mutate the registry.
     */
    static IVehicleMobility* findMobility(const std::string& name);
    /** @brief 已注册移动模型数量。 */
    static int mobilityCount();

    /** @brief 注册驾驶者实现；同名替换。 */
    static void registerDriver(IVehicleDriver* driver);
    /**
     * @brief 按名字取驾驶者；未注册返回 nullptr。
     * @return Borrowed nullable implementation owned by the static registry.
     * @ownership VehicleSystem owns only the registry entry; the provider owns the implementation.
     * @lifetime Valid while registered and until replacement/unregister; do not retain across registry mutation.
     * @thread Call on the Vehicle registry thread.
     * @reentrancy The lookup does not invoke provider callbacks or mutate the registry.
     */
    static IVehicleDriver* findDriver(const std::string& name);
    /** @brief 已注册驾驶者数量。 */
    static int driverCount();

    /** @brief 写入某玩家的控制状态（游戏每帧填充）。 */
    static void setPlayerControls(int playerId, const PlayerControl& control);
    /**
     * @brief 取某玩家的控制状态；未设置返回 nullptr。
     * @return Borrowed nullable state owned by VehicleSystem's control store.
     * @ownership VehicleSystem owns the stored control record; callers must not delete it.
     * @lifetime Valid until the next write for this player or registry reset.
     * @thread Call on the vehicle input thread.
     * @reentrancy The lookup does not invoke callbacks; copy the state before re-entering input code.
     */
    static const PlayerControl* playerControls(int playerId);

    /** @brief 注册事件汇（替换旧的；传 nullptr 清空）。 */
    static void setEventSink(VehicleEventSink sink);

    /** @brief 每帧推进：命令 → 输入 → 移动模型。自动瞄准由 Vehicle 模块完成。 */
    static void update(VehicleEntity& v, float dt);

    /** @brief 处理命令队列：把队首命令转成 v.input()。 */
    static void processOrders(VehicleEntity& v, float dt);

    /**
     * @brief 追加一条命令；Move/AttackMove 会清空旧命令（RTS 习惯）。
     * @return The generic queue identity, or a structured rejection.
     */
    [[nodiscard]] static eve::Result<void> pushOrder(VehicleEntity& v, const VehicleOrder& order);
    /** @brief 清空命令队列。 */
    static void clearOrders(VehicleEntity& v);
    /**
     * @brief 当前命令；无命令返回 nullptr。
     * @return Borrowed nullable order owned by the vehicle's order adapter.
     * @ownership The VehicleEntity/order adapter owns the command; callers never release it.
     * @lifetime Valid until the next order mutation or vehicle destruction.
     * @thread Call on the vehicle's owning simulation thread.
     * @reentrancy Do not retain across callbacks or order mutation.
     */
    static const VehicleOrder* currentOrder(VehicleEntity& v);

    /** @brief 按角度差生成转向输入（-1..1）。 */
    static float steerToward(VehicleEntity& v, float targetHeadingDeg);

    /** @brief 座位：进入 / 离开 / 按玩家找座位。 */
    static bool enterSeat(VehicleEntity& v, int seatIndex, int playerId);
    static bool exitSeat(VehicleEntity& v, int seatIndex);
    static int  findSeatByPlayer(VehicleEntity& v, int playerId);
};

}  // namespace eve::vehicle
