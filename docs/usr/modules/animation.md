# 动画模块

**脚本入口：** `eve.Animation()`

使用 Tween 对标量或角度属性做延迟、重复、yoyo 和缓动插值。

## 基本用法

```squirrel
local anim = eve.Animation();
local move = anim.newTween(0.6);
move.setFrom("x", 0);
move.setTo("x", 200);
move.setEase("outQuad");
move.start();
anim.update(dt);
```

## 对象关系与调用时机

`Animation` 拥有多个 Tween 并统一 update；Tween 以字符串属性名保存多个标量轨道。Tween 只计算值，不会自动写回任意游戏对象。

## 目标导向指南

### 做 UI 滑入动画

创建 Tween，给 `x` 设置 from/to，选择 `outQuad`，调用 `start()`；每帧 `anim.update(dt)` 后读取 `tween.get("x")` 更新 UI 位置。

### 做往返呼吸效果

设置 duration、repeat 和 `setYoyo(true)`；颜色或缩放用多个命名属性并行插值。角度必须使用 `setFromAngle` / `setToAngle`，避免跨 360° 绕远路。

## 常见问题

- 创建 Tween 后忘记 `start()`。
- update 后不读取 `get(property)` 写回对象。
- 普通标量接口插值角度导致跨 360° 绕行。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `clearAll()`、`clearFinished()`、`evaluate()`、`get()`、`getActiveCount()`、`getDelay()`、`getDelta()`、`getDuration()`
- `getEase()`、`getEasedProgress()`、`getElapsed()`、`getFrom()`、`getName()`、`getProgress()`、`getPropertyCount()`、`getPropertyName()`
- `getRepeat()`、`getTo()`、`getTweenCount()`、`getYoyo()`、`has()`、`isActive()`、`isDelayed()`、`isFinished()`
- `isPaused()`、`isRunning()`、`isStopped()`、`newTween()`、`pause()`、`reset()`、`resume()`、`setDelay()`
- `setDelta()`、`setDeltaAngle()`、`setDuration()`、`setEase()`、`setFrom()`、`setFromAngle()`、`setRepeat()`、`setTo()`
- `setToAngle()`、`setYoyo()`、`start()`、`stop()`、`update()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/animation/`](../../../src/modules/animation/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `animation`。
