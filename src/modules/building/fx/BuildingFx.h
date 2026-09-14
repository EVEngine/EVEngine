#pragma once

// 建筑放置渲染桥（可选模块）：监听 building::PlacementWorld 的实例，
// 按 BuildingDefinition.renderMode 生成 / 同步 / 销毁 Renderable2D 或 Renderable3D，
// 并提供鬼影预览 + 占地光标 + 网格可视化。
// 纯逻辑（building）与渲染解耦：本模块依赖 building / graphics / ECS，不反向依赖。

#include "common/Module.h"
#include "common/Result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::building {
struct BuildingDefinition;
class Ghost;
class PlacementWorld;
class PlacementSession;
struct PlacedBuilding;
}

namespace eve::graphics {
class Graphics;
class Mesh;
class Renderable2D;
class Renderable3D;
}

namespace eve::buildingfx {

/** @brief Script-safe outcome of refreshing a custom-surface curve preview. */
enum class CurvePreviewUpdateStatus { Updated, Rejected };

class BuildingFx : public Module {
public:
    /** @brief One cubic Bezier control point in logical grid-vertex coordinates. */
    struct CurveControlPoint {
        float x = 0.f;
        float y = 0.f;
        bool operator==(const CurveControlPoint &) const = default;
    };
    /** @brief Owning backend-neutral indexed extrusion generated along an edge curve. */
    struct CurveMeshData {
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> uvs;
        std::vector<uint32_t> indices;
        int sampleCount = 0;
        float length = 0.f;
    };
    /** @brief One committed world-space centerline frame for terrain-conforming extrusion. */
    struct CurveSurfaceSample {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        float normalX = 0.f;
        float normalY = 1.f;
        float normalZ = 0.f;
    };
    /** @brief Resolve a stable visual resource id to a borrowed graphics mesh. */
    using MeshResolver = std::function<graphics::Mesh *(const std::string &resourceId)>;
    Module_REG(BuildingFx);
    BuildingFx() = default;
    ~BuildingFx() override = default;

    /** 开始同步一个放置世界（可重复调用；幂等）。 */
    bool attach(building::PlacementWorld *world);
    bool detach(building::PlacementWorld *world);
    bool isAttached(building::PlacementWorld *world) const;
    int getAttachedCount() const;

    /** 与当前建筑实例做 diff：创建缺失视觉、刷新位姿、销毁移除的视觉。 */
    void sync(building::PlacementWorld *world);
    int getVisualCount(building::PlacementWorld *world) const;
    /** @brief Install the synchronous mesh resolver used by visual3d `mesh` metadata. @lifetime Returned meshes remain provider-owned and must outlive every referencing visual. @reentrancy The callback runs without a BuildingFx lock and must not call sync recursively. */
    void setMeshResolver(MeshResolver resolver) { meshResolver_ = std::move(resolver); }
    /** @brief Remove the mesh resolver; configured resources fall back to the primitive cube. */
    void clearMeshResolver() { meshResolver_ = {}; }
    /** @brief Current derived topology variant for one visual, or empty when unavailable. */
    std::string getVisualVariant(building::PlacementWorld *world, int instanceId) const;
    /** @brief Selected configured resource id after mask, class, and base resolution. */
    std::string getVisualResource(building::PlacementWorld *world, int instanceId) const;
    /** @brief Empty on resolved resources, otherwise a stable observable degradation reason. */
    std::string getVisualFallbackReason(building::PlacementWorld *world, int instanceId) const;
    /** @brief Set per-world floor visibility: `all`, `active`, or `active_and_below`. */
    void setLevelVisibilityMode(building::PlacementWorld *world, const std::string &mode);
    /** @brief Return the stable per-world floor visibility token. */
    std::string getLevelVisibilityMode(building::PlacementWorld *world) const;
    /** @brief Return whether one synchronized instance is currently visible. */
    bool isVisualVisible(building::PlacementWorld *world, int instanceId) const;
    /** @brief Number of authoritative curve groups observed by the last sync. */
    int getCurveGroupCount(building::PlacementWorld *world) const;
    /** @brief Number of curve groups currently rendered as one continuous 3D mesh. */
    int getContinuousCurveVisualCount(building::PlacementWorld *world) const;
    /** @brief Stable degradation reason for the curve group containing an instance. */
    std::string getCurveVisualFallbackReason(building::PlacementWorld *world,
                                             int instanceId) const;

    /**
     * @brief Generate a continuous rectangular wall extrusion along a cubic Bezier curve.
     * @param world Borrowed world used only during this synchronous call for grid projection.
     * @param controlPoints Exactly four finite logical grid control points.
     * @param subdivisions Number of analytic curve segments in the range 2..4096.
     * @param width Positive wall thickness in world units.
     * @param height Positive extrusion height along the grid-plane normal.
     * @param elevation Base elevation along the grid-plane normal.
     * @return Owning CPU mesh data, or a structured diagnostic without graphics-side mutation.
     */
    [[nodiscard]] static eve::Result<CurveMeshData>
    buildEdgeCurveMesh(const building::PlacementWorld &world,
                       const std::vector<CurveControlPoint> &controlPoints, int subdivisions,
                       float width, float height, float elevation = 0.f);
    /** @brief Extrude a wall from committed world-space surface frames. */
    [[nodiscard]] static eve::Result<CurveMeshData>
    buildSurfaceCurveMesh(const std::vector<CurveSurfaceSample> &samples, float width,
                          float height);

    /**
     * @brief Generate the continuous mesh owned by the curve group containing an edge instance.
     * @param world Borrowed authoritative placement world used only during this call.
     * @param instanceId Positive edge instance id linked to a curve group.
     * @param width Positive wall thickness in world units.
     * @param height Positive extrusion height along the grid-plane normal.
     * @param elevation Base elevation along the grid-plane normal.
     * @return Owning CPU mesh data, or the placement/group diagnostic without mutation.
     */
    [[nodiscard]] static eve::Result<CurveMeshData>
    buildEdgeCurveGroupMeshForInstance(const building::PlacementWorld &world, int instanceId,
                                       float width, float height, float elevation = 0.f);

    /** @brief Build and present a transient continuous curve draft without mutating placement. */
    [[nodiscard]] eve::Result<void> updateEdgeCurvePreview(
        building::PlacementWorld *world, const std::string &buildingId,
        const std::vector<CurveControlPoint> &controlPoints, int subdivisions, int level = 0);
    /**
     * @brief Present the active session curve using frames from a named custom surface.
     * @param world Attached authoritative world, borrowed for this call.
     * @param session Active placement session owning exactly four curve controls.
     * @param subdivisions Analytic surface sample segment count.
     * @param surfaceName Registered surface provider used by a subsequent matching commit.
     * @return Success after CPU generation (including explicit graphics-unavailable degradation),
     * or a diagnostic without replacing the last valid preview.
     */
    [[nodiscard]] eve::Result<void> updateEdgeCurveSurfacePreview(
        building::PlacementWorld *world, building::PlacementSession *session,
        int subdivisions, const std::string &surfaceName);
    /** @brief Script projection of updateEdgeCurveSurfacePreview's structured result. */
    CurvePreviewUpdateStatus updateEdgeCurveSurfacePreviewStatus(
        building::PlacementWorld *world, building::PlacementSession *session,
        int subdivisions, const std::string &surfaceName);
    /** @brief Remove the transient continuous curve draft for a world. */
    void clearEdgeCurvePreview(building::PlacementWorld *world);
    /** @brief Return whether the world owns a current CPU curve-preview mesh. */
    bool hasEdgeCurvePreview(building::PlacementWorld *world) const;
    /** @brief Return the stable backend degradation reason for the transient curve preview. */
    std::string getEdgeCurvePreviewFallbackReason(building::PlacementWorld *world) const;
    /** @brief Surface identity represented by the current preview, or empty. */
    std::string getEdgeCurvePreviewSurfaceId(building::PlacementWorld *world) const;
    /** @brief Surface revision represented by the current preview, or zero. */
    std::uint64_t getEdgeCurvePreviewSurfaceRevision(building::PlacementWorld *world) const;

    /** 每帧调用：按 ghost 位姿与合法性更新预览与占地光标（绿=合法 / 红=非法）。 */
    void updateGhost(building::PlacementWorld *world, building::Ghost *ghost);
    void hideGhost(building::PlacementWorld *world);
    /** @brief Copy the session's owning area-preview projection for 2D/3D heatmap rendering. */
    void updateAreaPreview(building::PlacementWorld *world, building::PlacementSession *session);
    /** @brief Clear all area-preview cells and their derived render entities. */
    void clearAreaPreview(building::PlacementWorld *world);
    int getAreaPreviewCount(building::PlacementWorld *world) const;
    bool getAreaPreviewAccepted(building::PlacementWorld *world, int index) const;

    void setGridVisible(building::PlacementWorld *world, bool visible);
    bool getGridVisible(building::PlacementWorld *world) const;
    /** 2D 网格叠加（立即模式；在渲染循环中、tilemap 绘制之后调用）。 */
    void drawGrid2D(building::PlacementWorld *world, graphics::Graphics *gfx);
    /** 3D 平面线框（懒创建薄盒实体；在 3D 渲染循环中调用）。 */
    void drawGrid3D(building::PlacementWorld *world, graphics::Graphics *gfx, float height = 0.f);

private:
    struct Visual {
        graphics::Renderable2D *r2d = nullptr;
        graphics::Renderable3D *r3d = nullptr;
        std::string topologyVariant;
        std::string resourceId;
        std::string fallbackReason;
        int topologyMask = 0;
    };

    struct WorldState {
        enum class LevelVisibility { All, Active, ActiveAndBelow };
        struct HeatCell {
            int x = 0;
            int y = 0;
            bool accepted = false;
        };
        struct CurveVisual {
            graphics::Renderable3D *renderable = nullptr;
            graphics::Mesh *mesh = nullptr;
            CurveMeshData cpuMesh;
            std::vector<CurveControlPoint> controlPoints;
            std::vector<int> memberIds;
            int subdivisions = 0;
            float width = 0.f;
            float height = 0.f;
            float elevation = 0.f;
            std::string surfaceId;
            std::uint64_t surfaceRevision = 0;
            std::string fallbackReason;
            bool active = false;
        };
        std::unordered_map<int, Visual> visuals;
        std::unordered_map<std::uint64_t, CurveVisual> curveVisuals;
        CurveVisual curvePreview;
        LevelVisibility levelVisibility = LevelVisibility::All;
        Visual ghost;
        Visual cursor;
        std::string ghostBuildingId;
        bool gridVisible = false;
        std::vector<graphics::Renderable3D *> gridLines;
        int gridLineCount = -1;
        std::vector<HeatCell> heatCells;
        std::vector<graphics::Renderable3D *> heatCells3d;
    };

    bool is3d(const building::BuildingDefinition &def) const;
    void createVisual(WorldState &st, const building::BuildingDefinition &def,
                      const building::PlacedBuilding &pb, building::PlacementWorld *world,
                      Visual &v, float alpha);
    void updateVisual(const building::BuildingDefinition &def,
                      const building::PlacedBuilding &pb, building::PlacementWorld *world,
                      Visual &v);
    void destroyVisual(Visual &v);
    void destroyAll(WorldState &st);

    graphics::Mesh *cubeMesh(graphics::Graphics *gfx);
    void rebuildGridLines(WorldState &st, building::PlacementWorld *world,
                          graphics::Graphics *gfx, float height);
    void setVisible(Visual &v, bool visible);
    void destroyHeatmap(WorldState &st);
    void syncCurveVisuals(WorldState &st, building::PlacementWorld *world);
    [[nodiscard]] eve::Result<void> presentCurvePreview(
        WorldState &state, CurveMeshData mesh, std::vector<CurveControlPoint> controls,
        int subdivisions, float width, float height, float elevation,
        std::string surfaceId = {}, std::uint64_t surfaceRevision = 0);

    std::unordered_map<building::PlacementWorld *, WorldState> states_;
    graphics::Mesh *cubeMesh_ = nullptr;
    MeshResolver meshResolver_;
};

}  // namespace eve::buildingfx
