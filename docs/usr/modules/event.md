# 事件模块

**脚本入口：** `eve.PlatformEvent()`

泵送平台事件，并用字符串消息队列在模块或线程之间传递通知。

## 基本用法

```squirrel
local events = eve.PlatformEvent();
events.pushData("quest-complete", "intro");
local name = events.poll();
if (name == "quest-complete") showReward();
```

## 对象关系与调用时机

`PlatformEvent` 承接 SDL 平台事件泵和简单字符串消息队列。平台输入由 `pump()` 收集；业务消息由 `pushData()` 写入并由 `poll()` / `pollData()` 取出。默认循环已泵送事件，游戏一般只消费队列。可重放的领域事件使用独立的 `GameEvent` 日志。

## 目标导向指南

### 在系统之间发送一次性消息

生产者使用 `pushData(name, data)` 写入事件；消费者在 `eve_update` 中循环 `poll()` 或 `pollData()`，并用 `getLastData()` 读取最近消息的关联字符串。事件消费后即从队列移除，适合“任务完成”“切换关卡”而不是持久状态。

### 保持窗口响应

自定义主循环时每帧调用 `pump()`；模块没有阻塞等待接口；无事件时继续下一帧。清空业务消息应由消费者持续 `poll()`，不要停住渲染线程。

## 常见问题

- 同一事件被多个系统竞争消费：指定唯一消费者，再转发领域状态。
- 把队列当状态存储：队列是一次性的；持久状态应放组件或游戏模型。
- 在渲染阶段消费事件：会造成帧内顺序不稳定，应在更新阶段完成。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getLastData()`、`getName()`、`poll()`、`pollData()`、`pump()`、`pushData()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/platform_event/`](../../../src/modules/platform_event/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `event`。
