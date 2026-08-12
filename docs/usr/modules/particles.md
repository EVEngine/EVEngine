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

- `applyConfig()`、`applyPreset()`、`attachToBone()`、`attachToBoneByName()`、`attachToSkeleton2D()`、`attachToSkeleton3D()`、`attachToSpineBone()`、`attachToSpineBoneByName()`、`clearSkinSource()`、`detach()`、`emit()`、`emitFromSkin()`、`getAttachBone()`、`getAttachKind()`、`getAutoReload()`、`getBufferSize()`、`getConfigPath()`、`getCount()`、`getDirection()`
- `getEmissionAreaType()`、`getEmissionAreaX()`、`getEmissionAreaY()`、`getEmissionRate()`、`getEmitterCount()`、`getEmitterLifetime()`、`getLayer()`、`getName()`
- `getParticleHeight()`、`getParticleLifetimeMax()`、`getParticleLifetimeMin()`、`getParticleWidth()`、`getSizeVariation()`、`getSpread()`、`getX()`、`getY()`
- `hasSkinSource()`、`isActive()`、`isAttached()`、`isPaused()`、`isStopped()`、`isVisible()`、`loadConfig()`、`moveTo()`、`newEmitter()`、`newEmitterFromFile()`
- `pause()`、`pollConfigs()`、`reloadConfig()`、`render()`、`reset()`、`setAttachOffset()`、`setAttachPlane()`、`setAttachScale()`、`setAutoReload()`、`setCamera()`、`setCanvas()`
- `setColorEnd()`、`setColorStart()`、`setDirection()`、`setEmissionArea()`、`setEmissionRate()`、`setEmitterLife()`、`setEmitterLifetime()`、`setEmitterTime()`
- `setFollowBoneRotation()`、`setLayer()`、`setLinearAcceleration()`、`setParticleLife()`、`setParticleLifetime()`、`setParticleSize()`、`setPosition()`、`setRadialAcceleration()`、`setSizeVariation()`
- `setSizes()`、`setSkinBoneFilter()`、`setSkinBoneFilterByName()`、`setSkinPlane()`、`setSkinScale()`、`setSkinSource()`、`setSpeed()`、`setSpin()`、`setSpread()`、`setTangentialAcceleration()`、`setTexture()`、`setVisible()`、`start()`
- `stop()`、`syncAttach()`、`update()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/particles/`](../../../src/modules/particles/)
**相关测试：** [`test/particles.cpp`](../../../test/particles.cpp)、[`test/particles_attach_skin.cpp`](../../../test/particles_attach_skin.cpp)、[`test/particles_dynamic_bones.cpp`](../../../test/particles_dynamic_bones.cpp)。
