# 动画模块

**脚本入口：** `eve.Animation()`

支持三类能力：

1. **Tween**：标量/角度属性补间（delay、repeat、yoyo、缓动）
2. **3D 骨骼动画播放**：`AnimSkeleton` + `AnimClip`，可用 `AnimPlayer`、状态机 `AnimStateMachine`、或 Motion Matching（`MotionDatabase` + `MotionMatcher`）驱动
3. **CPU 蒙皮**：`AnimSkin` 从 `ModelData` 读取骨骼权重与 inverse-bind，按 `AnimPose` 世界矩阵做线性混合蒙皮
4. **控制论程序动画**：`ControlAnim`（命名标量通道）与 `ControlPose`（骨骼姿态跟踪），基于二阶 LTI / 闭式阻尼弹簧 / 单位质量 PD

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
db.setRootBoneByName("mixamorig:Hips"); // Mixamo 等角色常用髋骨做轨迹根
db.addFeatureBoneByName("mixamorig:LeftFoot");
db.addFeatureBoneByName("mixamorig:RightFoot");
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

## 基本用法（控制论程序动画）

二阶动力学把目标当成输入 `x`，输出 `y` 满足：

\[
\ddot y + k_1 \dot y + k_2 y = x + k_3 \dot x
\]

其中 \(k_1=\zeta/(\pi f)\)，\(k_2=1/(2\pi f)^2\)，\(k_3=r\zeta/(2\pi f)\)（t3ssel8r 参数化）。也可用闭式阻尼弹簧（Ryan Juckett）或单位质量 PD：\(\tau=K_p(x-y)+K_d(\dot x-\dot y)\)，\(K_p=\omega^2\)，\(K_d=2\zeta\omega\)。

```squirrel
local anim = eve.Animation();
local ca = anim.newControlAnim(3.0, 0.5, -1.0); // f, ζ, r
ca.setIntegrator("secondOrder"); // 或 "spring" / "pd"
ca.set("arm", 0);
ca.setTarget("arm", 1.2);
ca.update(dt);
local y = ca.get("arm");

local sk = anim.newSkeleton();
// ... addBone ...
local cp = anim.newControlPose(sk);
cp.setFrequency(4.0);
cp.setDamping(0.8);
cp.setResponse(1.0);
local target = anim.newPose(sk.getBoneCount());
sk.applyBindPose(target);
target.setLocalPosition(1, 0, 2, 0);
cp.setTargetPose(target);
cp.update(dt);
local pose = cp.getPose();
```

## 从 Mixamo / FBX 导入

```squirrel
// Assimp 路径（Model3D 解码后）：
local model = model3d.newModelDataFromFile("Idle.fbx");
local sk = anim.newSkeletonFromModel(model);
local idle = anim.newClipFromModel(model, sk, 0);

// 或加载测试用紧凑 `.eva`（无网格关键帧，见 test/assets/mixamo/）：
local sk2 = anim.newSkeletonFromEvaFile("test/assets/mixamo/Idle.eva");
local idle2 = anim.newClipFromEvaFile("test/assets/mixamo/Idle.eva");
// Mixamo 原地跑可补平面根运动：
run.applyPlanarRootMotion(sk.findBone("mixamorig:Hips"), 0, 300);
```

## CPU 蒙皮（glTF / FBX 蒙皮网格）

```squirrel
local model = model3d.newModelDataFromFile("CesiumMan.gltf");
local sk = anim.newSkeletonFromModel(model);
local clip = anim.newClipFromModel(model, sk, 0);
local skin = anim.newSkinFromModel(model, 0, sk); // meshIndex 需 hasBones
local player = anim.newPlayer(sk);
player.play(clip);
player.update(dt);
local pose = player.getPose();
pose.computeWorld(sk);
skin.updateSkinnedPositions(pose); // 脚本可读 getSkinnedPositionX/Y/Z(i)
// 也可交给粒子：emitter.setSkinSource(skin, pose) 从皮肤表面发射
```

测试资源：`scripts/download_skinned_character.sh` 下载 Khronos **CesiumMan**（约 0.5 MB）到 `test/assets/skinned/`；CMake 选项 `EVENGINE_DOWNLOAD_SKINNED_CHARACTER`（默认 ON）会在构建 `unit_test` 时联网拉取。

## 对象关系与调用时机

- `Animation` 拥有 Tween 注册表并统一 `update`；3D 对象由脚本持有，各自 `update(dt)`。
- `AnimSkeleton` 定义骨骼层级与 bind pose；`AnimClip` 保存各骨 local TRS 关键帧。
- `AnimPlayer` / `AnimStateMachine` / `MotionMatcher` / `ControlPose` 每帧写出 `AnimPose`；`AnimSkin` 用世界矩阵 + inverse-bind 做 CPU 蒙皮；渲染侧也可读取 local/world 同步调试骨骼。
- Motion Matching：先 `MotionDatabase.bake()`，再周期性搜索 + 交叉淡入。
- `ControlAnim` / `ControlPose`：每帧更新目标后调用各自的 `update(dt)`；积分器字符串为 `secondOrder` | `spring` | `pd`。

## 目标导向指南

### 做 UI 滑入动画

创建 Tween，给 `x` 设置 from/to，选择 `outQuad`，调用 `start()`；每帧 `anim.update(dt)` 后读取 `tween.get("x")` 更新 UI 位置。

### 做往返呼吸效果

设置 duration、repeat 和 `setYoyo(true)`；颜色或缩放用多个命名属性并行插值。角度必须使用 `setFromAngle` / `setToAngle`，避免跨 360° 绕远路。

### 用状态机切换 Idle/Walk

`addState` 绑定 clip，`addTransition` + `addFloatCondition`/`addTriggerCondition`；每帧写参数并 `sm.update(dt)`，从 `getPose()` 取姿态。

### 用 Motion Matching 跟手移动

把行走/奔跑等 clip 加入 `MotionDatabase` 并 `bake`；每帧设置 `setDesiredVelocity` / `setDesiredYaw`，调用 `mm.update(dt)`。

### 给武器或肢体加惯性/跟手感

用 `newControlAnim` 或 `newControlPose`，调低 `ζ`（欠阻尼）并设负的 `r` 可做预期回摆；调高 `f` 让跟踪更快。目标每帧变化时用 `setTarget` / `setTargetPose`，不要每帧 `set`（`set` 会清速度）。

## 常见问题

- 创建 Tween 后忘记 `start()`。
- update 后不读取 `get(property)` 写回对象。
- 普通标量接口插值角度导致跨 360° 绕行。
- MotionMatcher 在 `bake()` 之前调用 `search`/`update`。
- 状态机/播放器持有的 skeleton、clip 被提前销毁。
- `ControlAnim.setIntegrator` / `ControlPose.setIntegrator` 传入未知字符串。
- 把 `ControlAnim.set` 当每帧追目标用（会清零速度，失去动力学感）。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象的方法也列在这里。

- Tween：`clearAll()`、`clearFinished()`、`evaluate()`、`get()`、`getActiveCount()`、`getDelay()`、`getDelta()`、`getDuration()`、`getEase()`、`getEasedProgress()`、`getElapsed()`、`getFrom()`、`getName()`、`getProgress()`、`getPropertyCount()`、`getPropertyName()`、`getRepeat()`、`getTo()`、`getTweenCount()`、`getYoyo()`、`has()`、`isActive()`、`isDelayed()`、`isFinished()`、`isPaused()`、`isRunning()`、`isStopped()`、`newTween()`、`pause()`、`reset()`、`resume()`、`setDelay()`、`setDelta()`、`setDeltaAngle()`、`setDuration()`、`setEase()`、`setFrom()`、`setFromAngle()`、`setRepeat()`、`setTo()`、`setToAngle()`、`setYoyo()`、`start()`、`stop()`、`update()`
- 3D 工厂：`newSkeleton()`、`newClip()`、`newPose()`、`newPlayer()`、`newStateMachine()`、`newMotionDatabase()`、`newMotionMatcher()`、`newControlAnim()`、`newControlPose()`、`newSkinFromModel()`
- `AnimSkeleton`：`addBone()`、`getBoneCount()`、`getBoneName()`、`findBone()`、`getParent()`、`setBindPosition()`、`setBindRotation()`、`setBindScale()`、`getBind*()`、`applyBindPose()`
- `AnimClip`：`setName()`、`getName()`、`setDuration()`、`getDuration()`、`setLoop()`、`getLoop()`、`setSampleRate()`、`addPositionKey()`、`addRotationKey()`、`addScaleKey()`、`sample()`、`wrapTime()`
- `AnimPose`：`resize()`、`copyFrom()`、`blendFrom()`、`setLocal*()`、`getLocal*()`、`computeWorld()`、`getWorld*()`、`getWorldMatrixElement()`
- `AnimSkin`：`getVertexCount()`、`getBoneCount()`、`getSkeletonBone()`、`getSkinBoneName()`、`getInverseBindElement()`、`getBindPosition*()`、`getVertexBone()`、`getVertexWeight()`、`updateSkinnedPositions()`、`hasSkinnedPositions()`、`getSkinnedPosition*()`
- `AnimPlayer`：`play()`、`crossFade()`、`stop()`、`pause()`、`resume()`、`setSpeed()`、`setTime()`、`setLoop()`、`getPose()`、`update()`
- `AnimStateMachine`：`addState()`、`setEntry()`、`addTransition()`、`addFloatCondition()`、`addBoolCondition()`、`addTriggerCondition()`、`setExitTime()`、`setFloat()`、`setBool()`、`setTrigger()`、`getPose()`、`update()`
- `MotionDatabase`：`addFeatureBone()`、`addFeatureBoneByName()`、`addClip()`、`bake()`、`getFrameCount()`、`getFeatureSize()`
- `MotionMatcher`：`setDesiredVelocity()`、`setDesiredYaw()`、`setSearchInterval()`、`setBlendTime()`、`search()`、`update()`、`getPose()`、`getMatchedClipIndex()`
- `ControlAnim`：`setFrequency()`、`getFrequency()`、`setDamping()`、`getDamping()`、`setResponse()`、`getResponse()`、`setIntegrator()`、`getIntegrator()`、`set()`、`setTarget()`、`setTargetVelocity()`、`impulse()`、`has()`、`get()`、`getVelocity()`、`getTarget()`、`clear()`、`remove()`、`getPropertyCount()`、`getPropertyName()`、`update()`
- `ControlPose`：`setFrequency()`、`getFrequency()`、`setDamping()`、`getDamping()`、`setResponse()`、`getResponse()`、`setIntegrator()`、`getIntegrator()`、`setBoneWeight()`、`getBoneWeight()`、`setTargetPose()`、`snapToTarget()`、`getPose()`、`getTargetPose()`、`update()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/animation/`](../../../src/modules/animation/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `animation`。
