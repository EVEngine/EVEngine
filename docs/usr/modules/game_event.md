# 游戏事件日志：有序记录与独立消费

**入口：** `eve.GameEvent()`，槽位 `game_event`；newLog 创建模块拥有的 GameEventLog。
它记录领域事件，不是 SDL 输入队列，也不等于 Rx 的临时推送流。事件的 sequence 在本日志
内有序，eventId 是稳定 UUID；不要用 sequence 当跨日志身份。

## 任务一：记录一个可追踪事件

在已初始化的游戏中，配置包含 game_event。

```squirrel
local history = game_event.newLog();
local appended = history.append(
    "00000000-0000-7000-8000-00000000e001", "tutorial_hit",
    "tutorial:combat", "tutorial:hero", "", "", 1, 0, "{\"damage\":10}"
);
if (!appended.ok) throw appended.status.summary;
local sequence = appended.value;
assert(history.size() == 1);
local item = history.find(sequence);
assert(item != null && item.getType() == "tutorial_hit");
assert(item.getTick() == 1);
```

tick 由模拟注入，不能用墙钟替代。payload 是 JSON 文本，flags 是范围受限的整数。
type 会映射到 `game_event:<type>` schema v1；若域注册了 payload schema，需满足它。
append 失败时不要先改 UI/存档日志再假装事件已成功。业务状态与事件是否同事务发布，
必须由该业务入口保证，单独 append 不会自动撤销已经扣掉的 HP。

## 任务二：两个消费者各自推进

```squirrel
local hud_reader = history.newConsumer(1);
local quest_reader = history.newConsumer(1);
hud_reader.read(16);
quest_reader.read(16);
assert(hud_reader.batchCount() == 1);
assert(quest_reader.batchCount() == 1);
assert(hud_reader.batchAt(0).getSubject() == "tutorial:hero");
local saved = history.snapshotJson();
local restored = history.restore(saved);
if (!restored.ok) throw restored.status.summary;
```

每个消费者有自己的 position/seek，不互相抢消息。batchAt/find/at 是日志内容的借用视图；
修改、清理或恢复日志后重新读取，不把事件原生对象长期留在脚本闭包里。newConsumer 的
对象和日志均由模块管理，模块先销毁时停止使用它们。clearBefore 只能在所有必需消费者
不再需要旧事件后执行；快照恢复还要重建消费者游标和领域关联。

日志快照 schema 是 `game_event:stream` v1。持久化前检查 schema/version，未来版本不能
当普通数组读入；顺序、因果、correlation 和 payload 不能靠字符串替换迁移。
queryType/Source/Subject/Sequence 会形成查询结果，再用 queryAt 消费，下一次查询可能
覆盖前一次查询集合。实现及生命周期见
[GameEvent.cpp](../../../src/modules/game_event/GameEvent.cpp)。
完整领域组合可参考 [Action 事件适配测试](../../../test/action_game_event.cpp)。

## 补充绑定参考

参数与接收者见 [源码生成参考](game_event/api-reference.md)，使用前先确认本章的前置条件与生命周期。
