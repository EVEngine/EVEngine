# 通用命令队列模块

**脚本入口：** `eve.Orders()`

Orders 提供不绑定具体实体类型的确定性优先级队列。一个队列同时最多有一个 active
命令，其余命令按优先级降序、同优先级按插入顺序调度；完成、失败、取消、抢占和超时
都会留下可查询事件。完整组合示例见 `examples/commandery-rts`。

## 创建和执行队列

```squirrel
local result = eve.Orders().newQueueOwned();
if (!result.ok) throw result.status.summary;
local queue = result.value;
local idResult = queue.append("move", 10, 5.0);
if (!idResult.ok) throw idResult.status.summary;
local order = queue.current();
order.getPayload().setNumber("x", 12.0);
order.getPayload().setNumber("y", 8.0);

queue.update(dt);
if (arrived) queue.complete(idResult.value);
```

`append` 添加普通命令；`replace` 取消所有未结束命令并立即开始新命令；`interrupt`
只在优先级允许时抢占 active 命令。所有 Result 都必须检查，负数/非有限 `dt`、空 kind
和非法状态迁移不会静默成功。

## API 快查

| 对象 | API | 说明 |
|---|---|---|
| `Orders` | `getName()` / `newQueueOwned()` | 查询模块名或创建模块拥有的队列 Result。 |
| 拥有型队列 | `ownership()` / `handle()` / `isStale()` / `release()` | 查询所有权、句柄和失效状态，或提前释放。 |
| 队列 | `append()` / `replace()` / `interrupt()` | 创建命令并返回稳定 ID。 |
| 队列 | `complete()` / `fail()` / `cancel()` | 终结命令；失败/取消可附带 reason。 |
| 队列 | `update(dt)` / `clear()` | 推进超时或完全重置队列和计数器。 |
| 队列 | `current()` / `find(id)` | 借用当前命令或按稳定 ID 查询历史命令。 |
| 队列 | `queuedCount()` / `orderCount()` / `orderAt(i)` | 查询排队数量或按创建顺序枚举所有命令。 |
| 队列 | `eventCount()` / `eventAt(i)` / `clearEvents()` | 消费确定性生命周期事件。 |
| 队列 | `snapshot()` / `restore(json)` | 返回 Result 快照或事务性恢复；失败保持原状态。 |
| `Order` | `getId()` / `getKind()` / `getState()` | 命令身份、类型及 queued/active/completed/failed/cancelled 状态。 |
| `Order` | `getPriority()` / `getTimeout()` / `getElapsed()` | 调度和超时信息。 |
| `Order` | `getPayload()` | 返回命令拥有的即时借用 Payload。 |
| Payload | `setString()` / `setNumber()` / `setBool()` / `setNull()` | 写入 JSON 兼容值。 |
| Payload | `setJson()` / `getJson()` / `toJson()` | 解析字段 JSON、读取字段或确定性序列化。 |
| Payload | `has()` / `erase()` / `clear()` | 测试、删除或清空字段。 |
| Event | `getSequence()` / `getOrderId()` / `getKind()` | 事件顺序与关联命令。 |
| Event | `getFrom()` / `getTo()` / `getReason()` | 状态迁移和原因。 |

## 生命周期与确定性

- 队列是模块拥有的 generation-qualified 对象；模块重载或 `release()` 后 stale。
- `Order`、Payload 和 Event 都是即时借用观察，任何队列修改后应重新查询。
- `update(dt)` 只使用注入的模拟时间；确定性回放需保持相同输入顺序和 `dt`。
- 快照包含 schema/version、队列成员、历史、事件和计数器；只恢复 `snapshot()` 产物。
