# 线程与异步模块

**脚本入口：** `eve.Thread()`

使用线程池执行原生安全任务，通过 Channel/Event 把结果送回主线程。

## 基本用法

```squirrel
local threads = eve.Thread();
local channel = threads.newChannel();
local pool = threads.newThreadPool(2);
pool.submitPush(channel, "done", 100);
```

## 对象关系与调用时机

`Thread` 创建 ThreadPool 与 Channel；Pool 返回 Task；Channel 是 worker 到主线程的字符串通道。脚本 VM 和绝大多数引擎对象只允许主线程访问。

## 目标导向指南

### 把耗时任务移出主线程

创建线程池和 Channel，用 `submitSleep` / `submitPush` / `submitPost` 提交受支持任务；主线程轮询 Channel 或 Event 获取结果。worker 不得访问 Squirrel VM、窗口或 GPU 对象。

### 等待关卡加载任务

为每项任务保留 Task，轮询 `isDone()` / `hasFailed()`，显示进度；仅在退出或确定不会卡住界面时调用 `wait()` / `waitAll()`。

## 常见问题

- worker 调用 gfx/ui/Squirrel：这些对象不是线程安全的。
- 主线程立即 `waitAll()`：异步退化成卡顿的同步。
- 忽略 `hasFailed()` / `getError()`：失败任务会悄悄丢结果。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `clear()`、`demand()`、`getChannel()`、`getCount()`、`getError()`、`getHardwareConcurrency()`、`getName()`、`getPendingCount()`
- `getPool()`、`getStatus()`、`getWorkerCount()`、`hasData()`、`hasFailed()`、`isDone()`、`isRunning()`、`newChannel()`
- `newThreadPool()`、`pop()`、`postMain()`、`push()`、`stop()`、`submitPost()`、`submitPush()`、`submitSleep()`
- `supply()`、`wait()`、`waitAll()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/thread/`](../../../src/modules/thread/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `thread`。
