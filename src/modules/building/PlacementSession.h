#pragma once

// 放置会话：封装「选择建筑 -> 跟随指针 -> 校验 -> 放置 / 拆除」的交互状态。
// 纯逻辑、不接输入设备；脚本每帧喂指针坐标 / 表面命中，会话内部驱动 Ghost。

#include <string>
#include <vector>

#include "building/PlacementSystem.h"

namespace eve::building {

class PlacementWorld;
class Ghost;

/** @brief Outcome of refreshing an edge placement preview. */
enum class EdgeUpdateStatus { Updated, Rejected };
/** @brief Outcome of refreshing a corner placement preview. */
enum class CornerUpdateStatus { Updated, Rejected };
/** @brief Outcome of refreshing an exact free-object placement preview. */
enum class FreeUpdateStatus { Updated, Rejected };
/** @brief Outcome of committing one continuous edge line. */
enum class EdgeLineExecuteStatus { Placed, Rejected };
/** @brief Outcome of adding a logical vertex to an edge path preview. */
enum class EdgePathUpdateStatus { Updated, Rejected };
/** @brief Outcome of committing one move or replace edit. */
enum class PlacementEditExecuteStatus { Committed, Rejected };
/** @brief Outcome of refreshing or committing an area tool request. */
enum class AreaExecuteStatus { Accepted, Rejected };
/** @brief Outcome of configuring or previewing a unified placement pattern. */
enum class PatternUpdateStatus { Updated, Rejected };
/** @brief Outcome of atomically committing a unified placement pattern. */
enum class PatternExecuteStatus { Placed, Rejected };

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
    /** @brief Refresh an edge-object ghost from a cell-relative cardinal edge. */
    EdgeUpdateStatus updateEdge(PlacementWorld *world, int cellX, int cellY,
                                const std::string &direction);
    /** @brief Refresh a corner-object ghost at a canonical grid vertex. */
    CornerUpdateStatus updateCorner(PlacementWorld *world, int vertexX, int vertexY);
    /** @brief Refresh a free-object ghost at an exact world-plane position. */
    FreeUpdateStatus updateFree(PlacementWorld *world, float worldX, float worldY,
                                float elevation = 0.f);

    bool isValid() const;
    std::string getReason() const;

    /**
     * 确认当前姿态：
     *  place  -> world.placeGhost(ghost)，成功返回 instanceId（0 = 失败）。
     *  remove -> 拆除鬼影所在格（默认通道）的建筑，返回被拆 instanceId（0 = 无）。
     */
    int execute();
    /** @brief Atomically place an axis-aligned edge line between logical vertices. */
    EdgeLineExecuteStatus executeEdgeLine(int startVertexX, int startVertexY, int endVertexX,
                                          int endVertexY);
    /** @brief Start a multi-segment edge path at one logical grid vertex. */
    EdgePathUpdateStatus beginEdgePath(int vertexX, int vertexY);
    /** @brief Append one orthogonally connected logical vertex to the current edge path. */
    EdgePathUpdateStatus appendEdgePathVertex(int vertexX, int vertexY);
    /** @brief Atomically commit the complete edge path. */
    EdgeLineExecuteStatus executeEdgePath();
    /** @brief Number of logical vertices in the current edge path. */
    int getEdgePathVertexCount() const { return static_cast<int>(edgePathVertices_.size()); }
    /** @brief Begin authoring four cubic Bezier controls in logical grid space. */
    EdgePathUpdateStatus beginEdgeCurve(float x, float y);
    /** @brief Append a cubic Bezier control point; exactly four controls are accepted. */
    EdgePathUpdateStatus appendEdgeCurveControlPoint(float x, float y);
    /** @brief Sample and atomically commit the authored cubic Bezier curve. */
    EdgeLineExecuteStatus executeEdgeCurve(int subdivisions);
    /**
     * @brief Sample a named custom surface and atomically commit the authored curve.
     * @param subdivisions Analytic curve segment count in the supported placement range.
     * @param surfaceName Registered surface provider sampled for every curve frame.
     * @return Placed only after both edge occupancy and every surface sample validate.
     * @thread Caller-thread only; the session and world are borrowed for the call.
     */
    EdgeLineExecuteStatus executeEdgeCurveOnSurface(int subdivisions,
                                                    const std::string &surfaceName);
    /** @brief Number of currently authored cubic Bezier control points. */
    int getEdgeCurveControlPointCount() const {
        return static_cast<int>(edgeCurveControlPoints_.size());
    }
    /** @brief Borrow the authored controls for same-frame presentation adapters. */
    const std::vector<PlacementSystem::EdgeCurvePoint> &edgeCurveControlPoints() const {
        return edgeCurveControlPoints_;
    }
    /** @brief Number of ids committed by the last successful edge-line operation. */
    int getLastPlacedCount() const { return static_cast<int>(lastPlacedIds_.size()); }
    /** @brief Instance id from the last successful edge-line operation, or zero. */
    int getLastPlacedId(int index) const;
    /** @brief Move an existing instance to the current domain-specific ghost pose atomically. */
    PlacementEditExecuteStatus executeMove(int instanceId);
    /** @brief Replace an existing instance with the selected definition atomically. */
    PlacementEditExecuteStatus executeReplace(int instanceId);
    /** @brief Instance id affected by the last successful move/replace, or zero. */
    int getLastEditedId() const { return lastEditedId_; }
    /** @brief Cache a non-mutating inclusive rectangular preview for UI/BuildingFX. */
    AreaExecuteStatus previewRectangle(int minCellX, int minCellY, int maxCellX, int maxCellY);
    /** @brief Cache a non-mutating circular cell brush preview. */
    AreaExecuteStatus previewBrush(int centerCellX, int centerCellY, int radius);
    /** @brief Atomically place an inclusive rectangle using the selected definition. */
    AreaExecuteStatus executeRectangle(int minCellX, int minCellY, int maxCellX, int maxCellY);
    /** @brief Atomically place a circular brush using the selected definition. */
    AreaExecuteStatus executeBrush(int centerCellX, int centerCellY, int radius);
    /** @brief Number of cells in the last successful preview. */
    int getAreaPreviewCount() const { return static_cast<int>(areaPreview_.cells.size()); }
    int getAreaPreviewCellX(int index) const;
    int getAreaPreviewCellY(int index) const;
    bool getAreaPreviewAccepted(int index) const;
    std::string getAreaPreviewReason(int index) const;

    /**
     * @brief Start a renderer-neutral built-in pattern request.
     * @param kind edge_line, edge_path, edge_cubic_bezier, rectangle_fill,
     * rectangle_outline, or circle_brush.
     * @return Updated when the kind is known and the session can author it.
     */
    PatternUpdateStatus beginPattern(const std::string &kind);
    /** @brief Append one logical grid point/control to the current pattern. */
    PatternUpdateStatus appendPatternPoint(float x, float y);
    /** @brief Configure the circle radius used by circle_brush. */
    PatternUpdateStatus setPatternRadius(int radius);
    /** @brief Configure analytic segments used by edge_cubic_bezier. */
    PatternUpdateStatus setPatternSubdivisions(int subdivisions);
    /** @brief Configure an optional named surface for edge_cubic_bezier. */
    PatternUpdateStatus setPatternSurface(const std::string &surfaceName);
    /** @brief Expand and cache the complete pattern without world mutation. */
    PatternUpdateStatus previewPattern();
    /** @brief Atomically commit the complete cached pattern request. */
    PatternExecuteStatus executePattern();
    /** @brief Stable name of the currently authored pattern, or empty. */
    std::string getPatternKind() const;
    /** @brief Number of authoring points in the current pattern. */
    int getPatternPointCount() const {
        return static_cast<int>(patternRequest_.points.size());
    }
    /** @brief Number of concrete edge/cell anchors in the last pattern preview. */
    int getPatternPreviewCount() const {
        return patternPreview_.anchorCount();
    }
    /** @brief Logical x coordinate for one concrete preview anchor, or zero. */
    int getPatternPreviewX(int index) const;
    /** @brief Logical y coordinate for one concrete preview anchor, or zero. */
    int getPatternPreviewY(int index) const;
    /** @brief Edge axis for an edge preview anchor, or empty for area anchors. */
    std::string getPatternPreviewAxis(int index) const;
    /** @brief Whether one concrete preview anchor passed validation. */
    bool getPatternPreviewAccepted(int index) const;
    /** @brief Stable rejection reason for an area anchor, or empty. */
    std::string getPatternPreviewReason(int index) const;

private:
    void refreshValidate();

    PlacementWorld *world_ = nullptr;
    Ghost *ghost_ = nullptr;
    std::string mode_ = "place";
    bool active_ = false;
    std::vector<int> lastPlacedIds_;
    std::vector<CornerAddress> edgePathVertices_;
    std::vector<PlacementSystem::EdgeCurvePoint> edgeCurveControlPoints_;
    int lastEditedId_ = 0;
    PlacementSystem::AreaPreview areaPreview_;
    PlacementSystem::PatternRequest patternRequest_;
    PlacementSystem::PatternPreview patternPreview_;
    bool patternActive_ = false;
};

}  // namespace eve::building
