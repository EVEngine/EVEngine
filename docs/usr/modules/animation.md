# 动画模块

**脚本入口：** `eve.Animation()`

支持九类能力：

1. **Tween**：标量/角度属性补间（delay、repeat、yoyo、缓动）——兼容保留
2. **Motion（LitMotion 风格）**：typed float/Vec2/Vec3 补间，Builder + Push Bind + Handle
3. **2D 帧动画**：`SpriteSheet` + `SpriteClip` + `SpriteAnim`（sprite sheet / 图集格子）
4. **Spine（region 子集）**：`.atlas` + skeleton JSON → `SpineAnim.collectDrawItems` 进 2D 队列
5. **3D 骨骼动画播放与动画图**：`AnimSkeleton` + `AnimClip`，可用 `AnimPlayer`、`AnimGraph`、状态机 `AnimStateMachine`、或 Motion Matching（`MotionDatabase` + `MotionMatcher`）驱动
6. **CPU 蒙皮**：`AnimSkin` 从 `ModelData` 读取骨骼权重与 inverse-bind，按 `AnimPose` 世界矩阵做线性混合蒙皮
7. **控制论程序动画**：`ControlAnim`（命名标量通道）与 `ControlPose`（骨骼姿态跟踪），基于二阶 LTI / 闭式阻尼弹簧 / 单位质量 PD
8. **拖尾轨迹**：`AnimTrail` 记录采样点并绘制淡出轨迹（2D 点或骨骼世界坐标投影）
9. **程序化骨骼**：`DynamicBoneSolver` 提供弹簧骨、碰撞、风场和距离休眠；`FootIKSolver` 提供地面探测、脚掌对齐、锁足和骨盆补偿

## Motion（LitMotion 风格 Push 补间）

C++ 入口（Phase 1）：`Animation::motion` / `motionVec2` / `motionVec3` 返回 Builder；
`bind(sink)` 时入库播放。Sink 由调用方提供（`FloatPointerSink` / 自定义
`IMotionFloatSink`），跨模块写回不要让 `animation` 直接 include 上层。

```cpp
float x = 0.f;
FloatPointerSink sink(&x);
auto handle = anim->motion(0.f, 200.f, 0.6f)
                  .ease("outQuad")
                  .delay(0.1f)
                  .loops(2, MotionLoopMode::Yoyo)
                  .bind(sink)
                  .expect("spawn");
anim->advance(step);          // 与 Tween 共用 SimulationStep 泵
anim->motions().complete(handle); // 跳到终点并触发 onComplete
anim->motions().cancel(handle);   // 取消并触发 onCancel
```

无 sink 时可 `run()`，再用 `motions().floatValue(handle)` 拉取当前值。

### Sequence（Phase 2）

```cpp
auto seq = anim->sequence();
seq.append(std::move(anim->motion(0.f, 1.f, 0.3f).ease("outQuad").to(sinkX))).expect("append");
seq.appendInterval(0.1f).expect("gap");
seq.join(std::move(anim->motion(0.f, 1.f, 0.3f).ease("linear").to(sinkY))).expect("join");
seq.insert(0.05f, std::move(anim->motion(1.f, 0.f, 0.2f).to(sinkZ))).expect("insert");
auto playback = seq.run().expect("run");
anim->advance(step);
playback.complete().expect("complete");
```

脚本：

```squirrel
local b = anim.newMotion(0, 100, 0.5);
b.ease("outQuad");
b.loops(2, "yoyo");
b.cancelOnError(true);
local h = b.run();
print(h.value());
h.complete();
print(anim.getMotionCount());

local seq = anim.newMotionSequence();
seq.append(anim.newMotion(0, 1, 0.3));
seq.appendInterval(0.1);
seq.join(anim.newMotion(0, 1, 0.3));
print(seq.cursor());
print(seq.itemCount());
local sh = seq.run();
print(sh.childCount());
```

Ease 扩展：`in/out/inOut` + `Back` / `Elastic` / `Bounce`。

Punch / Shake（有限时长阻尼正弦；`to`/`strength` 为振幅）与 Color/Quat：

```cpp
anim->punch(0.f, 12.f, 0.4f).frequency(18).dampingRatio(0.f).bind(sinkX);
anim->shake(0.f, 0.2f, 0.5f).frequency(20).seed(7).bind(sinkX);
anim->motionColor(MotionColor{0,0,0,1}, MotionColor{1,0,0,1}, 0.3f).bind(colorSink);
anim->motionQuat(MotionQuat{0,0,0,1}, MotionQuat{0,1,0,0}, 0.3f).bind(quatSink);
```

脚本：

```squirrel
local p = anim.newMotionPunch(0, 12, 0.4);
p.frequency(18);
p.dampingRatio(0.0);
local h = p.run();
local s = anim.newMotionShake(0, 0.2, 0.5);
s.frequency(20);
s.seed(7);
s.run();
```

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

## 程序化骨骼与 Foot IK

Dynamic Bone 在基础动画采样后修改 Pose，适合头发、裙摆、尾巴和挂件。链、碰撞体及
Skeleton 均为 solver 内部状态；传入的 Skeleton 和地面查询对象为 borrowed，必须比
solver 活得更久。`update` 使用固定子步并限制单帧最大步数，非有限时间步会被忽略。

```squirrel
local dynamic = anim.newDynamicBoneSolver(skeleton);
// 发卡/马尾推荐：setupHairChain 带上 tip 延伸与自碰撞默认值
local hair = anim.setupHairChain(dynamic, "hair_root", "hair_tip");
anim.setupHairHeadCollider(dynamic, "head", 0.12);
dynamic.setGlobalGravity(0, -9.81, 0);
dynamic.setExternalForce(windX, windY, windZ);
dynamic.update(pose, dt);

local feet = anim.newFootIKSolver(skeleton);
feet.setPelvisBone(skeleton.findBone("hips"));
feet.configureLeftLeg(upperLeft, lowerLeft, footLeft, 0.03);
feet.configureRightLeg(upperRight, lowerRight, footRight, 0.03);
feet.configureLeftToe(toeLeft, 0.5);
feet.configureRightToe(toeRight, 0.5);
feet.setFootLockEnabled(true);
feet.setGroundQuery(groundQuery);
feet.apply(pose, dt);
```

`setGroundQuery` 接收实现地面探测契约的运行时对象；没有 provider 或 provider 返回
Unavailable 时，solver 保留脚本通过 `setLeftContact` / `setRightContact` 提供的手动
接触点。调用顺序应为动画采样、Foot IK、Dynamic Bone，最后计算世界 Pose 与蒙皮。

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
`addAdditive(base, delta, weight)` 默认作用于全身；默认参考是 identity（样本本身就是局部空间 delta），可用 `setAdditiveReference(node, "bind"|"identity")` 改为相对 bind pose，并用 `getAdditiveReference(node)` 读取当前参考。Additive clip 应以 identity
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

原地动画必须在 `bake()` 前用 `clip.applyPlanarRootMotion(rootBone, vx, vz)`
补入标定的移动速度；恒定的单关键帧 root 轨道也会补齐终点。数据库对循环的平面
位移累计整周期行程，避免把回到起点误当作反向速度。`setDesiredVelocity` 输入
世界 XZ 速度，`setDesiredYaw` 为绕 Y 轴的弧度，正向是 `(sin(yaw), cos(yaw))`。

匹配器以当前播放时刻为连续候选；新候选须改善至少 10% 的代价才切换，避免反复
重启交叉淡入。`setIgnoreRadius` 保留当前时刻邻域内的连续播放，也覆盖循环接缝。
`setPlayRateRange(minimum, maximum)` 返回 `{ok,message}`，配置 Motion Matching
播放速率的闭区间。可变特征布局会按 UE Pose Search 的规则，累加查询与选中姿势中
所有未归一化轨迹速度通道的长度，以二者比值作为播放速率并夹到该区间；搜索节流仍按
模拟时间推进。`getPlayRateMinimum()` / `getPlayRateMaximum()` 返回当前速率区间，
`getPlayRate()` 返回当前实际速率，默认区间为 `[1,1]`。参数必须满足
有限的 `0 < minimum <= maximum <= 10`；失败时保留原区间和当前速率。

`setPoseReselectHistory(seconds)` 复制 UE PoseSearch 的短期姿势历史：正时间步会记录
当前最接近的烘焙姿势，在指定时间内禁止把它重新选为跳转目标，但不会阻止当前姿势继续播放。
`getPoseReselectHistory()` 返回当前期限。零会清空并关闭历史；脚本返回的 `{ok,message}` 必须检查。候选只需严格优于继续播放成本即可切换，
不会再额外施加非源自 UE 的百分比改善门槛。
若物理由角色控制器负责，先复制 `getPose()`，再从渲染副本移除平面 root 位移；
不要修改匹配器持有的姿态。参考 `examples/climbing-motion-matching`。

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

// 或加载测试用紧凑 `.anim.txt`（无网格关键帧，见 test/assets/mixamo/）：
local sk2 = anim.newSkeletonFromAnimationFixtureText("test/assets/mixamo/Idle.anim.txt");
local idle2 = anim.newClipFromAnimationFixtureText("test/assets/mixamo/Idle.anim.txt");
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
- Motion（LitMotion 风格）：`newMotion()`、`newMotionPunch()`、`newMotionShake()`、`newMotionSequence()`、`ensureMotionCapacity()`、`getMotionFloatCapacity()`、`getMotionFloatFreeCount()`、`getMotionCount()`；`MotionBuilder`：`ease()`、`delay()`、`loops()`、`cancelOnError()`、`frequency()`、`dampingRatio()`、`seed()`、`run()`；`Motion`：`isActive()`、`value()`、`complete()`、`cancel()`；`MotionSequence`：`append()`、`join()`、`insert()`、`appendInterval()`、`cursor()`、`duration()`、`itemCount()`、`run()`；`MotionSequenceHandle`：`isActive()`、`childCount()`、`complete()`、`cancel()`
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
- `AnimClip`：`setName()`、`getName()`、`setDuration()`、`getDuration()`、`setLoop()`、`getLoop()`、`setSampleRate()`、`addPositionKey()`、`addRotationKey()`、`addScaleKey()`、`compress()`、`retarget()`、`sample()`、`wrapTime()`。自定义时间轴可通过 `getTrackCount()`、`getPositionKeyCount()`、`getPositionKeyTime()`、`getPositionKeyX()`、`getPositionKeyY()`、`getPositionKeyZ()`、`getRotationKeyCount()`、`getRotationKeyTime()`、`getRotationKeyX()`、`getRotationKeyY()`、`getRotationKeyZ()`、`getRotationKeyW()`、`getScaleKeyCount()`、`getScaleKeyTime()`、`getScaleKeyX()`、`getScaleKeyY()`、`getScaleKeyZ()` 枚举关键帧，通过 `setPositionKey()`、`setRotationKey()`、`setScaleKey()`、`removePositionKey()`、`removeRotationKey()`、`removeScaleKey()` 和 `clearTrack()` 原位编辑；事件标记使用 `addEvent()`、`setEvent()`、`removeEvent()`、`getEventCount()`、`getEventTime()`、`getEventName()`、`getEventPayload()`；步态同步标记使用 `addSyncMarker()`、`setSyncMarker()`、`removeSyncMarker()`、`getSyncMarkerCount()`、`getSyncMarkerTime()`、`getSyncMarkerName()`。这些是 UI 无关的数据接口，项目可以组合成骨骼时间轴、Avatar 动作面板或游戏内动画工具，无需引擎内置固定窗口。
- AnimRetargetProfile：用 `addBoneMapping()` / `clearBoneMappings()` 管理 Avatar 式骨骼映射；`setNormalizedNameMatching()` / `getNormalizedNameMatching()` 配置自动匹配；`setRootBones()`、`setAutoRootScale()`、`getAutoRootScale()`、`setRootTranslationScale()`、`getRootHorizontalScale()`、`getRootVerticalScale()`、`setUseSkeletonSpaceRotation()`、`getUseSkeletonSpaceRotation()` 配置重定向；`setSkinnedInteractionPreserve()` / `getSkinnedInteractionPreserve()`、`setInteractionContactThreshold()` / `getInteractionContactThreshold()`、`setInteractionCorrectionWeight()` / `getInteractionCorrectionWeight()`、`addInteractionIkChain()` / `clearInteractionIkChains()` 配置 MeshRet 风格蒙皮交互保持；`setNeuralRetargetEnabled()` / `getNeuralRetargetEnabled()`、`setNeuralBackend()` / `getNeuralBackend()`、`setNeuralModelPath()` / `getNeuralModelPath()` 配置可选神经 MeshRet 路径；通过 `getMatchedBoneCount()`、`getUnmatchedBoneCount()`、`getUnmatchedTargetBone()`、`getInteractionCorrectionCount()`、`getNeuralInferenceCount()` 读取最近一次烘焙诊断；配合 `retargetWithProfile()` 使用。
- AnimSmrSensorCloud：`fromSkeleton()` 生成骨骼附着传感器，`getSensorCount()` / `getSensorPart()` / `evaluateWorldPositions()` 供诊断或自定义交互查询。
- `AnimPose`：`resize()`、`copyFrom()`、`blendFrom()`、`setLocal*()`、`getLocal*()`、`computeWorld()`、`aimBone()`、`solveTwoBoneIK()`、`getWorld*()`、`getWorldMatrixElement()`
- `AnimSkin`：`getVertexCount()`、`getBoneCount()`、`getSkeletonBone()`、`getSkinBoneName()`、`getInverseBindElement()`、`updateMatrixPalette()`、`getMatrixPaletteElement()`、`bindGpuMesh()`、`updateGpuMesh()`、`getBindPosition*()`、`getVertexBone()`、`getVertexWeight()`、`updateSkinnedPositions()`、`hasSkinnedPositions()`、`getSkinnedPosition*()`、`getSkinnedPositions()`、`updateSkinnedNormals()`、`hasSkinnedNormals()`、`getSkinnedNormals()`、`applyToMesh()`
- `AnimPlayer`：`play()`、`crossFade()`、`stop()`、`pause()`、`resume()`、`setSpeed()`、`setTime()`、`setLoop()`、`getPose()`、`setRootMotionBone()`、`getRootMotionBone()`、`getRootMotionX()`、`getRootMotionY()`、`getRootMotionZ()`、`getRootMotionRotationX()`、`getRootMotionRotationY()`、`getRootMotionRotationZ()`、`getRootMotionRotationW()`、`consumeEvent()`、`setUpdateRate()`、`getUpdateRate()`、`update()`；每次更新跨过的事件由 `getEventCount()`、`getEventName()`、`getEventPayload()` 读取，`clearEvents()` 可提前清空。
- `AnimGraph`：`addClip()`、`addBlend()`、`addAdditive()`、`addLayer()`、`addOneShot()`、`addBlendSpace1D()`、`addBlendSpace2D()`、`addBlendSpace1DPoint()`、`addBlendSpace2DPoint()`、`setBoneMask()`、`clearBoneMask()`、`setRoot()`、`getRoot()`、`getNodeCount()`、`setWeight()`、`setPosition1D()`、`setPosition2D()`、`setSpeed()`、`trigger()`、`isOneShotActive()`、`setAdditiveReference()`、`getAdditiveReference()`、`getPose()`、`update()`
- `AnimBoneMask`：由 `newBoneMask()` 创建；`setAll()`、`setBoneWeight()`、`setBoneWeightByName()`、`setBoneAndChildren()`、`getBoneWeight()`、`getBoneCount()` 定义逐骨权重。
- `AnimLayerMixer`：由 `newLayerMixer()` 创建；`setBasePlayer()` / `setBaseGraph()` / `setBaseStateMachine()` 设置基础姿态源，`getBasePlayer()` 读取当前基础 Player（若基础是 Graph/StateMachine 则为 `null`），`addLayer` / `addGraphLayer` / `addStateMachineLayer` 添加 `override` 或 `additive` 层。Additive 默认以骨架 bind pose 为参考，可用 `setLayerAdditiveReference(name, "bind"|"identity")` 切换。禁用层仍会推进时间。另有 `removeLayer()`、`setLayerWeight()`、`setLayerEnabled()`、`getLayerCount()`、`getLayerName()`、`getLayerWeight()`、`getLayerEnabled()`、`getLayerMode()`、`getLayerAdditiveReference()`、`update()`、`getPose()`。层事件通过 `getEventCount()`、`getEventLayer()`、`getEventName()`、`getEventPayload()`、`clearEvents()` 汇总。
- `AnimStateMachine`：`addState()`、`setEntry()`、`addTransition()`、`addFloatCondition()`、`addBoolCondition()`、`addTriggerCondition()`、`setExitTime()`、`setFloat()`、`setBool()`、`setTrigger()`、`getPose()`、`update()`
- `MotionDatabase`：`addFeatureBone()`、`addFeatureBoneByName()`、`addClip()`、`bake()`、`getFrameCount()`、`getFeatureSize()`
- `MotionMatcher`：`setDesiredVelocity()`、`setDesiredYaw()`、`setSearchInterval()`、`setBlendTime()`、`setPlayRateRange()`、`getPlayRateMinimum()`、`getPlayRateMaximum()`、`getPlayRate()`、`search()`、`update()`、`getPose()`、`getMatchedClipIndex()`
- `ControlAnim`：`setFrequency()`、`getFrequency()`、`setDamping()`、`getDamping()`、`setResponse()`、`getResponse()`、`setIntegrator()`、`getIntegrator()`、`set()`、`setTarget()`、`setTargetVelocity()`、`impulse()`、`has()`、`get()`、`getVelocity()`、`getTarget()`、`clear()`、`remove()`、`getPropertyCount()`、`getPropertyName()`、`update()`
- `ControlPose`：`setFrequency()`、`getFrequency()`、`setDamping()`、`getDamping()`、`setResponse()`、`getResponse()`、`setIntegrator()`、`getIntegrator()`、`setBoneWeight()`、`getBoneWeight()`、`setTargetPose()`、`snapToTarget()`、`getPose()`、`getTargetPose()`、`update()`
- `AnimTrail`：`setCapacity()`、`getCapacity()`、`setDuration()`、`getDuration()`、`setMinDistance()`、`getMinDistance()`、`setWidth()`、`getWidth()`、`setColor()`、`getColor*()`、`setFade()`、`getFade()`、`setStyle()`、`getStyle()`、`setDrawScale()`、`getDrawScale*()`、`setDrawOffset()`、`getDrawOffset*()`、`addPoint()`、`addPoint3()`、`sampleBone()`、`sampleBoneOffset()`、`clear()`、`update()`、`getPointCount()`、`getPoint*()`、`getPointAge()`、`getPointAlpha()`、`draw()`
- 程序化骨骼工厂：`newDynamicBoneSolver()`、`newFootIKSolver()`；发卡便捷：`setupHairChain()`、`setupHairHeadCollider()`。
- `DynamicBoneSolver`：`setSkeleton()`、`addChain()`、`addChainByName()`、`clearChains()`、`getChainCount()`、`setChainEnabled()`、`isChainEnabled()`、`isChainSleeping()`、`setChainParticleParameters()`、`setChainFreezeAxis()`、`setChainEndLength()`、`setChainEndOffset()`、`clearChainEnd()`、`setChainSelfCollision()`、`setGlobalGravity()`、`getGlobalGravityX()`、`getGlobalGravityY()`、`getGlobalGravityZ()`、`setExternalForce()`、`setWeight()`、`getWeight()`、`setPositionResponse()`、`setRotationResponse()`、`setObjectMoveResponse()`、`getObjectMoveResponse()`、`setTeleportThreshold()`、`getTeleportThreshold()`、`setDistanceReference()`、`setDistanceLimit()`、`addColliderSphere()`、`addColliderCapsule()`、`addBoneColliderSphere()`、`addBoneColliderCapsule()`、`removeCollider()`、`clearColliders()`、`getColliderCount()`、`setColliderEnabled()`、`setColliderRadius()`、`setColliderInside()`、`update()`。
- `FootIKSolver`：`setSkeleton()`、`setPelvisBone()`、`configureLeftLeg()`、`configureRightLeg()`、`configureLeftToe()`、`configureRightToe()`、`setGroundQuery()`、`setLeftContact()`、`setRightContact()`、`setMinGroundNormalY()`、`setMaxPelvisOffset()`、`setFootLockEnabled()`、`setFootLockThresholds()`、`setContactGraceTime()`、`isLeftFootLocked()`、`isRightFootLocked()`、`apply()`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 3D clip 轨道使用二分查找采样，长动画不会随单轨关键帧数线性退化；Motion Database 应离线/加载时 bake，不要逐帧重建。
- 远处角色可用 `AnimPlayer.setUpdateRate(hz)` 降低姿态求值频率（例如 15 Hz）；播放器会累积时间，达到间隔后一次推进，设为 `0` 恢复逐帧求值。姿态、Graph 中间结果以及 CPU 蒙皮矩阵均复用内部缓存，稳定播放不产生逐帧容器分配。
- Graph、状态机或 Motion Matching 求值后，可对返回的 `AnimPose` 调用 `aimBone`（骨骼本地 +Z 朝向目标）或 `solveTwoBoneIK` 做世界空间后处理；两者都接受 `0..1` 权重并会更新 world pose。
- `solveTwoBoneIK` 使用解析双骨求解，保持当前弯曲方向，并将不可达目标限制到肢体长度范围；不需要反复调用来逼近目标。精度测试覆盖刚性链和带旋转、均匀缩放的父节点，不保证非均匀缩放链具有相同的目标精度。
- GPU 蒙皮 shader 可调用 `AnimSkin.updateMatrixPalette(pose)` 后按骨骼读取 `getMatrixPaletteElement(bone, 0..15)` 上传调色板；顶点关节/权重由 `getVertexBone` / `getVertexWeight` 提供。CPU 路径复用同一调色板缓存。
- 内建 GPU 蒙皮路径只需在 Mesh 创建后调用一次 `skin.bindGpuMesh(gfx, mesh)`，之后每帧在 `pose.computeWorld(skeleton)` 后调用 `skin.updateGpuMesh(mesh, pose)`；顶点保持 Bind Pose，Vulkan/WebGPU 顶点着色器读取四关节权重和最多 128 个骨骼矩阵完成变形，前向、阴影与 GBuffer 路径共享同一调色板。
- 离线导入后可调用 `clip.compress(positionError, rotationErrorDegrees, scaleError)`，以逐轨道曲线误差为上限删除冗余关键帧；首尾关键帧、clip 属性和 gameplay notify 均保留。
- 简单同构骨架可继续使用 `sourceClip.retarget(sourceSkeleton, targetSkeleton)`。它现在默认使用骨架空间 Bind Pose 校正，并支持忽略大小写、标点及 `mixamorig:` 等命名空间的骨名匹配；输出按 clip 的 sample rate 离线烘焙，可继续压缩、进入状态机或 Animation Graph。
- 不同命名或体型应创建 AnimRetargetProfile 实例，用 `addBoneMapping(sourceName, targetName)` 建立 Avatar 式映射，`setRootBones(sourcePelvis, targetPelvis)` 指定 Retarget Root，随后调用 `sourceClip.retargetWithProfile(sourceSkeleton, targetSkeleton, profile)`。根位移默认按两副骨架的空间范围等比缩放，可用 `setAutoRootScale(false)` 或 `setRootTranslationScale(horizontal, vertical)` 调整；`getMatchedBoneCount()`、`getUnmatchedBoneCount()` 和 `getUnmatchedTargetBone(i)` 提供导入诊断。若需兼容旧的局部骨轴算法，可关闭 `setUseSkeletonSpaceRotation(false)`。
- 体型差导致手贴胸、双脚交叉等部位关系被破坏时，可开启 `setSkinnedInteractionPreserve(true)`：引擎在 FK 烘焙后用骨骼附着传感器重建源动作的身体部位相对关系，并通过双骨 IK 拉回末端（经典 SMR/MeshRet 思路，不依赖神经网络权重）。可用 `addInteractionIkChain(root, mid, tip)` 指定手臂/腿链，用 `getInteractionContactThreshold()` / `getInteractionCorrectionWeight()` 读取阈值，用 `getInteractionCorrectionCount()` 确认修正帧数。若构建包含 `animation_tensor`，可再开 `setNeuralRetargetEnabled(true)`（`getNeuralRetargetEnabled()`）并用 `setNeuralBackend("tensor"|"onnx")` / `getNeuralBackend()`、`setNeuralModelPath` / `getNeuralModelPath()` 走 MeshRet 神经路径，用 `getNeuralInferenceCount()` 确认推理帧数；模块未加载或推理失败时自动回退到双骨 IK。
- 批量角色求值使用 `AnimBatch`：通过 `anim.newBatch()` 创建，逐角色 `add(clip, skeleton, pose, time, lodLevel)`，最后 `evaluate(workerCount)`；另提供 `clear()`、`getCount()` 和 `getLastWorkerCount()`。`workerCount=0` 自动采用硬件并发度，任务以无锁原子索引分发，且拒绝同一 Pose 的并发写入。Pose 混合的平移/缩放在 x86/x64 使用 SSE 路径。
- 骨骼 LOD 以 0 为最高细节。`skeleton.setBoneLodLimit(bone, highestLod)` 指定骨骼保留到哪一级，`skeleton.getBoneLodLimit(bone)` 可查询；`clip.sampleLod(...)` 和 `AnimBatch` 会让被裁剪轨道回退到目标 Bind Pose。根骨、碰撞骨和 IK 末端应保留较高 limit，手指与装饰骨通常只保留 LOD 0。
- 程序化后处理使用 `anim.newConstraintStack(skeleton)`。`AnimConstraintStack` 按插入顺序执行 `addAim()`、`addTwoBoneIK()` 和 `addFootIK()`，通过 `apply(pose)` 一次求解；`clear()` 与 `getCount()` 管理队列。Foot IK 将髋-膝-足链种植到地面高度并按地面法线调整足部局部 +Z，可传 sole offset 与权重。
- 步态同步使用 `anim.newSyncGroup()`。`AnimSyncGroup.addPlayer(player, phaseOffset)` 加入播放器，`setLeader()` / `getLeader()` 选择主时钟。若 leader 与 follower 都有至少两个顺序兼容的 Sync Marker，`update(dt)` 会保持在共同标记区间内的相对进度（例如 `left_plant → right_plant`），跨循环区间同样生效；否则自动回退到归一化时长同步。Clip 可用 `hasCompatibleSyncMarkers()` 检查兼容性，并用 `mapSyncTimeTo()` 单独映射时间；`getUsedMarkerSync()` 可诊断最近一次更新采用的路径，`getPhase()`、`getCount()`、`clear()` 用于调试与复用。Sync Marker 会随 EVA 导入导出和重定向 clip 一起保留。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/animation/`](../../../src/modules/animation/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `animation`。


### Compact runtime animation tracks

`eve.AnimClip()` constructs a script-owned clip. Keep its instance alive while a
player, graph or motion database borrows it. `clip.loadBinary(path, skeleton)`
returns `{ok, message}`; check `ok` before using the new data. The synchronous
owner-thread decoder borrows the skeleton during loading, copies all tracks and
releases file bytes after decoding. Failure preserves the existing clip. Native
consumers use `loadAnimationTracks(clip, bytes, skeleton) -> Result<void>`.

For multiple clips, `eve.loadAnimationTrackBatch(clips, paths, skeleton, workers)`
returns `{ok, message, workers}`. The arrays must have the same length (at most 64)
and distinct script-owned clip instances. Zero workers chooses up to eight hardware
threads; one forces serial decoding, and values outside 0..8 are rejected. The
binding reads files on the owner thread, then native workers decode independent
candidates. All destinations remain unchanged if any input fails. Retain the
instances while their players/databases borrow them. The batch is synchronous:
no worker accesses the VM or filesystem, and every worker joins before return or
exception. The native canonical API is `loadAnimationTrackBatch(inputs, skeleton,
workers) -> Result<int>`; inputs borrow destinations and immutable encoded bytes
for this call only. Total input is bounded to 256 MiB; the version-1 codec and its
validation rules are unchanged.

`MotionDatabase.bake()` computes clips in parallel for banks of at least 16 clips,
using at most eight native threads. Its borrowed clips and skeleton must remain
immutable throughout the synchronous call. Frame IDs, sample times, and floating
point normalization order remain identical to serial baking. Worker exceptions
are joined and propagated; an interrupted bake is not exposed as searchable.

The `eve.animation-tracks/1` wire format is little-endian: `EVAC`, uint32 version
(1), uint32 track count, Float32 duration/sample rate, uint32 loop flag (0 or 1),
then a UTF-8 clip name. Strings have a uint32 byte length. Each track has a bone
name followed by three channels (position, rotation, scale). Each channel starts
with uint32 key count, then Float32 time and 3/4/3 Float32 components per key.
Times must be strictly increasing and within the duration; all values must be
finite and quaternions nonzero. Bone names must map uniquely to the supplied
skeleton. Unknown versions, flags, trailing bytes, malformed UTF-8 and truncation
are rejected. Files are limited to 128 MiB and channels to one million keys.
There is no implicit version migration; regenerate from the source library.

The offline corpus builder removes only exactly constant channels' redundant
keys, preserves varying Float32 samples and represents STEP boundaries with
adjacent Float32 times. It does not encode UE montages, event graphs or curves;
those remain in the import library's native archives and metadata. Binary import
uses temporary candidate ownership and publishes through `AnimClip::adopt` only
on success, without retaining input pointers or creating a mesh resource.

### Authored candidate selection

`MotionMatcher::setCandidateRanges` accepts clip indices, start/end seconds,
cost biases, disable-reselection flags, transition blocks and authored cost overrides.
Squirrel ranges contain exactly `[clip,start,end,bias,disableReselection,blocks,
continuingBias,costOverrides,continuingCostOverrides]`,
where `blocks` contains `[start,end]` pairs (an empty array permits all entries).
Transition blocks use inclusive start/exclusive end without endpoint tolerance;
they are unioned across the clip's ranges and prevent new matches while allowing
eligible continuation. A protected terminal sample needs an end beyond clip duration.
Each cost override is `[start,end,bias]`: it replaces the candidate or continuing
default inside that interval, and the last active override wins. Candidate cost
includes an authored looping bias before entering this API; continuing cost does
not. Duplicate ranges retain the continuing cost paired with the lowest candidate
cost at that frame. Nonfinite, negative, empty/reversed intervals or more than
100000 total metadata intervals reject
the entire update. Candidate count includes protected continuation frames.
It atomically replaces the searchable
frame set and preserves database frame identities. Empty/invalid selections are
rejected without changing the previous set. The borrowed database must remain
immutable while its matcher is used. Candidate restrictions apply to continuation
as well as new searches, allowing a Chooser to switch state without retaining an
ineligible animation.

`setQueryPose` copies a pose with matching bone count; `setTrajectory` copies three
finite world displacements and yaw angles at 0.33, 0.66 and 1.0 seconds. All three
native APIs return checked Results, run synchronously on the owner thread outside
search/advance, retain no input references and invoke no callbacks. Squirrel
candidate/trajectory setters return `{ok,message}` (candidate selection also
returns `count`); the pose setter throws a script diagnostic on rejection.


### Locomotion feature layout

Before baking, `MotionDatabase::setLocomotionFeatures(leftFoot, rightFoot, pelvis)`
selects a 30-dimensional layout. Its three distinct bone indices replace the basic
feature-bone list. The pelvis heading axis is local +Z in Y-up imported assets.
Invalid configuration leaves the previous layout intact; changes after baking are
rejected. Keep the caller-owned skeleton, clips and database alive and immutable
through matching. Existing databases retain the basic layout.

Trajectory channels: current planar velocity; position at -0.05 seconds; current
heading; position/heading at +0.35; position/velocity/heading at +0.7; and 3D velocity
direction at +1 second. Pose channels: left-foot position relative to right foot,
both feet's velocities relative to their own time-sampled root, and planar pelvis
heading. Weights follow the reference PSS_Default: past position 0.3, feet velocities
0.3 each, pelvis heading 0.1, final velocity direction 1.5, other channels 1.0.
Group multipliers are normalized together before candidate biases are applied.
Velocity direction clamps at 0.01 m/s, matching the source's 1 cm/s threshold.
Vector channels use mean Euclidean deviation from their centroid; feet velocities
share one normalization group. For imported UE data,
`setFeatureNormalizationRanges([[clip,start,end], ...])` selects the inclusive
layout-rate samples used for statistics before `bake()`. Ordered duplicate and
overlapping ranges contribute repeatedly, which preserves database-entry
multiplicity in a UE `NormalizationSet`. The call returns
`{ok,message,count}` and copies its input atomically.

`MotionMatcher::setLocomotionQuery(current, previous, elapsedSeconds, samples)` copies
both local poses and exactly five trajectory samples at -0.05, 0, 0.35, 0.7 and
1 seconds. Each sample contains world displacement XYZ, world velocity XYZ in m/s,
and yaw in radians. The positive simulation interval must be at most one second.
Nonfinite values, mismatched poses, wrong counts or use with a basic database fail
atomically. This owner-thread call runs outside search/advance, retains no input
references and invokes no callbacks. A cold start may pass the same pose twice;
action handoffs should provide actual adjacent poses. Eligible continuing poses
supply pose-channel query values during playback; trajectory always uses character
prediction. A locomotion database requires this query API before search; the basic
pose/trajectory setters reject it. Both new Squirrel setters return `{ok,message}`
and must be checked. Native setters return `[[nodiscard]] Result`.

Baking accumulates translation and yaw across looping root trajectories and
extrapolates endpoint root velocity outside nonlooping clips. Pose samples clamp
at nonlooping endpoints. These runtime objects are not serialized; rebuilding a
database also rebuilds statistics. Determinism is within floating-point tolerance
for identical assets and ordered simulation inputs; no wall clock or RNG is used.


Nonlooping matcher playheads clamp at clip duration. Reaching the end forces a
search even before the normal search interval expires. Disable-reselection also
excludes the exhausted current asset from fresh candidates; it cannot pin playback
to its final frame. When no other candidate exists, a one-shot-only database holds
its terminal pose until the candidate set changes. This applies to both layouts.


### Finite-duration pose inertia

`eve.AnimInertializer()` creates an independent object owned by Squirrel GC. It owns
its samples and retains no clip, skeleton or pose pointers. It complements
`ControlPose`: inertia ends exactly at its deadline, while ControlPose continuously
tracks a changing target.

`begin(source, previousSource, target, previousTarget, historySeconds, durationSeconds, boneTimeFactors)`
returns `{ok, message}`. All four poses must have the same bone order and local
coordinate space. Supply actual outgoing output history when interrupting a blend,
and incoming samples at the same two instants. History must be finite in (0, 1]
seconds; duration must be finite in [0, 10] seconds. Factors are empty for uniform
duration or one [0, 1] value per bone: zero snaps, 0.75 finishes in 75% of the time.
Inputs are copied atomically; invalid input leaves the prior transition intact.

`evaluate(target, elapsedSeconds)` returns `{ok, message}` and samples the transition
at absolute nonnegative simulation time since begin. Target is the currently
advancing incoming pose, not the initial pose. Call `copyPose(destination)` to copy
the evaluated output. At or after each bone's deadline it equals the incoming pose.
Translation/scale and quaternion log-space rotation offsets preserve sampled source
velocity and decay to zero. Repeated evaluation at the same time/target is deterministic
within floating-point tolerance. Calls are owner-thread only and have no callbacks.
Native users access `AnimInertializer::pose()` as a borrowed const reference whose
contents change after successful begin/evaluate and whose lifetime ends with the owner.

The climbing example queues this transition for action, air and animation-bank
handoffs, using fixed-step pose history. Root travel is stripped before blending and
its duration factor is zero, preserving capsule collision authority. Authored montage
exit tails retain their separate advancing playback and source blend profiles.

`AnimPlayer.getClip()` borrows the selected clip (null before selection); retain the owning clip while sampling it. `MotionDatabase.hasLocomotionFeatures()` reports whether the database uses the configured locomotion feature layout.

### Variable motion feature layouts

`MotionDatabase.setFeatureLayout(sampleRate, channels, normalizationLengthScale)` copies an ordered vector
layout before baking and returns `{ok,message}`. Each channel is
`[kind,source,query,bone,origin,axesMask,headingAxis,sampleTime,weight,characterSpaceVelocity,normalizeVelocity,normalizationGroup]`.
Kinds are `position`, `velocity`, `heading`, `curve`; sources are `pose`, `trajectory`;
query policies are `USE_CHARACTER_POSE`, `USE_CONTINUING_POSE`. The X/Y/Z mask
uses bits 1/2/4. Heading axes are 0/1/2. Values use metres, seconds and radians.
Pose sample times currently require zero; trajectory offsets cover -10..10 seconds.
Trajectory channels require character queries and world-space velocity.
Equal nonempty normalization groups pool channels of equal kind and cardinality;
normalized and physical velocities may share an explicitly authored pool.
`normalizationLengthScale` scales metre-based positions and physical velocities
before pooling; normalized velocities, headings and curves remain dimensionless.
Use 100 for source configurations authored in centimetres, or 1 for metres
(the C++ layout default). Empty groups remain independent. Zero-weight channels
retain their dimensions. Query inputs remain metres/seconds regardless of this scale.

`MotionMatcher.setFeatureQuery(current, previous, elapsedSeconds, samples, curves)` copies
and normalizes the query atomically, returning `{ok,message}`. Each trajectory
sample is `[seconds,x,y,z,vx,vy,vz,yaw]`, where position is world-axis displacement
from the current root. Provide every distinct configured trajectory time exactly
once. Call `setDesiredYaw` first: the query captures that orientation immediately.
Invalid input, including normalization overflow, preserves the previous query.
Basic and fixed-locomotion query setters cannot configure this layout. Pass an empty
curve array for vector-only schemas; otherwise each curve sample is
`[name,seconds,value]` and must cover every configured curve/time pair.

Scalar channels use kind `curve`, source `pose`, axesMask=1 and a thirteenth
channel field containing the curve name. Scalar sample offsets may range from
-10 to +10 seconds. After adding all clips and configuring the layout, call
`MotionDatabase.loadFeatureCurves(path, sources)` before baking. It loads EVFC/1
data and copies source names in exact clip order, returning `{ok,message}` with
atomic failure. C++ uses `setFeatureCurves(bytes, sources)` with borrowed spans
and an owning decoded snapshot. Missing source records reject; missing channels
in a known source evaluate to zero. Scalar values retain their sign and use the
same normalization and continuing-query rules as other feature channels.

These APIs are owner-thread operations without callbacks or reentrancy. The
matcher borrows its skeleton and database; retain both unchanged until it is
destroyed. Layout/query inputs are copied and need not outlive their setter.
`hasFeatureLayout()` reports this mode. A baked variable-layout database rejects
layout, root and clip-list changes. Native equivalents use the owning typed
values in `MotionFeatureLayout.h` and checked `Result` returns. The climbing
example records evaluated Phase history alongside contact curves and routes
ordinary searches to the imported schema indexes.

### Scalar animation curve libraries

`eve.AnimCurveLibrary()` owns scalar curve tracks independently of skeletons and
players. `loadBinary(path)` returns `{ok,message}` and atomically replaces the
library from `eve.animation-curves/1` (EVFC/1); failures preserve previous data.
`contains(source)` and `getSourceCount()` inspect its source records.
`sample(source,channel,seconds,loop)` returns `{ok,message,present,value}`. An
absent channel succeeds with `present=false`; an unknown source or nonfinite
time is an error. Sampling linearly interpolates, clamps non-looping playback,
and wraps positive or negative time for looping playback.

Native `AnimCurveLibrary::load(span<const byte>)` copies all decoded data and
retains no input pointers. `sample` returns `Result<optional<float>>`; all
operations are owner-thread only, without callbacks. Repeated samples use
explicit simulation time and are deterministic within Float32 rounding.
Unknown versions, trailing data, invalid names, nonfinite/unsorted keys and
resource limits are rejected. Regenerate older contact script tables using
`scripts/build_unreal_contact_curves.py --library ...`; the binary layout and
limits are documented in `tools/unreal-uasset-converter/README.md`.
