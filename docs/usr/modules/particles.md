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

## 可复现播放、固定步进与按距离发射

需要录制回放、网络同步、慢动作或稳定拖尾时，可显式控制粒子时间线：

```squirrel
local trail = particles.newEmitter(512);
trail.setRandomSeed(20260826);             // 每次 start() 重放相同随机序列
trail.setAutoRandomSeed(false);
trail.setEmitterLifetime(1.5);
trail.setLooping(true);                    // 有限时间线循环，burst 也会重新触发
trail.setPlaybackSpeed(0.5);               // 半速播放；0 = 冻结
trail.setFixedTimeStep(1.0 / 120.0, 8);    // 固定步长 + 每帧最多追 8 步
trail.setEmissionRateOverDistance(0.25);   // 每移动 4 个世界单位发射 1 个
trail.start();
```

JSON 等价配置：

```json
{
  "randomSeed": 20260826,
  "autoRandomSeed": false,
  "emitterLife": 1.5,
  "looping": true,
  "playbackSpeed": 0.5,
  "fixedTimeStep": 0.008333333,
  "maxSubSteps": 8,
  "emissionRateOverDistance": 0.25
}
```

- 固定种子会让寿命、速度、方向、大小、旋转、发射形状和序列帧起点等随机采样可复现；`autoRandomSeed: true`（默认）则每次 `start()` 生成新序列。
- 固定步进由 `maxSubSteps` 限制追帧工作量，超过上限的时间债务会被丢弃，避免暂停恢复后形成长时间尖峰。
- 按距离发射会沿上一位置到当前位置的线段均匀插值，世界空间拖尾不会因帧率变化形成粒子团。第一次更新只建立运动基线，不会补发创建前的路径。

## 质量等级、预算、剔除与性能统计

大规模战斗或天气效果应通过运行时预算稳定降级，而不是等粒子缓冲溢出：

```squirrel
particles.setQualityLevel(2);       // 0=最低，3=最高
particles.setBudget(50000, 256);    // 全局存活粒子软上限、每帧模拟发射器上限；0=不限

local heroFx = particles.newEmitter(2048);
heroFx.setPriority(100);            // 高优先级先获得模拟/生成预算
heroFx.setMinimumQuality(0);        // 所有质量等级都保留
heroFx.setCullingMode("always");    // 即使离屏也继续模拟
heroFx.setMaxSpawnPerFrame(256);

local ambience = particles.newEmitter(8192);
ambience.setPriority(-10);
ambience.setMinimumQuality(2);
ambience.setCullingMode("pause");  // 离屏或超出距离时冻结
ambience.setCullDistance(1800);
ambience.setMaxSpawnPerFrame(96);
```

JSON 发射器策略：

```json
{
  "priority": -10,
  "minimumQuality": 2,
  "cullingMode": "pause",
  "cullDistance": 1800,
  "maxSpawnPerFrame": 96
}
```

- 发射器按 `priority` 从高到低分配每帧模拟器数量和剩余粒子空间；总粒子数是软上限，调低预算不会立即删除已经存活的粒子，但会停止超额生成，让它们自然死亡。
- `cullingMode`：`"automatic"`（默认，仅跳过离屏且为空的发射器）、`"pause"`（离屏时冻结全部模拟）、`"always"`（关键玩法效果始终模拟）。渲染阶段仍会跳过确定不可见的粒子。
- `minimumQuality` 高于当前全局质量等级时，发射器冻结且不渲染；恢复质量后继续运行，不破坏已有粒子状态。
- 每次 `update`/`render` 后可读取 `getLastSimulatedEmitters()`、`getLastCulledEmitters()`、`getLastBudgetSkippedEmitters()`、`getLastParticleCount()`、`getLastSpawnedParticles()`、`getLastDroppedSpawns()`、`getLastRenderedParticles()`、`getLastSimulationMs()` 与 `getLastRenderMs()`，用于 HUD、自动伸缩和性能回归。

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

精灵朝向统一由 `setRenderMode` 控制：`"billboard"` 使用粒子自身旋转，`"axis"` 配合 `setRenderAxis(degrees)` 固定到屏幕空间轴，`"stretched"`（也接受 `"velocity"`）按速度方向拉伸。JSON 对应 `renderMode`、`renderAxis` 和 `stretch`。

透明粒子可用 `setSortMode("none" | "oldest" | "youngest" | "distance")` 选择稳定的逐发射器提交顺序，`getSortMode()` 返回规范化后的策略。`distance` 在有相机时按远到近排列。当前 GPU 常驻后端只支持 `none`；其他策略明确保留在 CPU 后端，避免宣称排序已在 GPU 上完成。

连续拖尾使用 `setRibbon(width, minSegmentLength)`，它按稳定的粒子出生顺序连接相邻控制点，跳过过短段，并沿段方向生成带宽度的纹理四边形；JSON 为 `ribbon: {width, minSegmentLength}`。Ribbon 当前明确使用 CPU 渲染后端，适合配合 `setEmissionRateOverDistance` 制作弹道、刀光和移动轨迹。

`setSoftParticles(true, depth, fadeDistance)` 让常驻粒子采样当前 G-buffer 的线性场景深度，在与几何相交或被遮挡时平滑衰减 alpha；JSON 为 `softParticles: {enabled, depth, fadeDistance}`，深度值均为 `[0,1]` 线性深度。`isSoftParticlesActive()` 只有在 GPU 常驻渲染器已激活且当前帧确实产生场景深度时才返回 true；纯 2D 帧不会伪造深度，而是保持普通粒子外观。

粒子材质默认是 `unlit`，不受场景灯光影响。`setMaterialMode("lit")` 会让粒子接收现有 2D 环境光和点光/方向光；配合 `setNormalTexture(normalTex)` 时使用切线空间法线贴图，否则使用无贴图的逐粒子中心光照。JSON 可写为：

```json
{
  "material": {
    "mode": "lit",
    "normalTexture": "particles/smoke-normal.png"
  }
}
```

Lit/法线贴图目前明确使用 CPU 粒子模拟加 GPU 2D lit 绘制；即使请求了 `gpuSimulation`，也会安全回退，不会静默忽略灯光。切回 `unlit` 后可重新满足 GPU 常驻条件。

运行时诊断可读取 `getSimulationBackend()`（`"cpu"` / `"gpu"`）和 `getGpuFallbackReason()`。后者在 GPU 已激活时为空；可返回 `disabled`、`backend_unavailable`、`pending_activation`，或具体功能原因：`canvas`、`custom_shader`、`collision`、`force_fields`、`sub_emitters`、`particle_lights`、`curves`、`sorting`、`ribbon`、`lit_material`。`isGpuFeatureSetSupported()` 只检查当前功能组合，不把机器是否支持 Vulkan resident 后端混在一起。

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

`setGpuSimulation(true)` 或 JSON `"gpuSimulation": true` 会请求常驻 GPU 后端。首次可提交帧把当前新生粒子上传到后端拥有的多帧 SSBO；之后更新、死亡剔除、存活压缩和 `VkDrawIndirectCommand` 都在同一帧命令缓冲内完成。渲染直接读取压缩后的 SSBO，不逐帧回读粒子状态，也不为每个发射器单独提交或等待队列。CPU 只维护确定性的发射调度、预算和寿命数量估计。

```squirrel
local embers = particles.newEmitter(20000);  // 上万粒子场景
embers.setGpuSimulation(true);
embers.setGravity(0, 30);
embers.start();
```

调用 `isGpuSimulationActive()` 可区分“资产请求 GPU”与“本帧已经迁移到 GPU”。`particles.getLastGpuResidentEmitters()` 和 `particles.getLastGpuResidentParticles()` 可用于性能 HUD 和自动质量伸缩。后者是 CPU 侧精确寿命估计；后端的存活、生成、死亡、丢弃和间接实例计数采用帧槽延迟读数，不会阻塞当前帧。

当前常驻 GPU 后端覆盖基础点/线/矩形/椭圆发射、重力、线性/径向/切向加速度、阻尼、限速、噪声、本地/世界空间、旋转、尺寸和起止颜色、flipbook、拉伸与常用混合模式。需要玩法回调或逐粒子 CPU 状态的功能会自动保留在确定性 CPU 后端，包括碰撞、力场、子发射器、粒子灯光、自定义 shader/canvas，以及自定义速度/尺寸/旋转曲线和多段颜色渐变。没有可用图形后端时也安全回退 CPU。

着色器源位于 `graphics/shaders/particle_resident.*`；修改后运行 `python scripts/compile_particle_gpu_shaders.py` 更新随引擎编译的 SPIR-V 与 include 文件。

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

- `addBurst()`、`addColorStop()`、`addForceField()`、`addRotationCurvePoint()`、`addSizeCurvePoint()`、`addSubEmitter()`、`addVelocityCurvePoint()`、`applyConfig()`、`applyPreset()`、`attachToBone()`、`attachToBoneByName()`、`attachToSkeleton2D()`、`attachToSkeleton3D()`、`attachToSpineBone()`、`attachToSpineBoneByName()`、`clearBursts()`、`clearColorGradient()`、`clearForceFields()`、`clearRotationCurve()`、`clearSizeCurve()`、`clearSkinSource()`、`clearSubEmitters()`、`clearVelocityCurve()`、`detach()`、`emit()`、`emitFromSkin()`、`getAttachBone()`、`getAttachKind()`、`getAutoRandomSeed()`、`getAutoReload()`、`getBlendMode()`、`getBufferSize()`、`getConfigPath()`、`getCount()`、`getDirection()`、`getEmissionRateOverDistance()`、`getFixedTimeStep()`、`getGpuSimulation()`、`getLightsEnabled()`、`getLooping()`、`getPlaybackSpeed()`、`getPrewarmSeconds()`、`getRandomSeed()`、`getShader()`
- `getEmissionAreaType()`、`getEmissionAreaX()`、`getEmissionAreaY()`、`getEmissionRate()`、`getEmitterCount()`、`getEmitterLifetime()`、`getGpuFallbackReason()`、`getLayer()`、`getName()`、`getPriority()`、`getMinimumQuality()`、`getCullingMode()`、`getCullDistance()`、`getMaxSpawnPerFrame()`、`getMaterialMode()`、`getNormalTexture()`、`getSimulationBackend()`
- `getMaxParticles()`、`getMaxSimulatedEmitters()`、`getQualityLevel()`、`getLastSimulatedEmitters()`、`getLastCulledEmitters()`、`getLastBudgetSkippedEmitters()`、`getLastParticleCount()`、`getLastSpawnedParticles()`、`getLastDroppedSpawns()`、`getLastRenderedParticles()`、`getLastSimulationMs()`、`getLastRenderMs()`
- `getParticleHeight()`、`getParticleLifetimeMax()`、`getParticleLifetimeMin()`、`getParticleWidth()`、`getSizeVariation()`、`getSpread()`、`getX()`、`getY()`
- `hasSkinSource()`、`isActive()`、`isAttached()`、`isGpuFeatureSetSupported()`、`isPaused()`、`isStopped()`、`isVisible()`、`loadConfig()`、`moveTo()`、`newEmitter()`、`newEmitterFromFile()`
- `pause()`、`pollConfigs()`、`reloadConfig()`、`render()`、`reset()`、`setAttachOffset()`、`setAttachPlane()`、`setAttachScale()`、`setAutoReload()`、`setCamera()`、`setCanvas()`
- `setAutoRandomSeed()`、`setBlendMode()`、`setBudget()`、`setCollision()`、`setCollisionBounds()`、`setColorEnd()`、`setColorStart()`、`setCullingMode()`、`setCullDistance()`、`setDamping()`、`setDirection()`、`setEmissionArea()`、`setEmissionRate()`、`setEmissionRateOverDistance()`、`setEmitterLife()`、`setEmitterLifetime()`、`setEmitterTime()`、`setFixedTimeStep()`、`setFlipbook()`、`setGpuSimulation()`、`setGravity()`、`setInheritVelocity()`、`setLights()`、`setLimitVelocity()`、`setLooping()`、`setMaterialMode()`、`setMaxDeltaTime()`、`setMaxSpawnPerFrame()`、`setMinimumQuality()`、`setNoise()`、`setNormalTexture()`、`setOverflowMode()`、`setPlaybackSpeed()`、`setPrewarm()`、`setPriority()`、`setQualityLevel()`、`setRandomSeed()`、`setRenderMode()`、`setShader()`、`setSimulationSpace()`、`setWorldCollision()`
- `setFollowBoneRotation()`、`setLayer()`、`setLinearAcceleration()`、`setParticleLife()`、`setParticleLifetime()`、`setParticleSize()`、`setPosition()`、`setRadialAcceleration()`、`setSizeVariation()`
- `setSizes()`、`setSkinBoneFilter()`、`setSkinBoneFilterByName()`、`setSkinPlane()`、`setSkinScale()`、`setSkinSource()`、`setSpeed()`、`setSpin()`、`setSpread()`、`setStartRotation()`、`setTangentialAcceleration()`、`setTexture()`、`setVisible()`、`start()`
- `stop()`、`syncAttach()`、`update()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/particles/`](../../../src/modules/particles/)
**相关测试：** [`test/particles.cpp`](../../../test/particles.cpp)、[`test/particles_attach_skin.cpp`](../../../test/particles_attach_skin.cpp)、[`test/particles_dynamic_bones.cpp`](../../../test/particles_dynamic_bones.cpp)、[`test/particles_attach_more.cpp`](../../../test/particles_attach_more.cpp)、[`test/particles_attach_extra.cpp`](../../../test/particles_attach_extra.cpp)。
