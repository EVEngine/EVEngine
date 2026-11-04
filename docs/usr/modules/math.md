# 数学模块

**脚本入口：** `eve.Math()`

提供向量、矩阵、几何、无状态 2D/3D steering、噪声、随机数、插值和缓动工具。2D/3D 对象的**点选、重叠与射线检测**也放在本模块（轻量几何测试）；带刚体/接触响应的模拟见 [Physics](physics.md)，屏幕坐标换算见 [Graphics](graphics.md) 的 Camera2D/Camera3D。

## 基本用法

```squirrel
local math = eve.Math();
math.setRandomSeed(42);
local p = math.newVec2(10, 20);
local n = math.noise2(p.getX(), p.getY());
```

## 对象关系与调用时机

`Math` 是算法与随机状态入口；Vec2/Vec3/Mat4 是值对象；noise、random、ease、Bezier 和几何测试由模块提供。需要确定性的系统应拥有独立且明确 seed 的 Math 状态。

## 目标导向指南

### 可复现随机地图

生成前 `setRandomSeed(seed)`，所有随机选择使用同一 Math 实例；存档只需记录 seed 和玩家改动。不要用时间 seed 做需要联网同步的逻辑。

### 平滑移动和程序噪声

用 lerp/smoothstep/ease 做相机与 UI 插值；`noise2()` / fractal noise 适合高度图，输入坐标乘 frequency 控制尺度。Vec/Mat 对象用于变换组合，热点循环避免反复分配临时对象。

### 山体与地形的平滑融合

`math.smoothMax(a, b, width)` 对两个高度取平滑最大值。`a`、`b` 必须有限且使用
相同的坐标基准和高度单位；`width` 是有限、非负的**高度差融合带宽**，不是水平距离。
宽度为 0 时返回普通 max；高度差达到 width 后也精确返回 max，因此不会影响远处地形。
带内公式为 `max(a,b) + width * (1 - abs(a-b)/width)^2 / 4`，函数及一阶导数连续
（C1，二阶导数不连续）。两高度相等时会抬高 `width/4`，可据此选择宽度。

```squirrel
local math = eve.Math();
function sampleMountainBlend(math, x, z) {
    local terrain = 3.0 + 2.0 * math.noise2(x * 0.02, z * 0.02);
    local mountain = 25.0 - 0.01 * (x * x + z * z);
    return math.smoothMax(terrain, mountain, 4.0);
}
```

在生成高度图时对每个世界坐标采样该函数。山体边缘必须降到地形以下至少 width，
再裁切其覆盖范围；直接截断仍可能产生接缝。若使用局部遮罩，需让遮罩在边缘平滑降到
零。输入高度场本身应连续，并从融合后的高度重新计算法线；分块地形共享同一世界坐标
采样规则。此函数不修改网格、碰撞或现有 TerrainStamp 操作。

C++ 核心入口为 `eve::math::smoothMax`（`common/SmoothMax.h`），脚本入口委托同一实现。
每次采样仅固定数量运算，无分配、状态、时间、RNG 或回调；线程安全，不保留输入。
参数前置条件由启用断言的构建检查。内部用 double 避免有限 float 的高度差溢出；
结果超出 float 范围时返回正无穷。跨平台按浮点容差比较；多座山逐次融合时固定顺序，
因为此运算满足交换律但不满足结合律。

### 2D/3D steering 数学

`steeringSeek2/3`、`steeringFlee2/3`、`steeringArrive2/3` 和
`steeringAvoid2/3` 返回新的 Vec2/Vec3 值对象。`steeringSeparation2/3` 与
`steeringPathTarget2/3` 的脚本适配器分别接受 `x:y,x:y` 和
`x:y:z,x:y:z` 格式；C++ 的 `math/Steering.h` 核心接口使用值类型和
`std::span`，不解析字符串，也不保存 agent、路径、邻居或时间状态。

```squirrel
local desired2 = math.steeringArrive2(x, y, targetX, targetY, 6.0, 20.0, 2.0);
local desired3 = math.steeringSeek3(x, y, z, targetX, targetY, targetZ, 8.0);
```

### 2D 点击拾取与重叠

鼠标/触摸先经 `Camera2D.screenToWorld*` 得到世界坐标，再用：

- `pointInCircle` / `pointInRect` — 点选精灵或按钮
- `circlesOverlap` / `rectsOverlap` / `circleRectOverlap` — 无物理的碰撞/触发
- `raycastCircle2` / `raycastRect2` — 子弹、视线；命中点 = `o + t * d`，未命中返回 `-1`
- `segmentsIntersect` — 线段相交（弹道、边缘）

```squirrel
local mx = mouse.getX();
local my = mouse.getY();
local wx = cam2d.screenToWorldX(mx, my, gfx.getWidth(), gfx.getHeight());
local wy = cam2d.screenToWorldY(mx, my, gfx.getWidth(), gfx.getHeight());
if (math.pointInRect(wx, wy, spriteX, spriteY, spriteW, spriteH)) {
    // picked
}
```

### 3D 射线拾取

`Camera3D.screenToRay` 生成世界射线后，用 `raycastSphere` / `raycastBox` / `raycastPlane` 测距；`t >= 0` 表示命中。多个候选取最小 `t`。

```squirrel
cam3d.screenToRay(mouse.getX(), mouse.getY(), gfx.getWidth(), gfx.getHeight());
local ox = cam3d.getScreenRayOriginX();
local oy = cam3d.getScreenRayOriginY();
local oz = cam3d.getScreenRayOriginZ();
local dx = cam3d.getScreenRayDirX();
local dy = cam3d.getScreenRayDirY();
local dz = cam3d.getScreenRayDirZ();
local t = math.raycastSphere(ox, oy, oz, dx, dy, dz, cx, cy, cz, radius);
if (t >= 0) {
    local hx = ox + dx * t;
    local hy = oy + dy * t;
    local hz = oz + dz * t;
}
```

## 常见问题

- 客户端各自用时间 seed，造成联机状态分叉。
- 度和弧度混用。
- 未归一化方向向量就用于速度或点积判断。
- 射线 API 的 `t` 是参数：`hit = origin + t * dir`；`dir` 不必单位化，线段检测时用 `dir = B - A` 并要求 `t ∈ [0,1]`。
- 把 Math 几何测试当成刚体物理：需要接触事件、摩擦或传感器时用 Physics。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。

**源码：** [`src/modules/math/`](../../../src/modules/math/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `math`。

## C++ 二维与三维凸包

`math/ConvexHull.h` 提供无状态的 `eve::math::convexHull2D` 与
`eve::math::convexHull3D`，输入为 `std::span<const HullPoint2/3>`
（点为 `std::array<float, 2/3>`），输出使用引擎 `Result`。调用方必须检查
`ok()` 后再读取 `value()`。结果拥有全部数据，不保留输入引用，支持任意线程
独立调用，不依赖 Math 实例、脚本 VM、图形或物理后端。当前入口为 C++ API。

```cpp
#include "math/ConvexHull.h"

std::vector<eve::math::HullPoint3> points{
    {0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}};
auto result = eve::math::convexHull3D(points);
if (result.ok()) {
    const auto& hull = result.value();
    // hull.vertices: 紧凑顶点数组；hull.faces: 顶点索引三元组。
}
```

二维采用 Andrew 单调链，O(n log n) 时间、O(n) 空间，返回逆时针边界，
不重复首点，删除精确重复点和共线边上的中间点。三维采用增量法，
有序 horizon 边表使最坏时间为 O(n² log n)，空间 O(n)；实体三角形法线朝外。
三维平面面片保留三角剖分，可能保留边上中间顶点。

`HullDimension` 显式区分 Empty、Point、Segment、Plane、Volume。
空集、单点、线段均成功且没有三角形；三维共面输入返回有序多边形边界和
三角扇，共面边界方向未规定。NaN/Infinity 或非法 tolerance 返回
`DiagnosticCode::InvalidArgument`，诊断路径分别为 `points`、`tolerance`。

三维先平移并按包围盒最大边长归一化，使用 double 判定；默认 tolerance
为 1e-10，表示相对该边长的距离，范围 [0,1)。容差以内的细节可能被省略，
近共面结果仍保留被选中顶点的原始坐标。二维也使用 double 判定，但不使用
容差合并。算法不使用精确几何谓词，不保证近退化点集的精确拓扑或跨平台
逐位一致；同一平台相同点集的排序与处理顺序确定，不依赖输入排列。
昂贵构建应按点集缓存，避免每帧重算。
