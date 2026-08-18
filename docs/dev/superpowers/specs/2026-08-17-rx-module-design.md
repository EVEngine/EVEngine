# Rx 响应式编程模块设计

日期：2026-08-17
状态：已实现（`src/modules/rx/`），待合入 review

## 背景

EVEngine 以「模块 + Squirrel 脚本」为主，事件系统（`event`）是主线程轮询的字符串消息
队列，异步层（`async.nut`）用 Promise 串接一次性流程。缺少 UniRx 风格的「基于推送的
响应式流」：可多播、可组合变换、可观察状态、可与事件队列桥接。

## 目标

1. 同时服务 **C++ API** 与 **Squirrel 脚本 API**（`eve.Rx`）。
2. 覆盖 UniRx 核心能力：`Subject` 家族、LINQ 风格操作符、`ReactiveProperty`、事件桥接。
3. 全部走推送模型：`onNext` / `onError` / `onCompleted` + 可取消订阅。
4. 线程安全：`onNext` 可跨线程调用（内部互斥锁），回调在调用线程同步执行。

## 非目标

- 不做 Scheduler / 线程切换（主线程回调由调用方保证，worker 先 `postMain` 回主线程）。
- 不做时间相关操作符（interval / throttle / debounce）的完整实现（后续可加）。
- 不改造现有 `event` 模块；只在 `Rx` 侧做只读桥接（`pump`）。
- 不做 C++ 同名重载与脚本名分叉（脚本侧用 `subscribe` / `subscribe3` 区分）。
- 不在 v1 支持 `ReactiveCollection` / `ReactiveDictionary`。

## 方案

### C++ 核心（`Rx.h`，模板头文件）

- `Observer<T>`：`onNext` / `onError` / `onCompleted` 三个 `std::function`，带终止标记。
- `Subscription`：move-only 的 RAII 句柄，`dispose()` 幂等，析构自动释放。
- `Observable<T>`：纯虚 `subscribe(Observer<T>)` + 便利重载；操作符返回**新分配的
  `Observable*`（调用方持有/负责释放）**。
- `AnonymousObservable<T>`：用 `std::function<Subscription(Observer<T>)>` 构造任意可观察源。
- 操作符（模板方法）：`map` / `filter` / `take` / `skip` / `first` / `takeUntil` /
  `distinctUntilChanged`。
- `Subject<T>`：多播流；内部 vector + mutex；`dispose` 惰性移除（不缩容，`observerCount`
  只统计活跃者）。
- `BehaviorSubject<T>`：订阅时重放最近值；`getValue` / `setValue` / `onNext` 等。
- `ReplaySubject<T>`：按容量缓存并重放；`capacity=0` 表示不限。
- `ReactiveProperty<T>`：`get` / `set` / `subscribe`；基于 `BehaviorSubject`。

### Squirrel 绑定（`Rx.cpp`）

- `Value` 变体（nil / int / float / bool / string / ptr）承载跨语言值，配 `ssq::detail`
  的 `pushValue` / `popValue` 特化，保证绑定方法与返回值可直接用 `Value` 收发。
- 模块 `Rx`：`newSubject` / `newBehaviorSubject` / `newReplaySubject` / `newProperty` /
  `fromEvent(name)` / `pump(ev)`。
- 类：`Observable`、`Subject`、`BehaviorSubject`、`ReplaySubject`、`ReactiveProperty`、
  `Subscription`。
- 脚本回调：`subscribe(onNext)` 与 `subscribe3(onNext, onError, onCompleted)`（全部必填，
  不需要传 `null`），内部经 `callScript` 用原生 `sq_call` 同步调用，绕过 ssq 的严格参数
  数检查，兼容可选参数闭包。
- 操作符接收脚本闭包：`map(fn)` / `filter(pred)` 等通过 `callScript(..., wantResult=true)`
  取返回值。

### 事件桥接

- `fromEvent(name)` 注册一个共享 `Subject`（`bridges_` 字典持有 `shared_ptr`），返回一个
  包装后的 `AnonymousObservable`（脚本持有）。
- `pump(ev)` 排空 `event::Event` 队列，把 name 匹配的消息（首个 string 参数）推给对应流。
- 订阅生命周期独立：脚本持有的包装 Observable 被回收不影响注册表里的共享 Subject。

## 线程与回调约定

- `Subject::onNext` 等带互斥锁；`emit` 在锁外调用回调，避免死锁。
- 脚本闭包只能在主线程执行（同 `thread` 模块约定）；worker 请用 `thread.postMain` 回主
  线程后再 `onNext`。
- 回调同步执行：`onNext` 返回时订阅者已完成处理。

## 文件清单

| 文件 | 内容 |
|---|---|
| `src/modules/rx/Rx.h` | C++ 模板核心（Observer/Subscription/Observable/操作符/Subject 家族/ReactiveProperty） |
| `src/modules/rx/Rx.cpp` | Value 变体、模块注册、Squirrel 绑定、事件桥接 |
| `test/rx.cpp` | 26 个用例（Value / Subject / 操作符 / Behavior/Replay/Property / 脚本 / 事件桥接） |
| `src/modules/CMakeLists.txt` | `create_module(EVRx rx)` |
| `docs/usr/modules/rx.md` | 用户文档 |

## 验证

- `cmake --build build/macosx-debug --target unit_test -j8`
- `./build/macosx-debug/test/unit_test --testcase="rx.*"` → 26 用例 / 65 断言全绿。
- 全量套件无新增失败（仅原有 RenderSystem 图形容差告警）。
