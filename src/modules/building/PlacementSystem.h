#pragma once

// 放置操作静态入口 + 可插拔校验 / 吸附 / 变更钩子。
//
// C++ 侧通过 register* 扩展；脚本侧通过定义/世界上的策略名字符串选用已注册规则。

#include "building/BuildingTypes.h"
#include "common/Result.h"

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::building {

class PlacementWorld;
class Ghost;
class HeightfieldSurface;
class StaticMeshSurface;

/** @brief Outcome of restoring an exact placement snapshot. */
enum class PlacementRestoreStatus { Restored, Rejected };

class PlacementSystem {
public:
    using ValidateFn = std::function<bool(const PlacementWorld &world, const PlacementQuery &q,
                                          std::string *reason)>;
    using SnapFn =
        std::function<SnapResult(const PlacementWorld &world, float worldX, float worldY)>;
    using ChangeHook = std::function<void(const BuildingChangeEvent &ev)>;

    /** @brief One sampled point and orthonormal frame on a placement surface. */
    struct PlacementHit {
        float worldX = 0.f;
        float worldY = 0.f;
        float worldZ = 0.f;
        float normalX = 0.f;
        float normalY = 1.f;
        float normalZ = 0.f;
        float tangentX = 1.f;
        float tangentY = 0.f;
        float tangentZ = 0.f;
        float bitangentX = 0.f;
        float bitangentY = 0.f;
        float bitangentZ = 1.f;
        std::string surfaceId;
        uint64_t surfaceRevision = 0;
        uint64_t primitiveId = 0;
        std::vector<std::string> tags;
    };
    /** @brief One footprint-relative surface sample. */
    struct SurfacePatchSample {
        int cellX = 0;
        int cellY = 0;
        PlacementHit hit;
    };
    /** @brief Surface samples and continuity metrics for one rotated building footprint. */
    struct SurfacePatch {
        PlacementHit anchor;
        std::vector<SurfacePatchSample> samples;
        float maxSlopeDegrees = 0.f;
        float heightDelta = 0.f;
    };
    /** @brief Atomic result of placing one axis-aligned continuous edge line. */
    struct EdgeLinePlacement {
        std::vector<int> instanceIds;
    };
    /** @brief Atomic result of placing one multi-segment edge path. */
    struct EdgePathPlacement {
        std::vector<int> instanceIds;
    };
    /** @brief Owning validated edge addresses for one non-mutating path preview. */
    struct EdgePathPreview {
        std::string buildingId;
        int level = 0;
        std::vector<EdgeAddress> edges;
    };
    /** @brief One control point in logical grid-vertex space. */
    using EdgeCurvePoint = EdgeCurveControlPoint;
    /** @brief Owning same-identity surface frames sampled along an analytic edge curve. */
    struct EdgeCurveSurface {
        std::string providerName;
        std::string surfaceId;
        uint64_t surfaceRevision = 0;
        std::vector<EdgeCurveSurfaceSample> samples;
    };
    /** @brief One requested area anchor and its deterministic validation result. */
    struct AreaCellPreview {
        int cellX = 0;
        int cellY = 0;
        bool accepted = false;
        std::string reason;
    };
    /** @brief Owning row-major preview for a rectangular or circular brush request. */
    struct AreaPreview {
        std::string buildingId;
        int level = 0;
        float rotationDeg = 0.f;
        std::vector<AreaCellPreview> cells;
        int acceptedCount = 0;
        int rejectedCount = 0;
    };
    /** @brief Atomic receipt for a committed area placement. */
    struct AreaPlacement {
        AreaPreview preview;
        std::vector<int> instanceIds;
    };
    /** @brief Built-in geometry families accepted by the unified placement-pattern protocol. */
    enum class PatternKind {
        EdgeLine,
        EdgePath,
        EdgeCubicBezier,
        RectangleFill,
        RectangleOutline,
        CircleBrush
    };
    /** @brief Owning, renderer-neutral request for one atomic placement pattern. */
    struct PatternRequest {
        PatternKind kind = PatternKind::RectangleFill;
        std::vector<EdgeCurvePoint> points;
        int radius = 0;
        int subdivisions = 16;
        float rotationDeg = 0.f;
        std::string surfaceName;
    };
    /** @brief Owning non-mutating expansion of an edge or area pattern. */
    struct PatternPreview {
        PatternRequest request;
        EdgePathPreview edge;
        AreaPreview area;
        /** @brief Number of concrete edge/cell anchors expanded by the pattern. */
        int anchorCount() const {
            return request.kind == PatternKind::EdgeLine ||
                           request.kind == PatternKind::EdgePath ||
                           request.kind == PatternKind::EdgeCubicBezier
                       ? static_cast<int>(edge.edges.size())
                       : static_cast<int>(area.cells.size());
        }
    };
    /** @brief Atomic placement receipt shared by every built-in pattern kind. */
    struct PatternPlacement {
        PatternPreview preview;
        std::vector<int> instanceIds;
    };
    /**
     * 放置表面：把指针坐标（2D 场景为鼠标世界坐标；3D 场景由游戏自行做
     * 射线/高度采样后传入平面坐标）换算成真实世界命中点。
     * 引擎只提供接口 + 内置 "plane"（Y=常数平面），物理射线 / 高度场等
     * 表面由游戏侧按接口实现。
     */
    using SurfaceFn = std::function<bool(const PlacementWorld &world, float x, float y,
                                         PlacementHit *hit)>;
    /**
     * @brief Canonical surface provider contract.
     * @return An owning sample or a structured diagnostic. Providers execute synchronously on the
     *         caller thread and must not retain references to `world` or invoke script callbacks.
     */
    using SurfaceProviderFn =
        std::function<eve::Result<PlacementHit>(const PlacementWorld &world, float x, float y)>;

    /**
     * @brief Process singleton holding all PlacementSystem state.
     *
     * Rules / hooks / events / counters used to live in one function-local
     * static per accessor, which made the state invisible, non-destructible
     * and hard to reason about across hot-reload and tests. State now lives in
     * this single instance's members; the static API below delegates to it.
     */
    static PlacementSystem &inst();

    static void registerValidateRule(const std::string &name, ValidateFn fn);
    static void unregisterValidateRule(const std::string &name);
    static bool hasValidateRule(const std::string &name);

    static void registerSnapRule(const std::string &name, SnapFn fn);
    static void unregisterSnapRule(const std::string &name);
    static bool hasSnapRule(const std::string &name);

    static void registerChangeHook(const std::string &name, ChangeHook fn);
    static void unregisterChangeHook(const std::string &name);
    static bool hasChangeHook(const std::string &name);

    static void registerSurface(const std::string &name, SurfaceFn fn);
    /** @brief Register or replace a structured surface provider. */
    static void registerSurfaceProvider(const std::string &name, SurfaceProviderFn fn);
    /**
     * @brief Register an immutable built-in heightfield and retain it until unregistration.
     * @return Applied, or a structured validation failure without changing the registry.
     * @thread Caller-thread only; registration must not race sampling or unregistration.
     */
    [[nodiscard]] static eve::Result<void>
    registerHeightfieldSurface(const std::string &name,
                               std::shared_ptr<const HeightfieldSurface> surface);
    /**
     * @brief Register an immutable BVH-accelerated triangle mesh and retain its snapshot.
     * @param name Registry key used by placement and preview operations.
     * @param surface Immutable owning mesh snapshot retained until replacement or removal.
     * @return Applied, or a structured validation failure without changing the registry.
     * @thread Caller-thread only; registration must not race sampling or unregistration.
     */
    [[nodiscard]] static eve::Result<void>
    registerStaticMeshSurface(const std::string &name,
                              std::shared_ptr<const StaticMeshSurface> surface);
    static void unregisterSurface(const std::string &name);
    static bool hasSurface(const std::string &name);
    static bool surfaceHit(const PlacementWorld &world, const std::string &name, float x,
                           float y, PlacementHit *hit);
    /**
     * @brief Sample a named placement surface.
     * @return Owning hit data, or NotFound/Rejected with a stable diagnostic.
     * @thread Caller-thread only; provider registration must not race sampling.
     */
    [[nodiscard]] static eve::Result<PlacementHit>
    sampleSurface(const PlacementWorld &world, const std::string &name, float x, float y);
    /**
     * @brief Sample the occupied cells of a rotated footprint relative to an exact anchor hit.
     * @return A continuous same-surface/same-revision patch, or a structured failure.
     */
    [[nodiscard]] static eve::Result<SurfacePatch>
    sampleSurfacePatch(const PlacementWorld &world, const std::string &buildingId,
                       const std::string &surfaceName, float x, float y,
                       float rotationDegrees = 0.f);
    static std::vector<std::string> surfaceNames();
    /** 内置 "plane" 表面的常量高度（默认 0）。 */
    static void setPlaneSurfaceHeight(float h);
    static float getPlaneSurfaceHeight();

    /** @brief 确保内置规则已注册（模块首次使用时自动调用）。 */
    static void ensureBuiltins();

    static SnapResult snap(const PlacementWorld &world, const std::string &buildingId, float worldX,
                           float worldY);
    /** 3D 版本：按世界平面轴把 (wx, wy, wz) 映射到网格平面坐标再吸附。 */
    static SnapResult snap3D(const PlacementWorld &world, const std::string &buildingId,
                             float worldX, float worldY, float worldZ);
    static SnapResult snapWithMode(const PlacementWorld &world, const std::string &mode,
                                   float worldX, float worldY);

    /** @brief 规范化旋转角（cardinal → 0/90/180/270；none → 0）。 */
    static float normalizeRotation(const std::string &buildingId, float rotationDeg);

    /** @brief 旋转后的占地宽高（cardinal 90/270 交换）。 */
    static void effectiveFootprint(const BuildingDefinition &def, float rotationDeg, int *outW,
                                   int *outH);

    /** @brief 枚举占地格子（旋转后局部 → 世界格子）。返回 false 若定义未知。 */
    static bool foreachFootprintCell(const BuildingDefinition &def, int originCellX,
                                     int originCellY, float rotationDeg,
                                     const std::function<bool(int cx, int cy)> &fn);

    /** @brief Normalize a cell-relative cardinal direction to a unique edge address. */
    [[nodiscard]] static eve::Result<EdgeAddress> canonicalEdge(int cellX, int cellY,
                                                                 const std::string &direction);
    /** @brief Validate an edge-domain placement without mutating the world. */
    static bool canPlaceEdge(PlacementWorld *world, const std::string &buildingId, int cellX,
                             int cellY, const std::string &direction,
                             int excludeInstanceId = 0, std::string *reason = nullptr,
                             int level = std::numeric_limits<int>::min());
    /** @brief Place an edge-domain definition, returning the new instance id or zero. */
    static int placeEdge(PlacementWorld *world, const std::string &buildingId, int cellX,
                         int cellY, const std::string &direction);
    /** @brief Validate a corner-domain placement at one canonical grid vertex. */
    static bool canPlaceCorner(PlacementWorld *world, const std::string &buildingId, int vertexX,
                               int vertexY, int excludeInstanceId = 0,
                               std::string *reason = nullptr,
                               int level = std::numeric_limits<int>::min());
    /**
     * @brief Atomically place a corner-domain definition.
     * @return Owning placed snapshot, or a diagnostic without world/event mutation.
     * @thread Caller-thread only; hooks run synchronously after commit.
     */
    [[nodiscard]] static eve::Result<PlacedBuilding>
    placeCornerResult(PlacementWorld *world, const std::string &buildingId, int vertexX,
                      int vertexY);
    /** @brief Compatibility-only id projection; prefer placeCornerResult in C++. */
    static int placeCorner(PlacementWorld *world, const std::string &buildingId, int vertexX,
                           int vertexY);
    /** @brief Validate an exact free-domain world-plane anchor and circular or oriented-box footprint. */
    static bool canPlaceFree(PlacementWorld *world, const std::string &buildingId, float worldX,
                             float worldY, int excludeInstanceId = 0,
                             std::string *reason = nullptr,
                             int level = std::numeric_limits<int>::min(),
                             float rotationDeg = 0.f);
    /** @brief Test a world-plane point against a committed circular or oriented-box footprint. */
    static bool containsFreePoint(const PlacedBuilding &placed, float worldX, float worldY);
    /** @brief Atomically place a free-domain definition at an unsnapped world-plane anchor. */
    [[nodiscard]] static eve::Result<PlacedBuilding>
    placeFreeResult(PlacementWorld *world, const std::string &buildingId, float worldX,
                    float worldY, float elevation = 0.f, float rotationDeg = 0.f);
    /** @brief Atomically place a free object from an owning validated surface patch snapshot. */
    [[nodiscard]] static eve::Result<PlacedBuilding>
    placeFreeSurfaceResult(PlacementWorld *world, const std::string &buildingId,
                           const SurfacePatch &patch, float rotationDeg = 0.f);
    /** @brief Compatibility-only id projection; prefer placeFreeResult in C++. */
    static int placeFree(PlacementWorld *world, const std::string &buildingId, float worldX,
                         float worldY, float elevation = 0.f, float rotationDeg = 0.f);
    /**
     * @brief Compute compatible neighbouring edge topology.
     * @return Bits 0..5: collinear start/end, perpendicular start -/+, perpendicular end -/+.
     */
    static uint8_t edgeConnectionMask(const PlacementWorld &world, int instanceId);
    /** @brief Stable visual topology class: isolated/end/straight/corner/tee/cross. */
    static std::string edgeVariant(const PlacementWorld &world, int instanceId);
    /**
     * @brief Atomically place every unit edge between two logical grid vertices.
     * @return All new instance ids, or a failure with no occupancy, instance, or event mutation.
     */
    [[nodiscard]] static eve::Result<EdgeLinePlacement>
    placeEdgeLine(PlacementWorld *world, const std::string &buildingId, int startVertexX,
                  int startVertexY, int endVertexX, int endVertexY);
    /**
     * @brief Atomically place every unit edge along a multi-segment orthogonal vertex path.
     * @return Ordered instance ids, or a diagnostic without world/event mutation.
     * @thread Caller-thread only; hooks run synchronously after the complete commit.
     */
    [[nodiscard]] static eve::Result<EdgePathPlacement>
    placeEdgePath(PlacementWorld *world, const std::string &buildingId,
                  const std::vector<CornerAddress> &vertices);
    /** @brief Validate and expand a multi-segment orthogonal path without mutation. */
    [[nodiscard]] static eve::Result<EdgePathPreview>
    previewEdgePath(PlacementWorld *world, const std::string &buildingId,
                    const std::vector<CornerAddress> &vertices);
    /**
     * @brief Deterministically rasterize four cubic Bezier controls to adjacent grid vertices.
     * @return An owning vertex path, or InvalidArgument for invalid controls/subdivision limits.
     */
    [[nodiscard]] static eve::Result<std::vector<CornerAddress>>
    sampleEdgeCubicBezier(const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions);
    /** @brief Sample and validate a cubic Bezier edge curve without mutating the world. */
    [[nodiscard]] static eve::Result<EdgePathPreview>
    previewEdgeCubicBezier(PlacementWorld *world, const std::string &buildingId,
                           const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions);
    /** @brief Atomically place the deterministic grid-edge rasterization of a cubic Bezier. */
    [[nodiscard]] static eve::Result<EdgePathPlacement>
    placeEdgeCubicBezier(PlacementWorld *world, const std::string &buildingId,
                         const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions);
    /** @brief Sample one same-identity custom surface frame at every analytic curve segment. */
    [[nodiscard]] static eve::Result<EdgeCurveSurface>
    sampleEdgeCurveSurface(const PlacementWorld &world, const std::string &surfaceName,
                           const std::vector<EdgeCurvePoint> &controlPoints, int subdivisions);
    /** @brief Atomically place a curve whose committed centerline follows a custom surface. */
    [[nodiscard]] static eve::Result<EdgePathPlacement>
    placeEdgeCubicBezierOnSurface(PlacementWorld *world, const std::string &buildingId,
                                  const std::vector<EdgeCurvePoint> &controlPoints,
                                  int subdivisions, const std::string &surfaceName);
    /** @brief Preview every anchor in an inclusive rectangular area without mutation. */
    [[nodiscard]] static eve::Result<AreaPreview>
    previewRectangle(PlacementWorld *world, const std::string &buildingId, int minCellX,
                     int minCellY, int maxCellX, int maxCellY, float rotationDeg = 0.f);
    /** @brief Preview anchors inside an inclusive Euclidean cell-radius brush. */
    [[nodiscard]] static eve::Result<AreaPreview>
    previewBrush(PlacementWorld *world, const std::string &buildingId, int centerCellX,
                 int centerCellY, int radius, float rotationDeg = 0.f);
    /** @brief Preview only the perimeter anchors of an inclusive rectangle. */
    [[nodiscard]] static eve::Result<AreaPreview>
    previewRectangleOutline(PlacementWorld *world, const std::string &buildingId,
                            int minCellX, int minCellY, int maxCellX, int maxCellY,
                            float rotationDeg = 0.f);
    /** @brief Atomically commit a previously shaped rectangle; any rejection aborts all cells. */
    [[nodiscard]] static eve::Result<AreaPlacement>
    placeRectangle(PlacementWorld *world, const std::string &buildingId, int minCellX,
                   int minCellY, int maxCellX, int maxCellY, float rotationDeg = 0.f);
    /** @brief Atomically commit a circular brush; any rejection aborts all cells. */
    [[nodiscard]] static eve::Result<AreaPlacement>
    placeBrush(PlacementWorld *world, const std::string &buildingId, int centerCellX,
               int centerCellY, int radius, float rotationDeg = 0.f);
    /** @brief Atomically commit only the perimeter anchors of an inclusive rectangle. */
    [[nodiscard]] static eve::Result<AreaPlacement>
    placeRectangleOutline(PlacementWorld *world, const std::string &buildingId,
                          int minCellX, int minCellY, int maxCellX, int maxCellY,
                          float rotationDeg = 0.f);
    /**
     * @brief Expand any built-in placement pattern without mutating the world.
     * @param world Borrowed placement world.
     * @param buildingId Definition placed at every expanded anchor.
     * @param request Owning geometry and pattern options.
     * @return Complete edge/area preview or a structured validation failure.
     */
    [[nodiscard]] static eve::Result<PatternPreview>
    previewPattern(PlacementWorld *world, const std::string &buildingId,
                   const PatternRequest &request);
    /**
     * @brief Validate and atomically commit any built-in placement pattern.
     * @return Unified preview and all committed instance ids, or no mutation on failure.
     * @thread Caller-thread only; hooks run synchronously after the whole pattern commits.
     */
    [[nodiscard]] static eve::Result<PatternPlacement>
    placePattern(PlacementWorld *world, const std::string &buildingId,
                 const PatternRequest &request);

    static bool canPlace(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                         float rotationDeg = 0.f, int excludeInstanceId = 0,
                         std::string *reason = nullptr);
    /** 带 elevation 的校验（校验语义仍按 2D 占用/地形，elevation 只进 query 上下文）。 */
    static bool canPlaceElev(PlacementWorld *world, const std::string &buildingId, int cellX,
                             int cellY, float elevation, float rotationDeg = 0.f,
                             int excludeInstanceId = 0, std::string *reason = nullptr,
                             int level = std::numeric_limits<int>::min());

    /** @brief 成功返回 instanceId；失败返回 0。 */
    static int placeAt(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                       float rotationDeg = 0.f);
    /** @brief 先吸附再放置；free/自定义 snap 会保留吸附后的世界坐标。 */
    static int placeAtWorld(PlacementWorld *world, const std::string &buildingId, float worldX,
                            float worldY, float rotationDeg = 0.f);
    /** 3D 版本：真实世界坐标 (wx, wy, wz)，按平面轴吸附后放置。 */
    static int placeAtWorld3D(PlacementWorld *world, const std::string &buildingId, float worldX,
                              float worldY, float worldZ, float rotationDeg = 0.f);
    static int placeGhost(PlacementWorld *world, Ghost *ghost);

    /**
     * @brief Restore an exact persistent instance for editor undo or snapshot loading.
     * @param world Destination placement world.
     * @param placed Complete instance value with a positive unused instance id.
     * @param reason Optional validation failure token.
     * @return Restored when validation passed and the exact id/pose was restored; otherwise Rejected.
     */
    static PlacementRestoreStatus restoreExact(PlacementWorld *world, const PlacedBuilding &placed,
                                                std::string *reason = nullptr);

    /**
     * @brief Atomically restore an exact curve group and all of its exact-id edge members.
     * @param world Destination placement world.
     * @param group Complete group metadata with a positive unused identity.
     * @param members Complete edge instances ordered exactly like group.instanceIds.
     * @return Owning restored group, or a diagnostic with no destination mutation.
     */
    [[nodiscard]] static eve::Result<EdgeCurveGroup>
    restoreEdgeCurveGroupExact(PlacementWorld *world, const EdgeCurveGroup &group,
                               const std::vector<PlacedBuilding> &members);

    static bool removeBuilding(PlacementWorld *world, int instanceId);
    /**
     * @brief Atomically remove a support subtree in dependent-first order.
     * @return Owning removal receipt, or a diagnostic without world/event mutation.
     */
    [[nodiscard]] static eve::Result<StructuralRemovalReceipt>
    removeBuildingCascadeResult(PlacementWorld *world, int instanceId);
    /** @brief Compatibility-only count projection; prefer removeBuildingCascadeResult in C++. */
    static int removeBuildingCascade(PlacementWorld *world, int instanceId);
    /**
     * @brief Atomically validate and rebuild every derived structural support link.
     *
     * Intended for snapshot restore and definition hot reload. Validation and link computation
     * happen in an independently owned candidate; failure leaves the source world unchanged.
     * @return Counts for the committed rebuild, or a diagnostic naming the invalid instance.
     */
    [[nodiscard]] static eve::Result<StructuralLinkRebuildReceipt>
    rebuildStructuralLinksResult(PlacementWorld *world);
    /**
     * @brief Atomically move a cell-domain instance while preserving its identity and state.
     * @return An owning before/after receipt, or a diagnostic without world/event mutation.
     * @thread Caller-thread only; change hooks run synchronously after the commit.
     */
    [[nodiscard]] static eve::Result<PlacementEditReceipt>
    moveBuildingResult(PlacementWorld *world, int instanceId, int cellX, int cellY,
                       float rotationDeg = -1.f);
    /**
     * @brief Atomically move an edge-domain instance to a canonical grid edge.
     * @return An owning before/after receipt, or a diagnostic without world/event mutation.
     * @thread Caller-thread only; change hooks run synchronously after the commit.
     */
    [[nodiscard]] static eve::Result<PlacementEditReceipt>
    moveEdgeResult(PlacementWorld *world, int instanceId, int cellX, int cellY,
                   const std::string &direction);
    /**
     * @brief Atomically move a corner-domain instance to another grid vertex.
     * @return An owning before/after receipt, or a diagnostic without world/event mutation.
     * @thread Caller-thread only; change hooks run synchronously after commit.
     */
    [[nodiscard]] static eve::Result<PlacementEditReceipt>
    moveCornerResult(PlacementWorld *world, int instanceId, int vertexX, int vertexY);
    /** @brief Atomically move a free-domain instance to an exact world-plane anchor. */
    [[nodiscard]] static eve::Result<PlacementEditReceipt>
    moveFreeResult(PlacementWorld *world, int instanceId, float worldX, float worldY,
                   float elevation, float rotationDeg);
    /** @brief Atomically move a free-domain instance and replace its surface attachment. */
    [[nodiscard]] static eve::Result<PlacementEditReceipt>
    moveFreeSurfaceResult(PlacementWorld *world, int instanceId, const SurfacePatch &patch,
                          float rotationDeg);
    /**
     * @brief Atomically replace an instance definition at its current cell or edge address.
     *
     * The instance id, properties, garrison and surface attachment remain owned by the instance.
     * Source and replacement definitions must use the same placement domain.
     * @return An owning before/after receipt, or a diagnostic without world/event mutation.
     * @thread Caller-thread only; change hooks run synchronously after the commit.
     */
    [[nodiscard]] static eve::Result<PlacementEditReceipt>
    replaceBuildingResult(PlacementWorld *world, int instanceId,
                          const std::string &replacementBuildingId);
    /** @brief Compatibility projection of moveBuildingResult; prefer the Result API in C++. */
    static bool moveBuilding(PlacementWorld *world, int instanceId, int cellX, int cellY,
                             float rotationDeg = -1.f);
    static void clearBuildings(PlacementWorld *world);

    static void pushEvent(BuildingChangeEvent ev);
    static void pollEvents(std::vector<BuildingChangeEvent> &out);
    static void clearEvents();
    static const std::vector<BuildingChangeEvent> &events();

    static int nextInstanceId();

private:
    [[nodiscard]] static eve::Result<EdgePathPlacement>
    commitEdgePath(PlacementWorld *world, EdgePathPreview preview,
                   const std::vector<EdgeCurvePoint> *curveControls = nullptr,
                   int curveSubdivisions = 0,
                   const EdgeCurveSurface *curveSurface = nullptr);
    static void dissolveEdgeCurveGroup(PlacementWorld *world, EdgeCurveGroupId groupId);
    friend class Ghost;

    static bool canPlaceQuery(PlacementWorld *world, const PlacementQuery &q,
                              std::string *reason);
    static bool runValidate(const PlacementWorld &world, const BuildingDefinition &def,
                            const PlacementQuery &q, std::string *reason);
    static bool checkBoundsAndOccupancy(const PlacementWorld &world, const BuildingDefinition &def,
                                        const PlacementQuery &q, bool checkOccupancy,
                                        std::string *reason);
    static bool checkTerrain(const PlacementWorld &world, const BuildingDefinition &def,
                             const PlacementQuery &q, std::string *reason);
    static bool checkAdjacency(const PlacementWorld &world, const BuildingDefinition &def,
                               const PlacementQuery &q, std::string *reason);
    static bool checkSurfacePatch(const BuildingDefinition &def, const PlacementQuery &q,
                                  std::string *reason);
    static bool checkStructuralSupport(const PlacementWorld &world,
                                       const BuildingDefinition &def,
                                       const PlacementQuery &q, std::string *reason);
    static std::vector<int> collectStructuralSupports(const PlacementWorld &world,
                                                      const BuildingDefinition &def,
                                                      const PlacementQuery &q);
    static bool hasStructuralDependents(const PlacementWorld &world, int instanceId);
    static bool replacementPreservesStructuralDependents(
        const PlacementWorld &world, int instanceId, const BuildingDefinition &replacement);
    static bool removeBuildingUnchecked(PlacementWorld *world, int instanceId);
    static void writeOccupancy(PlacementWorld &world, const BuildingDefinition &def,
                               const PlacedBuilding &placed, int instanceId);
    static void clearOccupancy(PlacementWorld &world, int instanceId);
    static void emit(BuildingChangeEvent ev);
    static eve::Result<AreaPreview>
    previewArea(PlacementWorld *world, const std::string &buildingId,
                const std::vector<std::pair<int, int>> &anchors, float rotationDeg);
    static eve::Result<AreaPlacement> commitArea(PlacementWorld *world, AreaPreview preview);

    static std::unordered_map<std::string, ValidateFn> &validateRules();
    static std::unordered_map<std::string, SnapFn> &snapRules();
    static std::unordered_map<std::string, ChangeHook> &changeHooks();
    static std::unordered_map<std::string, SurfaceFn> &surfaces();
    static std::unordered_map<std::string, SurfaceProviderFn> &surfaceProviders();
    static float &planeSurfaceHeight();
    static std::vector<BuildingChangeEvent> &eventQueue();
    static int &instanceCounter();
    static bool &builtinsReady();

    PlacementSystem() = default;
    ~PlacementSystem() = default;
    PlacementSystem(const PlacementSystem &) = delete;
    PlacementSystem &operator=(const PlacementSystem &) = delete;

    std::unordered_map<std::string, ValidateFn> validateRules_;
    std::unordered_map<std::string, SnapFn> snapRules_;
    std::unordered_map<std::string, ChangeHook> changeHooks_;
    std::unordered_map<std::string, SurfaceFn> surfaces_;
    std::unordered_map<std::string, SurfaceProviderFn> surfaceProviders_;
    std::vector<BuildingChangeEvent> eventQueue_;
    int instanceCounter_ = 0;
    bool builtinsReady_ = false;
    float planeSurfaceHeight_ = 0.f;
};

}  // namespace eve::building
