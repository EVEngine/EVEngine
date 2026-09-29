# Chaos Destruction 风格几何破碎系统设计

> 状态：P0 实施中（领域核心已落地；cook/graphics/editing 未做）。日期：2026-09-29  
> 目标：参考 Unreal Chaos Destruction，在 EVEngine 上落地一套 **预切几何集合 +
> 连接图 + 聚类层级 + 运行时场驱动** 的 3D 模型破碎框架，覆盖墙体、道具、掩体等
> 硬表面破坏，并给脚本/编辑器留稳定 API。  
> 关联：[`物理系统分层与后端契约.md`](./物理系统分层与后端契约.md)、
> [`模块编排与裁剪架构.md`](./模块编排与裁剪架构.md)、
> [`领域短根继承与跨域组合架构.md`](./领域短根继承与跨域组合架构.md)、
> [`重构代码质量与系统完整性规范.md`](./重构代码质量与系统完整性规范.md)、
> [`Result检查与不得丢弃返回值规范.md`](./Result检查与不得丢弃返回值规范.md)、
> [`体素引擎差距分析.md`](./体素引擎差距分析.md)、
> [`docs/usr/modules/physics.md`](../usr/modules/physics.md)。

## 0. P0/P1 落地备注（2026-09-29）

已合入模块：

- `physics_destruction`（`eve.Destruction()`）：Asset / Instance / Fields / PhysicsLink
- `physics_destruction_cook`：`FractureRecipe` + `cookGeometryCollection`（Uniform/Clustered Voronoi、Planar、Radial）

测试：`test/physics_destruction.cpp`、`test/physics_destruction_cook.cpp`；示例：`examples/destruction-basic`。

**层纠正**：因绑定 `World3D`/`Body3D`，宿主为 **LAYER 5**；cook 同层依赖宿主 + `asset`。
三角网格 CSG 切面盖帽与层级 cluster 骨块（schema 扩展）留待 graphics / schema v2。

P2+（graphics / editing / editor）仍按下文阶段表推进。

---

> 以下为原始设计正文。

## 1. 问题与非目标

### 1.1 要解决什么

游戏需要把一个（或多个）封闭 3D 网格在受击后按结构拆成碎块，并进入刚体模拟：

- 断口可控（Voronoi / 平面 / 径向），切面有内材 UV；
- 大块先裂、小块后碎（聚类层级），远处不必立刻模拟到叶子；
- 墙根可锚固、局部可弱化，玩法只注入“场”，不手写每块删除逻辑；
- 与现有 `World3D` / `Body3D` / `PhysicsLink` 组合，不引入第二套物理真源。

### 1.2 非目标（本设计刻意不做）

| 排除项 | 原因 / 替代 |
|--------|-------------|
| 命中瞬间对高模做完整 CSG / 实时 Voronoi | 成本尖峰；切块一律离线/加载期 cook |
| Teardown 式任意挖穿体素墙 | 走既有 `voxel`；本系统保留原 mesh 外观与 UV |
| Noita 式 2D 像素材料碎裂 | 走 `pixelworld` + `pixelworld_physics` |
| SoftBody / Cloth 撕裂 | 已有 `physics_softbody` / cloth tear |
| `building` 经营拆除与支撑图 | 放置权威仍在 `building`；本系统可作视觉/物理表现桥，不拥有占格真源 |
| 统一 `DestructibleActor` 大根类 | 违反短根架构；用短根 + Link |

## 2. Chaos 对照与选型

### 2.1 Chaos 能力映射

| Chaos 概念 | Eve 对应 | 说明 |
|------------|----------|------|
| Geometry Collection | `GeometryCollection`（cooked 资产 + 运行时实例） | 从 `CanonicalMeshData` 预切得到的骨块树 |
| Fracture Editor | `physics_destruction_cook` + `physics_destruction_editing` | 离线切分；编辑器 UI 后期 |
| Uniform / Clustered Voronoi、Radial、Planar | `FractureRecipe` 枚举 + 参数 | cook 期一次性生成叶子碎片 |
| Clustering / Fracture Level | `ClusterHierarchy` | 骨块父子树；运行时先解绑高层 cluster |
| Connection Graph | `ConnectionGraph` | 骨块邻接 + 应变阈值；断边才解绑 |
| Anchor Field | `DestructionField` kind=`Anchor` | 区域骨块不进入动态模拟 |
| External Strain / Master Field | `DestructionField` kind=`Strain` | 降低局部阈值或施加应变 |
| Sleep Field | `DestructionField` kind=`Sleep` | 落地碎块退出活跃模拟 |
| Field System Actor | `DestructionField` + 一次性/持续发射器 | 玩法只发场，不直接删骨块 |

### 2.2 路线选择

**采用 Chaos 同款：离线预切 + 运行时连接图解绑。**

不选：

- **纯关节焊接多 Body**：可作 v0 原型，但缺少聚类层级、内材盖帽与统一资产；正式路径仍收敛到 Geometry Collection。
- **仅换预制碎块 prefab**：无结构连接与层级，适合一次性爆炸特效，不适合墙体渐进坍塌。
- **运行时任意切面**：仅作为可选实验卫星，默认关闭，不进 `full` 必选路径。

## 3. 总体架构

```text
CanonicalMesh / 多 mesh 输入
        │  cook（asset → physics_destruction_cook）
        ▼
GeometryCollectionAsset (schema)     ← 唯一几何/连接真源（immutable 发布）
        │  instantiate
        ▼
GeometryCollectionInstance            ← 运行时权威：骨块状态、应变、活跃集合
   ├── PhysicsLink[]（叶子/活跃 cluster → Body3D）
   ├── RenderLink / 投影（可选 graphics 卫星）
   └── ConnectionGraph + ClusterHierarchy
        ▲
DestructionField（应变/锚固/冲量/休眠）
```

分层（对齐 softbody 卫星形态，**不**新建顶层 `destruction/` 目录）：

| 模块名 | DIR | LAYER | 职责 |
|--------|-----|-------|------|
| `physics_destruction` | `physics/destruction` | **5** | 资产解码、实例、连接图步进、场应用、与 `World3D` 解绑；无 graphics。P0 因依赖 Body3D 落在 L5（初稿 L3 已纠正） |
| `physics_destruction_cook` | `physics/destruction/cook` | **5** | `CanonicalMeshData` → asset；Voronoi/平面/径向（盒代理 + 连接图；三角 CSG 盖帽延后） |
| `physics_destruction_graphics` | `physics/destruction/graphics` | 4 | 骨块 mesh 同步、内材材质、休眠后静态合并（可选） |
| `physics_destruction_editing` | `physics/destruction/editing` | 5 | FractureRecipe schema、属性面板契约 |
| `physics_destruction_editor` | `physics/destruction/editor` | 7 | Fracture 编辑器 Mode（后期） |

- `physics` 宿主**不反向依赖** destruction；玩法通过脚本模块 `eve.Destruction()` 或 capability 接入。
- `headless` / `server` 可只开 `physics_destruction`（无 cook/graphics/editing）。
- 跨域：Instance 持有 `PhysicsLink`；不跨帧缓存 `Body3D*`；World 或 Instance 任一先销毁均可 `StaleHandle`。

## 4. 权威状态与身份

### 4.1 唯一权威

| 事实 | Owner |
|------|--------|
| 预切骨块几何、层级、连接边、默认阈值 | `GeometryCollectionAsset`（不可变发布后） |
| 运行时骨块状态（Attached / Detached / Sleeping）、当前应变、活跃物理绑定 | `GeometryCollectionInstance` |
| 刚体位姿、速度、碰撞 | `World3D`（经 `PhysicsLink`） |
| 渲染可见实例 | graphics 卫星投影；失败不回写 Instance |

禁止：把碎块 mesh 再抄一份进 scene/building 当第二真源；building 占格拆除仍由 `building` 拥有。

### 4.2 身份分层

| 身份 | 类型 | 用途 |
|------|------|------|
| 资产内容 | `ContentId` / schema fingerprint | 热重载比对 |
| 资产逻辑名 | `LogicalId`（如 `destruction:wall_brick_a`） | 引用 |
| 运行时实例 | `RuntimeHandle<GeometryCollectionTag>` | 进程内 |
| 骨块 | `BoneId`（asset 内稳定 index + 可选 PersistentId） | 连接图、存档 |
| 物理体 | `PhysicsLink` | 跨域；restore 后重建 |

## 5. 资产与 cook 契约

### 5.1 Schema

- Schema id：`physics:geometry-collection`
- Version：从 `1` 开始；未知字段拒绝；未知版本拒绝；迁移走 `SnapshotMigrationChain` 同类机制。
- Codec：`toValue()` / `fromValue()`；编辑器用 `physics_destruction_editing` 的 schema 镜像，默认值来自强类型定义，不另起一份真源。

### 5.2 `FractureRecipe`（cook 输入）

```text
FractureRecipe
  mode: UniformVoronoi | ClusteredVoronoi | Radial | Planar
  siteCountMin / siteCountMax   // Voronoi
  clusterCount / clusterRadius  // ClusteredVoronoi
  radialPlanes / radialSpokes   // Radial
  planeNormals[] / planeOffsets // Planar（可多次叠加）
  interiorMaterialLogicalId     // 切面内材
  randomStreamName              // 命名 RNG；禁止墙钟
  seed                          // 与 stream 组合，bit-exact cook
```

`cookGeometryCollection(CanonicalMeshData | span<CanonicalMeshData>, FractureRecipe)
  → Result<unique_ptr<GeometryCollectionAsset>>`

失败（非封闭、自交、过薄、超预算面数）返回结构化错误，**不**部分写出资产。

### 5.3 Asset 内容（v1）

- 叶子骨块：局部顶点/索引、外表面 vs 切面 material slot、凸包顶点（或预 cook 的碰撞代理句柄描述）、质量、质心、惯性近似；
- `ClusterHierarchy`：父子骨块、每层 fracture level；
- `ConnectionGraph`：无向边列表 `(a,b, restLength, strainThreshold, torqueThreshold?)`；
- 锚固默认骨骼集合（可选）；
- `revision` / content hash。

碰撞：叶子默认 **凸包**（对齐 Unity 预 bake hull）；三角网格仅调试或英雄物体，并受预算约束。

### 5.4 聚类规则

对齐 Chaos Auto Cluster：

1. cook 先生成叶子；
2. 再按 `clusterSites` 把空间邻近叶子收成上层 bone；
3. 运行时默认只为 **当前未断开的最高活跃 cluster** 创建/保持 `Body3D`；
4. 解绑后子骨块晋升为新活跃体；叶子才是不可再分的碰撞代理（v1 不做运行时再切）。

## 6. 运行时模型

### 6.1 短根与组件

```text
GeometryCollectionInstance : 领域短根（非 ECS Entity 亦可；若进 ECS 则 ENTITY 挂在 physics 侧短根）
  COMPONENT(CollectionRef)     // LogicalId / RuntimeHandle → Asset
  COMPONENT(Pose)              // 世界变换权威在 Instance，直到整簇 Sleep 合并
  COMPONENT(DestructionState)  // Active / Anchored / Settling
```

不引入 `DestructibleActor`。玩法对象（墙、掩体）通过 `PhysicsLink` / 自定义 Link 引用 Instance。

### 6.2 骨块状态机

```text
Attached ──(边应变 ≥ 阈值 或 Strain 场)──► Detached
Detached ──(速度/接触休眠条件 或 Sleep 场)──► Sleeping
Sleeping ──(可选 consolidate)──► 静态合并代理 / 保持独立静态 Body
```

- `Attached`：随父 cluster 刚体；
- `Detached`：独立 `Body3D`，动态；
- `Sleeping`：静态或移除活跃求解；graphics 可合并 draw。

### 6.3 步进与确定性

- 时间：注入 `SimulationStep{tick, delta}`，与 `World3D` 同拍或显式依赖其 observation tick；
- RNG：命名流（碎片抖动、可选二次特效），声明 `ToleranceBounded`（位姿随物理）或对拓扑事件 `BitExact`（哪条边在哪一 tick 断开必须可复现）；
- 拓扑事件顺序：按 `(tick, boneA, boneB)` 排序写入 owning event buffer；
- 失败的 step：不修改连接图、不创建/销毁 Body。

### 6.4 与 Joint 应力 API 的关系

现有 `Joint3D::setForceThreshold` + `jointstress3d` 适合 **少量手工焊接体**。  
Geometry Collection 的连接图是 **领域内图**，不强制为每条边创建一个 `Joint3D`（避免 N² 关节成本）。

可选桥：

- v1：Instance 内部积分相对应变（位置约束或冲量预算），超阈值断边；
- 可选：对英雄级少边结构投影为 `Joint3D`，复用 stress 事件——须在文档标明为兼容投影，权威仍在 ConnectionGraph。

## 7. Destruction Field（运行时控制）

```text
DestructionField
  kind: Anchor | Strain | Impulse | Sleep | Disable
  shape: Sphere | Box | Convex
  falloff: None | Linear | Squared
  magnitude / radius / durationTicks
  space: World | CollectionLocal
```

应用规则：

- 场不拥有 Instance；`applyField(InstanceHandle, Field)` 返回 `Result<FieldApplicationReceipt>`；
- Anchor：命中骨块标记 `Anchored`，创建动态 Body 时跳过或 kinematic 钉住；
- Strain：累加到边/骨块应变或临时降低 `strainThreshold`；
- Impulse：对已 Detached 或即将解绑的 Body 施加冲量（经 `World3D`）；
- Sleep：对落入区域且速度低于阈值的 Detached 骨块请求休眠；
- 子弹在持有 Instance 锁/步进期间 **不** 调用脚本回调。

玩法示例（对齐 Chaos “子弹削弱、物理完成破坏”）：

```squirrel
// 子弹命中点发射 Strain 场，而不是直接 destroy 墙
destruction.applyStrainField(instance, hitX, hitY, hitZ, radius, magnitude);
world3.update(dt);
destruction.step(instance, dt);
```

## 8. 公共 API 形状（C++ / 脚本）

一律 `Result` / `[[nodiscard]]`；禁止 `bool + lastError`。

### 8.1 Cook / Asset

```cpp
[[nodiscard]] Result<std::unique_ptr<GeometryCollectionAsset>>
cookGeometryCollection(const CanonicalMeshData& mesh, const FractureRecipe& recipe);

[[nodiscard]] Result<GeometryCollectionAsset>
GeometryCollectionAsset::fromValue(const Value& v);

[[nodiscard]] Result<Value>
GeometryCollectionAsset::toValue() const;
```

### 8.2 Instance

```cpp
[[nodiscard]] Result<RuntimeHandle<GeometryCollectionTag>>
DestructionWorld::createInstance(const GeometryCollectionAsset& asset, const Pose3& pose);

[[nodiscard]] Result<void>
DestructionWorld::destroyInstance(RuntimeHandle<GeometryCollectionTag> h);

[[nodiscard]] Result<void>
DestructionWorld::step(SimulationStep step);  // 所有实例或按 filter

[[nodiscard]] Result<FieldApplicationReceipt>
DestructionWorld::applyField(RuntimeHandle<GeometryCollectionTag>, const DestructionField&);
```

脚本模块名预留：`eve.Destruction()`（`SCRIPT Destruction SLOT destruction`），工厂与 poll 事件（`boneDetach`、`clusterBreak`、`boneSleep`）对齐 `platform_event` 或模块内 owning buffer + 脚本 poll，二选一在实施 PR 定稿，但 **不得** 在持锁时入脚本。

### 8.3 查询

- `getBoneState(boneId) → Result<BoneState>`
- `getActiveBodyLink(boneId) → Result<PhysicsLink>`（仅 Detached/活跃 cluster）
- `getDetachEventCount` / getters（帧缓冲，下次 step 清空或消费型 drain）

## 9. 生命周期与销毁顺序

| 顺序 | 行为 |
|------|------|
| Instance 先毁 | 释放全部 `PhysicsLink` 对应 Body（若 Instance 拥有创建权），再注销 handle |
| World3D 先毁 | Link resolve → `StaleHandle`；Instance 进入 `OrphanedPhysics` 可观测状态，禁止继续 step 写物理 |
| Asset 热重载 | 旧 Instance 不可静默换骨；`rebindAsset` 事务失败则保持旧拓扑 |
| 碎块 Sleep 合并 | 仅 Instance 可发起；graphics 投影重建；失败保留独立 Sleeping Body |

所有权：默认 **Instance 拥有其创建的 Body3D**（经 World 工厂创建，销毁责任在 Instance）。若外部注入已有 Body，必须显式 `Borrowed` 契约并禁止 Instance 销毁它们。

## 10. 与兄弟系统的边界

| 系统 | 边界 |
|------|------|
| `voxel` | 体素挖穿 ≠ Geometry Collection；可用 voxel 表现废墟层，但收藏集不读写 voxel 真源 |
| `pixelworld_physics` | 2D 碎片适配器模式可参考（owning fragment + PhysicsLink，无裸指针）；维度与资产格式不同 |
| `building` / `buildingfx` | 占格/支撑权威在 building；可选后期 `building_destruction` 桥：拆除或受击时生成/驱动 Collection，失败可观测 |
| `combat` / 武器 HitProbe | 命中只发 Field 或伤害事件；不直接改连接图 |
| SoftBody3D | 变形体积比可作“是否触发 Fracture 实例化”的前置判定，不与 Collection 混为同一资产 |

## 11. 分阶段实施

### P0 — 领域核心可测（无编辑器）

- [x] `physics_destruction`：Asset v1 编解码、Instance、手工 fixture 连接图（跳过 cook）、断边 → Body、双销毁顺序测试
- [x] `DestructionField`：Strain + Anchor + Sleep
- [x] Contract：`physics-destruction-bone-link-contract` + `test/physics_destruction.cpp`
- [x] 示例：`examples/destruction-basic`（预置两块焊接盒子）

### P1 — Cook

- [x] `physics_destruction_cook`：网格 AABB 上 Uniform Voronoi 盒代理 + 邻接连接图（三角 CSG 盖帽延后至 graphics cook）
- [x] Planar / Radial；命名 RNG（seed + stream）bit-exact fixture
- [x] ClusteredVoronoi 自动聚类：岛内边更高 `strainThreshold`，岛间更低（层级骨块 schema 延后）

### P2 — 表现与编辑

- [ ] `physics_destruction_graphics`：骨块 draw、内材、Sleep 后合批
- [ ] `physics_destruction_editing` schema；最小 Fracture 面板
- [ ] 用户文档 `docs/usr/modules/destruction.md` + MODULES 索引

### P3 — 生产强化

- [ ] 预算：每 tick 最大解绑边数、最大新生 Body、Sleep 合并
- [ ] 存档：Instance snapshot schema `physics:geometry-collection-instance/1`
- [ ] 可选 `building` 表现桥；可选英雄物体 Joint3D 投影

每阶段单一 PR：接口 + 后端 + 消费者 + 测试 + 文档；禁止中间破坏 CI 的半截提交。

## 12. 验收矩阵

| 类别 | 要求 |
|------|------|
| 架构 | 卫星目录在 `physics/destruction/**`；`module_depgraph --check` 无向上泄漏；公共 API 无含混 `bool` |
| Cook | 同 mesh + 同 recipe + 同 seed → 骨块拓扑与 content hash 一致 |
| 运行时 | 同场序列 → 断边事件集合 bit-exact；Body 位姿 tolerance-bounded |
| Link | World 先毁 / Instance 先毁 / Sleep 后再毁 Body 三条路径有测试 |
| 裁剪 | 无 graphics 时 Instance 仍可 step；无 cook 时仍可加载预烤资产 |
| 失败注入 | cook 非法网格、applyField 对 stale handle、step 在 OrphanedPhysics 下返回明确错误且状态不变 |

## 13. 风险与明确延期

| 风险 | 缓解 / 延期 |
|------|-------------|
| 薄片墙 Voronoi 不稳定 | cook 拒绝过薄；文档要求封闭实体厚度 |
| 碎块数量爆炸 | cluster + 每 tick 预算 + Sleep |
| 凹网格凸包穿透 | v1 凸包近似；英雄物体可选三角网格且限数量 |
| 运行时再碎叶子 | 延期到 P3+；v1 叶子为终态 |
| 与客户自己的损坏网格换装 | 可作为 Asset 变体，不替代连接图 |

## 14. 架构规范符合性（设计阶段自检）

| 规范条目 | 本设计中的落点 |
|----------|----------------|
| Result / nodiscard | §8 全部失败路径 |
| 短根 + Link | §6.1、§4.2；无 DestructibleActor |
| 单一权威 | §4.1 Asset / Instance / World3D |
| 双销毁顺序 | §9 |
| 注入时间与命名 RNG | §6.3、§5.2 |
| 持久 schema/version/未知字段 | §5.1、§11 P3 |
| 可选依赖双边测试 | §3 裁剪、§12 |
| 卫星不新建顶层破坏目录 | §3 `physics/destruction` |
| TODO 元数据 | 实施 PR 若留 HACK 必须带 owner/issue/expiry |

**故意不做的例外**：无。若实施时需保留“脚本 `bool` 兼容工厂”，必须标明 compatibility-only，并以 Result API 为内向真源（需另开用户确认）。

## 15. 参考

- Unreal Chaos Destruction Overview（Geometry Collection / Fracture / Clustering / Fields）
- Chaos Fracture modes：Uniform / Clustered Voronoi、Radial、Planar
- EVEngine：`SoftBody3DDefinition` schema 模式、`pixelworld_physics` Link 适配器、`Joint3D` stress 事件
- 行业实践：预切 + 预 bake 凸包；避免命中帧 Voronoi
