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

## 对象关系与调用时机

`Particles` 管理 Emitter 及配置、模拟、渲染系统；Emitter 持有容量、发射配置与运行状态。模块 update 统一推进所有 emitter，render 按 layer 提交。

## 目标导向指南

### 从 JSON 创建火焰

配置 buffer、发射率、寿命、速度、颜色和 `autoReload`，调用 `newEmitterFromFile()`；设置位置后 `start()`。模块统一 `update(dt)` 和 `render(gfx)`，无需逐粒子操作。

### 制作一次性爆炸

创建容量足够的 emitter，设置有限 emitter life 和较高瞬时发射率，停止循环；播放结束后检查 active 状态并回收。预览时用 preset 起步，再逐项覆盖参数。

## 常见问题

- buffer 太小导致高发射率粒子被覆盖。
- 只 render 不 update，粒子静止。
- 无限 emitter 离开场景后未 stop/回收。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `applyConfig()`、`applyPreset()`、`emit()`、`getAutoReload()`、`getBufferSize()`、`getConfigPath()`、`getCount()`、`getDirection()`
- `getEmissionAreaType()`、`getEmissionAreaX()`、`getEmissionAreaY()`、`getEmissionRate()`、`getEmitterCount()`、`getEmitterLifetime()`、`getLayer()`、`getName()`
- `getParticleHeight()`、`getParticleLifetimeMax()`、`getParticleLifetimeMin()`、`getParticleWidth()`、`getSizeVariation()`、`getSpread()`、`getX()`、`getY()`
- `isActive()`、`isPaused()`、`isStopped()`、`isVisible()`、`loadConfig()`、`moveTo()`、`newEmitter()`、`newEmitterFromFile()`
- `pause()`、`pollConfigs()`、`reloadConfig()`、`render()`、`reset()`、`setAutoReload()`、`setCamera()`、`setCanvas()`
- `setColorEnd()`、`setColorStart()`、`setDirection()`、`setEmissionArea()`、`setEmissionRate()`、`setEmitterLife()`、`setEmitterLifetime()`、`setEmitterTime()`
- `setLayer()`、`setLinearAcceleration()`、`setParticleLife()`、`setParticleLifetime()`、`setParticleSize()`、`setPosition()`、`setRadialAcceleration()`、`setSizeVariation()`
- `setSizes()`、`setSpeed()`、`setSpin()`、`setSpread()`、`setTangentialAcceleration()`、`setTexture()`、`setVisible()`、`start()`
- `stop()`、`update()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/particles/`](../../../src/modules/particles/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `particles`。
