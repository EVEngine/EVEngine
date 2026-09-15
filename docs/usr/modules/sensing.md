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

## API 快查

- 模块：`newWorld()` / `resolve` / `release` / `isStale`
- World：`upsert` / `remove` / `query` / `circle` / `box` / `executePreset` / `resultAt` / `snapshotJson` / `restoreJson`
- 管线：`TargetingPipeline::withBuiltins` / `registerTask` / `registerPreset` / `execute` / `executePreset`

**源码：** [`src/modules/sensing/`](../../../src/modules/sensing/)  
**设计文档：** [`docs/dev/感知与目标选择系统设计.md`](../../dev/感知与目标选择系统设计.md)  
**相关测试：** [`test/sensing.cpp`](../../../test/sensing.cpp)、[`test/targeting.cpp`](../../../test/targeting.cpp)
