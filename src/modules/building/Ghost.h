#pragma once

// 鬼影预览：候选建筑姿态 + 最近一次校验结果，供 UI 着色与确认放置。

#include "building/BuildingTypes.h"

#include <string>

namespace eve::building {

class PlacementWorld;

class Ghost {
public:
    Ghost() = default;
    ~Ghost() = default;

    void destroy();

    std::string getBuildingId() const { return buildingId_; }
    void setBuildingId(const std::string &id);

    int getCellX() const { return cellX_; }
    int getCellY() const { return cellY_; }
    void setCell(int cellX, int cellY);

    float getWorldX() const { return worldX_; }
    float getWorldY() const { return worldY_; }
    void setWorld(float worldX, float worldY);
    float getElevation() const { return elevation_; }
    void setElevation(float elevation);

    float getRotationDeg() const { return rotationDeg_; }
    void setRotationDeg(float deg);
    void rotateBy(float deltaDeg);

    bool isValid() const { return valid_; }
    std::string getReason() const { return reason_; }

    /** @brief 按世界吸附模式，从世界坐标刷新格子与世界位姿。 */
    void setFromWorld(PlacementWorld *world, float worldX, float worldY);
    /** 3D 版本：真实世界坐标 (wx, wy, wz)，按世界平面轴映射后吸附。 */
    void setFromWorld3D(PlacementWorld *world, float worldX, float worldY, float worldZ);
    /** 通过注册的放置表面（如内置 "plane"）刷新位姿。 */
    void setFromSurface(PlacementWorld *world, const std::string &surface, float x, float y);
    /** @brief 对当前姿态做校验，写入 valid_/reason_。 */
    bool validate(PlacementWorld *world);

private:
    friend class PlacementSystem;

    std::string buildingId_;
    int cellX_ = 0;
    int cellY_ = 0;
    float worldX_ = 0.f;
    float worldY_ = 0.f;
    float elevation_ = 0.f;
    float rotationDeg_ = 0.f;
    bool valid_ = false;
    std::string reason_;
};

}  // namespace eve::building
