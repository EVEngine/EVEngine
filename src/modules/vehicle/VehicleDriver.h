#pragma once

/**
 * @brief 驾驶者接口与玩家控制状态。
 *
 * FPS/直接驾驶面：游戏每帧把玩家输入（键盘/鼠标/手柄）写入
 * VehicleSystem::setPlayerControls，座位系统把控制路由给驾驶员座位
 * （移动输入）与武器座位（瞄准/开火）。新增一种驾驶行为（AI 驾驶、
 * 触屏、脚本驾驶）= 实现 IVehicleDriver 并按名字注册。
 */

namespace eve::vehicle {

struct VehicleInput;
class VehicleEntity;

/** @brief 一名玩家的原始控制（归一化，游戏侧填充）。角度为度。 */
struct PlayerControl {
    float throttle  = 0.f;
    float steer     = 0.f;
    float brake     = 0.f;
    bool  handbrake = false;
    bool  fire      = false;
    float aimYaw    = 0.f;
    float aimPitch  = 0.f;
};

/** @brief 驾驶者：把某乘客的控制转成载具输入。 */
class IVehicleDriver {
public:
    virtual ~IVehicleDriver() = default;

    /**
     * @brief 稳定名字（"player" 等）。
     * @return Borrowed non-null pointer to immutable provider-owned/static text.
     * @ownership The driver implementation owns the storage; callers must not free it.
     * @lifetime Valid while the driver implementation is registered; copy the text if retaining it.
     * @thread Query on the registry/vehicle thread unless the implementation documents a broader guarantee.
     * @reentrancy This accessor must not invoke callbacks or mutate the registry.
     */
    virtual const char* name() const = 0;

    /** @brief 采样 occupantId 乘客的控制；返回是否有效（有效则填充 out）。 */
    virtual bool sample(VehicleEntity& v, int occupantId, VehicleInput& out) = 0;
};

}  // namespace eve::vehicle
