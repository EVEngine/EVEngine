# 标签与能力：事实筛选，不代替权限或属性

**入口：** `eve.Tags()`，槽位 `tags`。TagStore 保存某主体拥有的标签和能力集合，并维护
反向索引及变更事件。subject 是游戏指定的稳定键；删除主体时必须移除其标签。

## 任务一：筛选可行动单位

初始化执行，项目需 tags。

```squirrel
assert(tags.add("tutorial:hero", "friendly"));
assert(tags.add("tutorial:hero", "selected"));
assert(tags.addCapability("tutorial:hero", "can.move"));
assert(tags.requireAll("tutorial:hero", ["friendly", "selected"]));
assert(tags.requireAny("tutorial:hero", ["enemy", "friendly"]));
assert(tags.requireAllCapabilities("tutorial:hero", ["can.move"]));
assert(!tags.hasCapability("tutorial:hero", "can.fly"));
```

requireAll/Any 接收数组，不是变长参数。Tag 表示分类/事实，Capability 表示能力事实；
能移动不等于某玩家有权限控制它，权限判断见[Authority](authority.md)。add/remove 的
bool 不能当成 Result 表读取；重复添加是否产生变化应按返回值和事件语义处理。

## 任务二：反向查找并清理死亡主体

```squirrel
assert(tags.subjectCount("friendly") == 1);
assert(tags.subjectAt("friendly", 0) == "tutorial:hero");
local events_before = tags.eventCount();
assert(tags.removeSubject("tutorial:hero"));
assert(!tags.has("tutorial:hero", "friendly"));
assert(!tags.hasCapability("tutorial:hero", "can.move"));
assert(tags.eventCount() >= events_before);
```

先收集查询结果，再修改同一集合。事件的 sequence/action/subject/value 用于更新投影，
不能成为标签事实的第二份权威状态；clearEvents 由调度者在消费者处理完后调用。

## 与 GameplayTag 的区别

Action/Combat 的层级 GameplayTag，例如 `ability.attack.melee`，由
GameplayTagRegistry 管理；匹配 descendant 需要明确选项，不能把任意字符串前缀当层级。
该注册目录有 `eve.gameplay-tags` schema/version，名称、ID 和元数据须一致。
本章的 `eve.Tags()` 不是该 C++ 注册目录的全部方法自动绑定；不要假设可调用
tags.registerTag。分别参阅 Action/Combat 的 tag 数据接口与它们的实际脚本契约。

证据：[Tags.cpp](../../../src/modules/tags/Tags.cpp)、
[tags.cpp](../../../test/tags.cpp)、
[GameplayTag 测试](../../../test/gameplay_tags.cpp)。

## 补充绑定参考

参数与接收者见 [源码生成参考](tags/api-reference.md)，使用前先确认本章的前置条件与生命周期。
