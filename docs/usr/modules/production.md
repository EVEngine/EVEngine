# 生产队列模块

**脚本入口：** `eve.Production()`

Production 是按 owner 分组的确定性连续进度队列。每个 owner 有独立并行槽位；任务
按优先级和入队顺序调度，并保留终态记录及事件。队列不解释 kind、product 或 context，
实际扣资源和生成实体应由领域系统在事务中完成。

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
| Queue | `advance(tick,dt)` / `getTick()` | 注入一个确定性模拟步或查询最后 tick。 |
| Queue | `setSlotCount(owner,n)` / `slotCount(owner)` / `runningCount(owner)` | 配置和查询 owner 的并发槽。 |
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
