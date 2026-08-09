# 数学模块

**脚本入口：** `eve.Math()`

提供向量、矩阵、几何、噪声、随机数、插值和缓动工具。

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

## 常见问题

- 客户端各自用时间 seed，造成联机状态分叉。
- 度和弧度混用。
- 未归一化方向向量就用于速度或点积判断。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `add()`、`angle()`、`angle2()`、`angleBetween2()`、`approach()`、`bezierCubic()`、`bezierCubic2X()`、`bezierCubic2Y()`
- `bezierQuadratic()`、`bezierQuadratic2X()`、`bezierQuadratic2Y()`、`bias()`、`bilinear()`、`cartesianAngle()`、`cartesianRadius()`、`clamp()`
- `clone()`、`cross()`、`cross2()`、`degToRad()`、`distance2()`、`distance3()`、`distanceTo()`、`dot()`
- `dot2()`、`dot3()`、`ease()`、`fbm2()`、`fbm3()`、`fract()`、`gain()`、`get()`
- `getName()`、`getRandomSeed()`、`getX()`、`getY()`、`getZ()`、`hash1()`、`hash2()`、`hash3()`
- `identity()`、`inverseLerp()`、`length()`、`length2()`、`length3()`、`lengthSquared()`、`lerp()`、`lerpAngle()`
- `lerpTo()`、`multiplied()`、`multiply()`、`newMat4()`、`newMat4RotationZ()`、`newMat4Scale()`、`newMat4Translation()`、`newVec2()`
- `newVec3()`、`noise1()`、`noise2()`、`noise3()`、`normalize()`、`normalize2X()`、`normalize2Y()`、`normalize3X()`
- `normalize3Y()`、`normalize3Z()`、`normalized()`、`perlin2()`、`perlin3()`、`pingPong()`、`pointInCircle()`、`pointInRect()`
- `polarX()`、`polarY()`、`quantize()`、`radToDeg()`、`random()`、`randomGaussian()`、`randomInt()`、`randomRange()`
- `remap()`、`ridged2()`、`ridged3()`、`rotate2X()`、`rotate2Y()`、`rotateX()`、`rotateY()`、`rotateZ()`
- `scale()`、`set()`、`setRandomSeed()`、`setRandomSeedFromTime()`、`setX()`、`setY()`、`setZ()`、`sign()`
- `smootherstep()`、`smoothstep()`、`snap()`、`step()`、`sub()`、`transformPoint2()`、`transformVec3()`、`translate()`
- `turbulence2()`、`voronoi2()`、`voronoiEdge2()`、`warpNoise2()`、`wrap()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/math/`](../../../src/modules/math/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `math`。
