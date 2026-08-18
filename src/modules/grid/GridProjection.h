#pragma once

#include "grid/GridConfig.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace eve::grid {

/** cell -> 平面坐标 (px, py)。XY 平面：世界 (x, y)；XZ 平面：世界 (x, z)。 */
void cellToWorld(const GridConfig &cfg, int cx, int cy, float &px, float &py);

/** 2D 排序键（格脚点 Y，与 map::tileToDepthY 语义一致）；XZ 平面不使用。 */
float cellToDepthY(const GridConfig &cfg, int cx, int cy);

/**
 * 平面坐标 -> 最近格子。staggered / hex 采用有界最近邻搜索
 * （mapW / mapH 提供搜索范围，与 Tiled 拾取语义一致）。
 */
void worldToCell(const GridConfig &cfg, float px, float py, int &cx, int &cy,
                 int mapW = 1, int mapH = 1);

/**
 * 旋转后的占地局部格枚举。mask 为行主序 w*h（空 = 实心矩形）。
 * steps：cardinal 为 90° 步数（0..3）；hexMode 为 60° 步数（0..5）。
 * 回调收到归一化局部坐标（旋转后包围盒最小角归零，原点格为锚点）。
 */
void foreachRotatedFootprint(int w, int h, const std::vector<uint8_t> &mask, int steps,
                             bool hexMode,
                             const std::function<void(int lx, int ly)> &fn);

/** 旋转后的占地包围盒尺寸。 */
void rotatedFootprintSize(int w, int h, const std::vector<uint8_t> &mask, int steps,
                          bool hexMode, int &outW, int &outH);

}  // namespace eve::grid
