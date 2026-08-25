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
    /** @brief 按名字取移动模型；未注册返回 nullptr。 */
    static IVehicleMobility* findMobility(const std::string& name);
    /** @brief 已注册移动模型数量。 */
    static int mobilityCount();

    /** @brief 注册驾驶者实现；同名替换。 */
    static void registerDriver(IVehicleDriver* driver);
    /** @brief 按名字取驾驶者；未注册返回 nullptr。 */
    static IVehicleDriver* findDriver(const std::string& name);
    /** @brief 已注册驾驶者数量。 */
    static int driverCount();

    /** @brief 写入某玩家的控制状态（游戏每帧填充）。 */
    static void setPlayerControls(int playerId, const PlayerControl& control);
    /** @brief 取某玩家的控制状态；未设置返回 nullptr。 */
    static const PlayerControl* playerControls(int playerId);

    /** @brief 注册事件汇（替换旧的；传 nullptr 清空）。 */
    static void setEventSink(VehicleEventSink sink);

    /** @brief 每帧推进：命令 → 输入 → 移动模型。自动瞄准由 Vehicle 模块完成。 */
    static void update(VehicleEntity& v, float dt);

    /** @brief 处理命令队列：把队首命令转成 v.input()。 */
    static void processOrders(VehicleEntity& v, float dt);

    /** @brief 追加一条命令；Move/AttackMove 会清空旧命令（RTS 习惯）。 */
    static void pushOrder(VehicleEntity& v, const VehicleOrder& order);
    /** @brief 清空命令队列。 */
    static void clearOrders(VehicleEntity& v);
    /** @brief 当前命令；无命令返回 nullptr。 */
    static const VehicleOrder* currentOrder(VehicleEntity& v);

    /** @brief 按角度差生成转向输入（-1..1）。 */
    static float steerToward(VehicleEntity& v, float targetHeadingDeg);

    /** @brief 座位：进入 / 离开 / 按玩家找座位。 */
    static bool enterSeat(VehicleEntity& v, int seatIndex, int playerId);
    static bool exitSeat(VehicleEntity& v, int seatIndex);
    static int  findSeatByPlayer(VehicleEntity& v, int playerId);
};

}  // namespace eve::vehicle
