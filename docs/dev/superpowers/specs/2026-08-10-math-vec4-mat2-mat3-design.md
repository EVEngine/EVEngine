# Math 模块补齐 Vec4 / Mat2 / Mat3 与 Vec 参数 API

日期：2026-08-10  
状态：已确认设计，待实现

## 背景

`Math` 目前提供 `Vec2`、`Vec3`、`Mat4`，以及大量 float 分量几何/碰撞/噪声 API。缺少常见的 `Vec4`、`Mat2`、`Mat3`；高参数量 API 也不便于用已有 `Vec*` 对象调用。Squirrel 绑定不支持同名重载，现有约定为「无重载、用区分名」。

## 目标

1. 同时服务 **C++ API** 与 **Squirrel 脚本 API**。
2. 新增类型：`Vec4`、`Mat2`、`Mat3`（完整补齐）。
3. 为几何、碰撞、射线、噪声/hash、Bezier 等能接受向量的 API 增加 `*V` 版本。
4. 多分量结果返回 **新分配的 `Vec*`**（不提供 `Into`/`out` 版）。
5. 保留全部现有 float API 与签名。

## 非目标

- 不把几何逻辑大规模迁到 `Vec*`/`Mat*` 成员上（保持 `Math` 模块函数为主）。
- 不把实现整体重写为「内部统一 glm、对外两套门面」。
- 不做 C++ 同名重载与脚本名分叉（统一 `*V`）。
- 不加空指针防护（与现有 `Vec2::dot` 等一致，调用方保证非空）。
- 不改现有 float 函数行为。

## 方案

采用 **薄包装层**：

- 新类型 API 风格对齐现有 `Vec2`/`Vec3`/`Mat4`。
- `Math::*V` 拆分量后调用现有 float 实现，或包一层 `new Vec*`。
- 行为与对应 float API 数值一致。

## 新类型

### Vec4

对齐 `Vec2`/`Vec3`：

- 分量：`get/setX/Y/Z/W`、`set(x,y,z,w)`
- 运算：`length`、`lengthSquared`、`normalize`、`normalized`、`dot`、`distanceTo`、`add`、`sub`、`scale`、`lerpTo`、`clone`
- 不加 `cross`（4D 不常用）

### Mat2

列主序，内部可包 `glm::mat2`：

- `identity`、`rotate(radians)`、`scale(sx,sy)`、`multiply`/`multiplied`
- `transformVec2`、`get/set(index 0..3)`、`clone`
- 工厂：`Math::newMat2`、`newMat2Rotation`、`newMat2Scale`

### Mat3

列主序，内部可包 `glm::mat3`：

- `identity`、`translate(x,y)`（2D 仿射）、`rotate(radians)`、`scale(sx,sy)`、`multiply`/`multiplied`
- `transformVec2`（点，含平移）、`transformVec3`、`get/set(0..8)`、`clone`
- 工厂：`Math::newMat3`、`newMat3Translation`、`newMat3Rotation`、`newMat3Scale`

### Mat4 / 工厂补齐

- `Math::newVec4(x,y,z,w)`
- `Mat4::transformVec4`（齐次），与现有 `transformVec3` / `transformPoint2` 并列

## Math `*V` API

命名：脚本与 C++ 均用 **`V` 后缀**（如 `pointInCircleV`）。

说明：不新增 `length4`/`dot4`/`distance4` 等 float 分量 API；`length4V` 等直接基于 `Vec4` 分量计算（或委托 `Vec4` 成员），与「保留现有 float API、不强制对称扩 float」一致。

### 几何 / 向量

| float | Vec 版 |
|---|---|
| `length2/3` | `length2V(Vec2*)` / `length3V(Vec3*)` / `length4V(Vec4*)` |
| `distance2/3` | `distance2V` / `distance3V` / `distance4V` |
| `dot2/3` | `dot2V` / `dot3V` / `dot4V` |
| `cross2` | `cross2V` |
| `angle2` / `angleBetween2` | `angle2V` / `angleBetween2V` |
| `normalize2X/Y`、`normalize3X/Y/Z` | `normalize2V`→`Vec2*`、`normalize3V`→`Vec3*`、`normalize4V`→`Vec4*` |
| `rotate2X/Y` | `rotate2V(v, radians)`→`Vec2*` |
| `polarX/Y` | `polarV(radius, radians)`→`Vec2*` |
| `cartesianRadius/Angle` | `cartesianRadiusV` / `cartesianAngleV` |

### 2D / 3D 碰撞与射线

| float | Vec 版 |
|---|---|
| `pointInCircle` / `pointInRect` | `pointInCircleV(p,c,r)` / `pointInRectV(p, origin, size)`（`size` 为 w,h） |
| `circlesOverlap` / `rectsOverlap` / `circleRectOverlap` | 对应 `*V`；圆心/原点 `Vec2`，尺寸 `Vec2` |
| `segmentsIntersect` | `segmentsIntersectV(a,b,c,d)` |
| `raycastCircle2` / `raycastRect2` | `raycastCircle2V(o,d,c,r)` / `raycastRect2V(o,d,origin,size)` |
| `closestPointOnSegment2X/Y` | `closestPointOnSegment2V`→`Vec2*` |
| `pointInSphere` / `pointInBox` / `spheresOverlap` / `boxesOverlap` | 对应 `*V`；AABB 用 `min`/`max` 两个 `Vec3` |
| `raycastSphere` / `raycastBox` / `raycastPlane` | 对应 `*V` |
| `closestPointOnSegment3X/Y/Z` | `closestPointOnSegment3V`→`Vec3*` |

### Noise / Hash / Bezier

| float | Vec 版 |
|---|---|
| `hash2/3`、`noise2/3`、`perlin2/3` | `hash2V` 等（`Vec2*`/`Vec3*`） |
| `fbm2/3`、`ridged2/3`、`turbulence2`、`voronoi2`、`voronoiEdge2`、`warpNoise2` | 对应 `*V`；octaves 等标量参数不变 |
| `bezierQuadratic2X/Y`、`bezierCubic2X/Y` | `bezierQuadratic2V(t,p0,p1,p2)`→`Vec2*`，`bezierCubic2V` 同理 |
| `hash1` / `noise1` / `bezierQuadratic` / `bezierCubic` | 标量，不加 `V` |

## 所有权与绑定

- 返回对象：`new` 分配；脚本侧由 GC 管理，C++ 侧由调用方负责（与现有一致）。不引入对象池。
- 输入指针：非空由调用方保证。
- Squirrel：在 `Math::expose(Table)` 注册 `Vec4`/`Mat2`/`Mat3`；在 `Math::expose(Class)` 注册工厂与全部 `*V`；现有 float 绑定不变。

## 测试

扩展 `test/math.cpp`：

1. 新类型冒烟：构造、`length`/`dot`、矩阵乘与 `transform*`。
2. 抽样 `*V` 与对应 float API 数值一致（至少含 `pointInCircleV`、`normalize2V`、`raycastSphereV`、`bezierQuadratic2V`）。
3. 不要求每个 `*V` 单独用例。

## 实现顺序建议

1. 新增 `Vec4` / `Mat2` / `Mat3` 及工厂、Squirrel 类绑定、基础测试。
2. 实现几何与碰撞类 `*V` 薄包装。
3. 实现 noise/hash/bezier 类 `*V`。
4. 补 `Mat4::transformVec4` 与剩余绑定、抽样测试。

## 决策记录

| 项 | 选择 |
|---|---|
| 目标层 | C++ + Squirrel |
| 类型范围 | `Vec4` + `Mat2` + `Mat3` |
| 脚本命名 | `V` 后缀 |
| `*V` 覆盖 | 尽量全覆盖（几何 + noise/hash/bezier） |
| 多分量返回 | 返回新 `Vec*` |
| 架构 | 薄包装层 |
