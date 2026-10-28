# 几何破碎（Destruction）

**脚本入口：** `eve.Destruction()`（仿真核心）、`eve.DestructionFx()`（可选表现）

Chaos 风格的预切几何集合：骨块 + 连接图 + 运行时场驱动。宿主包
`physics_destruction` 依赖 `physics` / `schema` 和纯值烘焙模块 `physics_bake` / `physics_bake_collapse`，不拉渲染；需要画骨块或 Sleep
合批时再开 `physics_destruction_graphics`。离线切分在
`physics_destruction_cook`（`FractureRecipe` + `cookGeometryCollection`）；编辑器
属性 schema 在 `physics_destruction_editing`。

`headless` / `server` 可只开核心模块；无 graphics 时 Instance 仍可 `step`，无 cook
时仍可加载预烤资产。

## 基本用法

```squirrel
local physics = eve.Physics();
local destruction = eve.Destruction();
destruction.registerGeometryCollectionSchema();
local world3 = physics.newWorld3D(0.0, -9.8, 0.0, true);
local asset = destruction.newWeldedBoxesFixture(1.0);
local instance = destruction.createInstance(world3, asset, 0.0, 0.0, 0.0);

instance.applyStrainField(0.0, 1.0, 0.0, 3.0, 1.5);
world3.update(dt);
instance.step(tick, dt);

if (has_module("destructionFx")) {
    local fx = eve.DestructionFx();
    local renderer = fx.createRenderer(instance);
    renderer.setExteriorColor(0.62, 0.58, 0.52, 1.0);
    renderer.setInteriorColor(0.78, 0.42, 0.28, 1.0); // Detached = 内材色
    renderer.setSleepColor(0.45, 0.45, 0.48, 1.0);
    // eve_render:
    gfx.clear();
    gfx.render3D();
    renderer.draw(gfx);
}
```

完整烟雾示例：[`examples/destruction-basic`](../../../examples/destruction-basic/)、
[`examples/destruction-clusters`](../../../examples/destruction-clusters/)（P3 预算/cluster/快照）。

## 目标导向指南

### 预置焊接盒子被应变场打断

1. `newWeldedBoxesFixture` 得到两块盒子 + 一条焊边的资产。
2. `createInstance` 在 World3D 上生成 Body3D，并只通过 `PhysicsLink` 绑定。
3. `applyStrainField` 在焊边中点附近累积应变；下一 `step` 断边并把骨块标为
   `Detached`（脚本 `boneState` 返回整数枚举）。
4. 可选：速度足够低时 `applySleepField` 把碎块标为 `Sleeping`；DestructionFx
   按 `sleepBatchRevision` 重建合批网格。

### 从网格资产离线 cook（P1）

C++ / 工具侧用 `FractureRecipe`（schema `physics:fracture-recipe` v1）调用
`cookGeometryCollection`。同 mesh + 同 recipe + 同命名 RNG seed/stream 得到
bit-exact 骨块拓扑。模式：`UniformVoronoi`、`ClusteredVoronoi`、`Radial`、
`Planar`。三角 CSG 切面盖帽延后。

编辑侧可用 `eve::physics_editing::fractureRecipeSchema()` 拉取与默认值对齐的
`PropertySchema`（无 UI 依赖）。

## 生产强化（P3）

### 步进预算

`setStepBudget(maxEdgeBreaks, maxSleeps)`：`0` = 不限。超额断边进入
`pendingEdgeBreakCount()`，下一 `step` 继续消化；超额 Sleep 记入场回执的
`sleepsDeferred`（C++ `FieldApplicationReceipt`）。v1 在 `createInstance` 时已为
全部叶子创建 Body，预算控制的是本帧激活/休眠工作量，不是运行时再分配 Body。

### Cluster 成员

Asset schema **v2** 在骨块上带 `clusterId` / `fractureLevel`（叶子一般为 0）。
`ClusteredVoronoi` cook 写入岛 id；跨岛断边产生 `clusterBreakEventCount()`。
v1 文档在加载时迁移为 `clusterId=0`。完整父子层级 Body 仍延期。

### Instance 快照

`captureSnapshotJson()` / `restoreSnapshotJson(json)`：schema
`physics:geometry-collection-instance@1`。只存骨块状态/边应变/原点，不存
`PhysicsLink`；restore 要求活 World 且骨边拓扑一致，失败不改拓扑。

## 绑定与行为说明

### `Destruction`（模块）

- `registerGeometryCollectionSchema()`：注册 `physics:geometry-collection@2` 与
  `physics:geometry-collection-instance@1`。
- `newWeldedBoxesFixture(threshold)` / `newClusterPillarFixture(interClusterStrain)`：
  预置资产（后者为双岛锚定柱）。
- `assetFromJson(json)`：解码 v1/v2 资产文档。
- `createInstance(world3, asset, x, y, z)`：在世界原点处实例化；失败抛脚本异常且不留半截 Body。

### `GeometryCollectionInstance`

  `applySleepField(...)` / `applyImpulseField(x,y,z,radius,mag,dirX,dirY,dirZ)`：
  场应用失败时实例状态不变。Impulse 只击中已 Detached/Sleeping 碎块，不松锚。
- `step(tick, dt)`：注入仿真时间；断边事件可查询。
- `setStepBudget(maxEdgeBreaks, maxSleeps)` / `pendingEdgeBreakCount()`。
  `captureSnapshotJson()` / `restoreSnapshotJson(json)` / `releaseBodies()`。
- 骨块生命周期：`Attached` → `Detached` → `Sleeping`。Instance 拥有它创建的 Body；
  World 先毁或 Instance 先毁都通过 `PhysicsLink` 陈旧解析，不保留裸指针。

表现 API 见 [`physics_destruction_graphics.md`](physics_destruction_graphics.md)
（`eve.DestructionFx()` / `GeometryCollectionRenderer`）。

## 生命周期

- Instance 借用 World3D；销毁前应 `releaseBodies()`，或保证 World 与 Instance
  任一先毁后另一侧不再写入。

## 相关

- 设计：[`docs/dev/2026-09-29-chaos-destruction-geometry-collection设计.md`](../../dev/2026-09-29-chaos-destruction-geometry-collection设计.md)
- 物理宿主：[physics.md](physics.md)
- 表现卫星：[physics_destruction_graphics.md](physics_destruction_graphics.md)
- 测试：`test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp`、`test/unit/physics_destruction_cook/physics_destruction_cook.cpp`、
  `test/integration/gameplay-composition/gameplay_composition_graphics_procgen_core_boundary_tests.cpp`、`test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp`

## 烘焙片段与播放器

`CollapseAnimation` 录制 `capture(instance, seconds)`，通过 `save(path)` / `load(path)`
保存和装入资产；这些操作返回 Result，调用者检查 `ok` 后再继续。`duration()`、
`boneCount()`、`frameCount()` 查询录制内容，`contentKey(source)` 提供非密码学内容摘要。

碎块与灰尘缓存共用原子、不覆盖的文件发布器。已有目标返回 `Conflict`，不会先截断再写入。
Linux 在发布前同步文件数据、发布后同步父目录；存储设备必须兑现 `fsync` 语义。
若刷盘失败，诊断 details 的 `publication` 区分 `not_published` 和 `published`，
`durability=unconfirmed` 表示持久性未确认。后者会保留可读取的完整目标，不能直接将失败视为文件不存在。
Linux 使用匿名 `O_TMPFILE` 暂存，进程中断后由内核回收尚未发布的数据，需要文件系统支持及
`/proc/self/fd` 链接；不支持匿名暂存时返回 `Unsupported`。其他平台目前只承诺原子可见性，
仍可能留下暂存目录；历史暂存目录不会自动删除，避免误删仍在写入的数据。

`clip.createPlayback()` 返回 Result，其 `value` 是 VM 拥有的 `CollapsePlayback`。
此准备操作复制并冻结全部样本，成本随录制大小线性增长，应在加载阶段调用一次。
之后 `player.copy()` 返回新播放器的 Result，共享冻结样本并复制当前播放状态，成本不随样本数增长。
原 clip 重新加载或销毁不会改变已有播放器。玩家之间的时间、暂停和实例变换互不影响。

- `advance(elapsedSeconds)`：显式输入非负秒数，返回 Result；不调用物理解算器。
- `seek(seconds)`：有限时间夹取到 `[0,duration]`，暂停时仍可跳转，返回 Result。
- `setRate(rate)`：有限有符号速率，负数倒放、零冻结，返回 Result。
- `setLoop(enabled)`：设置循环或端点夹取，返回 Result。循环推进使用 `[0,duration)`，
  显式 seek 可以停在末帧；零步长不改变末帧，零时长录制永远停在零。
- `setPaused(paused)`：只改变暂停状态；`paused()`、`time()`、`rate()`、`duration()` 查询状态。
- `setPlacement(x,y,z,qx,qy,qz,qw)`：以米为单位，先绕烘焙世界原点旋转，再平移。
  非零四元数在入参校验时归一化，不含缩放；返回 Result，失败保留旧变换。

插值遵循片段的线性位置、最短路径归一化四元数及离散状态帧边界规则。
输入无穷、NaN 或推进溢出会失败且保留原时间；变换后坐标溢出会在采样/渲染提交时报错。
播放器只负责表现，不重放碰撞、伤害或玩法事件。各实例在自己的线程上独占修改，
不允许同一播放器被并发读写；准备和播放均不调用脚本回调。

## 烘焙身份与安全复用

`eve.CollapseBakeRecipe()` 是拥有数据的配方构建器，以下方法均返回 Result：

- `setProducer(revision)`、`setBackend(revision)`：填写实际烘焙算法/构建修订和后端修订。
  标识必须为 1–256 字节的无空格可打印 ASCII；不能仅填写会长期不变的显示名称。
- `setParameters(bytes)`：完整参数序列，包括生产者已经解析的默认值。内部保存内容摘要。
- `setSource(name, bytes)`：添加或替换一个逻辑依赖及其内容摘要，最多 64 个；包含所有影响结果的资产和脚本。
- `setTiming(stepSeconds, sampleStride)`：真实固定仿真步长和每次采样间隔的 tick 数，均须为正。
- `setRandomStream(name, seed)`：登记每个随机流和种子，最多 64 个；脚本种子支持非负 32 位有符号整数，
  C++ API 支持完整 uint64。没有随机流表示不使用 RNG。
- `key()`：完整配方的非密码学缓存键 Result。依赖/随机流按名称排序，加入顺序不影响键；
  未填写生产者、后端、参数、时序或至少一个来源时返回失败。

在第一帧 `capture` 前调用并检查 `clip.beginBake(recipe)`；它冻结配方副本，后续修改构建器
不影响录制身份。录制和加载都会校验时间戳符合固定采样网格；夹杂任意时间帧会被拒绝。
已有帧的片段不能重新贴上另一个配方身份。保存已识别录制输出 EVECOLL1 v4；无身份录制仍输出 v3。

`clip.loadForRecipe(path, expectedRecipe)` 会验证文件和完整配方，仅成功时替换当前 clip。
文件不存在返回 `not_found`；身份不匹配返回 `conflict`；旧文件没有配方返回 `precondition_violation`。
无法打开、内容损坏、版本未知和时序不符合配方均返回明确失败，不能把这些情况统统当作首次烘焙。
普通 `load(path)` 仍允许显式加载历史资产；它不证明资产匹配当前配方。

EVECREC1 固定单位为米、右手 Y-up，采用长度前缀和确定字段顺序，包含完整版本标识、
参数/依赖摘要、float 步长、采样间隔和 uint64 种子。v4 在网格数之后存储 uint32 配方长度及配方字节，
随后为几何和帧，末尾校验覆盖全部内容。配方上限 64 KiB，未知版本/字段、重复或非规范顺序均拒绝。
来源内容摘要用于检测变更，不承担安全认证；调用者必须如实提供依赖和版本，系统不能从名称推断算法实现。

`recipe.producerBuildRevision()` 返回本构建在配置阶段生成的源码指纹，覆盖物理/破坏及其
引擎、资产、schema 代码、相关构建规则、依赖补丁和编译器/ABI/配置参数。内容变化（包括未提交修改）
会触发重新配置并更新指纹；路径迁移或墙钟时间不影响它。
`recipe.box3DBackendRevision()` 返回本构建使用的 Box3D 依赖修订标识，仅适用于明确选择 Box3D 的生产者。
这两个查询返回字符串，不创建物理世界，也不读取运行时文件。
房屋示例将这些标识与脚本/模型/参数/种子组合，默认使用 `loadForRecipe` 读取缓存；
显式 `playbackAsset` 使用普通加载，因此可在另一个构建或平台播放已发布资产。
