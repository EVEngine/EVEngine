#pragma once

// 放置会话：封装「选择建筑 -> 跟随指针 -> 校验 -> 放置 / 拆除」的交互状态。
// 纯逻辑、不接输入设备；脚本每帧喂指针坐标 / 表面命中，会话内部驱动 Ghost。

#include <string>

namespace eve::building {

class PlacementWorld;
class Ghost;

class PlacementSession {
public:
    PlacementSession();
    ~PlacementSession() = default;

    PlacementSession(const PlacementSession &) = delete;
    PlacementSession &operator=(const PlacementSession &) = delete;

    void destroy();

    bool startPlacement(PlacementWorld *world, const std::string &buildingId);
    void stopPlacement();
    bool isActive() const { return active_; }
    PlacementWorld *getWorld() const { return world_; }
    std::string getBuildingId() const;
    Ghost *getGhost() const { return ghost_; }

    /** 交互模式："place" 放置 / "remove" 拆除（默认 place）。 */
    void setMode(const std::string &mode);
    std::string getMode() const { return mode_; }

    void setRotationDeg(float deg);
    void rotateBy(float deltaDeg);
    float getRotationDeg() const;

    /** 2D：世界坐标刷新鬼影并校验。 */
    bool updateFromWorld(PlacementWorld *world, float worldX, float worldY);
    /** 3D：真实世界坐标刷新鬼影并校验。 */
    bool updateFromWorld3D(PlacementWorld *world, float worldX, float worldY, float worldZ);
    /** 通过注册表面（如 "plane"）刷新鬼影并校验。 */
    bool updateFromSurface(PlacementWorld *world, const std::string &surface, float x, float y);

    bool isValid() const;
    std::string getReason() const;

    /**
     * 确认当前姿态：
     *  place  -> world.placeGhost(ghost)，成功返回 instanceId（0 = 失败）。
     *  remove -> 拆除鬼影所在格（默认通道）的建筑，返回被拆 instanceId（0 = 无）。
     */
    int execute();

private:
    void refreshValidate();

    PlacementWorld *world_ = nullptr;
    Ghost *ghost_ = nullptr;
    std::string mode_ = "place";
    bool active_ = false;
};

}  // namespace eve::building
