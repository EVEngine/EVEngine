# Box2D 物理模块

**脚本入口：** `eve.Physics()`

创建 World、Body 和 Fixture；脚本使用像素坐标，引擎内部换算为米。

## 基本用法

```squirrel
local physics = eve.Physics();
physics.setMeter(30.0);
local world = physics.newWorld(0, 980, true);
local body = world.newBody("dynamic", 100, 60);
body.newRectangleFixture(32, 32, 1.0, 0.3, 0.1);
world.update(dt);
```

## 对象关系与调用时机

`Physics` 保存像素/米比例并创建 World；World 管理 Body；Body 管理 Fixture。World 是模拟所有者，销毁应从 World 入口执行。碰撞消息经 Event 模块发出。

## 目标导向指南

### 创建会落地的角色

1. 用 `setMeter(30)` 定义像素与米的比例。
2. 创建向下重力的 World。
3. 地面用 `static` Body，角色用 `dynamic` Body。
4. 给 Body 创建矩形或圆形 Fixture，配置密度、摩擦和弹性。
5. 每帧只调用一次 `world.update(dt)`，渲染位置从 `body.getX/Y()` 读取。

### 制作触发区域

把 Fixture 设为 sensor，监听 `begincontact` / `endcontact` 事件；sensor 不产生碰撞推力，适合传送门、拾取物和区域检测。调试时调用 `world.drawDebug(gfx)` 核对形状。

## 常见问题

- 每帧改变 meter：会破坏单位一致性，应启动时设置一次。
- 直接把 Body 坐标当左上角：Box2D Body 通常表示形状中心。
- 更新 World 多次或完全不更新：每个逻辑帧一次。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `applyAngularImpulse()`、`applyForce()`、`applyForceAt()`、`applyLinearImpulse()`、`destroy()`、`destroyBody()`、`drawDebug()`、`getAngle()`
- `getAngularVelocity()`、`getBody()`、`getDensity()`、`getFriction()`、`getGravityX()`、`getGravityY()`、`getId()`、`getLinearVelocityX()`
- `getLinearVelocityY()`、`getMeter()`、`getName()`、`getRestitution()`、`getType()`、`getX()`、`getY()`、`isActive()`
- `isAwake()`、`isBullet()`、`isFixedRotation()`、`isSensor()`、`newBody()`、`newCircleFixture()`、`newRectangleFixture()`、`newWorld()`
- `setActive()`、`setAngle()`、`setAngularVelocity()`、`setAwake()`、`setBullet()`、`setDensity()`、`setFixedRotation()`、`setFriction()`
- `setGravity()`、`setLinearVelocity()`、`setMeter()`、`setPosition()`、`setRestitution()`、`setSensor()`、`setType()`、`update()`
- `updateFull()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/physics/`](../../../src/modules/physics/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `physics`。
