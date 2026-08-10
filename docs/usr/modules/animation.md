# 动画模块

**脚本入口：** `eve.Animation()`

支持两类能力：

1. **Tween**：标量/角度属性补间（delay、repeat、yoyo、缓动）
2. **3D 骨骼动画播放**：`AnimSkeleton` + `AnimClip`，可用 `AnimPlayer`、状态机 `AnimStateMachine`、或 Motion Matching（`MotionDatabase` + `MotionMatcher`）驱动

## 基本用法（Tween）

```squirrel
local anim = eve.Animation();
local move = anim.newTween(0.6);
move.setFrom("x", 0);
move.setTo("x", 200);
move.setEase("outQuad");
move.start();
anim.update(dt);
```

## 基本用法（3D 状态机）

```squirrel
local anim = eve.Animation();
local sk = anim.newSkeleton();
local root = sk.addBone("root", -1);
local hip = sk.addBone("hip", root);
sk.setBindPosition(hip, 0, 1, 0);

local idle = anim.newClip("idle");
// idle.addPositionKey / addRotationKey ...
local walk = anim.newClip("walk");

local sm = anim.newStateMachine(sk);
sm.addState("Idle", idle);
sm.addState("Walk", walk);
sm.setEntry("Idle");
local t = sm.addTransition("Idle", "Walk", 0.15);
sm.addFloatCondition(t, "speed", ">", 0.5);
sm.setFloat("speed", 1.0);
sm.update(dt);
local pose = sm.getPose();
```

## 基本用法（Motion Matching）

```squirrel
local db = anim.newMotionDatabase(sk);
db.addFeatureBoneByName("hip");
db.addClip(walk);
db.addClip(run);
db.bake();

local mm = anim.newMotionMatcher(sk, db);
mm.setDesiredVelocity(0, 3);
mm.setDesiredYaw(0);
mm.setSearchInterval(0.1);
mm.update(dt);
local pose = mm.getPose();
```

## 对象关系与调用时机

- `Animation` 拥有 Tween 注册表并统一 `update`；3D 对象由脚本持有，各自 `update(dt)`。
- `AnimSkeleton` 定义骨骼层级与 bind pose；`AnimClip` 保存各骨 local TRS 关键帧。
- `AnimPlayer` / `AnimStateMachine` / `MotionMatcher` 每帧写出 `AnimPose`；渲染侧读取 local/world 变换同步网格或调试骨骼。
- Motion Matching：先 `MotionDatabase.bake()`，再周期性搜索 + 交叉淡入。

## 目标导向指南

### 做 UI 滑入动画

创建 Tween，给 `x` 设置 from/to，选择 `outQuad`，调用 `start()`；每帧 `anim.update(dt)` 后读取 `tween.get("x")` 更新 UI 位置。

### 做往返呼吸效果

设置 duration、repeat 和 `setYoyo(true)`；颜色或缩放用多个命名属性并行插值。角度必须使用 `setFromAngle` / `setToAngle`，避免跨 360° 绕远路。

### 用状态机切换 Idle/Walk

`addState` 绑定 clip，`addTransition` + `addFloatCondition`/`addTriggerCondition`；每帧写参数并 `sm.update(dt)`，从 `getPose()` 取姿态。

### 用 Motion Matching 跟手移动

把行走/奔跑等 clip 加入 `MotionDatabase` 并 `bake`；每帧设置 `setDesiredVelocity` / `setDesiredYaw`，调用 `mm.update(dt)`。

## 常见问题

- 创建 Tween 后忘记 `start()`。
- update 后不读取 `get(property)` 写回对象。
- 普通标量接口插值角度导致跨 360° 绕行。
- MotionMatcher 在 `bake()` 之前调用 `search`/`update`。
- 状态机/播放器持有的 skeleton、clip 被提前销毁。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象的方法也列在这里。

- Tween：`clearAll()`、`clearFinished()`、`evaluate()`、`get()`、`getActiveCount()`、`getDelay()`、`getDelta()`、`getDuration()`、`getEase()`、`getEasedProgress()`、`getElapsed()`、`getFrom()`、`getName()`、`getProgress()`、`getPropertyCount()`、`getPropertyName()`、`getRepeat()`、`getTo()`、`getTweenCount()`、`getYoyo()`、`has()`、`isActive()`、`isDelayed()`、`isFinished()`、`isPaused()`、`isRunning()`、`isStopped()`、`newTween()`、`pause()`、`reset()`、`resume()`、`setDelay()`、`setDelta()`、`setDeltaAngle()`、`setDuration()`、`setEase()`、`setFrom()`、`setFromAngle()`、`setRepeat()`、`setTo()`、`setToAngle()`、`setYoyo()`、`start()`、`stop()`、`update()`
- 3D 工厂：`newSkeleton()`、`newClip()`、`newPose()`、`newPlayer()`、`newStateMachine()`、`newMotionDatabase()`、`newMotionMatcher()`
- `AnimSkeleton`：`addBone()`、`getBoneCount()`、`getBoneName()`、`findBone()`、`getParent()`、`setBindPosition()`、`setBindRotation()`、`setBindScale()`、`getBind*()`、`applyBindPose()`
- `AnimClip`：`setName()`、`getName()`、`setDuration()`、`getDuration()`、`setLoop()`、`getLoop()`、`setSampleRate()`、`addPositionKey()`、`addRotationKey()`、`addScaleKey()`、`sample()`、`wrapTime()`
- `AnimPose`：`resize()`、`copyFrom()`、`blendFrom()`、`setLocal*()`、`getLocal*()`、`computeWorld()`、`getWorld*()`
- `AnimPlayer`：`play()`、`crossFade()`、`stop()`、`pause()`、`resume()`、`setSpeed()`、`setTime()`、`setLoop()`、`getPose()`、`update()`
- `AnimStateMachine`：`addState()`、`setEntry()`、`addTransition()`、`addFloatCondition()`、`addBoolCondition()`、`addTriggerCondition()`、`setExitTime()`、`setFloat()`、`setBool()`、`setTrigger()`、`getPose()`、`update()`
- `MotionDatabase`：`addFeatureBone()`、`addFeatureBoneByName()`、`addClip()`、`bake()`、`getFrameCount()`、`getFeatureSize()`
- `MotionMatcher`：`setDesiredVelocity()`、`setDesiredYaw()`、`setSearchInterval()`、`setBlendTime()`、`search()`、`update()`、`getPose()`、`getMatchedClipIndex()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/animation/`](../../../src/modules/animation/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `animation`。
