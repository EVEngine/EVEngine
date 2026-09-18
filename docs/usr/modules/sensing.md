# Sensing 模块

**脚本入口：** `eve.Sensing()`

`Sensing` 持有独立的 `SensingWorld` 事实镜像，并提供可配置的候选查询（`QuerySpec`）与
可组合的 Targeting Preset 管线。模块只返回有序候选与分数，**不会**选定 primary /
“最佳目标”；技能、武器、RTS 命令等消费者保留最终选择语义。

## 基本用法

```squirrel
local sensing = eve.Sensing();
local created = sensing.newWorld();
local world = created.value;

world.upsert("hero", 0.0, 0.0, "player", "unit", "");
world.upsert("foe", 5.0, 0.0, "enemy", "unit,combat-target", "player");

// 糖：内部转为 QuerySpec（TruncateToMax + distance sort）
local n = world.circle(0.0, 0.0, 12.0, "combat-target", "", "", "player", "", 8).value;
local first = world.resultAt(0);

// Preset 管线（内置 cone 选择）
local m = world.executePreset("sensing.builtin.coneSelect", 0.0, 0.0, 1.0, 0.0).value;
```

## QuerySpec 与形状

`SensingWorld.query(origin, spec)` 是唯一过滤/排序实现面。`circle` / `box` 是糖。

| 字段 | 含义 |
|---|---|
| `shape` | `QueryCircle` / `QueryBox` / `QueryCone` / 空（仅按 origin 距离） |
| `minRange` / `maxRange` | 相对 origin 的距离窗 |
| `requiredTags` / `excludedTags` | 标签包含 / 排除 |
| `includeFactions` / `excludeFactions` | 阵营字符串过滤 |
| `visibleTo` | 观察者键（事实可见性，不是几何 LOS） |
| `minCount` / `maxCount` + `countPolicy` | `TruncateToMax` 或 `FailIfOutOfRange` |
| `sortKey` | `None` 或 `DistanceAscending`（分数为 `-distance`） |

`QueryCone`：顶点 `(x,y)`，朝向 `(dirX,dirY)`，`halfAngle` 为弧度 `[0, π]`，`range` 为非负距离。

## Targeting Preset 管线

`TargetingPipeline` 把有序 Select / Filter / Sort 任务组成 Preset：

| Task id | 行为 |
|---|---|
| `sensing.select.world` | 从 `SensingWorld` 广相（复用 QuerySpec） |
| `sensing.filter.spec` | 按 QuerySpec 字段子集再过滤 |
| `sensing.filter.cone` | 朝向 + 半角 + 距离 |
| `sensing.filter.los` | 需要注入的 `ILineOfSightQuery` capability |
| `sensing.sort.distance` | 近→远，写入 score |
| `sensing.sort.truncate` | top-K |

内置 Preset：`sensing.builtin.coneSelect`（广相 → 45°锥 → 距离排序 → 截断 8）。

脚本 `world.executePreset(id, originX, originY, dirX, dirY)` 走共享 builtins 管线，并刷新 `resultAt` 缓存。

C++ 可自建管线：

```cpp
auto pipeline = eve::sensing::TargetingPipeline::withBuiltins();
eve::sensing::TargetingSourceContext ctx;
ctx.world = &world;
ctx.origin = {x, y, std::nullopt};
ctx.dirX = dirX;
ctx.dirY = dirY;
auto ranked = pipeline.executePreset(ctx, "sensing.builtin.coneSelect");
```

## 与 Targeting* 协议的关系

- `SensingWorld` = 候选事实 + QuerySpec / Preset 查询面。
- `SensingWorldCandidateProvider` / `TargetingResolver` = 兼容壳（Capability / Action）。
- `visibleTo` 与几何 LOS **正交**；`domain≠Any` 需要注入阵营关系，否则 `Unsupported`。
- 结果从不隐式设置 primary。

## 常见问题

- 把 `visibleTo` 当成 LOS——那是事实键；几何遮挡走 `ILineOfSightQuery`。
- 期望 Preset 选出“最佳目标”——sensing 只排序；primary 由消费者显式 `setPrimary`。
- 在非 World2D 事实上跑 2D QuerySpec——World 事实面目前是 2D。

## 视线 provider 的路由（`LineOfSightRouter`）

`eve.sensing.ILineOfSightQuery` 是**单槽位** capability，但一个项目通常有多套互不相关的视线实现
（世界坐标 3D 探针、战棋棋盘上的格线行走）。各自直接注册会变成"最后注册者生效"，实际行为取决于
模块加载顺序。

`LineOfSightRouter`（`sensing/LineOfSightRouter.h`）就是那个**唯一被注册的 provider**：每个后端声明
自己能解释的坐标空间，查询按空间分发。

- 某个空间**没有后端**时返回 `Unsupported`，诊断里带上空间名——不会静默返回"看不见"
  （那会被 `LineOfSightMode::Required` 当成"没有视线"而悄悄丢掉动作）。
- 两端点**空间不同**时返回 `InvalidArgument`：接口本来就禁止在空间之间隐式换算。
- 同一空间**重复认领**返回 `Conflict`；释放只能由认领者自己发起（否则 `NoOp`），因此一个模块
  不会误释放另一个模块的后端，重复释放也安全。
- 路由器**不拥有**后端：它只持有借用指针，认领方必须保证其后端在认领期间存活。

## API 快查

| 对象 | API | 说明 |
|---|---|---|
| `Sensing` | `getName()` / `newWorld()` | 查询模块名，或创建模块拥有的 `SensingWorld` Result。 |
| 拥有型 World | `ownership()` / `ownerEpoch()` / `handle()` / `isStale()` / `release()` | 查询世代生命周期或释放 World。 |
| World | `upsert` / `remove` / `setZones` | 写入/删除主体事实，或更新逻辑 Zone 成员。 |
| World | `query` / `circle` / `box` / `executePreset` / `resultAt` | 查询候选并读取缓存结果。 |
| World | `setSpatialIndexEnabled` / `spatialIndexEnabled` / `debugLastQueryJson` | 可选空间广相与上次查询调试 JSON。 |
| World | `snapshotJson` / `restoreJson` | 确定性快照或事务性恢复 Result。 |
| `SensingCandidate` | `getId()` / `getX()` / `getY()` / `getDistance()` | `resultAt` 返回的只读候选字段。 |
| 管线 | `TargetingPipeline::withBuiltins` / `registerTask` / `registerPreset` / `execute` / `executePreset` | C++ Preset 注册与执行。 |

**源码：** [`src/modules/sensing/`](../../../src/modules/sensing/)  
**设计文档：** [`docs/dev/感知与目标选择系统设计.md`](../../dev/感知与目标选择系统设计.md)  
**相关测试：** [`test/sensing.cpp`](../../../test/sensing.cpp)、[`test/targeting.cpp`](../../../test/targeting.cpp)

## 消费者迁移（Phase 3）

- RTS `CombatFireSystem` 自动索敌已改为组装 `QuerySpec`（`QueryCircle` + tags/factions + `TruncateToMax`）；目标 priority / stance 仍在 RTS。
- `perceptionFactsFrom(CandidateQueryResult)` 把有序候选投影为中立 `PerceptionFact`；npc_ai 侧 `perceptionMemoriesFrom` / `perceptionMemoryFrom`（`npc_ai/SensingPerception.h`）再写入 `PerceptionMemory`。sensing 不依赖 npc_ai。
- Weapon soft-lock / lock-on 获取路径尚未在 weapon 模块落地；落地时应走同一 QuerySpec/preset，aim assist 留游戏侧。

## 性能与调试（Phase 4）

- `setSpatialIndexEnabled(true, cellSize)` 打开可选 `SpatialHash2D` 广相（默认关）。过滤/排序仍由 `QuerySpec` 负责；索引只做候选裁剪。
- `debugLastQueryJson()` 返回上次成功查询的确定性 JSON（`eve.sensing.lastQuery`）：origin、shape、spatial.enabled/used/scanned/accepted、ranked scores。可供 MCP/debug overlay 消费；sensing 不依赖 editor。
- 当前查询路径保持同步；异步 request 未引入。

## Zone 成员

`setZones(id, "ns:name,...")` 写入主体的逻辑 Zone 成员（`LogicalId` 文本）。`SensingWorldCandidateProvider` 将其投影为 `TargetCandidate.zones`，并支持 `TargetingSpec.zone` 过滤。几何 Zone 文档仍由 crowd/editor 等模块拥有；sensing 只镜像成员关系。
