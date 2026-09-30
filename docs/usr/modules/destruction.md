# 几何破碎（Destruction）

**脚本入口：** `eve.Destruction()`（仿真核心）、`eve.DestructionFx()`（可选表现）

Chaos 风格的预切几何集合：骨块 + 连接图 + 运行时场驱动。宿主包
`physics_destruction` 只依赖 `physics` / `schema`，不拉渲染；需要画骨块或 Sleep
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

完整烟雾示例：[`examples/destruction-basic`](../../../examples/destruction-basic/)。

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

## API 快查

### `Destruction`（模块）

- `registerGeometryCollectionSchema()`：注册 `physics:geometry-collection@1`。
- `newWeldedBoxesFixture(size)`：返回脚本拥有的预置资产。
- `createInstance(world3, asset, x, y, z)`：在世界原点处实例化；失败抛脚本异常且不留半截 Body。

### `GeometryCollectionInstance`

- `applyStrainField(x, y, z, radius, magnitude)` / `applyAnchorField(...)` /
  `applySleepField(...)`：场应用失败时实例状态不变。
- `step(tick, dt)`：注入仿真时间；断边事件可查询。
- `boneCount()` / `edgeCount()` / `boneState(i)` / `edgeStrain(i)` / `edgeBroken(i)` /
  `detachEventCount()` / `hasLiveWorld()` / `releaseBodies()`。
- 骨块生命周期：`Attached` → `Detached` → `Sleeping`。Instance 拥有它创建的 Body；
  World 先毁或 Instance 先毁都通过 `PhysicsLink` 陈旧解析，不保留裸指针。

### `DestructionFx`（可选）

- `createRenderer(instance)`：返回脚本 VM 拥有的 `GeometryCollectionRenderer`；
  只借用 Instance，不拥有仿真状态。
- Renderer：`setInstance` / `getInstance`、`setExteriorColor` / `setInteriorColor` /
  `setSleepColor`、`draw(gfx)`、`lastActiveDrawCount()` / `lastSleepBatchCount()`。
- `draw` 失败（无 Graphics、无 Instance、World 已 stale）抛脚本异常；成功时
  Attached 用外材色、Detached 用内材色逐骨块画盒代理，Sleeping 走合批 Mesh。

## 生命周期

- Instance 借用 World3D；销毁前应 `releaseBodies()`，或保证 World 与 Instance
  任一先毁后另一侧不再写入。
- Renderer 借用 Instance 与 Graphics；销毁 Instance 前先 `setInstance(null)` 或
  丢弃 renderer。
- 不跨帧保留 Body3D / Mesh 裸指针以外的引擎对象；Sleep 合批 Mesh 由 Graphics
  拥有，revision 变化时重建。

## 相关

- 设计：[`docs/dev/2026-09-29-chaos-destruction-geometry-collection设计.md`](../../dev/2026-09-29-chaos-destruction-geometry-collection设计.md)
- 物理宿主：[physics.md](physics.md)
- 测试：`test/physics_destruction.cpp`、`test/physics_destruction_cook.cpp`、
  `test/physics_destruction_graphics.cpp`
