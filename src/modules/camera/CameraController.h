#pragma once

#include "common/Module.h"

#include <glm/glm.hpp>

#include <string>
#include <cstdint>
#include <deque>
#include <vector>

namespace eve::graphics {
class Camera3D;
}
namespace eve::event {
class Event;
}
namespace eve::scene {
class SceneNodeRef;
}

namespace eve::camera {

/**
 * @brief 3D 摄像机控制器（eve.Camera / eve.CameraController）。
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
    void                setCamera(graphics::Camera3D* cam);
    graphics::Camera3D* getCamera() const;

    // --- 目标 / 观察锚点 ---
    void setTarget(float x, float y, float z);
    /** @brief Follow a stable scene-node handle. nullptr restores explicit coordinates. */
    void  setTargetNode(scene::SceneNodeRef* node);
    float getTargetX() const;
    float getTargetY() const;
    float getTargetZ() const;
    /** @brief follow：摄像机相对 target 的偏移（默认 (0, 2, 6)）。 */
    void setOffset(float x, float y, float z);
    /** @brief follow：额外的视线前移点（可让镜头看向玩家前方）。 */
    void setLookAhead(float x, float y, float z);

    // --- 视角模式 ---
    void        setMode(const std::string& mode);
    std::string getMode() const;

    // --- orbit / topdown 参数 ---
    void setRadius(float r);              // orbit 半径 / topdown 高度
    void setAzimuth(float deg);           // orbit 方位角
    void setElevation(float deg);         // orbit 仰角
    void setOrbitSpeed(float degPerSec);  // 自动盘旋转速

    // --- firstperson 参数 ---
    void setYaw(float deg);
    void setPitch(float deg);
    /** @brief Applies device-independent yaw, pitch and zoom deltas. */
    void addInput(float yawDeltaDeg, float pitchDeltaDeg, float zoomDelta);

    // --- framing / lens ---
    /** @brief Screen-space composition offset, expressed as normalized view fractions. */
    void setComposition(float screenX, float screenY);
    /** @brief Dead-zone radius in world units around the tracked target. */
    void  setDeadZone(float radius);
    void  setFov(float degrees);
    float getFov() const;

    // --- 平滑 ---
    void setSmooth(float damping);  // 每秒指数阻尼，越大越跟手
    void setPositionSmooth(float damping);
    void setTargetSmooth(float damping);
    void setMaxSpeed(float unitsPerSec);  // 0 = 不限速
    void snap();                          // 立即应用当前目标视角

    // --- obstruction / collision ---
    /** @brief Enables swept-sphere obstruction against boxes registered below. */
    void setCollisionEnabled(bool enabled);
    void setCollisionRadius(float radius);
    void setCollisionRecovery(float damping);
    /** @brief Sets the Box3D category mask used by dynamic obstruction queries. */
    void setCollisionMask(uint64_t maskBits);
    /** @brief Excludes one Box3D body id, normally the followed player body. */
    void setCollisionIgnoredBody(int bodyId);
    void clearCollisionBoxes();
    void addCollisionBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    bool isObstructed() const;
    int  getCollisionBodyId() const;

    // --- camera rigs / director ---
    bool addRig(const std::string& name, const std::string& mode, int priority);
    bool removeRig(const std::string& name);
    bool setRigPriority(const std::string& name, int priority);
    bool setRigEnabled(const std::string& name, bool enabled);
    /** @brief Stores the controller's current framing/lens parameters in a rig preset. */
    bool        saveRigState(const std::string& name);
    bool        activateRig(const std::string& name, float blendTime);
    std::string getActiveRig() const;

    // --- additive modifiers ---
    /** @brief Adds a damped positional/rotational camera impulse. */
    void addImpulse(float positionAmplitude, float rotationAmplitude, float duration, unsigned int seed = 0);
    void addFovImpulse(float degrees, float duration);
    void clearImpulses();

    // --- cinematic / 自动切换视角 ---
    void addView(const std::string& name, float ex, float ey, float ez, float tx, float ty, float tz);
    bool switchTo(const std::string& name, float blendTime);
    void playSequence(float stepTime);  // 每隔 stepTime 秒切换到下一个 View
    void stopSequence();
    bool isPlaying() const;

    // --- timeline / events ---
    void        clearTimeline();
    bool        addTimelineCut(float time, const std::string& rigName, float blendTime);
    bool        addTimelineEvent(float time, const std::string& name, const std::string& data);
    /** @brief Adds a linearly interpolated camera property key. */
    bool        addTimelineFloat(float time, const std::string& property, float value);
    void        setEventSink(event::Event* sink);
    void        playTimeline(bool loop);
    void        pauseTimeline();
    void        stopTimeline();
    void        seekTimeline(float time, bool fireEvents = false);
    bool        isTimelinePlaying() const;
    float       getTimelineTime() const;
    std::string consumeTimelineEvent();
    std::string getTimelineEventData() const;
    int         getPendingTimelineEventCount() const;
    float       getTimelineDuration() const;
    int         getRigCount() const;
    /** @brief Serializes rigs and timeline data as a versioned JSON asset. */
    std::string serializeAsset() const;
    /** @brief Replaces rigs and timeline data from a versioned JSON asset. */
    bool        deserializeAsset(const std::string& json);

    // --- 每帧驱动 ---
    void update(float dt);

private:
    struct View {
        std::string name;
        glm::vec3   eye{0.f};
        glm::vec3   target{0.f};
        float       fov = 60.f;
    };

    struct Rig {
        std::string name;
        std::string mode;
        int         priority = 0;
        bool        enabled  = true;
        glm::vec3   target{0.f};
        glm::vec3   offset{0.f, 2.f, 6.f};
        glm::vec3   lookAhead{0.f};
        glm::vec2   composition{0.f};
        float       radius    = 10.f;
        float       azimuth   = 45.f;
        float       elevation = 30.f;
        float       yaw       = 0.f;
        float       pitch     = 0.f;
        float       fov       = 60.f;
        float       smooth    = 6.f;
        float       maxSpeed  = 0.f;
    };

    struct CollisionBox {
        glm::vec3 min{0.f};
        glm::vec3 max{0.f};
    };
    struct Impulse {
        float        positionAmplitude = 0.f;
        float        rotationAmplitude = 0.f;
        float        duration          = 0.f;
        float        age               = 0.f;
        unsigned int seed              = 0;
        float        fovAmplitude      = 0.f;
    };
    struct TimelineCut {
        float       time = 0.f;
        std::string rig;
        float       blend = 0.f;
        bool        fired = false;
    };
    struct TimelineEvent {
        float       time = 0.f;
        std::string name;
        std::string data;
        bool        fired = false;
    };
    struct TimelineFloat {
        float       time = 0.f;
        std::string property;
        float       value = 0.f;
    };

    View        desired() const;
    void        applyView(const View& v);
    void        easeToward(const View& v, float dt);
    void        updateTrackedTarget();
    void        updateDirector();
    void        applyCollision(View& v, float dt);
    void        applyModifiers(View& v, float dt);
    void        updateTimeline(float dt);
    void        evaluateTimelineFloats();
    void        emitTimelineEvent(const TimelineEvent& marker);
    static bool validMode(const std::string& mode);

    graphics::Camera3D* cam_ = nullptr;

    std::string mode_ = "follow";

    glm::vec3   target_{0.f, 0.f, 0.f};
    std::string targetNodeHost_;
    std::string targetNodeId_;
    glm::vec3   offset_{0.f, 2.f, 6.f};
    glm::vec3   lookAhead_{0.f, 0.f, 0.f};

    float radius_        = 10.f;
    float azimuthDeg_    = 45.f;
    float elevationDeg_  = 30.f;
    float orbitSpeedDeg_ = 20.f;

    float     yawDeg_   = 0.f;
    float     pitchDeg_ = 0.f;
    glm::vec2 composition_{0.f};
    float     deadZone_ = 0.f;
    glm::vec3 deadZoneTarget_{0.f};
    bool      deadZoneInitialized_ = false;
    float     fovDeg_              = 60.f;

    float smooth_         = 6.f;
    float positionSmooth_ = 6.f;
    float targetSmooth_   = 6.f;
    float maxSpeed_       = 0.f;
    bool  initialized_    = false;
    View  cur_{};

    bool                      collisionEnabled_  = false;
    float                     collisionRadius_   = 0.25f;
    float                     collisionRecovery_ = 4.f;
    float                     collisionDistance_ = -1.f;
    bool                      obstructed_        = false;
    uint64_t                  collisionMask_     = UINT64_MAX;
    int                       collisionIgnoredBody_ = -1;
    int                       collisionBodyId_      = -1;
    std::vector<CollisionBox> collisionBoxes_;

    std::vector<Rig>     rigs_;
    std::string          activeRig_;
    bool                 directorEnabled_ = false;
    std::vector<Impulse> impulses_;

    // cinematic
    std::vector<View> views_;
    int               viewIndex_ = -1;
    bool              playing_   = false;
    float             stepTime_  = 3.f;
    float             timer_     = 0.f;
    bool              blending_  = false;
    View              blendFrom_{};
    View              blendTo_{};
    float             blendT_   = 0.f;
    float             blendDur_ = 1.f;

    std::vector<TimelineCut>   timelineCuts_;
    std::vector<TimelineEvent> timelineEvents_;
    std::vector<TimelineFloat> timelineFloats_;
    event::Event*              eventSink_        = nullptr;
    float                      timelineTime_     = 0.f;
    float                      timelineDuration_ = 0.f;
    bool                       timelinePlaying_  = false;
    bool                       timelineLoop_     = false;
    std::deque<std::pair<std::string, std::string>> pendingTimelineEvents_;
    std::string                consumedTimelineEventData_;
};

/**
 * @brief 摄像机模块命名空间（eve.Camera）。主要职责是把 CameraController 绑定进脚本。
 */
class Camera : public Module {
public:
    Module_REG(Camera);
    Camera()           = default;
    ~Camera() override = default;
};

}  // namespace eve::camera
