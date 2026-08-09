# 计时器模块

**脚本入口：** `eve.Timer()`

读取启动后的高精度时间和帧间隔。

## 基本用法

```squirrel
local timer = eve.Timer();
local dt = timer.step();
print("dt=" + dt + ", elapsed=" + timer.getTime() + "\n");
```

## 对象关系与调用时机

一个 `Timer` 保存自己的起始时间、上一帧计数和 delta。默认循环已提供 `dt`，只有自定义循环、性能统计或独立计时域才需要额外 Timer。

## 目标导向指南

### 制作与帧率无关的移动

每帧调用一次 `step()` 得到秒单位 `dt`，位移使用 `speed * dt`。暂停恢复后若不希望出现大步长，应丢弃恢复后的第一个 `dt` 或自行限制最大值。

### 统计阶段耗时

在任务前后读取 `getTime()` 并相减；`getDelta()` 返回最近一次 `step()` 保存的帧间隔，适合在多个系统间共享同一个 dt。

## 常见问题

- 每个系统各自调用 `step()`：会得到不同 dt；应由主循环统一调用。
- 把返回值当毫秒：所有时间值单位都是秒。
- 暂停恢复后物体穿透：限制用于模拟的最大 dt。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getDelta()`、`getName()`、`getTime()`、`step()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/timer/`](../../../src/modules/timer/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `timer`。
