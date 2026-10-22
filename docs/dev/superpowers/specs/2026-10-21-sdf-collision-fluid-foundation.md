# SDF 精准碰撞与流体固耦合：业界对照与 EVEngine 落地

目标：对照 Unity / UE5 的 Signed Distance Field（SDF）用法，把 EVEngine
已有的两套 SDF 能力收成可组合产品面，优先服务**复杂形状碰撞精度**与
**流体↔刚体双向耦合**，而不是一次性复刻 Lumen / Global DF 渲染栈。

## 1. 业界对照

### 1.1 Unity

| 能力面 | 形态 | 用途 |
|--------|------|------|
| VFX Graph `Collide with Signed Distance Field` | 预烘焙 SDF Asset + Transform | GPU 粒子与复杂刚体形状碰撞（Solid / Inverted） |
| Obi Distance Field | 自适应八叉树节点场（非均匀纹理） | Cloth / Softbody / Fluid 粒子对复杂静态网格的廉价、实心碰撞 |
| 引擎 PhysX/BoxCollider | 一般不走 SDF | 刚体仍用凸包 / MeshCollider |

要点：Unity 原生把 SDF 主要当作**粒子/变形体碰撞表示**；刚体物理不把 SDF
当一等形状。Obi 强调“实心场可从穿透恢复”，且仅均匀缩放可靠；变形网格不能
复用离线场。

### 1.2 Unreal Engine 5

| 能力面 | 形态 | 用途 |
|--------|------|------|
| Mesh Distance Field | 离线按 Static Mesh 烘焙进 volume texture | Soft shadow、DFAO、Lumen 软件追迹、Niagara 查询 |
| Global Distance Field | 相机周围 clipmap 合成 | 大场景近似查询；视点相关；无单 mesh 速度 |
| Niagara Fluids Static Mesh Collision | **per-mesh** DF（非 Global） | 流体碰撞：位置 / 法线 / 网格速度；推荐路径 |
| Niagara Global DF Collision | 运行时合成 | 成本恒定，但视点相关且缺 mesh velocity |
| Chaos Geometry Collection | 预破碎刚体图，不是 SDF 连续介质 | 破坏；与 Niagara 事件桥接 |

要点：UE5 的 SDF **首先服务渲染与 VFX/流体查询**；Niagara Fluids 明确偏好
per-mesh DF 做精准流体碰撞。刚体求解器（Chaos）本身不以 SDF 为形状原语。

### 1.3 共性结论（可移植到 EVEngine）

1. **离线 / 设置期烘焙**，运行时只采样与搬姿态；不要每帧从三角网格重算场。
2. **负值 = 实体**（或显式 inverted 容器），粒子可从完全穿透恢复。
3. **粒子 / 流体 / 布料 / 绳索**是 SDF 的主消费者；刚体仍用凸包 / 网格。
4. **per-object 场**做精准耦合；**场景级合成场**（若要做）服务查询近似与渲染。
5. 均匀缩放友好；非均匀缩放与蒙皮变形需要单独策略（禁用或运行时窄带更新）。

## 2. EVEngine 现状（已有，非从零）

| 组件 | 位置 | 角色 |
|------|------|------|
| `DistanceField3D` | `physics/` | 规则顶点网格；角色 `moveCapsule`、球/胶囊 TOI；绳索可借用 |
| `MeshSdf` | `fluids/FluidSdf.*` | 体素场；表面流体投影；`VolumeFluid` SDF 障碍 |
| `VolumeFluid::{setSdfColliders,updateSdfColliderPoses}` | `fluids/` | 最多 16 个拥有型 SDF；姿态更新不拷贝距离缓冲 |
| `VolumeFluidCoupling` | `fluids/` | 解析碰撞体 ↔ `Body3D` 分区双向冲量 |
| `IParticleSdfField` | `common/ParticleSdf.h` | 2D 粒子借用接口 |
| `GeometryCollection*` | `physics/destruction/` | Chaos 风格预破碎（非 SDF） |

差距（相对 Unity Obi / UE Niagara Fluids 产品体验）：

1. ~~`VolumeFluidCoupling` 不能附着 SDF~~ → **P0 已补** `attachSdf`。
2. ~~`DistanceField3D` 与 `MeshSdf` 双栈，无共享资产 / 编解码~~ → **P1 已补**
   `ISdfField3D` + `eve.sdf.field3d` blob + 双向转码。
3. Softbody / Cloth 仍探针 `World3D`，不采样距离场。
4. 无场景级 Global DF；无 Texture3D 共享采样路径。
5. 刚体后端（Box3D/Jolt）不把 SDF 当形状（与 Unity/UE 一致，保持）。

## 3. 目标架构（分层）

```
common/                 ISdfField3D + SdfField3DBlob（eve.sdf.field3d v1） [P1 已有]
physics/                DistanceField3D + DistanceField3DSdf 适配/导入导出 [已有+P1]
fluids/                 MeshSdf + MeshSdfField 适配；VolumeFluid 障碍/耦合 [已有+P0+P1]
particles/              3D 扩展或继续 2D IParticleSdfField                  [P2]
gpgpu/                  可选 SSBO/Texture3D 上传（表面流体已 SSBO）         [P2]
graphics/               （可选）场景合成 DF — 后置，勿阻塞物理              [P3]
```

规则对齐：

- fluids 可依赖 physics；physics **不得**反向依赖 fluids。
- 跨模块只暴露 `common` 接口 / `eve::cap`；禁止向上 `#include`。
- 公开突变 API 用 `Result` + `[[nodiscard]]`；Link 用 generation 校验。
- SDF 烘焙标 `@cost`：设置期；每帧只允许姿态更新。

## 4. 阶段计划

### P0 — 流体精准固耦合（本变更）

- `VolumeFluidCoupling::attachSdf(Body3D&, VolumeFluidSdfCollider)`：本地场 +
  姿态随刚体；标签与解析附着共用唯一空间；最多 16 个 SDF 绑定。
- `VolumeFluid::stepWithColliders(analytic, sdf, …)`：原子替换两侧碰撞体，
  失败回滚。
- 脚本 `attachSdf`；集成测试 `fluids.volumeCoupling.sdfObstacleTransfersImpulseToDynamicBody`。
- 文档：本规格 + `docs/usr/fluid3d.md` 耦合段。

验收：动态刚体携带烘焙网格/球 SDF，流体粒子产生带 label 的 contact，并对
刚体施加反向冲量；`detach` 后不再供应该 SDF。

### P1 — 共享 3D SDF 接口与资产（本变更）

- `eve::ISdfField3D`（`sample` / `gradient` / `revision` / `bounds`）+ capability 名。
- 可序列化 `SdfField3DBlob`（`schemaId=eve.sdf.field3d`，`schemaVersion=1`，
  `VertexGrid` 布局）；`encodeSdfField3DBlob` / `decodeSdfField3DBlob`。
- `physics::DistanceField3DSdf` 适配器；`sdfField3DBlobFromDistanceField` /
  `distanceFieldFromSdfField3DBlob`。
- `fluids::MeshSdfField` 适配器；`sdfField3DBlobFromMeshSdf` /
  `meshSdfFromSdfField3DBlob`；`distanceFieldFromMeshSdf` /
  `meshSdfFromDistanceField`（边界策略：导入 DistanceField3D 后场外为常量
  `outsideDistance`，不再是 MeshSdf 的欧氏外推）。
- 脚本：`fluids.sdfField3DBlobFromModel`（设置期烘焙）、
  `physics.distanceField3DFromSdfBlob`、`physics.sdfField3DBlobFromDistanceField`。
- 测试：`common.sdfField3D.*`、`box3d.sdf.sharedInterfaceAndBlobRoundTrip`、
  `fluids.sdf.sharedInterfaceAndDistanceFieldConvert`。

### P2 — 消费者扩展

- Softbody / Cloth：可选借用 `ISdfField3D`（与 `World3D` 探针并存）。
- 粒子 3D：扩展或并列 capability；GPU 路径明确回退原因。
- `VolumeFluidCoupling`：`updateSdfColliderPoses` 热路径（避免每步拷贝距离缓冲）—
  在 P0 正确性之后做的性能项。

### P3 — 场景级 / 渲染级（可选，非阻塞）

- 仅当 Lumen 类 AO / 大世界粒子导航需要时，再引入 clipmap 合成 DF。
- 与物理真相解耦：渲染 DF 允许视点相关近似；物理 / 流体继续 per-object。

## 5. 非目标

- 不把 SDF 做成 Box3D/Jolt 的原生形状（与 Unity/UE 一致）。
- 不做运行时任意蒙皮网格的完整 DF 重烘焙作为默认路径。
- 不把 Geometry Collection 破坏改造成连续 SDF 介质。
- 不做 MPM / FLIP / LevelSet 自由面求解器（另开规格）。

## 6. 应用的架构规范

- 模块边界 / capability 接线（`模块接口契约与机制选型规范`）
- Result 不得丢弃（`Result检查与不得丢弃返回值规范`）
- 跨域用 Link，不持有裸 `Body3D*` 跨帧（`领域短根继承与跨域组合架构`）
- 昂贵操作两步化：烘焙 vs 姿态更新（成本分层）

## 7. 验证

- 集成：`fluids.volumeCoupling.sdfObstacleTransfersImpulseToDynamicBody`
- 单元：`common.sdfField3D.*`、`box3d.sdf.sharedInterfaceAndBlobRoundTrip`、
  `fluids.sdf.sharedInterfaceAndDistanceFieldConvert`
- 回归：既有 `fluids.volumeCoupling.*`、`fluids.volume.ownedSdfCollider*`、
  `box3d.sdf.*`、`fluids.sdf.*`
- 源码门禁：`make check`（本变更触及公开 API 与文档）
