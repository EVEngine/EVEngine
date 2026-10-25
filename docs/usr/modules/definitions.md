# 内容定义：版本、热替换和失效检测

**入口：** `eve.Definitions()`，槽位 `definitions`；`newRegistry()` 返回脚本持有的
DefinitionRegistry。它保存定义，不是玩家可变状态。type/id 是逻辑引用，version 是内容
格式版本，generation 是同一逻辑定义替换次数；两个数字不能互相替代。

## 任务一：注册并读取单位定义

初始化执行，项目需 definitions。JSON 使用 Squirrel 普通转义字符串，不照搬 JS 对象。

```squirrel
local registry = definitions.newRegistry();
local inserted = registry.insert("general", "tutorial-guard", 1, "{\"rank\":2}");
if (!inserted.ok) throw inserted.status.summary;
local initial = registry.resolve("general", "tutorial-guard");
if (!initial.ok) throw initial.status.summary;
assert(initial.value.version == 1);
local old_generation = initial.value.generation;
local replaced = registry.replace("general", "tutorial-guard", 2, "{\"rank\":3}");
if (!replaced.ok) throw replaced.status.summary;
local current = registry.resolve("general", "tutorial-guard");
if (!current.ok) throw current.status.summary;
assert(current.value.version == 2);
local stale = registry.resolveHandle("general", "tutorial-guard", old_generation);
assert(!stale.ok);
```

不能把 resolve 的借用视图一直留在下一次 replace 之后。跨帧保存逻辑键和 generation，
访问时重新 resolveHandle；明确选择“拒绝旧 generation”或“重新读取当前定义”。
不能悄悄把旧战斗中的技能改成新定义，热替换行为由业务域决定。

## 任务二：克隆目录状态用于加载校验

```squirrel
local snapshot = registry.snapshotJson();
local candidate = definitions.newRegistry();
local restored = candidate.restoreJson(snapshot);
if (!restored.ok) throw restored.status.summary;
assert(candidate.countType("general") == 1);
assert(candidate.atType("general", 0).getJson() == "{\"rank\":3}");
```

registry 的快照包含自身版本、generation 和事件，不等于整个游戏存档。
失败恢复保留原状态；玩家存档只记录逻辑引用及匹配的 contentVersion，先加载定义再恢复。
eventAt 用于观察 definition_registered/reloaded 等变化，消费后由一个拥有者清理。
有 UI/AI 多消费者时不要让任一消费者单独清空共享事件。

定义是否符合某个游戏 schema，需要业务域或 SchemaRegistry 显式验证；“JSON 可解析”
不等于完整的兵种/技能数据已经合法。避免每帧加载 JSON 与 replace；加载阶段一次验证，
模拟阶段使用受验证的定义。源码：[Definitions.cpp](../../../src/modules/definitions/Definitions.cpp)，
脚本插入、替换、枚举与恢复证据：[definitions.cpp](../../../test/integration/script-api/script_api_graphics_dap_tests.cpp)。

## 补充绑定参考

参数与接收者见 [源码生成参考](definitions/api-reference.md)，使用前先确认本章的前置条件与生命周期。
