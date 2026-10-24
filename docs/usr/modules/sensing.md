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

后端不该自己去 `cap::provide<ILineOfSightQuery>`，而是调用 `sensing::ensureLineOfSightRouter()`：
首次调用由 `sensing` 把路由器注册为该 capability（`Applied`），之后是 `NoOp`；**如果槽位已经被
别的 provider 占用，则返回 `Conflict` 并把那个 provider 原样保留**——项目自己注册的实现不会被
静默顶掉。`physics` 就是这么做的：它认领 `World3D`，而不是独占整个 capability。

## 绑定与行为说明

| 对象 | API | 说明 |
|---|---|---|
| `Sensing` | `getName()` / `newWorld()` | 查询模块名，或创建模块拥有的 `SensingWorld` Result。 |
| 拥有型 World | `ownership()` / `ownerEpoch()` / `handle()` / `isStale()` / `release()` | 查询世代生命周期或释放 World。 |
| World | `upsert` / `remove` / `setZones` | 写入/删除主体事实，或更新逻辑 Zone 成员。 |
| World | `query` / `circle` / `box` / `executePreset` / `resultAt` | 查询候选并读取缓存结果。 |
| World | `queryPositions` | 圆环位置候选与距离评分，返回拥有型结果；可选 LOS。 |
| World | `setSpatialIndexEnabled` / `spatialIndexEnabled` / `debugLastQueryJson` | 可选空间广相与上次查询调试 JSON。 |
| World | `snapshotJson` / `restoreJson` | 确定性快照或事务性恢复 Result。 |
| `SensingCandidate` | `getId()` / `getX()` / `getY()` / `getDistance()` | `resultAt` 返回的只读候选字段。 |
| 管线 | `TargetingPipeline::withBuiltins` / `registerTask` / `registerPreset` / `execute` / `executePreset` | C++ Preset 注册与执行。 |

**源码：** [`src/modules/sensing/`](../../../src/modules/sensing/)  
**设计文档：** [`docs/dev/感知与目标选择系统设计.md`](../../dev/感知与目标选择系统设计.md)  
**相关测试：** [`test/integration/script-api/script_api_graphics_scene_tests.cpp`](../../../test/integration/script-api/script_api_graphics_scene_tests.cpp)、[`test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp`](../../../test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp)、[`test/integration/sensing/environment_query_tests.cpp`](../../../test/integration/sensing/environment_query_tests.cpp)

## 消费者迁移（Phase 3）

- RTS `CombatFireSystem` 自动索敌已改为组装 `QuerySpec`（`QueryCircle` + tags/factions + `TruncateToMax`）；目标 priority / stance 仍在 RTS。
- `perceptionFactsFrom(CandidateQueryResult)` 把有序候选投影为中立 `PerceptionFact`；npc_ai 侧 `perceptionMemoriesFrom` / `perceptionMemoryFrom`（`npc_ai/SensingPerception.h`）再写入 `PerceptionMemory`。sensing 不依赖 npc_ai。
- Weapon soft-lock / lock-on 获取路径尚未在 weapon 模块落地；落地时应走同一 QuerySpec/preset，aim assist 留游戏侧。

## 性能与调试（Phase 4）

- `setSpatialIndexEnabled(true, cellSize)` 打开可选 `SpatialHash2D` 广相（默认关）。过滤/排序仍由 `QuerySpec` 负责；索引只做候选裁剪。
- `debugLastQueryJson()` 返回上次成功查询的确定性 JSON（`eve.sensing.lastQuery`）：origin、shape、spatial.enabled/used/scanned/accepted、ranked scores。可供 MCP/debug overlay 消费；sensing 不依赖 editor。
- 当前查询路径保持同步；异步 request 未引入。

## 编译式环境查询

`EnvironmentQuery.h` 提供位置与主体共用的测量执行器。入口分为
`EnvironmentQueryPlan::compile(definition)` → `plan.instantiate(request)` →
`instance.run(services)`。计划拥有不可变配置和测量实现，实例拥有 Context 快照、
候选和诊断；两者均不借用定义对象。实例只运行一次，返回值不设置 primary。

`SensingWorld::executeEnvironmentQuery(plan, request, services)` 是带 MCP 诊断发布的
组合入口，调用相同执行器。它不修改旧的 `resultAt()` 缓存；环境查询返回拥有型结果。
`environmentInput(origin, spec)` 用既有空间广相收集主体，不排序、不应用最终数量限制，
也不发布中间结果。主体键是 World 内的事实键，不冒充持久 UUID。

### 查询定义与 Context

- `contexts` 声明名字；请求提供同名的非空、同坐标空间位置集合。实例创建时按槽位解析一次。
- `Input` 生成器使用请求里的主体或位置；`Ring` / `Grid` 在 World2D 或 World3D 的 XY 平面生成点。
  3D 点保留中心 Z，**不自动投影导航网格**。Grid2D/3D 可使用 Input 和距离测试。
- `maxItems` 是资源硬上限，超过会失败，不静默截断；`maxResults` 只限制最终返回量。
- Option 有优先级：当前 Option 没有合格候选才尝试下一项；错误立即传播，不转换为兜底。
- Context 是本次请求的值快照；LOS 后端仍读实时世界。消费方使用位置前应按玩法需要再次验证。

### 测试与评分

内置 Distance 和 LineOfSight；C++ 可实现 `IEnvironmentMetric` 返回有限原始数值。
一次测量由框架同时用于过滤和评分，不要求自定义实现再次遍历或重写归一化。

| 配置 | 行为 |
|---|---|
| `purpose` | Filter / Score / FilterAndScore |
| `filterOp` | 每个 Context 的范围判断取 All 或 Any |
| `aggregate` | 所有 Context 原始值取 Average / Minimum / Maximum，独立于过滤组合 |
| `normalization` | FixedRange 或 ActivePopulation（当前测试通过的有效候选） |
| `preference` | Higher / Lower / Reference（靠近 referenceValue） |
| `curve` | Linear / Square / SquareRoot |
| `weight` | 非负有限权重，贡献累加为总分 |

相对归一化遇到相同上下界时贡献为零；同分按候选 ID 排序。缺失指标、NaN、溢出
是结构化失败。测试完成即固定该测试的归一化范围，后续过滤不会追溯改写评分。

### 安全优化

`optimize` 只重排声明 `reorderable` 的连续测试段，有依赖的自定义测试构成屏障。
固定范围评分时，纯过滤测试优先，并按成本排序；所有参与评分的测试保留相对顺序，
避免浮点累加次序改变同分边界。存在 ActivePopulation 评分时
保留作者顺序，避免改变归一化语义。

`mode = Best` 只返回一个最优合格候选，仍不设置 primary。所有测试可重排且没有相对
归一化时，可以把一个昂贵纯过滤测试延后，按既有分数顺序验证直到首个通过者。
All 模式和有顺序屏障的查询不执行这一早停。该模式不是取一次 top-1 后再过滤。

### 脚本位置查询与 MCP

```squirrel
local world = eve.Sensing().newWorld().value;
local result = world.queryPositions(0.0, 0.0, 10.0, 0.0, 3.0, 16, 1, false);
if (result.ok && result.value.ranked.len() > 0) {
    local position = result.value.ranked[0].location;
    // 消费者自行决定是否移动到 position.x / position.y。
}
```

参数依次是 originX、originY、targetX、targetY、radius、samples、limit、requireLineOfSight，
脚本必须传满。该便利入口每次编译并执行圆环查询；频繁的 C++ 调用应保留计划。
开启 LOS 时检查的是 target → candidate，缺少后端返回 Unsupported。
圆环查询按到 origin 的直线距离评分，不保证可达、不提供路径规划。

`debugLastQueryJson()` 保留 `eve.sensing.lastQuery` v1 既有字段，并增添可空 `environment`。
其内部 `eve.sensing.environmentTrace` v1 包含计划 revision、状态、实际执行阶段、测量次数、
被淘汰测试和各项分数贡献；成功时附 `eve.sensing.environmentResult` v1。
`None/Summary/Detailed` 控制记录，详细候选达到 `maxTraceItems` 后标记 truncated。
没有某测试贡献表示尚未测试，不能推断为通过。执行失败也发布失败 trace，旧主体结果不变。
这些是输出协议，读者应忽略未知字段；没有反序列化入口或持久化查询资产格式。

运行示例见 [`environment-query`](../../../examples/environment-query/README.md)。

### 旧 Preset 的兼容行为

`TargetingPipeline::compile()` 返回拥有任务引用的 `TargetingPlan`；`registerPreset` 编译后
事务替换注册计划，已持有的旧计划继续使用旧定义。旧任务仍保持顺序，不参与环境测试优化。
Select 的 min/maxCount 延后到管线末尾；显式 truncate 必须是最后一步。
Select 只调用 `collectCandidates`，不再提前排序、截断或发布 World 结果。
无显式排序的旧管线在末尾按距离排序；仅配置 truncate 时，在截断前补上距离排序。
`SensingWorld::executePreset()` 只在整条成功后发布。

## Zone 成员

`setZones(id, "ns:name,...")` 写入主体的逻辑 Zone 成员（`LogicalId` 文本）。`SensingWorldCandidateProvider` 将其投影为 `TargetCandidate.zones`，并支持 `TargetingSpec.zone` 过滤。几何 Zone 文档仍由 crowd/editor 等模块拥有；sensing 只镜像成员关系。

## 用脚本提供候选（不用写 C++）

`ISensingCandidateProvider` 这条插座可以由脚本实现：脚本发布一个对象、实现 `queryCandidates(query)`，
`eve.sensing.TargetingResolver` 式的消费者就会用它来粗筛候选。

```squirrel
class MyCandidates {
    function queryCandidates(query) {
        // query.space         → "world2d" | "world3d" | "grid2d" | "grid3d"
        // query.origin        → 发起查询的主体 uuid
        // query.originLocation→ { space, x, y, z }
        // query.domain        → "any" | "self" | "ally" | "enemy" | "neutral"
        // query.minRange / query.maxRange（null = 无界）
        // query.requiredTags / query.excludedTags / query.zone / query.area
        local out = []
        foreach (id in myEnemyIds()) {
            out.append({ subject = id,
                         location = { space = query.space, x = 3.0, y = 4.0 },
                         domain = "enemy", tags = ["unit"], zones = ["arena:central"] })
        }
        return out
    }
}

eve.provider.publishFor("eve.sensing.ISensingCandidateProvider", "game.candidates", MyCandidates())
```

要点：

- **位置一律自带 `space`，且必须与 `query.space` 一致**。格子与世界单位不可互换，不一致时本次查询
  返回 `Unsupported`（提示显式换算），而不是猜一个单位。格子坐标必须是整数。
- `domain` 缺省 `neutral`，`tags`/`zones` 缺省为空；`zones` 用 `LogicalId` 文本（如 `arena:central`）。
- **可见性与数量策略不由提供方负责**：`lineOfSight` / `minCount` / `maxCount` / `countPolicy` 是
  消费者的策略，不会下发；脚本只返回粗筛候选，过滤与排序由消费者完成。
- `query.area` 是可选的空间约束，形状有 `circle2d` / `box2d` / `sphere3d` / `box3d` / `cone2d` /
  `gridBox2d` / `gridBox3d`；字段见 `SensingScriptCandidateSeam.h`。
- 失败不静默：脚本没实现该方法 → `Unsupported`；脚本抛错 → 查询失败（`Failed`）；答案形状不对
  （不是数组、主体不是规范 uuid、坐标不是数）→ `Rejected`。发布脚本 sink 会顶掉原生提供方，并在
  `eve.log` 里留一行 `warn`。
