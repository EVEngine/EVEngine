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

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `add()`、`angle()`、`angle2()`、`angleBetween2()`、`approach()`、`bezierCubic()`、`bezierCubic2X()`、`bezierCubic2Y()`
- `bezierQuadratic()`、`bezierQuadratic2X()`、`bezierQuadratic2Y()`、`bias()`、`bilinear()`、`boxesOverlap()`、`cartesianAngle()`
- `cartesianRadius()`、`circleRectOverlap()`、`circlesOverlap()`、`clamp()`、`clone()`、`closestPointOnSegment2X()`、`closestPointOnSegment2Y()`、`closestPointOnSegment3X()`
- `closestPointOnSegment3Y()`、`closestPointOnSegment3Z()`、`cross()`、`cross2()`、`degToRad()`、`distance2()`、`distance3()`、`distanceTo()`
- `dot()`、`dot2()`、`dot3()`、`ease()`、`fbm2()`、`fbm3()`、`fract()`、`gain()`
- `get()`、`getName()`、`getRandomSeed()`、`getX()`、`getY()`、`getZ()`、`hash1()`、`hash2()`
- `hash3()`、`identity()`、`inverseLerp()`、`length()`、`length2()`、`length3()`、`lengthSquared()`、`lerp()`
- `lerpAngle()`、`lerpTo()`、`multiplied()`、`multiply()`、`newMat4()`、`newMat4RotationZ()`、`newMat4Scale()`、`newMat4Translation()`
- `newVec2()`、`newVec3()`、`noise1()`、`noise2()`、`noise3()`、`normalize()`、`normalize2X()`、`normalize2Y()`
- `normalize3X()`、`normalize3Y()`、`normalize3Z()`、`normalized()`、`perlin2()`、`perlin3()`、`pingPong()`、`pointInBox()`
- `pointInCircle()`、`pointInRect()`、`pointInSphere()`、`polarX()`、`polarY()`、`quantize()`、`radToDeg()`、`random()`
- `randomGaussian()`、`randomInt()`、`randomRange()`、`raycastBox()`、`raycastCircle2()`、`raycastPlane()`、`raycastRect2()`、`raycastSphere()`
- `rectsOverlap()`、`remap()`、`ridged2()`、`ridged3()`、`rotate2X()`、`rotate2Y()`、`rotateX()`、`rotateY()`
- `rotateZ()`、`scale()`、`segmentsIntersect()`、`set()`、`setRandomSeed()`、`setRandomSeedFromTime()`、`setX()`、`setY()`
- `setZ()`、`sign()`、`smootherstep()`、`smoothstep()`、`snap()`、`spheresOverlap()`、`step()`、`sub()`
- `steeringArrive2()`、`steeringArrive3()`、`steeringAvoid2()`、`steeringAvoid3()`、`steeringFlee2()`、`steeringFlee3()`
- `steeringPathTarget2()`、`steeringPathTarget3()`、`steeringSeek2()`、`steeringSeek3()`、`steeringSeparation2()`、`steeringSeparation3()`
- `transformPoint2()`、`transformVec3()`、`translate()`、`turbulence2()`、`voronoi2()`、`voronoiEdge2()`、`warpNoise2()`、`wrap()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/math/`](../../../src/modules/math/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `math`。
