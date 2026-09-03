#pragma once

/**
 * @brief 建筑放置模块的核心数据结构：定义、实例、变更事件、鬼影状态。
 * 全部策略名 / id / 标签使用字符串，便于 JSON 配置与跨版本兼容。
 */

#include "common/Revision.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::building {

/** @brief Canonical orientation of an edge in logical grid coordinates. */
enum class EdgeAxis : uint8_t { Horizontal, Vertical };

/**
 * @brief Canonical address of one grid edge.
 *
 * Horizontal `(x,y)` spans vertices `(x,y)` to `(x+1,y)`; vertical `(x,y)`
 * spans `(x,y)` to `(x,y+1)`. North/east/south/west input is normalized to
 * this representation so the same physical edge has exactly one owner.
 */
struct EdgeAddress {
    int x = 0;
    int y = 0;
    EdgeAxis axis = EdgeAxis::Horizontal;

    bool operator==(const EdgeAddress &) const = default;
};

/** @brief Canonical address of one logical grid vertex used by corner objects. */
struct CornerAddress {
    int x = 0;
    int y = 0;

    bool operator==(const CornerAddress &) const = default;
};

/** @brief Strong world-local identity of one authoritative edge-curve group. */
struct EdgeCurveGroupId {
    uint64_t value = 0;

    explicit operator bool() const { return value != 0; }
    bool operator==(const EdgeCurveGroupId &) const = default;
};

/** @brief One cubic Bezier control point in logical grid-vertex space. */
struct EdgeCurveControlPoint {
    float x = 0.f;
    float y = 0.f;

    bool operator==(const EdgeCurveControlPoint &) const = default;
};

/** @brief One committed 3D centerline frame sampled from a custom placement surface. */
struct EdgeCurveSurfaceSample {
    float worldX = 0.f;
    float worldY = 0.f;
    float worldZ = 0.f;
    float normalX = 0.f;
    float normalY = 1.f;
    float normalZ = 0.f;

    bool operator==(const EdgeCurveSurfaceSample &) const = default;
};

/** @brief World-owned authoritative description and membership of one committed edge curve. */
struct EdgeCurveGroup {
    EdgeCurveGroupId id;
    std::string buildingId;
    int level = 0;
    std::vector<EdgeCurveControlPoint> controlPoints;
    int subdivisions = 0;
    std::vector<int> instanceIds;
    /** @brief Provider key used to author surfaceSamples; empty means planar projection. */
    std::string surfaceProviderName;
    /** @brief Stable provider-returned surface identity shared by every committed sample. */
    std::string surfaceId;
    uint64_t surfaceRevision = 0;
    /** @brief Owning subdivisions+1 centerline frames for deterministic surface conformance. */
    std::vector<EdgeCurveSurfaceSample> surfaceSamples;

    bool operator==(const EdgeCurveGroup &) const = default;
};

/** @brief 建筑模板（进程级注册表中的定义）。 */
struct BuildingDefinition {
    std::string id;
    std::string displayName;
    std::string category;
    /** @brief 占地格子尺寸。 */
    /** 占用通道：同一格允许多通道建筑叠放（如 floor + furniture），空 = 默认通道。 */
    std::string channel;
    /** 视觉形态："2d" | "3d"，空 = 由渲染桥默认（2d）。仅元数据，building 模块不渲染。 */
    std::string renderMode;
    /** @brief Placement domain: `cell` (default), `edge`, `corner`, or `free`. */
    std::string placementKind = "cell";
    /** @brief Circular free-domain collision radius measured in grid-cell units. */
    float freeRadiusCells = 0.25f;
    /** @brief Optional oriented-box width in grid-cell units; positive width and height select OBB collision. */
    float freeFootprintWidthCells = 0.f;
    /** @brief Optional oriented-box height in grid-cell units; positive width and height select OBB collision. */
    float freeFootprintHeightCells = 0.f;
    /** @brief Optional convex local polygon as x/y pairs in grid-cell units, centered on the free anchor. */
    std::vector<float> freeFootprintVertices;
    /** @brief Edge topology compatibility group; empty falls back to building id. */
    std::string connectionGroup;
    /** @brief Structural classification such as floor, wall, ceiling, roof, or stair. */
    std::string structuralRole;
    /** @brief Structural support policy: `none`, `cell_below`, or `corner_below`. */
    std::string supportMode = "none";
    /** @brief Optional tag required on every supporting instance. */
    std::string supportTag;
    int footprintW = 1;
    int footprintH = 1;
    /** @brief 可选占地掩码，长度 footprintW*footprintH；空表示实心矩形。行主序、原点在最小 x/y。 */
    std::vector<uint8_t> footprintMask;
    /** @brief grid | cell | free | 自定义。 */
    std::string snapMode = "grid";
    /** @brief none | cardinal | free。 */
    std::string rotationMode = "cardinal";
    /** @brief default | boundsOnly | overlapOk | 自定义。 */
    std::string validateRule = "default";
    /** @brief Maximum angle between a footprint normal and the grid-plane up axis. */
    float maxSurfaceSlopeDegrees = 180.f;
    /** @brief Maximum elevation range across a sampled footprint; negative disables it. */
    float maxSurfaceHeightDelta = -1.f;
    std::vector<std::string> tags;
    /** @brief 空 = 不限制地形语义。 */
    std::vector<int> requireTerrain;
    std::vector<int> forbidTerrain;
    /** @brief 非空：邻格需有带该 tag 的建筑。 */
    std::string requireAdjacentTag;
    /** @brief >=0：邻格地形语义；-1 = 不限。 */
    int requireAdjacentTerrain = -1;
    std::unordered_map<std::string, int> cost;
    std::unordered_map<std::string, std::string> extra;
    /** 2D 视觉元数据：texture / frame / anchorX / anchorY / layer / colorR..B 等。 */
    std::unordered_map<std::string, std::string> visual2d;
    /** 3D 视觉元数据：mesh / height / colorR..B / offsetX..Z 等。 */
    std::unordered_map<std::string, std::string> visual3d;

    /** @brief 查询辅助。 */
    bool hasTag(const std::string &tag) const;
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;
    std::string getVisual2d(const std::string &key, const std::string &fallback = {}) const;
    std::string getVisual3d(const std::string &key, const std::string &fallback = {}) const;
    int getCost(const std::string &resource, int fallback = 0) const;
    /** @brief 占地掩码在局部坐标 (localX, localY) 处是否占用。 */
    bool maskAt(int localX, int localY) const;
};

/**
 * @brief One persistent member of a placed building's logical garrison.
 *
 * The building instance owns this value. It is deliberately a compact domain
 * record rather than an ECS base class; links to an RTS/RPG entity belong in
 * the member's stable id and are resolved by that domain.
 */
struct GarrisonMember {
    std::string              id;
    std::string              type = "building.garrison.member";
    std::vector<std::string> tags;
};

/** @brief 已放置的建筑实例。 */
struct PlacedBuilding {
    int instanceId = 0;
    std::string buildingId;
    /** @brief Placement domain snapshot: `cell`, `edge`, `corner`, or `free`. */
    std::string placementKind = "cell";
    /** @brief Canonical edge address when placementKind is `edge`. */
    EdgeAddress edge;
    /** @brief Optional link to a world-owned curve group; zero means an ordinary edge. */
    EdgeCurveGroupId edgeCurveGroupId;
    /** @brief Canonical grid vertex when placementKind is `corner`. */
    CornerAddress corner;
    /** @brief Committed free-domain collision radius in world-plane units. */
    float freeRadius = 0.f;
    /** @brief Committed OBB half width in world-plane units; zero denotes circular collision. */
    float freeHalfWidth = 0.f;
    /** @brief Committed OBB half height in world-plane units; zero denotes circular collision. */
    float freeHalfHeight = 0.f;
    /** @brief Committed convex local polygon as world-plane x/y pairs; empty selects OBB or circle. */
    std::vector<float> freeFootprintVertices;
    int originCellX = 0;
    int originCellY = 0;
    /** @brief Authoritative discrete floor coordinate used by occupancy and topology. */
    int level = 0;
    float worldX = 0.f;
    float worldY = 0.f;
    /** 垂直高度（平面法向）：XY 平面 = 世界 Z；XZ 平面 = 世界 Y。 */
    float elevation = 0.f;
    /** @brief Stable provider-local surface name used to create this placement; empty for unsurfaced placement. */
    std::string surfaceId;
    /** @brief Provider revision observed when the surface attachment was sampled. */
    uint64_t surfaceRevision = 0;
    /** @brief Unit surface normal captured at placement time. */
    float surfaceNormalX = 0.f;
    float surfaceNormalY = 1.f;
    float surfaceNormalZ = 0.f;
    /** @brief Unit tangent captured with the normal to define the local placement frame. */
    float surfaceTangentX = 1.f;
    float surfaceTangentY = 0.f;
    float surfaceTangentZ = 0.f;
    int surfaceSampleCount = 0;
    float surfaceMaxSlopeDegrees = 0.f;
    float surfaceHeightDelta = 0.f;
    float rotationDeg = 0.f;
    std::string channel;
    std::unordered_map<std::string, std::string> props;
    std::vector<std::string> tags;
    /** @brief Derived support links rebuilt from authoritative level/address data. */
    std::vector<int> supportInstanceIds;
    /** @brief Authoritative logical garrison membership, not a render list. */
    std::vector<GarrisonMember> garrison;
    /** @brief Revision used by the garrison container adapter for stale checks. */
    eve::Revision garrisonRevision = eve::Revision::zero();

    bool hasTag(const std::string &tag) const;
    std::string getProp(const std::string &key, const std::string &fallback = {}) const;
    void setProp(const std::string &key, const std::string &value);
};

/** @brief 一次成功放置变更的事件（供脚本 poll / C++ hook）。 */
struct BuildingChangeEvent {
    std::string action;  ///< place / remove / move / rotate
    std::string worldId;
    std::string buildingId;
    /** @brief Previous definition id for a replace event; empty for other actions. */
    std::string otherBuildingId;
    int instanceId = 0;
    int cellX = -1;
    int cellY = -1;
    int otherCellX = -1;
    int otherCellY = -1;
    int level = 0;
    int otherLevel = 0;
    float rotationDeg = 0.f;
    float worldX = 0.f;
    float worldY = 0.f;
    float elevation = 0.f;
    std::string channel;
};

/** @brief 校验上下文：传给可插拔规则。 */
struct PlacementQuery {
    std::string buildingId;
    int cellX = 0;
    int cellY = 0;
    /** @brief Discrete floor whose occupancy and adjacency are queried. */
    int level = 0;
    float worldX = 0.f;
    float worldY = 0.f;
    float elevation = 0.f;
    /** @brief Surface metadata available to validation rules for surface-derived ghosts. */
    std::string surfaceId;
    uint64_t surfaceRevision = 0;
    float surfaceNormalX = 0.f;
    float surfaceNormalY = 1.f;
    float surfaceNormalZ = 0.f;
    int surfaceSampleCount = 0;
    float surfaceMaxSlopeDegrees = 0.f;
    float surfaceHeightDelta = 0.f;
    float rotationDeg = 0.f;
    int excludeInstanceId = 0;  ///< move 时排除自身占用
};

/** @brief 吸附结果。 */
struct SnapResult {
    int cellX = 0;
    int cellY = 0;
    float worldX = 0.f;
    float worldY = 0.f;
    float elevation = 0.f;
};

/** @brief Owning before/after receipt produced by one committed placement edit. */
struct PlacementEditReceipt {
    PlacedBuilding before;
    PlacedBuilding after;
};

/** @brief Owning record of one committed dependent-first structural removal. */
struct StructuralRemovalReceipt {
    /** @brief Removed instances in the exact order in which occupancy was released. */
    std::vector<PlacedBuilding> removed;
};

/** @brief Result metadata for an atomic rebuild of all derived structural links. */
struct StructuralLinkRebuildReceipt {
    int inspectedCount = 0;
    int changedCount = 0;
};

}  // namespace eve::building
