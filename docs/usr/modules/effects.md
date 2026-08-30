# 通用效果模块

**脚本入口：** `eve.Effects()`

Effects 按 `subject` 保存临时或永久效果。效果具有类型、来源、优先级、持续时间、
强度、堆叠键、标签和 JSON 载荷；容器还保留 applied/refreshed/removed/expired
事件，适合状态效果、政策修正和临时增益。容器由模块拥有，脚本拿到的是带世代的
句柄，不要跨模块重载保存内部 `Effect`、`EffectEvent` 或 `EffectPayload` 引用。

## 创建容器并施加效果

```squirrel
local effectsResult = eve.Effects().newContainer();
if (!effectsResult.ok) throw effectsResult.status.summary;
local effects = effectsResult.value;

local idResult = effects.apply("hero:1", "buff.speed", "potion:7",
                               10, 8.0, "speed-buff", "refresh");
if (!idResult.ok) throw idResult.status.summary;
local effect = effects.find(idResult.value);
effect.addTag("positive");
effect.getPayload().setNumber("multiplier", 1.25);
```

`apply(subject, type, source, priority, duration, stackKey, policy)` 返回 Result。
`policy` 的可用值和堆叠行为由定义/运行时策略决定；不要把失败当作空 ID。
`applyCanonical(...)` 用已注册的效果定义创建效果。每帧调用 `update(dt)` 推进剩余
时间；消费完事件后调用 `clearEvents()`，否则事件会继续保留。

## 查询主体和标签

```squirrel
effects.update(dt);
for (local i = 0; i < effects.subjectCount("hero:1"); ++i) {
    local e = effects.subjectAt("hero:1", i);
    print(e.getType() + " remaining=" + e.getRemaining() + "\n");
}
for (local i = 0; i < effects.taggedCount("positive"); ++i)
    print(effects.taggedAt("positive", i).getId() + "\n");
```

查询返回借用引用：容器发生 `apply`、`remove`、`clear`、`update` 或释放后重新查询，
不要长期缓存。`release()` 会主动释放容器；`isStale()` 可检查句柄是否已经失效。

## API 快查

### `Effects` 与拥有型容器

| API | 说明 |
|---|---|
| `getName()` | 返回模块名。 |
| `newContainer()` | 返回 `{ ok, value, status }`，其中 value 是模块拥有的容器。 |
| `ownership()` / `handle()` / `isStale()` / `release()` | 查询所有权和句柄、检测失效或提前释放。 |
| `apply(...)` / `applyCanonical(...)` | 施加临时效果或按 canonical 定义施加效果，返回 Result。 |
| `remove(id, reason)` / `clear()` / `update(dt)` | 删除、清空或推进效果时间。 |
| `find(id)` / `effectCount()` / `effectAt(i)` | 按 ID 或稳定的当前索引查询效果。 |
| `subjectCount(subject)` / `subjectAt(subject, i)` | 查询某主体的效果。 |
| `taggedCount(tag)` / `taggedAt(tag, i)` | 查询带指定标签的效果。 |
| `eventCount()` / `eventAt(i)` / `clearEvents()` | 读取并清空变更事件。 |

### `Effect`、载荷与事件

| API | 说明 |
|---|---|
| `getId()` / `getSubject()` / `getType()` / `getSource()` | 效果身份和来源。 |
| `getStackKey()` / `getPriority()` / `getDuration()` / `getRemaining()` | 堆叠和时间信息。 |
| `getMagnitude()` / `getStackCount()` / `getPayload()` | 强度、层数和结构化载荷。 |
| `addTag(tag)` / `removeTag(tag)` / `hasTag(tag)` | 修改或测试标签，修改操作返回 Result。 |
| `tagCount()` / `tagAt(i)` | 枚举标签。 |
| `setString()` / `setNumber()` / `setBool()` / `setNull()` | 设置载荷字段。 |
| `setJson()` / `getJson()` / `toJson()` | 写入规范 JSON、读取单字段或序列化整个载荷。 |
| `has()` / `erase()` / `clear()` | 测试、删除或清空载荷字段。 |
| `getSequence()` / `getKind()` / `getEffectId()` / `getReason()` | 事件序号、类型、效果 ID 和原因。事件也提供 `getSubject()`、`getType()`、`getSource()`。 |

## 生命周期与确定性

- 容器是 `owned` 对象；模块卸载、显式 `release()` 或世代变化后句柄 stale。
- `update(dt)` 的 `dt` 由游戏传入，回放时必须使用相同的模拟步长。
- 遍历事件或效果时不要在循环中结构性修改容器；先记录 ID，再统一修改。
- Payload 接受 JSON 值而不是任意 Squirrel 对象；持久化时保存 canonical JSON。
