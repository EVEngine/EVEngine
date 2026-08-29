# 响应式编程模块

**脚本入口：** `eve.Rx()`

参考 [UniRx](https://github.com/neuecc/UniRx) 提供基于推送的响应式流：`Subject` 家族、
LINQ 风格操作符、`ReactiveProperty`，以及把现有事件队列桥接为 Observable 流的能力。

## 基本用法

```squirrel
local rx = eve.Rx();
local clicks = rx.newSubject();

// 订阅（返回 Subscription，可取消）
local sub = clicks.subscribe(function(v) {
    print("click at " + v);
});

// 推送
clicks.onNext("(100, 200)");
clicks.onNext("(300, 50)");

// 不再需要时取消订阅
sub.dispose();
```

## 对象关系与调用时机

`Rx` 是工厂模块：`newSubject()` / `newBehaviorSubject(v)` / `newReplaySubject(n)` /
`newProperty(v)` 创建流对象。流对象本身不绑定到帧循环——游戏代码负责在合适的时机
调用 `onNext` / `setValue`，订阅回调在调用线程同步执行。`fromEvent` + `pump` 提供与
`event` 模块的桥接：`pump(ev)` 排空事件队列并把匹配名称的消息推给对应流。

## 目标导向指南

### 解耦生产者与消费者

生产者持有 `Subject` 并调用 `onNext`；消费者持有同一个对象并 `subscribe`。双方无需
互相引用，取消订阅即解除关联。适合按键、点击、资源加载完成等事件广播。

### 用操作符链做数据变换

```squirrel
local numbers = rx.newSubject();
numbers
    .filter(function(v) { return v % 2 == 0; })
    .map(function(v) { return v * 10; })
    .subscribe(function(v) { print("result " + v); });
numbers.onNext(1);  // 不输出
numbers.onNext(2);  // result 20
```

### 观察可变状态

`ReactiveProperty` 既是读取入口也是可订阅流：状态变化自动通知订阅者，适合 UI 绑定、
模型字段监听等场景。

```squirrel
local hp = rx.newProperty(100);
hp.subscribe(function(v) { print("hp -> " + v); });
hp.set(80);  // 打印 hp -> 80
print(hp.get());  // 80
```

### 桥接事件队列

```squirrel
local ev = eve.PlatformEvent();
local rx = eve.Rx();
local quest = rx.fromEvent("quest-complete");
quest.subscribe(function(data) { print("quest done: " + data); });

// 帧循环或任何时机排空事件队列，匹配的消息进入对应流
rx.pump(ev);
```

## 常见问题

- 忘记 `dispose()`：订阅者会一直留在流里接收通知。持订阅对象并在不再需要时 dispose。
- 在 worker 线程推送：回调在 `onNext` 的调用线程同步执行；脚本闭包只能在主线程被调用，
  worker 请通过 `thread.postMain` 回主线程后再推送。
- 把 `ReactiveProperty` 当普通变量：写 `set()` 会触发通知，若只是临时状态请用局部变量。
- 混淆 `subscribe` 与 `subscribe3`：前者只有 onNext 回调；后者是
  `subscribe3(onNext, onError, onCompleted)`，全部必填（不需要就传 `null`）。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象的方法也列在这里。

**模块 `Rx`：**

- `newSubject()` → `Subject`
- `newBehaviorSubject(v)` → `BehaviorSubject`
- `newReplaySubject(capacity)` → `ReplaySubject`
- `newProperty(v)` → `ReactiveProperty`
- `fromEvent(name)` → `Observable`
- `pump(event)`：排空事件队列，把匹配 `fromEvent` 的消息推给对应流

**`Observable` / `Subject` / `BehaviorSubject` / `ReplaySubject`：**

- `subscribe(onNext)` → `Subscription`
- `subscribe3(onNext, onError, onCompleted)` → `Subscription`
- `map(fn)` → `Observable`
- `filter(pred)` → `Observable`
- `take(n)` → `Observable`
- `skip(n)` → `Observable`
- `first()` → `Observable`
- `distinctUntilChanged()` → `Observable`
- `Subject` 额外：`onNext(v)` / `onError(msg)` / `onCompleted()` / `hasObservers()`
- `BehaviorSubject` 额外：`getValue()` / `setValue(v)`
- `ReactiveProperty`：`get()` / `set(v)` / `subscribe` / `subscribe3`

**`Subscription`：**

- `dispose()` / `isDisposed()`

## 使用要点

- 流对象和订阅对象应保存在全局或实体状态中，不要在每帧重复创建。
- 默认回调在当前线程同步执行；跨线程需求先回到主线程再推送。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/rx/`](../../../src/modules/rx/)
**相关测试：** [`test/rx.cpp`](../../../test/rx.cpp)