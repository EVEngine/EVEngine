#pragma once

// 统一网格抽象：与具体模块（building / map）解耦的纯数据结构。
// 布局/拓扑 + 单元尺寸/间距 + 原点 + stagger 参数 + 平面轴。
// 坐标换算全部是纯函数（GridProjection.h），本头文件零依赖。

#include <string>

namespace eve::grid {

enum class GridLayout { Rectangle, Hexagon, Isometric, Staggered, IsometricZAsY };
enum class StaggerAxis { X, Y };
enum class StaggerIndex { Odd, Even };

/**
 * 放置平面轴：
 *  XY —— 网格第二轴映射到世界 Y（2D tilemap / 俯视 2D 场景，默认）。
 *  XZ —— 网格第二轴映射到世界 Z，世界 Y 为垂直高度（3D 场景地面网格）。
 */
enum class GridPlane { XY, XZ };

/** 网格配置：形状拓扑 + 尺寸 + 原点 + 平面轴。 */
struct GridConfig {
    GridLayout layout = GridLayout::Rectangle;
    GridPlane plane = GridPlane::XY;
    float cellW = 32.f;
    float cellH = 32.f;
    float cellGapX = 0.f;
    float cellGapY = 0.f;
    float originX = 0.f;
    float originY = 0.f;
    StaggerAxis staggerAxis = StaggerAxis::Y;
    StaggerIndex staggerIndex = StaggerIndex::Odd;
    float hexSideLength = 0.f;

    static const char *layoutName(GridLayout l);
    static GridLayout layoutFromName(const std::string &name);
    static const char *planeName(GridPlane p);
    static GridPlane planeFromName(const std::string &name);
};

/** 单元有效间距（含 gap）。 */
inline float cellPitchX(const GridConfig &cfg) { return cfg.cellW + cfg.cellGapX; }
inline float cellPitchY(const GridConfig &cfg) { return cfg.cellH + cfg.cellGapY; }

}  // namespace eve::grid
