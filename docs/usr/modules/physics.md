# 物理模块（Box2D + Box3D + 布料 + 流体）

**脚本入口：** `eve.Physics()`

创建 World、Body、Fixture（Box2D 2D 刚体）、World3D / Body3D / Shape3D（Box3D 3D 刚体），以及可交互的 `Cloth`（Verlet 布料）与 `Fluid`（2D 粒子流体）。2D 脚本使用像素坐标并按 meter 换算；3D 使用米（Box3D 原生单位）；布料/流体直接在像素空间模拟。

## 基本用法

### 刚体（Box2D，2D）

```squirrel
local physics = eve.Physics();
physics.setMeter(30.0);
local world = physics.newWorld(0, 980, true);
local body = world.newBody("dynamic", 100, 60);
body.newRectangleFixture(32, 32, 1.0, 0.3, 0.1);
world.update(dt);
```

### 刚体（Box3D，3D）

```squirrel
local physics = eve.Physics();
local world3 = physics.newWorld3D(0, -9.8, 0, true); // 米 / s²，+Y 向上
local ground = world3.newBody("static", 0, -1, 0);
ground.newBoxShape(20, 2, 20, 0.0, 0.5, 0.0);       // 全尺寸宽高深（米）
local box = world3.newBody("dynamic", 0, 4, 0);
box.newBoxShape(1, 1, 1, 1.0, 0.3, 0.1);
box.newSphereShape(0.5, 1.0);                        // 或球体
box.newCapsuleShape(1.0, 0.25, 1.0);                 // 或胶囊（沿局部 Y）
world3.update(dt);
// 姿态用四元数：box.setRotation(qx, qy, qz, qw); box.getRotX/Y/Z/W()
```

### 布料

```squirrel
local cloth = physics.newCloth(18, 12, 14.0, 48.0, 36.0); // cols, rows, spacing, origin
cloth.setGravity(0, 980);
cloth.setBounds(0, 0, 800, 600);
cloth.setStiffness(0.9);
// 鼠标拖拽
local idx = cloth.grabAt(mx, my, 28.0);
if (idx >= 0) cloth.moveGrab(mx, my);
else cloth.releaseGrab();
cloth.update(dt);
cloth.draw(gfx);
```

### 流体

```squirrel
local fluid = physics.newFluid(600);
fluid.setBounds(400, 48, 360, 500);
fluid.setGravity(0, 980);
fluid.emit(520, 100, 120, 0, 40);          // x, y, count, vx, vy
fluid.interactAt(mx, my, 70.0, -4200.0);   // 负值排斥，正值吸引
fluid.update(dt);
fluid.draw(gfx);
```

示例：[`examples/softbody/`](../../../examples/softbody/)

## 对象关系与调用时机

`Physics` 保存 2D 像素/米比例并创建 World / World3D / Cloth / Fluid。World 管理 Body；Body 管理 Fixture。World3D 管理 Body3D；Body3D 管理 Shape3D。布料与流体独立于刚体世界，可同屏并存。2D 碰撞消息为 `begincontact` / `endcontact`；3D 为 `begincontact3d` / `endcontact3d`（参数均为 body id）。查询 API（`rayCast`、`queryAABB`）结果对应**上一次查询**写入的缓存。

每帧：`world.update` / `world3.update` / `cloth.update` / `fluid.update` 各调用一次；绘制在 `eve_render` 中用 `draw` / `drawDebug`。

## 目标导向指南

### 创建会落地的角色（2D）

1. 用 `setMeter(30)` 定义像素与米的比例。
2. 创建向下重力的 World。
3. 地面用 `static` Body，角色用 `dynamic` Body。
4. 给 Body 创建矩形或圆形 Fixture，配置密度、摩擦和弹性。
5. 每帧只调用一次 `world.update(dt)`，渲染位置从 `body.getX/Y()` 读取。

### 创建会落地的物体（3D）

1. `newWorld3D(0, -9.8, 0)`：坐标与重力均以米为单位。
2. 地面 `static` + `newBoxShape`；动态体用 box / sphere / capsule。
3. 每帧 `world3.update(dt)`；位置 `getX/Y/Z`，姿态四元数 `getRotX/Y/Z/W`。
4. 拾取：`shape.testPoint(x,y,z)`、`world3.rayCast`、`world3.queryAABB(min…, max…)`。

### 制作触发区域

把 Fixture / Shape3D 设为 sensor，监听 `begincontact` / `endcontact`（2D）或 `begincontact3d` / `endcontact3d`（3D）；sensor 不产生碰撞推力，适合传送门、拾取物和区域检测。调试时调用 `world.drawDebug(gfx)` 核对 2D 形状。

### 物理体拾取与视线

- `fixture.testPoint(x, y)` / `shape3.testPoint(x, y, z)` — 点是否在形状内
- `world.rayCast` / `world3.rayCast` — 段上最近命中，返回 body id（未命中 `-1`）
- `world.queryAABB(x, y, w, h)` — 2D 轴对齐盒（原点+宽高）
- `world3.queryAABB(minX, minY, minZ, maxX, maxY, maxZ)` — 3D AABB（角点）

### 可交互布料

1. `newCloth(cols, rows, spacing, originX, originY)` — 默认钉住顶行。
2. `setBounds` 限制摆动范围；`applyForce` 可作风场。
3. `grabAt` / `moveGrab` / `releaseGrab` 做鼠标拖拽；`pin` / `unpin` 控制固定点。
4. `draw(gfx)` 绘制约束网与质点。

### 可交互流体

1. `newFluid(capacity)` 后 `setBounds` 建容器。
2. `emit` 喷射粒子；`interactAt` 做吸引/排斥搅拌。
3. 可调 `setSmoothingRadius` / `setRestDensity` / `setPressureStiffness` / `setViscosity`。
4. `draw(gfx)` 按密度着色绘制粒子。

## 常见问题

- 每帧改变 meter：会破坏 2D 单位一致性，应启动时设置一次；3D 世界不受 `setMeter` 影响。
- 直接把 Body 坐标当左上角：Box2D / Box3D Body 通常表示形状中心。
- 更新 World 多次或完全不更新：每个逻辑帧一次。
- `rayCast` / `queryAABB` 结果会被下次同类查询覆盖；需要时立刻读出。
- 布料/流体粒子过多会占 CPU：演示规模建议布料 ≤ 20×15、流体 ≤ 600。
- 布料与流体**不会**自动与刚体碰撞；需要时用 bounds / 采样位置自行耦合。
- 3D 物体尺寸建议贴近真实尺度（约 0.1–10 m）；过大过小会降低求解稳定性。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`World3D`、`Body3D`、`Cloth`、`Fluid`）的方法也列在这里。

- `applyAngularImpulse()`、`applyForce()`、`applyForceAt()`、`applyLinearImpulse()`、`clear()`、`clearBounds()`、`destroy()`、`destroyBody()`
- `draw()`、`drawDebug()`、`emit()`、`getAngle()`、`getAngularVelocity()`、`getAngularVelocityX()`、`getAngularVelocityY()`、`getAngularVelocityZ()`、`getBody()`、`getCapacity()`、`getCols()`
- `getDamping()`、`getDensity()`、`getFriction()`、`getGrabIndex()`、`getGravityX()`、`getGravityY()`、`getGravityZ()`、`getId()`、`getIterations()`
- `getLinearVelocityX()`、`getLinearVelocityY()`、`getLinearVelocityZ()`、`getMeter()`、`getName()`、`getNearPressureStiffness()`、`getParticleCount()`、`getParticleSize()`、`getParticleVx()`
- `getParticleVy()`、`getParticleX()`、`getParticleY()`、`getPressureStiffness()`、`getQueryBodyId()`、`getQueryCount()`、`getRayHitBodyId()`、`getRayHitFraction()`
- `getRayHitNormalX()`、`getRayHitNormalY()`、`getRayHitNormalZ()`、`getRayHitX()`、`getRayHitY()`、`getRayHitZ()`、`getRestDensity()`、`getRestitution()`、`getRotW()`、`getRotX()`、`getRotY()`、`getRotZ()`、`getRows()`、`getSmoothingRadius()`
- `getSpacing()`、`getStiffness()`、`getType()`、`getViscosity()`、`getX()`、`getY()`、`getZ()`、`grabAt()`、`hasRayHit()`
- `interactAt()`、`isActive()`、`isAwake()`、`isBullet()`、`isFixedRotation()`、`isGrabbing()`、`isPinned()`、`isSensor()`
- `moveGrab()`、`newBody()`、`newBoxShape()`、`newCapsuleShape()`、`newCircleFixture()`、`newCloth()`、`newFluid()`、`newRectangleFixture()`、`newSphereShape()`、`newWorld()`、`newWorld3D()`、`pin()`
- `pinTopRow()`、`queryAABB()`、`rayCast()`、`releaseGrab()`、`setActive()`、`setAngle()`、`setAngularVelocity()`、`setAwake()`
- `setBounds()`、`setBullet()`、`setColor()`、`setDamping()`、`setDensity()`、`setFixedRotation()`、`setFriction()`、`setGravity()`
- `setIterations()`、`setLinearVelocity()`、`setMeter()`、`setNearPressureStiffness()`、`setParticlePosition()`、`setParticleSize()`、`setPosition()`、`setPressureStiffness()`
- `setRestDensity()`、`setRestitution()`、`setRotation()`、`setSensor()`、`setSmoothingRadius()`、`setStiffness()`、`setType()`、`setViscosity()`、`testPoint()`
- `unpin()`、`update()`、`updateFull()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/physics/`](../../../src/modules/physics/)
**相关测试：** [`test/box2d.cpp`](../../../test/box2d.cpp)、[`test/box3d.cpp`](../../../test/box3d.cpp)、[`test/softbody.cpp`](../../../test/softbody.cpp)
