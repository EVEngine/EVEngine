# 粒子模块

**脚本入口：** `eve.Particles()`

用代码或 JSON 创建发射器，配置运动、颜色、寿命并进行更新和渲染。

## 基本用法

```squirrel
local particles = eve.Particles();
local fire = particles.newEmitterFromFile("particles/fire.json");
fire.setPosition(320, 420);
fire.start();
particles.update(dt);
particles.render(gfx);
```

## 绑定到动态骨骼

粒子仍是 2D。支持多种运行时骨骼源；每帧 `particles.update` 会自动 `syncAttach`。

| 源 | API | 说明 |
|----|-----|------|
| 3D `AnimPose` | `attachToBone` / `attachToBoneByName` | 骨骼世界坐标经 `plane`（`xy`/`xz`/`yz`）与 `scale` 投影 |
| 2D Spine | `attachToSpineBone` / `attachToSpineBoneByName` | 像素空间；`scale` 仍生效，`plane` 忽略 |
| IK `Skeleton2D` | `attachToSkeleton2D` | FABRIK 链骨位置（像素/世界单位 × `scale`） |
| IK `Skeleton3D` | `attachToSkeleton3D` | 3D 骨位置经 `plane`+`scale` 投影 |

```squirrel
local anim = eve.Animation();
// … player/sm 驱动 pose，并 pose.computeWorld(sk) …
local spark = particles.newEmitter(128);
spark.applyPreset("spark");
spark.setAttachScale(100);      // 米 → 像素
spark.setAttachPlane("xy");
spark.setAttachOffset(0, 0.05, 0);
spark.setFollowBoneRotation(true);
spark.attachToBoneByName(pose, sk, "LeftHand");
spark.start();
```

Spine（2D）示例：

```squirrel
local spine = anim.newSpineSkeleton(data);
spine.updateWorldTransform();
local dust = particles.newEmitter(64);
dust.attachToSpineBoneByName(spine, "hand");
dust.start();
```

IK 2D/3D 示例：

```squirrel
local ik = eve.IK();
local sk2 = ik.newSkeleton2D();
// … createBone / solve …
local tipFx = particles.newEmitter(32);
tipFx.attachToSkeleton2D(sk2, tipBoneId);
tipFx.setFollowBoneRotation(true);
```

`detach()` 解除绑定；`getAttachKind()` 返回 `"anim"` / `"spine"` / `"ik2d"` / `"ik3d"` / `"none"`；也可手动 `syncAttach()`。

## 蒙皮表面发射（人物皮肤粒子）

从 CPU 蒙皮后的顶点采样发射位置，适合身体表面的火花、灰尘、能量层：

```squirrel
local skin = anim.newSkinFromModel(model, meshIndex, sk);
pose.computeWorld(sk);
local aura = particles.newEmitter(512);
aura.applyPreset("smoke");
aura.setSkinScale(100);
aura.setSkinPlane("xy");
aura.setSkinSource(skin, pose);
// 可选：只从某根骨附近的顶点发射
aura.setSkinBoneFilterByName(sk, "Spine", 0.2);
aura.start();
// 或瞬时爆发：
aura.emitFromSkin(80);
```

脚本侧也可主动刷新蒙皮缓存：`skin.updateSkinnedPositions(pose)`，再用 `getSkinnedPositionX/Y/Z(i)` 读取。

## 外观增强：混合模式、序列帧与生命周期曲线

发射器支持加法混合、精灵表序列帧动画、多段颜色渐变与大小/旋转曲线，均可通过 JSON 或脚本配置。

```squirrel
// 加法混合 + 序列帧（4x2 精灵表，12 帧/秒，随机起始帧 0~25%）
local burst = particles.newEmitter(256);
burst.setBlendMode("additive");
burst.setFlipbook(4, 2, 12, 0.25);
burst.setTexture(tex);  // 精灵表贴图
burst.start();

// 多段颜色渐变 + 大小曲线 + 初始随机旋转
burst.clearColorGradient();
burst.addColorStop(0.0, 1.0, 0.9, 0.4, 1.0);  // 白热
burst.addColorStop(0.5, 1.0, 0.3, 0.0, 0.8);  // 橙红
burst.addColorStop(1.0, 0.1, 0.0, 0.0, 0.0);  // 熄灭
burst.clearSizeCurve();
burst.addSizeCurvePoint(0.0, 0.3);
burst.addSizeCurvePoint(0.7, 1.0);
burst.addSizeCurvePoint(1.0, 0.1);
burst.setStartRotation(-30, 30);  // 度
```

JSON 等价配置：

```json
{
  "blendMode": "additive",
  "flipbook": { "hframes": 4, "vframes": 2, "frameRate": 12, "frameRandomStart": 0.25 },
  "startRotation": [-30, 30],
  "colorOverLifetime": [
    { "t": 0.0, "r": 1.0, "g": 0.9, "b": 0.4, "a": 1.0 },
    { "t": 0.5, "r": 1.0, "g": 0.3, "b": 0.0, "a": 0.8 },
    { "t": 1.0, "r": 0.1, "g": 0.0, "b": 0.0, "a": 0.0 }
  ],
  "sizeOverLifetime": [[0.0, 0.3], [0.7, 1.0], [1.0, 0.1]],
  "rotationOverLifetime": [[0.0, 0.0], [1.0, 120.0]]
}
```

说明：

- `blendMode`：`"alpha"`（默认）/ `"additive"` / `"opaque"`。加法混合适合火焰、火花、魔法等自发光效果。
- `flipbook`：`hframes`/`vframes` 为精灵表行列数；`frameRate` 为每秒帧数（0 = 静止第一帧）；`frameRandomStart`（0~1）随机化起始帧。
- `colorOverLifetime` / `sizeOverLifetime` / `rotationOverLifetime` 为按归一化寿命采样的多段渐变/曲线；不配置时回退到 `colorStart`/`colorEnd` 与 `sizes` 的两端线性插值。`rotationOverLifetime` 单位为度，叠加在初始旋转与自旋之上。

## 发射控制与受力增强

发射器支持定时爆发、预热、重力/阻尼/限速、速度曲线、继承速度与本地/世界仿真空间：

```squirrel
// 定时爆发 + 预热（一次性爆炸，开场即满）
local boom = particles.newEmitter(256);
boom.addBurst(0.0, 60);        // 第 0 秒爆发 60 个
boom.addBurst(0.15, 30);       // 第 0.15 秒再爆 30 个
boom.setEmitterLifetime(0.3);  // 单次效果
boom.setPrewarm(0.5);          // start() 时预模拟 0.5 秒
boom.start();

// 重力 + 阻尼 + 限速 + 继承发射器移动速度（角色跑动扬尘）
local dust = particles.newEmitter(128);
dust.setGravity(0, 40);
dust.setDamping(0.8);
dust.setLimitVelocity(120);
dust.setInheritVelocity(0.6);
dust.setSimulationSpace("local");  // 粒子跟随发射器（本地空间）
dust.start();
```

JSON 等价配置：

```json
{
  "bursts": [ { "time": 0.0, "count": 60 }, { "time": 0.15, "count": 30 } ],
  "prewarm": 0.5,
  "gravity": [0, 40],
  "damping": 0.8,
  "limitVelocity": 120,
  "velocityOverLifetime": [[0.0, 1.0], [1.0, 0.2]],
  "inheritVelocity": 0.6,
  "simulationSpace": "local",
  "noise": { "strength": 8, "frequency": 0.05, "speed": 1.5 },
  "maxDeltaTime": 0.1
}
```

说明：

- `bursts`：按发射器运行时间触发的爆发（数组元素为 `{time, count}` 或 `[time, count]`），每个爆发只触发一次。
- `prewarm`：`start()` 时按 1/60 步长预模拟，让循环效果在首帧就是满的。
- `gravity` 每步作为加速度叠加；`damping` 为每秒速度衰减比例（0~1）；`limitVelocity` 为速度上限（0 = 不限）；`velocityOverLifetime` 为速度倍率曲线。
- `inheritVelocity`（0~1）把发射器当前移动速度按比例附加到新粒子；`simulationSpace: "local"` 时粒子随发射器平移（默认 `"world"`）。
- `noise` 为 CPU 值噪声湍流（强度/频率/时间流速），适合烟雾、火焰的自然扰动。
- `maxDeltaTime` 限制单帧模拟步长，防止卡顿后的粒子爆炸。

## 碰撞与子发射器

```squirrel
// 世界边界反弹（雨滴落地溅射）
local rain = particles.newEmitter(512);
rain.setCollision("bounce", 2, 0.4, 0.1);   // 半径 2、弹性 0.4、每次碰撞损失 10% 寿命
rain.setCollisionBounds(true, 0, 0, 960, 640);
rain.setWorldCollision(true);               // 可选：查询引擎级碰撞解析器（tilemap/Box2D）

// 子发射器：出生时喷火花，死亡时冒烟
local sparks = particles.newEmitter(128);
local smoke = particles.newEmitter(128);
sparks.addSubEmitter(smoke, "death", 0.3);  // 继承 30% 速度
```

JSON：`collision: {mode, radius, restitution, lifetimeLoss}`、`collisionBounds: {enabled, minX, minY, maxX, maxY}`、`worldCollision: true`。子发射器目前通过脚本 `addSubEmitter(target, trigger, inherit)` 关联（trigger 为 `"birth"` / `"death"` / `"collision"`）。

拖尾：`setRenderMode("stretched", factor)` 或 JSON `renderMode: "stretched"` + `stretch`，粒子沿速度方向拉伸成彗星/流光。

健壮性：`overflowMode`（`"drop"` 默认 / `"pause"` 暂停发射直到有空位 / `"warn"` 日志提示）；`setMaxDeltaTime` 限制单帧步长；带相机的发射器在屏幕外且无存活粒子时会跳过模拟。

## 力场、自定义 Shader 与粒子灯光

```squirrel
// 径向力场：黑洞（吸引）/ 斥力场；strength > 0 吸引，< 0 排斥
local vortex = particles.newEmitter(512);
vortex.addForceField(400, 300, 180, 90, 1.5);   // 中心 (400,300)，半径 180，强度 90，衰减指数 1.5

// 自定义粒子 Shader（仅作用于带贴图的粒子；需要先创建 Shader）
local glow = particles.newEmitter(256);
glow.setShader(myShader);

// 粒子灯光：跟随存活粒子的 2D 点光源（每个发射器最多 8 盏，引擎单画布每帧最多 8 盏）
local torch = particles.newEmitter(128);
torch.setLights(true, 120, 1.2, 1.0, 0.6, 0.2, 4);
```

JSON：`forceFields: [{x, y, radius, strength, falloff}]`；`lights: {enabled, max, radius, intensity, color: [r,g,b]}`。粒子灯光由 `ParticleLightSystem`（随 `particles.update` 自动调用）维护一盏 `Light2D` 池，位置与最前面的存活粒子同步。

## GPU 加速模拟

`setGpuSimulation(true)` 或 JSON `"gpuSimulation": true` 可为单个发射器开启 GPU 模拟：每粒子的运动积分（重力、阻尼、限速、径向/切向加速度、速度帧、噪声湍流、旋转、序列帧推进、寿命衰减）在计算着色器中执行，生成与回收、碰撞、子发射器仍由 CPU 处理。不可用（无 GPU 设备/计算管线失败）时自动回退到 CPU 模拟，行为保持一致。

```squirrel
local embers = particles.newEmitter(20000);  // 上万粒子场景
embers.setGpuSimulation(true);
embers.setGravity(0, 30);
embers.start();
```

实现说明：粒子状态以 16 个 float/粒子存放在 SSBO（`ParticleGpuKernel.h`），每帧 upload → dispatch（64 线程/组）→ readback 后走原有渲染路径；渲染仍是 CPU 批量四边形，因此收益集中在把每粒子物理计算移到 GPU。该模式下暂不参与 GPU 的项：力场、噪声外的 CPU 专属逻辑仍在 CPU 侧生效（碰撞等）。GLSL 内核可通过 `glslc` 单独编译验证（`ParticleGpuKernel.h` 内注释含布局说明）。

## 对象关系与调用时机

`Particles` 管理 Emitter 及配置、模拟、渲染系统；Emitter 持有容量、发射配置与运行状态。模块 update 统一推进所有 emitter（含骨骼绑定同步与蒙皮表面采样），render 按 layer 提交。

帧序建议：动画 `computeWorld` → `particles.update(dt)` → `particles.render(gfx)`。

## 目标导向指南

### 从 JSON 创建火焰

配置 buffer、发射率、寿命、速度、颜色和 `autoReload`，调用 `newEmitterFromFile()`；设置位置后 `start()`。模块统一 `update(dt)` 和 `render(gfx)`，无需逐粒子操作。

### 制作一次性爆炸

创建容量足够的 emitter，设置有限 emitter life 和较高瞬时发射率，停止循环；播放结束后检查 active 状态并回收。预览时用 preset 起步，再逐项覆盖参数。

### 角色手上的拖尾 / 皮肤光晕

动画更新并 `computeWorld` 后，用 `attachToBoneByName` 绑定肢体，或用 `setSkinSource` 从蒙皮表面发射；`setAttachScale` / `setSkinScale` 把模型单位映射到像素空间。

## 常见问题

- buffer 太小导致高发射率粒子被覆盖。
- 只 render 不 update，粒子静止。
- 无限 emitter 离开场景后未 stop/回收。
- 骨骼绑定后位置不对：检查是否先更新姿态（`pose.computeWorld(sk)` / `spine.updateWorldTransform()` / IK `forwardKinematics`/`solve`），以及 `plane`/`scale` 是否匹配相机投影。
- 蒙皮表面无粒子：确认 `setSkinSource` 与 `hasBones` 网格，且过滤器未把候选顶点剔光。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `addBurst()`、`addColorStop()`、`addForceField()`、`addRotationCurvePoint()`、`addSizeCurvePoint()`、`addSubEmitter()`、`addVelocityCurvePoint()`、`applyConfig()`、`applyPreset()`、`attachToBone()`、`attachToBoneByName()`、`attachToSkeleton2D()`、`attachToSkeleton3D()`、`attachToSpineBone()`、`attachToSpineBoneByName()`、`clearBursts()`、`clearColorGradient()`、`clearForceFields()`、`clearRotationCurve()`、`clearSizeCurve()`、`clearSkinSource()`、`clearSubEmitters()`、`clearVelocityCurve()`、`detach()`、`emit()`、`emitFromSkin()`、`getAttachBone()`、`getAttachKind()`、`getAutoReload()`、`getBlendMode()`、`getBufferSize()`、`getConfigPath()`、`getCount()`、`getDirection()`、`getGpuSimulation()`、`getLightsEnabled()`、`getPrewarmSeconds()`、`getShader()`
- `getEmissionAreaType()`、`getEmissionAreaX()`、`getEmissionAreaY()`、`getEmissionRate()`、`getEmitterCount()`、`getEmitterLifetime()`、`getLayer()`、`getName()`
- `getParticleHeight()`、`getParticleLifetimeMax()`、`getParticleLifetimeMin()`、`getParticleWidth()`、`getSizeVariation()`、`getSpread()`、`getX()`、`getY()`
- `hasSkinSource()`、`isActive()`、`isAttached()`、`isPaused()`、`isStopped()`、`isVisible()`、`loadConfig()`、`moveTo()`、`newEmitter()`、`newEmitterFromFile()`
- `pause()`、`pollConfigs()`、`reloadConfig()`、`render()`、`reset()`、`setAttachOffset()`、`setAttachPlane()`、`setAttachScale()`、`setAutoReload()`、`setCamera()`、`setCanvas()`
- `setBlendMode()`、`setCollision()`、`setCollisionBounds()`、`setColorEnd()`、`setColorStart()`、`setDamping()`、`setDirection()`、`setEmissionArea()`、`setEmissionRate()`、`setEmitterLife()`、`setEmitterLifetime()`、`setEmitterTime()`、`setFlipbook()`、`setGpuSimulation()`、`setGravity()`、`setInheritVelocity()`、`setLights()`、`setLimitVelocity()`、`setMaxDeltaTime()`、`setNoise()`、`setOverflowMode()`、`setPrewarm()`、`setRenderMode()`、`setShader()`、`setSimulationSpace()`、`setWorldCollision()`
- `setFollowBoneRotation()`、`setLayer()`、`setLinearAcceleration()`、`setParticleLife()`、`setParticleLifetime()`、`setParticleSize()`、`setPosition()`、`setRadialAcceleration()`、`setSizeVariation()`
- `setSizes()`、`setSkinBoneFilter()`、`setSkinBoneFilterByName()`、`setSkinPlane()`、`setSkinScale()`、`setSkinSource()`、`setSpeed()`、`setSpin()`、`setSpread()`、`setStartRotation()`、`setTangentialAcceleration()`、`setTexture()`、`setVisible()`、`start()`
- `stop()`、`syncAttach()`、`update()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/particles/`](../../../src/modules/particles/)
**相关测试：** [`test/particles.cpp`](../../../test/particles.cpp)、[`test/particles_attach_skin.cpp`](../../../test/particles_attach_skin.cpp)、[`test/particles_dynamic_bones.cpp`](../../../test/particles_dynamic_bones.cpp)、[`test/particles_attach_more.cpp`](../../../test/particles_attach_more.cpp)、[`test/particles_attach_extra.cpp`](../../../test/particles_attach_extra.cpp)。
