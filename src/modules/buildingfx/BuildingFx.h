#pragma once

// 建筑放置渲染桥（可选模块）：监听 building::PlacementWorld 的实例，
// 按 BuildingDefinition.renderMode 生成 / 同步 / 销毁 Renderable2D 或 Renderable3D，
// 并提供鬼影预览 + 占地光标 + 网格可视化。
// 纯逻辑（building）与渲染解耦：本模块依赖 building / graphics / ECS，不反向依赖。

#include "common/Module.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::building {
class BuildingDefinition;
class Ghost;
class PlacementWorld;
struct PlacedBuilding;
}

namespace eve::graphics {
class Graphics;
class Mesh;
class Renderable2D;
class Renderable3D;
}

namespace eve::buildingfx {

class BuildingFx : public Module {
public:
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

    /** 每帧调用：按 ghost 位姿与合法性更新预览与占地光标（绿=合法 / 红=非法）。 */
    void updateGhost(building::PlacementWorld *world, building::Ghost *ghost);
    void hideGhost(building::PlacementWorld *world);

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
    };

    struct WorldState {
        std::unordered_map<int, Visual> visuals;
        Visual ghost;
        Visual cursor;
        std::string ghostBuildingId;
        bool gridVisible = false;
        std::vector<graphics::Renderable3D *> gridLines;
        int gridLineCount = -1;
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

    std::unordered_map<building::PlacementWorld *, WorldState> states_;
    graphics::Mesh *cubeMesh_ = nullptr;
};

}  // namespace eve::buildingfx
