#pragma once

#include "common/Module.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eve::graphics {
class Camera3D;
}

namespace eve::camera {

/**
 * 3D 摄像机控制器（eve.Camera / eve.CameraController）。
 *
 * 驱动一个 graphics::Camera3D，内置多种方便玩家使用的视角行为：
 *   - follow      自动追踪目标（第三人称跟随，镜头挂在 target + offset）
 *   - orbit       自动盘旋（绕 target 公转，azimuth 按 orbitSpeed 自动旋转）
 *   - topdown     俯视（从 target 正上方往下拍）
 *   - firstperson 第一人称（eye 位于 target，用 yaw/pitch 控制朝向）
 *   - cinematic   过场/自动切换视角（在一组命名 View 之间平滑插值）
 *
 * 所有行为都经过统一的指数阻尼平滑（setSmooth，对应"平滑移动"），
 * 也支持 maxSpeed 限速避免抖动，snap() 可立即到位不做平滑。
 */
class CameraController {
public:
    CameraController();
    ~CameraController() = default;

    // --- 绑定要驱动的摄像机 ---
    void setCamera(graphics::Camera3D *cam);
    graphics::Camera3D *getCamera() const;

    // --- 目标 / 观察锚点 ---
    void setTarget(float x, float y, float z);
    float getTargetX() const;
    float getTargetY() const;
    float getTargetZ() const;
    /** follow：摄像机相对 target 的偏移（默认 (0, 2, 6)）。 */
    void setOffset(float x, float y, float z);
    /** follow：额外的视线前移点（可让镜头看向玩家前方）。 */
    void setLookAhead(float x, float y, float z);

    // --- 视角模式 ---
    void setMode(const std::string &mode);
    std::string getMode() const;

    // --- orbit / topdown 参数 ---
    void setRadius(float r);               // orbit 半径 / topdown 高度
    void setAzimuth(float deg);            // orbit 方位角
    void setElevation(float deg);          // orbit 仰角
    void setOrbitSpeed(float degPerSec);   // 自动盘旋转速

    // --- firstperson 参数 ---
    void setYaw(float deg);
    void setPitch(float deg);

    // --- 平滑 ---
    void setSmooth(float damping);         // 每秒指数阻尼，越大越跟手
    void setMaxSpeed(float unitsPerSec);   // 0 = 不限速
    void snap();                           // 立即应用当前目标视角

    // --- cinematic / 自动切换视角 ---
    void addView(const std::string &name, float ex, float ey, float ez,
                 float tx, float ty, float tz);
    bool switchTo(const std::string &name, float blendTime);
    void playSequence(float stepTime);     // 每隔 stepTime 秒切换到下一个 View
    void stopSequence();
    bool isPlaying() const;

    // --- 每帧驱动 ---
    void update(float dt);

private:
    struct View {
        std::string name;
        glm::vec3 eye{0.f};
        glm::vec3 target{0.f};
    };

    View desired() const;
    void applyView(const View &v);
    void easeToward(const View &v, float dt);

    graphics::Camera3D *cam_ = nullptr;

    std::string mode_ = "follow";

    glm::vec3 target_{0.f, 0.f, 0.f};
    glm::vec3 offset_{0.f, 2.f, 6.f};
    glm::vec3 lookAhead_{0.f, 0.f, 0.f};

    float radius_ = 10.f;
    float azimuthDeg_ = 45.f;
    float elevationDeg_ = 30.f;
    float orbitSpeedDeg_ = 20.f;

    float yawDeg_ = 0.f;
    float pitchDeg_ = 0.f;

    float smooth_ = 6.f;
    float maxSpeed_ = 0.f;
    bool  initialized_ = false;
    View  cur_{};

    // cinematic
    std::vector<View> views_;
    int   viewIndex_ = -1;
    bool  playing_ = false;
    float stepTime_ = 3.f;
    float timer_ = 0.f;
    bool  blending_ = false;
    View  blendFrom_{};
    View  blendTo_{};
    float blendT_ = 0.f;
    float blendDur_ = 1.f;
};

/**
 * 摄像机模块命名空间（eve.Camera）。主要职责是把 CameraController 绑定进脚本。
 */
class Camera : public Module {
public:
    Module_REG(Camera);
    Camera() = default;
    ~Camera() override = default;
};

}  // namespace eve::camera
