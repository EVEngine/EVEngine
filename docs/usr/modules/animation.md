# 动画模块

**脚本入口：** `eve.Animation()`

支持七类能力：

1. **Tween**：标量/角度属性补间（delay、repeat、yoyo、缓动）
2. **2D 帧动画**：`SpriteSheet` + `SpriteClip` + `SpriteAnim`（sprite sheet / 图集格子）
3. **Spine（region 子集）**：`.atlas` + skeleton JSON → `SpineAnim.collectDrawItems` 进 2D 队列
4. **3D 骨骼动画播放与动画图**：`AnimSkeleton` + `AnimClip`，可用 `AnimPlayer`、`AnimGraph`、状态机 `AnimStateMachine`、或 Motion Matching（`MotionDatabase` + `MotionMatcher`）驱动
5. **CPU 蒙皮**：`AnimSkin` 从 `ModelData` 读取骨骼权重与 inverse-bind，按 `AnimPose` 世界矩阵做线性混合蒙皮
6. **控制论程序动画**：`ControlAnim`（命名标量通道）与 `ControlPose`（骨骼姿态跟踪），基于二阶 LTI / 闭式阻尼弹簧 / 单位质量 PD
7. **拖尾轨迹**：`AnimTrail` 记录采样点并绘制淡出轨迹（2D 点或骨骼世界坐标投影）

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

## 基本用法（2D 帧动画）

```squirrel
local anim = eve.Animation();
local gfx = eve.Graphics();
local sheet = anim.newSpriteSheet();
sheet.setGrid(4, 2, 32, 48, 0, 0, 0, 0); // cols, rows, frameW, frameH, margin, spacing, ox, oy

local walk = anim.newSpriteClip("walk");
walk.setLoop(true);
walk.addFrame(0, 0.1);
walk.addFrame(1, 0.1);
walk.addFrame(2, 0.1);
walk.addFrame(3, 0.1);

local quad = gfx.newQuad(0, 0, 32, 48);
local player = anim.newSpriteAnim();
player.setSheet(sheet);
player.bindQuad(quad); // 每帧自动 setViewport
player.play(walk);
// eve_update:
anim.update(dt);
// 把 quad 挂到 Renderable2D.sprite.quad 即可换帧
```

独立 PNG 序列可在运行时自动合并为共享图集，不需要先用外部工具合图；
每帧会自动扩展 1 像素边缘，避免线性过滤、缩放和旋转时采样到相邻帧：

```squirrel
local sheet = anim.newSpriteSheetFromSequence(
    gfx, "assets/frame_{n}.png", 1, 64, 8);
local burst = anim.newSpriteClip("burst");
burst.addRange(0, 63, 24.0); // inclusive range, 24 FPS

local quad = gfx.newQuad(0, 0, 128, 128);
local player = anim.newSpriteAnim();
player.setSheet(sheet);
player.bindQuad(quad);
player.play(burst);          // player.playReverse(burst) 可倒放
player.setSpeed(0.5);        // 支持负数；0 冻结时间
```

`consumeLooped()` / `consumeCompleted()` 用于每帧消费一次性事件，
`getLoopCount()` 返回本次播放以来累计跨过的循环边界数。

播放速度还可以由关键点曲线控制。曲线值与 `setSpeed()` 的基础倍率相乘，
曲线时间独立推进，因此首个关键点为 0 也不会把播放永久卡住：

```squirrel
player.setSpeed(1.0);
player.addSpeedCurveKey(0.0, 0.2);
player.addSpeedCurveKey(1.2, 2.4);
player.addSpeedCurveKey(2.6, 0.35);
player.addSpeedCurveKey(4.0, 0.2);
player.setSpeedCurveLoop(true);
// clearSpeedCurve / resetSpeedCurve / getSpeedCurveValue
```

关键点之间采用线性插值；如需平滑 S 曲线，可用更多采样关键点逼近。
也可调用 `setSpeedCurveInterpolation("linear"|"smooth"|"cubic")` 选择插值。

序列加载器会按 alpha 自动裁掉透明边缘，保留每帧原始尺寸和偏移；
`player.bindSprite(sprite)` 会同步这些布局信息，避免裁边动画抖动。相同加载参数会复用缓存图集，
可用 `getSpriteSequenceCacheCount/Bytes` 查看数量与估算显存。

Aseprite 与 TexturePacker 的 JSON Hash 格式可通过
`newSpriteSheetFromAtlasJson(gfx, texturePath, jsonPath)` 导入；当前明确拒绝 rotated frame。

## 基本用法（Spine region）

内置解析器支持 Spine `.atlas` + skeleton JSON 的 **region 附件**子集（骨骼 TRS、slot 附件切换）。Mesh / IK / path / deform 需自行接入官方 `spine-cpp` 插件。

```squirrel
local anim = eve.Animation();
local atlas = anim.newSpineAtlasFromFile("hero.atlas");
local data = anim.newSpineSkeletonDataFromFile("hero.json");
local sk = anim.newSpineSkeleton(data);
local spine = anim.newSpineAnim(sk);
spine.setAtlas(atlas);
spine.setPageTextureByName("hero.png", tex); // Graphics 纹理
spine.setPosition(400, 300);
spine.setFlipY(true); // 默认 true：Spine Y-up → 屏幕 Y-down
spine.play("idle");
// eve_update:
anim.update(dt);
// C++ / 自定义系统：spine.collectDrawItems(queue)
// 脚本可读：spine.getDrawSlotCount / getDrawSlotX/Y/Width/Height
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

## 可组合 3D Animation Graph

`AnimGraph` 用稳定整数句柄连接节点，支持共享子图单帧缓存、普通混合、
additive、逐骨骼 mask 分层、one-shot，以及 1D/2D blend space。现有
`AnimPlayer` 和 `AnimStateMachine` 保持兼容，适合简单控制器；复杂角色建议使用图。

```squirrel
local graph = anim.newGraph(sk);
local idleNode = graph.addClip(idle);
local walkNode = graph.addClip(walk);
local runNode = graph.addClip(run);

local locomotion = graph.addBlendSpace1D();
graph.addBlendSpace1DPoint(locomotion, 0.0, idleNode);
graph.addBlendSpace1DPoint(locomotion, 2.0, walkNode);
graph.addBlendSpace1DPoint(locomotion, 6.0, runNode);
graph.setPosition1D(locomotion, speed);

local fireNode = graph.addClip(fire);
local fireLayer = graph.addOneShot(locomotion, fireNode, 0.08, 0.12);
graph.clearBoneMask(fireLayer);
graph.setBoneMask(fireLayer, sk.findBone("Spine"), 1.0, true);
graph.setRoot(fireLayer);
graph.trigger(fireLayer);

// eve_update:
graph.update(dt);
local pose = graph.getPose();
```

`addLayer(base, overlay, weight)` 默认 mask 全为 0，须显式设置参与骨骼；
`addAdditive(base, delta, weight)` 默认作用于全身。Additive clip 应以 identity
姿态为参考：位移为差值、旋转为差值四元数、缩放以 1 为基准。
one-shot 的 shot 输入当前应是 clip 节点，用该 clip 的时长决定结束和淡出。

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

## 基本用法（拖尾轨迹）

```squirrel
local anim = eve.Animation();
local trail = anim.newTrail(64);
trail.setDuration(0.45);
trail.setWidth(4);
trail.setColor(1, 0.85, 0.35, 1);
trail.setFade(true);
trail.setStyle("line"); // 或 "points"
trail.setMinDistance(2);

// 每帧：写入采样 → update；渲染时 draw
trail.addPoint(x, y);
// 或从骨骼世界坐标投影（需先 pose.computeWorld(sk)）：
// trail.sampleBone(pose, tipBone, "xy"); // plane: xy|xz|yz
// trail.setDrawScale(40, -40); // 世界单位 → 像素
// trail.setDrawOffset(400, 300);
trail.update(dt);
// eve_render:
trail.draw(gfx);
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
// 推荐：蒙皮（位置+法线）并原地写回渲染网格，每帧一行：
skin.applyToMesh(gfx, mesh, pose); // mesh 与 skin 需同源（同一 ModelData/meshIndex）
// 也可拆开用底层原语（与 AnimLattice 一致）：
// skin.updateSkinnedPositions(pose);       // 脚本可读 getSkinnedPositionX/Y/Z(i)
// skin.updateSkinnedNormals(pose);         // 用 fromModel 捕获的 bind 法线
// gfx.updateMeshVertices(mesh, skin.getSkinnedPositions(), skin.getSkinnedNormals(),
//                        [], skin.getVertexCount(), [], 0);
// 也可交给粒子：emitter.setSkinSource(skin, pose) 从皮肤表面发射
```

## 3D Root Motion 与 Notify

`AnimClip.addEvent(time, name)` 在 3D clip 时间轴添加 gameplay notify；
`AnimPlayer.consumeEvent()` 按跨过时间顺序逐个消费，循环边界不会丢事件。
Player 还会从指定根骨骼提取本帧位移和旋转 delta，可交给角色控制器：

```squirrel
clip.addEvent(0.18, "footstep.left");
player.setRootMotionBone(sk.findBone("Hips"));
player.update(dt);
controller.move(player.getRootMotionX(), player.getRootMotionY(),
                player.getRootMotionZ());
local eventName = player.consumeEvent();
while (eventName != "") {
    // dispatch gameplay/audio/VFX event
    eventName = player.consumeEvent();
}
```

Root-motion 位移会补偿 loop 末尾到开头的跳变；旋转返回单位四元数
`getRootMotionRotationX/Y/Z/W()`。调用 `setTime()` 是 seek，不会生成 motion delta
或 notify，下一次 `update()` 从 seek 后时间继续计算。

测试资源：`scripts/download_skinned_character.sh` 下载 Khronos **CesiumMan**（约 0.5 MB）到 `test/assets/skinned/`；CMake 选项 `EVENGINE_DOWNLOAD_SKINNED_CHARACTER`（默认 ON）会在构建 `unit_test` 时联网拉取。

`model3d.createRenderable(gfx, model, meshIndex)` 建的网格可用
`ent.getMesh()` 取回句柄交给 `applyToMesh`。蒙皮网格必须与 `AnimSkin` 同源，
且上传时不要烘焙节点世界变换，否则回写的模型空间顶点会与网格对不上。

## 对象关系与调用时机

- `Animation` 拥有 Tween / SpriteAnim / SpineAnim 注册表并统一 `update`；3D 对象与 `AnimTrail` 由脚本持有，各自 `update(dt)`。
- `SpriteSheet` 定义图集格子；`SpriteClip` 引用格子索引；`SpriteAnim` 推进时间并可 `bindQuad`。
- `SpineAtlas` + `SpineSkeletonData` 为资源；`SpineSkeleton` 为运行时姿态；`SpineAnim` 采样动画并 `collectDrawItems`。
- `AnimSkeleton` 定义骨骼层级与 bind pose；`AnimClip` 保存各骨 local TRS 关键帧。
- `AnimPlayer` / `AnimGraph` / `AnimStateMachine` / `MotionMatcher` / `ControlPose` 每帧写出 `AnimPose`；`AnimSkin` 用世界矩阵 + inverse-bind 做 CPU 蒙皮；渲染侧也可读取 local/world 同步调试骨骼。
- `AnimTrail`：每帧 `addPoint` / `sampleBone` 后 `update(dt)`，在 `eve_render` 调用 `draw(gfx)`。
- Motion Matching：先 `MotionDatabase.bake()`，再周期性搜索 + 交叉淡入。
- Motion Database 在 bake 时按通道计算均值/标准差并标准化；搜索使用当前最优代价提前终止候选计算，避免量纲较大的通道意外支配结果。
- `ControlAnim` / `ControlPose`：每帧更新目标后调用各自的 `update(dt)`；积分器字符串为 `secondOrder` | `spring` | `pd`。

## 目标导向指南

### 做 UI 滑入动画

创建 Tween，给 `x` 设置 from/to，选择 `outQuad`，调用 `start()`；每帧 `anim.update(dt)` 后读取 `tween.get("x")` 更新 UI 位置。

### 做 2D 角色走路帧动画

`newSpriteSheet` + `setGrid`（或 `addFrame`），`newSpriteClip` 按序 `addFrame`，`newSpriteAnim` 绑定 `Quad` 后 `play`；把该 Quad 赋给 `Renderable2D.sprite.quad`。

### 播放 Spine 角色（region）

加载 `.atlas` / `.json`，`newSpineAnim`，绑定 page 纹理，每帧 `anim.update(dt)`；渲染侧调用 `collectDrawItems` 写入与地图/精灵同一 2D 队列。

### 做往返呼吸效果

设置 duration、repeat 和 `setYoyo(true)`；颜色或缩放用多个命名属性并行插值。角度必须使用 `setFromAngle` / `setToAngle`，避免跨 360° 绕远路。

### 用状态机切换 Idle/Walk

`addState` 绑定 clip，`addTransition` + `addFloatCondition`/`addTriggerCondition`；每帧写参数并 `sm.update(dt)`，从 `getPose()` 取姿态。

### 用 Motion Matching 跟手移动

把行走/奔跑等 clip 加入 `MotionDatabase` 并 `bake`；每帧设置 `setDesiredVelocity` / `setDesiredYaw`，调用 `mm.update(dt)`。

### 给武器或肢体加惯性/跟手感

用 `newControlAnim` 或 `newControlPose`，调低 `ζ`（欠阻尼）并设负的 `r` 可做预期回摆；调高 `f` 让跟踪更快。目标每帧变化时用 `setTarget` / `setTargetPose`，不要每帧 `set`（`set` 会清速度）。

### 给武器挥砍或移动目标加拖尾

`newTrail(capacity)`，设 `setDuration` / `setWidth` / `setColor`；每帧在目标位置 `addPoint`（2D）或 `sampleBone(pose, bone, "xy")`（骨骼），再 `update(dt)`；在 `eve_render` 调用 `draw(gfx)`。世界坐标可用 `setDrawScale` / `setDrawOffset` 映射到屏幕像素。

## 常见问题

- 创建 Tween 后忘记 `start()`。
- update 后不读取 `get(property)` 写回对象。
- 普通标量接口插值角度导致跨 360° 绕行。
- `SpriteAnim` 未 `setSheet` / `bindQuad` 就期望自动换帧。
- Spine 未 `setPageTexture*` 导致 `collectDrawItems` 无贴图。
- 期望内置 Spine 解析 mesh/IK（当前仅 region；全量请用 spine-cpp 插件）。
- MotionMatcher 在 `bake()` 之前调用 `search`/`update`。
- 状态机/播放器持有的 skeleton、clip 被提前销毁。
- `ControlAnim.setIntegrator` / `ControlPose.setIntegrator` 传入未知字符串。
- 把 `ControlAnim.set` 当每帧追目标用（会清零速度，失去动力学感）。
- `AnimTrail.sampleBone` 前忘记 `pose.computeWorld(sk)`；或 `setStyle` / plane 字符串拼错（仅 `line|points` 与 `xy|xz|yz`）。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象的方法也列在这里。

- Tween：`clearAll()`、`clearFinished()`、`evaluate()`、`get()`、`getActiveCount()`、`getDelay()`、`getDelta()`、`getDuration()`、`getEase()`、`getEasedProgress()`、`getElapsed()`、`getFrom()`、`getName()`、`getProgress()`、`getPropertyCount()`、`getPropertyName()`、`getRepeat()`、`getTo()`、`getTweenCount()`、`getYoyo()`、`has()`、`isActive()`、`isDelayed()`、`isFinished()`、`isPaused()`、`isRunning()`、`isStopped()`、`newTween()`、`pause()`、`reset()`、`resume()`、`setDelay()`、`setDelta()`、`setDeltaAngle()`、`setDuration()`、`setEase()`、`setFrom()`、`setFromAngle()`、`setRepeat()`、`setTo()`、`setToAngle()`、`setYoyo()`、`start()`、`stop()`、`update()`
- 2D 帧动画：`newSpriteSheet()`、`newSpriteSheetFromSequence()`、`newSpriteSheetFromAtlasJson()`、`newSpriteClip()`、`newSpriteAnim()`、`getSpriteAnimCount()`、`getSpriteSequenceCacheCount()`、`getSpriteSequenceCacheBytes()`、`clearSpriteSequenceCache()`
- Spine：`newSpineAtlas()`、`newSpineAtlasFromFile()`、`newSpineAtlasFromText()`、`newSpineSkeletonData()`、`newSpineSkeletonDataFromFile()`、`newSpineSkeletonDataFromJson()`、`newSpineSkeleton()`、`newSpineAnim()`、`getSpineAnimCount()`
- `SpriteSheet`：`addFrame()`、`setGrid()`、`clear()`、`setTexture()`、`getTexture()`、`getFrameCount()`、`findFrame()`、`getFrameName()`、`getFrameX()`、`getFrameY()`、`getFrameWidth()`、`getFrameHeight()`、`getFrameSourceWidth()`、`getFrameSourceHeight()`、`getFrameOffsetX()`、`getFrameOffsetY()`、`applyToQuad()`
- `SpriteClip`：`setName()`、`getName()`、`setLoop()`、`addFrame()`、`addFrameByName()`、`addRange()`、`setFPS()`、`getFPS()`、`addEvent()`、`getEvent()`、`getDuration()`、`frameAtTime()`
- `SpriteAnim`：`setSheet()`、`play()`、`playReverse()`、`playOnce()`、`queue()`、`stop()`、`pause()`、`resume()`、`setSpeed()`、`addSpeedCurveKey()`、`clearSpeedCurve()`、`resetSpeedCurve()`、`setSpeedCurveLoop()`、`setSpeedCurveInterpolation()`、`getSpeedCurveValue()`、`setTime()`、`setFrame()`、`step()`、`setLoop()`、`bindQuad()`、`bindSprite()`、`applyToQuad()`、`getSheetFrame()`、`getLoopCount()`、`consumeLooped()`、`consumeCompleted()`、`consumeEvent()`、`update()`
- `SpineAtlas`：`loadFromText()`、`loadFromFile()`、`getPage*()`、`findRegion()`、`getRegion*()`
- `SpineSkeletonData`：`loadFromJson()`、`loadFromFile()`、`findBone()`、`findSlot()`、`findAnimation()`、`getAnimationDuration()`
- `SpineSkeleton`：`setSkin()`、`setToSetupPose()`、`updateWorldTransform()`、`getBoneWorld*()`、`getSlotAttachmentName()`
- `SpineAnim`：`setAtlas()`、`setPageTexture()`、`setPageTextureByName()`、`play()`、`setPosition()`、`setScale()`、`setFlipY()`、`apply()`、`update()`、`getDrawSlot*()`
- 3D 工厂：`newSkeleton()`、`newClip()`、`newPose()`、`newPlayer()`、`newGraph()`、`newStateMachine()`、`newMotionDatabase()`、`newMotionMatcher()`、`newControlAnim()`、`newControlPose()`、`newSkinFromModel()`、`newTrail()`
- `AnimSkeleton`：`addBone()`、`getBoneCount()`、`getBoneName()`、`findBone()`、`getParent()`、`setBindPosition()`、`setBindRotation()`、`setBindScale()`、`getBind*()`、`applyBindPose()`
- `AnimClip`：`setName()`、`getName()`、`setDuration()`、`getDuration()`、`setLoop()`、`getLoop()`、`setSampleRate()`、`addPositionKey()`、`addRotationKey()`、`addScaleKey()`、`addEvent()`、`getEventCount()`、`getEventTime()`、`getEventName()`、`sample()`、`wrapTime()`
- `AnimPose`：`resize()`、`copyFrom()`、`blendFrom()`、`setLocal*()`、`getLocal*()`、`computeWorld()`、`getWorld*()`、`getWorldMatrixElement()`
- `AnimSkin`：`getVertexCount()`、`getBoneCount()`、`getSkeletonBone()`、`getSkinBoneName()`、`getInverseBindElement()`、`getBindPosition*()`、`getVertexBone()`、`getVertexWeight()`、`updateSkinnedPositions()`、`hasSkinnedPositions()`、`getSkinnedPosition*()`、`getSkinnedPositions()`、`updateSkinnedNormals()`、`hasSkinnedNormals()`、`getSkinnedNormals()`、`applyToMesh()`
- `AnimPlayer`：`play()`、`crossFade()`、`stop()`、`pause()`、`resume()`、`setSpeed()`、`setTime()`、`setLoop()`、`getPose()`、`setRootMotionBone()`、`getRootMotionBone()`、`getRootMotionX/Y/Z()`、`getRootMotionRotationX/Y/Z/W()`、`consumeEvent()`、`update()`
- `AnimGraph`：`addClip()`、`addBlend()`、`addAdditive()`、`addLayer()`、`addOneShot()`、`addBlendSpace1D()`、`addBlendSpace2D()`、`addBlendSpace1DPoint()`、`addBlendSpace2DPoint()`、`setBoneMask()`、`clearBoneMask()`、`setRoot()`、`getRoot()`、`getNodeCount()`、`setWeight()`、`setPosition1D()`、`setPosition2D()`、`setSpeed()`、`trigger()`、`isOneShotActive()`、`getPose()`、`update()`
- `AnimStateMachine`：`addState()`、`setEntry()`、`addTransition()`、`addFloatCondition()`、`addBoolCondition()`、`addTriggerCondition()`、`setExitTime()`、`setFloat()`、`setBool()`、`setTrigger()`、`getPose()`、`update()`
- `MotionDatabase`：`addFeatureBone()`、`addFeatureBoneByName()`、`addClip()`、`bake()`、`getFrameCount()`、`getFeatureSize()`
- `MotionMatcher`：`setDesiredVelocity()`、`setDesiredYaw()`、`setSearchInterval()`、`setBlendTime()`、`search()`、`update()`、`getPose()`、`getMatchedClipIndex()`
- `ControlAnim`：`setFrequency()`、`getFrequency()`、`setDamping()`、`getDamping()`、`setResponse()`、`getResponse()`、`setIntegrator()`、`getIntegrator()`、`set()`、`setTarget()`、`setTargetVelocity()`、`impulse()`、`has()`、`get()`、`getVelocity()`、`getTarget()`、`clear()`、`remove()`、`getPropertyCount()`、`getPropertyName()`、`update()`
- `ControlPose`：`setFrequency()`、`getFrequency()`、`setDamping()`、`getDamping()`、`setResponse()`、`getResponse()`、`setIntegrator()`、`getIntegrator()`、`setBoneWeight()`、`getBoneWeight()`、`setTargetPose()`、`snapToTarget()`、`getPose()`、`getTargetPose()`、`update()`
- `AnimTrail`：`setCapacity()`、`getCapacity()`、`setDuration()`、`getDuration()`、`setMinDistance()`、`getMinDistance()`、`setWidth()`、`getWidth()`、`setColor()`、`getColor*()`、`setFade()`、`getFade()`、`setStyle()`、`getStyle()`、`setDrawScale()`、`getDrawScale*()`、`setDrawOffset()`、`getDrawOffset*()`、`addPoint()`、`addPoint3()`、`sampleBone()`、`sampleBoneOffset()`、`clear()`、`update()`、`getPointCount()`、`getPoint*()`、`getPointAge()`、`getPointAlpha()`、`draw()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 3D clip 轨道使用二分查找采样，长动画不会随单轨关键帧数线性退化；Motion Database 应离线/加载时 bake，不要逐帧重建。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/animation/`](../../../src/modules/animation/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `animation`。
