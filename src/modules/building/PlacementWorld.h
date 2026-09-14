#pragma once

/**
 * @brief 放置世界：格子占用（多通道）+ 地形语义 + 已放置建筑实例。
 * 行为由 PlacementSystem 提供；本类暴露便于脚本绑定的薄封装方法。
 * 坐标换算统一走 eve::grid（支持 rectangle / iso / staggered / hex 与 XY/XZ 平面轴）。
 */

#include "building/BuildingTypes.h"
#include "common/Result.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::map {
class TileLayer;
}

namespace eve::grid {
struct GridConfig;
}

namespace eve::building {

class Ghost;

/** @brief 格子型建筑放置世界（脚本可直接操作）。 */
class PlacementWorld {
public:
    using ChannelMap = std::unordered_map<std::string, std::vector<int>>;
    /** @brief 创建 width×height 的格子世界，cellSize 为像素/格。 */
    PlacementWorld(int width, int height, float cellSize = 32.f);
    ~PlacementWorld();

    PlacementWorld(const PlacementWorld &) = delete;
    PlacementWorld &operator=(const PlacementWorld &) = delete;

    /** @brief 释放资源并使其失效。 */
    void destroy();

    /** @brief 世界 id（用于变更事件定位）。 */
    std::string getId() const { return id_; }
    void setId(const std::string &id) { id_ = id; }

    /** @brief 尺寸 / 格子大小 / 原点。 */
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    float getCellSize() const;
    void setCellSize(float s);

    float getOriginX() const;
    float getOriginY() const;
    void setOrigin(float x, float y);

    /** @brief Height between discrete floors along the grid plane normal. */
    float getFloorHeight() const { return floorHeight_; }
    /** @brief Set positive height between discrete floors. Invalid values are ignored. */
    void setFloorHeight(float height);
    /** @brief Floor used by compatibility placement/query APIs. */
    int getActiveLevel() const { return activeLevel_; }
    /** @brief Select the floor used by compatibility placement/query APIs. */
    void setActiveLevel(int level) { activeLevel_ = level; }

    /** @brief 放置策略：吸附模式 / 校验规则。 */
    std::string getSnapMode() const { return snapMode_; }
    void setSnapMode(const std::string &mode) { snapMode_ = mode; }
    std::string getValidateRule() const { return validateRule_; }
    void setValidateRule(const std::string &rule) { validateRule_ = rule; }

    /** @brief 世界级额外属性（键值）。 */
    void setExtra(const std::string &key, const std::string &value);
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;

    // ---- Grid 配置（默认正交 2D，等价旧行为）----
    const grid::GridConfig &getGrid() const;
    grid::GridConfig &getGrid();
    void setGridLayout(const std::string &layout);
    std::string getGridLayoutName() const;
    void setGridPlane(const std::string &plane);
    std::string getGridPlaneName() const;
    void setCellGap(float gapX, float gapY);
    void setHexSideLength(float s);
    void setStagger(const std::string &axis, const std::string &index);
    /** 复用 TileLayer 的投影（orientation / stagger / hex / 尺寸 / 原点）。 */
    void setGridFromLayer(map::TileLayer *layer);
    bool hasGridFromLayer() const { return tileLayer_ != nullptr; }

    // ---- 坐标换算 ----
    /** @brief 世界像素 ↔ 格子坐标。 */
    /** worldX 恒为平面 X 轴。 */
    int worldToCellX(float worldX) const;
    /** worldY 为平面第二轴（XY 平面 = 世界 Y；XZ 平面 = 世界 Z）。 */
    int worldToCellY(float worldY) const;
    float cellToWorldX(int cellX) const;
    float cellToWorldY(int cellY) const;
    /** 平面坐标对（非正交布局下两个轴必须一起换算）。 */
    void cellToWorldPlane(int cellX, int cellY, float &px, float &py) const;
    /** 3D 换算：cell + 高度 -> 真实世界坐标。 */
    void cellToWorld3D(int cellX, int cellY, float elevation, float &worldX, float &worldY,
                       float &worldZ) const;
    float cellToWorld3DX(int cellX, int cellY, float elevation) const;
    float cellToWorld3DY(int cellX, int cellY, float elevation) const;
    float cellToWorld3DZ(int cellX, int cellY, float elevation) const;
    /** 3D 拾取：真实世界坐标 -> 最近格子。 */
    void worldToCell3D(float worldX, float worldY, float worldZ, int &cellX, int &cellY) const;
    int worldToCell3DX(float worldX, float worldY, float worldZ) const;
    int worldToCell3DY(float worldX, float worldY, float worldZ) const;

    /** @brief 地形语义（占用检查用）。 */
    void fillTerrain(int semantic);
    void setTerrain(int cellX, int cellY, int semantic);
    /** 手动 setTerrain 优先；绑定 tilemap 后未覆盖的格由 GID 映射懒解析。 */
    int getTerrain(int cellX, int cellY) const;
    bool inBounds(int cellX, int cellY) const;

    // ---- Tilemap 绑定 ----
    void bindTileLayer(map::TileLayer *layer);
    map::TileLayer *getTileLayer() const { return tileLayer_; }
    void clearTileLayer();
    void setTerrainGidMapJson(const std::string &json);
    void setTerrainGid(int gid, int semantic);
    void clearTerrainGidMap();

    // ---- 占用查询 ----
    /** @brief 占用查询：格子上建筑实例 / 是否为空。 */
    /** 默认通道（""）占用。 */
    int getOccupant(int cellX, int cellY) const;
    int getOccupantInChannel(const std::string &channel, int cellX, int cellY) const;
    /** @brief Query one channel at an explicit discrete floor. */
    int getOccupantAtLevel(const std::string &channel, int cellX, int cellY, int level) const;
    /** 跨通道第一个占用实例（邻接等跨层查询用）。 */
    int getAnyOccupant(int cellX, int cellY) const;
    /** @brief Query the first occupant across channels at an explicit floor. */
    int getAnyOccupantAtLevel(int cellX, int cellY, int level) const;
    bool isCellEmpty(int cellX, int cellY) const;
    /** @brief 已放置建筑实例查询。 */
    bool isCellEmptyInChannel(const std::string &channel, int cellX, int cellY) const;
    /** @brief Return the occupant of a canonicalized north/east/south/west cell edge. */
    int getEdgeOccupant(const std::string &channel, int cellX, int cellY,
                        const std::string &direction) const;
    /** @brief Query a canonical edge at an explicit discrete floor. */
    int getEdgeOccupantAtLevel(const std::string &channel, int cellX, int cellY,
                               const std::string &direction, int level) const;
    /** @brief Return whether an edge is unoccupied in the requested channel. */
    bool isEdgeEmpty(const std::string &channel, int cellX, int cellY,
                     const std::string &direction) const;
    /** @brief Query a corner-domain occupant at the active floor. */
    int getCornerOccupant(const std::string &channel, int vertexX, int vertexY) const;
    /** @brief Query a corner-domain occupant at an explicit floor. */
    int getCornerOccupantAtLevel(const std::string &channel, int vertexX, int vertexY,
                                 int level) const;
    /** @brief Query the first corner occupant across channels at an explicit floor. */
    int getAnyCornerOccupantAtLevel(int vertexX, int vertexY, int level) const;
    /** @brief Return whether a corner-domain address is empty in one channel. */
    bool isCornerEmpty(const std::string &channel, int vertexX, int vertexY) const;
    /** @brief Return the first free-domain object whose circular footprint contains the point. */
    int getFreeOccupant(const std::string &channel, float worldX, float worldY) const;
    /** @brief Query a free-domain footprint at an explicit floor. */
    int getFreeOccupantAtLevel(const std::string &channel, float worldX, float worldY,
                               int level) const;
    /** @brief Six-bit local connection mask for an edge object's compatible neighbours. */
    int getEdgeConnectionMask(int instanceId) const;
    /** @brief Stable topology variant name for an edge instance. */
    std::string getEdgeVariant(int instanceId) const;
    int getBuildingCount() const;
    bool hasBuilding(int instanceId) const;
    std::string getBuildingId(int instanceId) const;
    int getBuildingCellX(int instanceId) const;
    int getBuildingCellY(int instanceId) const;
    float getBuildingWorldX(int instanceId) const;
    float getBuildingWorldY(int instanceId) const;
    /** 真实世界 Z：XY 平面 = elevation；XZ 平面 = 平面第二轴。 */
    float getBuildingWorldZ(int instanceId) const;
    /** 垂直高度（平面法向）。 */
    float getBuildingElevation(int instanceId) const;
    /** @brief Return an instance's authoritative discrete floor. */
    int getBuildingLevel(int instanceId) const;
    /** @brief Number of direct structural supports used by an instance. */
    int getBuildingSupportCount(int instanceId) const;
    /** @brief Direct structural support id by stable footprint order, or zero. */
    int getBuildingSupportAt(int instanceId, int index) const;
    /** @brief Number of instances that directly depend on this instance. */
    int getBuildingDependentCount(int instanceId) const;
    /** @brief Surface provider identity captured when this instance was placed. */
    std::string getBuildingSurfaceId(int instanceId) const;
    /** @brief Surface provider revision captured when this instance was placed. */
    int64_t getBuildingSurfaceRevision(int instanceId) const;
    /** @brief X component of the unit surface normal captured at placement time. */
    float getBuildingSurfaceNormalX(int instanceId) const;
    /** @brief Y component of the unit surface normal captured at placement time. */
    float getBuildingSurfaceNormalY(int instanceId) const;
    /** @brief Z component of the unit surface normal captured at placement time. */
    float getBuildingSurfaceNormalZ(int instanceId) const;
    std::string getBuildingChannel(int instanceId) const;
    float getBuildingRotation(int instanceId) const;
    std::string getBuildingProp(int instanceId, const std::string &key,
                                const std::string &fallback = {}) const;
    void setBuildingProp(int instanceId, const std::string &key, const std::string &value);
    bool buildingHasTag(int instanceId, const std::string &tag) const;
    /** @brief 按插入顺序取实例 id。 */
    int getBuildingInstanceAt(int index) const;
    /** @brief Number of complete authoritative edge-curve groups in this world. */
    int getEdgeCurveGroupCount() const { return static_cast<int>(edgeCurveGroups_.size()); }
    /** @brief Return the next monotonic curve-group identity without reserving it. */
    EdgeCurveGroupId nextEdgeCurveGroupId() const { return {nextEdgeCurveGroupId_}; }
    /** @brief Return an owning ascending list of all curve-group identities. */
    std::vector<EdgeCurveGroupId> edgeCurveGroupIds() const;
    /**
     * @brief Return an owning curve-group snapshot.
     * @return NotFound when the strong world-local id is absent.
     * @thread Caller-thread only; the returned value remains valid across world mutation.
     */
    [[nodiscard]] eve::Result<EdgeCurveGroup> edgeCurveGroup(EdgeCurveGroupId id) const;
    /** @brief Return the owning curve-group snapshot linked by an edge instance. */
    [[nodiscard]] eve::Result<EdgeCurveGroup> edgeCurveGroupForInstance(int instanceId) const;

    /** @brief 便捷操作（转发 PlacementSystem）：放置 / 移除 / 移动。 */
    bool canPlace(const std::string &buildingId, int cellX, int cellY, float rotationDeg = 0.f);
    std::string canPlaceReason(const std::string &buildingId, int cellX, int cellY,
                               float rotationDeg = 0.f);
    int placeAt(const std::string &buildingId, int cellX, int cellY, float rotationDeg = 0.f);
    int placeAtWorld(const std::string &buildingId, float worldX, float worldY,
                     float rotationDeg = 0.f);
    int placeAtWorld3D(const std::string &buildingId, float worldX, float worldY, float worldZ,
                       float rotationDeg = 0.f);
    int placeGhost(Ghost *ghost);
    /** @brief Place an edge definition on a north/east/south/west edge; returns instance id or 0. */
    int placeEdge(const std::string &buildingId, int cellX, int cellY,
                  const std::string &direction);
    /** @brief Validate edge placement without mutation. */
    bool canPlaceEdge(const std::string &buildingId, int cellX, int cellY,
                      const std::string &direction);
    /** @brief Stable rejection token for edge placement. */
    std::string canPlaceEdgeReason(const std::string &buildingId, int cellX, int cellY,
                                   const std::string &direction);
    /** @brief Compatibility-only script projection of canonical corner placement. */
    int placeCorner(const std::string &buildingId, int vertexX, int vertexY);
    /** @brief Validate corner placement without mutation. */
    bool canPlaceCorner(const std::string &buildingId, int vertexX, int vertexY);
    /** @brief Stable corner placement rejection token. */
    std::string canPlaceCornerReason(const std::string &buildingId, int vertexX, int vertexY);
    /** @brief Compatibility-only script projection of exact free placement. */
    int placeFree(const std::string &buildingId, float worldX, float worldY,
                  float elevation = 0.f, float rotationDeg = 0.f);
    bool canPlaceFree(const std::string &buildingId, float worldX, float worldY);
    std::string canPlaceFreeReason(const std::string &buildingId, float worldX, float worldY);
    bool removeBuilding(int instanceId);
    /** @brief Stable rejection token for ordinary removal (`support_in_use` when depended on). */
    std::string canRemoveBuildingReason(int instanceId) const;
    /**
     * @brief Compatibility-only script projection of the canonical Result removal API.
     * @return Removed count, or zero when the canonical operation is rejected.
     */
    int removeBuildingCascade(int instanceId);
    bool moveBuilding(int instanceId, int cellX, int cellY, float rotationDeg = -1.f);
    void clearBuildings();

    /** @brief Clone complete logical state into an independently owned world candidate. */
    [[nodiscard]] std::unique_ptr<PlacementWorld> cloneState() const;
    /** @brief Atomically exchange complete logical state with a prepared candidate. */
    void swapState(PlacementWorld& candidate) noexcept;

    // ---- 供 System 直接访问 ----
    const std::vector<int> &occupancy() const { return occupancy_; }
    std::vector<int> &occupancy() { return occupancy_; }
    const std::vector<int> &terrain() const { return terrain_; }
    std::vector<int> &terrain() { return terrain_; }
    const std::unordered_map<std::string, std::vector<int>> &allChannels() const {
        return allChannels_;
    }
    std::unordered_map<std::string, std::vector<int>> &allChannels() { return allChannels_; }
    /** 取某通道占用数组；空通道返回默认 occupancy_，其它懒创建。 */
    std::vector<int> &channelOccupancy(const std::string &channel);
    /** @brief Mutable occupancy storage for a channel and explicit floor. */
    std::vector<int> &channelOccupancy(const std::string &channel, int level);
    const std::unordered_map<int, PlacedBuilding> &buildings() const { return buildings_; }
    std::unordered_map<int, PlacedBuilding> &buildings() { return buildings_; }

private:
    friend class PlacementSystem;

    int terrainFromGid(int cellX, int cellY) const;
    ChannelMap &edgeChannels(EdgeAxis axis, int level);
    /**
     * @brief Find edge channel storage without creating it.
     * @return Borrowed pointer owned by this world, or null when the level has no storage.
     * @lifetime Valid until this world is destroyed or its edge occupancy is mutated.
     */
    const ChannelMap *findEdgeChannels(EdgeAxis axis, int level) const;
    ChannelMap &cornerChannels(int level);
    /**
     * @brief Find corner channel storage without creating it.
     * @return Borrowed pointer owned by this world, or null when the level has no storage.
     * @lifetime Valid until this world is destroyed or its corner occupancy is mutated.
     */
    const ChannelMap *findCornerChannels(int level) const;

    std::string id_;
    int width_ = 0;
    int height_ = 0;
    float floorHeight_ = 3.f;
    int activeLevel_ = 0;
    std::unique_ptr<grid::GridConfig> grid_;
    std::string snapMode_ = "grid";
    std::string validateRule_ = "default";
    std::vector<int> occupancy_;
    std::vector<int> terrain_;
    std::vector<uint8_t> terrainOverrides_;  ///< 1 = 手动 setTerrain 覆盖
    std::unordered_map<std::string, std::vector<int>> allChannels_;
    std::unordered_map<std::string, std::vector<int>> horizontalEdgeChannels_;
    std::unordered_map<std::string, std::vector<int>> verticalEdgeChannels_;
    std::unordered_map<std::string, std::vector<int>> cornerChannels_;
    std::unordered_map<int, ChannelMap> cellChannelsByLevel_;
    std::unordered_map<int, ChannelMap> horizontalEdgesByLevel_;
    std::unordered_map<int, ChannelMap> verticalEdgesByLevel_;
    std::unordered_map<int, ChannelMap> cornersByLevel_;
    std::unordered_map<int, PlacedBuilding> buildings_;
    std::unordered_map<uint64_t, EdgeCurveGroup> edgeCurveGroups_;
    uint64_t nextEdgeCurveGroupId_ = 1;
    bool publishEvents_ = true;
    std::unordered_map<std::string, std::string> extra_;
    std::vector<int> instanceOrder_;

    // tilemap 绑定（可空）
    map::TileLayer *tileLayer_ = nullptr;
    bool terrainBound_ = false;
    std::unordered_map<int, int> terrainGidMap_;
};

}  // namespace eve::building
