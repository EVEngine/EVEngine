# 生产队列模块

**脚本入口：** `eve.Production()`

Production 是按 owner 分组的确定性连续进度队列。每个 owner 有独立并行槽位；任务
按优先级和入队顺序调度，并保留终态记录及事件。队列不解释 kind、product 或 context，
实际扣资源和生成实体应由领域系统在事务中完成。

C++ 领域适配器可以使用 `ProductionRequest` 固定 generation-qualified definition、保存
外部资源预留凭证，并要求显式结算。此类任务完成工作量后进入 `ReadyToSettle`，只有
领域系统成功发布科技、单位或物品并提交唯一 `ProductionSettlementReceipt` 后才进入
`Completed`。失败可保留为 `SettlementFailed` 并显式重试；重复提交同一 receipt 是幂等
NoOp。旧式脚本 `enqueue` 保持自动结算兼容。

非空 reservation 依次经过 Reserved、Started、Consumed 或 Released。取消/失败后，
领域适配器调用 `releaseReservation(taskId, releaseId)` 获取原始预留载荷和退款千分比，
再在自己的库存或经济事务中执行退款；相同 release ID 重试返回 NoOp，因此队列不会重复
授权回滚。Production 只拥有生命周期与释放凭证，不越权修改领域账户。

RPG 不另建生产包：`rpg/Crafting.h` 的 `Crafting` 适配器把配方投影到同一队列。
`Crafting::begin` 在一个事务中扣除背包材料并入队，`process` 可填写 `craft`、
`alchemy`、`brewing`、`medicine`、`cooking` 等领域分类；队列只负责耗时和状态，
不硬编码这些玩法。`Crafting::settle` 从任务内保存的预留载荷发布全部产物；目标背包
容量不足时任务进入可重试的 `SettlementFailed`，相同任务重复结算不会重复发放物品。
`CraftingRequest::batchSize` 会在同一原子事务中同时放大材料成本和最终产物，并在任何
整数溢出前拒绝请求，因此批量不是只用于 UI 展示的任务元数据。

C++ `ProductionRequest::dependencies` 可以组成小型、向后引用的确定性 DAG：`AllOf`
等待全部前置成功，`AnyOf` 在任一前置成功后放行，`blocks` 则在指定任务进入终态前
禁止并发执行。依赖可以跨 owner；任何任务完成、取消或失败后，队列都会按稳定 owner
顺序重新调度。`blockReason(taskId)` 提供不修改队列的阻塞原因查询。依赖 ID 必须在入队
时已经存在，因此正常 API 不可能创建环；快照恢复仍会重新检查悬空引用、自引用和环。
`requiredDefinitions` 按完整 definition generation 匹配，避免热重载后误用旧科技或配方；
`requiredTags` 使用 owner 的精确标签事实。领域系统通过 `setDefinitionAvailable` 和
`setTagAvailable` 发布或撤回这些事实，变更会立即触发稳定重调度，并随快照保存。

C++ 请求还可声明 `batchSize`、千分比 `efficiencyPermille` 和命名资源需求。批次数量属于
产出契约，不会复制任务；固定步按整数效率缩放工作量，`contribute` 可注入工人、英雄或
设施提供的额外确定性工作量。命名资源只在 Running 期间占用，适合表达炼金坩埚、研究员、
装配线等能力容量。取消和失败分别使用显式退款策略；任务记录 0..1000 的可退款比例，领域
适配器再依据原始 reservation 执行一次性退款，Production 不直接修改领域库存。

高级确定性执行同样留在本模块：`OutputRandomProvenance` 固定命名随机流、seed 和 draw
区间，领域结算据此重放随机产出；repeat policy 支持有限周期、continuous 和外部库存回报
驱动的 maintain-stock。每个 owner 可选 Priority、FIFO 或 ShortestRemaining 调度。
`predict` 在不修改队列的前提下返回剩余工作量、最早开始/完成估计及是否精确；
`advanceTo` 只有在完整 tick 距离不超过 `maxSteps` 时才追帧，预算不足不会部分推进。
任务、事件和 `diagnose` 共享稳定 correlation ID，便于跨 RTS、RPG 和工具链定位同一订单。

## 入队和固定步推进

```squirrel
local result = eve.Production().newWorkQueue();
if (!result.ok) throw result.status.summary;
local queue = result.value;
queue.setSlotCount("factory:north", 2);
local taskResult = queue.enqueue("factory:north", "vehicle", "tank",
                                 "{\"armor\":100}", 8.0, 5);
if (!taskResult.ok) throw taskResult.status.summary;

// tick 必须严格递增，dt 使用固定模拟秒数：
queue.advance(simulationTick, fixedDt);
```

脚本 binding 的 `advance(tick, dt)` 要求 tick 严格递增；不要使用墙钟。duration 必须为
正数，context 必须是合法 JSON。暂停任务不占运行槽；恢复后重新参与确定性调度。

## API 快查

| 对象 | API | 说明 |
|---|---|---|
| `Production` | `getName()` / `newWorkQueue()` | 查询模块名或创建模块拥有的队列 Result。 |
| 拥有型队列 | `ownership()` / `ownerEpoch()` / `handle()` / `isStale()` / `release()` | 查询句柄生命周期或释放队列。 |
| Queue | `enqueue(owner,kind,product,context,duration,priority)` | 入队并返回稳定 task ID Result。 |
| Queue | `pause()` / `resume()` / `cancel()` / `fail()` | 改变非终态任务；操作返回 Result。 |
| Queue（C++） | `settle()` / `failSettlement()` / `retrySettlement()` | 领域产物的幂等提交和可重试失败。 |
| Queue（C++） | `releaseReservation(taskId,releaseId)` | 幂等领取取消/失败任务的领域回滚凭证。 |
| Queue | `advance(tick,dt)` / `getTick()` | 注入一个确定性模拟步或查询最后 tick。 |
| Queue | `setSlotCount(owner,n)` / `slotCount(owner)` / `runningCount(owner)` | 配置和查询 owner 的并发槽。 |
| Queue（C++） | `blockReason(taskId)` | 只读查询依赖或互斥任务造成的确定性阻塞原因。 |
| Queue（C++） | `setDefinitionAvailable()` / `setTagAvailable()` | 发布 owner 的定义版本和标签条件事实。 |
| Queue（C++） | `contribute(taskId,work)` | 按整数效率追加外部工作量。 |
| Queue（C++） | `setResourceCapacity(owner,name,n)` / `resourceCapacity()` | 配置命名生产资源容量。 |
| Queue（C++） | `setSchedulerStrategy()` / `schedulerStrategy()` | 配置 owner 的稳定调度策略。 |
| Queue（C++） | `reportStock()` / `predict()` / `diagnose()` | 驱动维持库存并查询预测或结构化诊断。 |
| Queue（C++） | `advanceTo(target,dt,maxSteps)` | 在显式预算内执行固定步追帧。 |
| Queue | `find(id)` / `taskCount()` / `taskAt(i)` | 查询任务或按入队顺序枚举。 |
| Queue | `ownerTaskCount()` / `ownerTaskAt()` | 枚举指定 owner 的任务。 |
| Queue | `eventCount()` / `eventAt(i)` / `clearEvents()` | 读取并清空生命周期事件。 |
| Queue | `snapshot()` / `restore(json)` / `clear()` | 快照、事务性恢复或完全重置。 |
| Task | `getId()` / `getOwner()` / `getKind()` / `getProduct()` | 任务身份和领域分类。 |
| Task | `getContext()` / `getDuration()` / `getProgress()` | owning context 表、总时长和已完成秒数。 |
| Task | `getPriority()` / `getState()` | 调度优先级与 queued/running/paused/terminal 状态。 |
| Event | `getSequence()` / `getTick()` / `getKind()` | 事件顺序、模拟 tick 和类型。 |
| Event | `getTaskId()` / `getOwner()` / `getTaskKind()` / `getProduct()` | 事件关联任务。 |
| Event | `getReason()` | cancelled/failed 等事件原因。 |

## 生命周期、事务与确定性

- Queue 是模块拥有对象；Task/Event 是即时借用，队列修改、restore 或 release 后重查。
- `restore` 先验证完整快照再替换，失败时保持原状态；成功后旧借用引用全部失效。
- 完成事件本身不生成产品；先验证并提交资源/实体事务，再确认对外可见的产品状态。
- 回放必须保持相同 tick、dt、入队顺序、优先级和槽位变更序列。
