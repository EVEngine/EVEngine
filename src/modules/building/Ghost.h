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
    /** @brief Current placement domain (`cell`, `edge`, `corner`, or `free`). */
    std::string getPlacementKind() const { return placementKind_; }
    /** @brief Canonical edge axis (`horizontal`/`vertical`), empty for cell ghosts. */
    std::string getEdgeAxis() const;
    /** @brief Snap this ghost to a canonicalized cell edge and its exact midpoint pose. */
    void setEdge(PlacementWorld *world, int cellX, int cellY, const std::string &direction);
    /** @brief Snap this ghost to one canonical grid vertex. */
    void setCorner(PlacementWorld *world, int vertexX, int vertexY);
    /** @brief Set an exact unsnapped world-plane anchor for a free-domain definition. */
    void setFree(PlacementWorld *world, float worldX, float worldY, float elevation = 0.f);

    bool isValid() const { return valid_; }
    std::string getReason() const { return reason_; }
    /** @brief Surface identity captured by the last successful setFromSurface call. */
    std::string getSurfaceId() const { return surfaceId_; }
    /** @brief Surface revision captured by the last successful setFromSurface call. */
    int64_t getSurfaceRevision() const { return static_cast<int64_t>(surfaceRevision_); }
    /** @brief X component of the captured unit surface normal. */
    float getSurfaceNormalX() const { return surfaceNormalX_; }
    /** @brief Y component of the captured unit surface normal. */
    float getSurfaceNormalY() const { return surfaceNormalY_; }
    /** @brief Z component of the captured unit surface normal. */
    float getSurfaceNormalZ() const { return surfaceNormalZ_; }
    /** @brief X component of the captured unit surface tangent. */
    float getSurfaceTangentX() const { return surfaceTangentX_; }
    /** @brief Y component of the captured unit surface tangent. */
    float getSurfaceTangentY() const { return surfaceTangentY_; }
    /** @brief Z component of the captured unit surface tangent. */
    float getSurfaceTangentZ() const { return surfaceTangentZ_; }
    /** @brief Number of occupied footprint cells sampled in the current patch. */
    int getSurfaceSampleCount() const { return surfaceSampleCount_; }
    /** @brief Maximum sampled slope relative to the grid-plane up axis. */
    float getSurfaceMaxSlopeDegrees() const { return surfaceMaxSlopeDegrees_; }
    /** @brief Sampled elevation range across the complete footprint. */
    float getSurfaceHeightDelta() const { return surfaceHeightDelta_; }

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
    std::string surfaceId_;
    uint64_t surfaceRevision_ = 0;
    float surfaceNormalX_ = 0.f;
    float surfaceNormalY_ = 1.f;
    float surfaceNormalZ_ = 0.f;
    float surfaceTangentX_ = 1.f;
    float surfaceTangentY_ = 0.f;
    float surfaceTangentZ_ = 0.f;
    int surfaceSampleCount_ = 0;
    float surfaceMaxSlopeDegrees_ = 0.f;
    float surfaceHeightDelta_ = 0.f;
    std::string surfaceProviderName_;
    float surfaceInputX_ = 0.f;
    float surfaceInputY_ = 0.f;
    bool surfacePatchStale_ = false;
    float rotationDeg_ = 0.f;
    std::string placementKind_ = "cell";
    EdgeAddress edge_;
    CornerAddress corner_;
    bool valid_ = false;
    std::string reason_;
};

}  // namespace eve::building
