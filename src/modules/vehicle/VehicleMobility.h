#pragma once

/**
 * @brief 载具移动模型接口。
 *
 * 新增一种载具类型（汽车 / 坦克 / 船 / 摩托 / 悬浮 / 直升机）= 实现本接口
 * 并按名字注册到 VehicleSystem（registerMobility），不需要改引擎头文件。
 * 移动模型只做"输入 → 运动"的转换：读 v.input()，写 v.motion()
 * （物理模式下通过 IVehicleBody 操作刚体）。
 */

namespace eve::vehicle {

class VehicleEntity;

/** @brief 移动模型：读输入、更新运动状态。 */
class IVehicleMobility {
public:
    virtual ~IVehicleMobility() = default;

    /** @brief 稳定名字（"kinematic" / "wheel" / "track" / "hover" / ...）。 */
    virtual const char* name() const = 0;

    /** @brief 每帧推进：把 v.input() 转成 v.motion() 的变化。 */
    virtual void update(VehicleEntity& v, float dt) = 0;
};

}  // namespace eve::vehicle
