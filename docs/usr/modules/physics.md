# 物理模块（Box2D + Box3D + 布料 + 流体）

**脚本入口：** `eve.Physics()`

创建 World、Body、Fixture（Box2D 2D 刚体）、World3D / Body3D / Shape3D（Box3D 3D 刚体），以及可交互的 `Cloth`（2D Verlet 布料）、`Cloth3D`（3D Verlet 布料）与 `Fluid`（2D 粒子流体）。2D 脚本使用像素坐标并按 meter 换算；3D 使用米（Box3D 原生单位）；2D 布料/流体在像素空间模拟，3D 布料在米制空间模拟。

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

### 布料（2D，像素空间）

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

2D 布料默认开启自碰撞（非相邻粒子保持至少 2×`particleSize` 距离），并带折角限制（`setMaxFoldAngle`，默认 90°）防止过锐褶皱。`setCollideWorld(world)` 可让自由粒子与 Box2D 非 sensor fixture 碰撞；碰撞按粒子/刚体折合质量交换动量，动态刚体越轻被推得越明显（粒子质量用 `setParticleMass` 调节，默认 0.1 kg）。

### 布料 3D（米制空间）

```squirrel
local cloth3 = physics.newCloth3D(16, 12, 0.4, -3.0, 3.2, -2.0); // cols, rows, spacing, origin(X,Y,Z)
cloth3.setGravity(0, -9.8, 0);       // 米 / s²，+Y 向上
cloth3.setCollideWorld(world3);      // 与 Box3D 非 sensor shape 碰撞
cloth3.setParticleSize(0.12);        // 自碰撞半径 / 碰撞厚度
cloth3.setParticleMass(0.1);         // 碰撞动量交换的粒子质量（kg）
cloth3.setMaxFoldAngle(130);         // 相邻三角面最大折叠角（度），默认 120
cloth3.setSelfCollision(true);
cloth3.setBounds(-4, -1, -3, 8, 5.5, 6);  // 原点 + 宽高深
// 鼠标拾取（射线与 y=2 平面求交后）：
// local idx = cloth3.grabAt(x, y, z, 0.5);
// if (idx >= 0) cloth3.moveGrab(x, y, z);
cloth3.applyForce(1.0, 0, 0);         // 均匀风
cloth3.interactAt(x, y, z, 1.1, -14); // 指针场：正吸引 / 负排斥（与 Fluid 相同语义）
cloth3.update(dt);
cloth3.draw(gfx);                     // 需要处于 3D 帧内（gfx.render3D 之后）
```

网格位于 XZ 平面、顶行默认钉住；`pin` / `unpin` / `pinTopRow`、`grabAt` / `moveGrab` / `releaseGrab`、`reset`、`setColor` 等与 2D 布料一致。3D 布料的折角约束基于相邻三角面的二面角：折叠超过 `maxFoldAngle` 时会绕共享边旋转两个三角面、把折角精确开回极限值，避免布料折成死褶或扭曲过高。3D 自碰撞除粒子级近邻外还带**三角面级**处理：非相邻三角面保持至少 2×`particleSize` 厚度，顶点穿透三角面会被沿面法线推出，布料不会在粒子间隙中互相穿过。

示例：[`examples/softbody3d/`](../../../examples/softbody3d/)（窗帘 + 平台/球体碰撞 + 自碰撞 + 折角限制）

### 布料 GPU（2D，compute shader 加速）

`newClothGPU(cols, rows, spacing, originX, originY)` 创建与 `Cloth` 同接口的 GPU 布料：Verlet 积分和距离约束全部跑在 Vulkan compute shader 里（每粒子一个线程 + 双缓冲 Jacobi 约束迭代），每帧回读位置用于绘制。适合大批量粒子：

```squirrel
local clothG = physics.newClothGPU(40, 30, 8.0, 40.0, 30.0); // 1200 粒子
clothG.setGravity(0, 980);
clothG.setBounds(0, 0, 800, 600);
clothG.setStiffness(0.9);
clothG.setIterations(4);
clothG.setSelfCollision(true);   // GPU 粒子级自碰撞（非相邻粒子 ≥ 2×particleSize）
clothG.setColor(0.6, 0.9, 0.7, 1.0);
clothG.update(dt);
clothG.draw(gfx);
```

接口与 2D `Cloth` 对齐（`setGravity` / `setStiffness` / `setIterations` / `setDamping` / `setParticleSize` / `setSelfCollision` / `setBounds` / `pin` / `unpin` / `pinTopRow` / `applyForce` / `interactAt` / `setColor` / `reset`）。自碰撞在 GPU 上做：设置了 `setBounds` 时用**空间哈希**（`atomicExchange` 每 cell 链表、无溢出，3×3 邻居遍历），可支持数万粒子；没有 bounds 时回退 O(n²) 扫描。默认关闭。约束求解是**双缓冲 Jacobi**：每次 dispatch 一 pass、外层按 `setIterations` 重复收敛，大网格需要更多迭代（如 100×80 建议 16–24）。注意粒子级自碰撞在极端压缩（如全部粒子被挤进角落）时无法保证完全分离——这是位置修正类自碰撞的共性限制；没有折角限制（需要时用 CPU 版）。每帧的积分 + 约束 + 哈希 + 回读通过 `Gpgpu.Sequence` 合成**一次 GPU 提交**（Sequence 自动在 dispatch 间插内存屏障，双缓冲因此正确）。需要 Gpgpu 模块和可用的 compute 后端；不可用时 `newClothGPU` 抛异常。

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

## 对象关系与调用时机

示例：[`examples/softbody/`](../../../examples/softbody/)（2D 布料 + 流体）、[`examples/softbody3d/`](../../../examples/softbody3d/)（3D 布料）

`Physics` 保存 2D 像素/米比例并创建 World / World3D / Cloth / Cloth3D / Fluid。World 管理 Body；Body 管理 Fixture。World3D 管理 Body3D；Body3D 管理 Shape3D。布料与流体独立于刚体世界，可同屏并存；`Cloth.setCollideWorld(world)` / `Cloth3D.setCollideWorld(world3)` 可将布料接到刚体世界做碰撞。2D 碰撞消息为 `begincontact` / `endcontact`；3D 为 `begincontact3d` / `endcontact3d`（参数均为 body id）。查询 API（`rayCast`、`queryAABB`）结果对应**上一次查询**写入的缓存。

每帧：`world.update` / `world3.update` / `cloth.update` / `cloth3.update` / `fluid.update` 各调用一次；绘制在 `eve_render` 中用 `draw` / `drawDebug`。

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

### 可交互布料（2D / 3D）

1. `newCloth(cols, rows, spacing, originX, originY)`（2D 像素）或 `newCloth3D(cols, rows, spacing, originX, originY, originZ)`（3D 米）— 默认钉住顶行。
2. `setBounds` 限制摆动范围；`applyForce` 可作风场。
3. `grabAt` / `moveGrab` / `releaseGrab` 做鼠标拖拽；`pin` / `unpin` 控制固定点。
4. `setSelfCollision(true)` 开启自碰撞（3D 含三角面级）；`setMaxFoldAngle` / `setFoldStiffness` 控制折角限制（3D 为相邻三角面二面角）。
5. `setCollideWorld(world)` / `setCollideWorld(world3)` 让布料与刚体碰撞（按质量比例动量交换）；`interactAt` 做指针吸引/排斥（与流体一致）。
6. `draw(gfx)` 绘制约束网与质点（2D）或三角网格（3D，需在 `gfx.render3D()` 之后调用）。

3D 版把 2D 接口按维度推广：`setGravity(gx,gy,gz)`、`setBounds(x,y,z,w,h,d)`、`getParticleX/Y/Z`、`grabAt(x,y,z,r)`、`applyForce(fx,fy,fz)`。

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
- 布料默认独立于刚体世界；需要碰撞时调用 `setCollideWorld`（2D 接 `World`，3D 接 `World3D`）。流体仍用 bounds / 采样位置自行耦合。
- 2D 自碰撞是粒子级近似：保证非相邻粒子间距 ≥ 2×`particleSize`；3D 另有三角面级修正（厚度同为 2×`particleSize`）。`particleSize` 调小可让布料更薄。
- 3D 布料网格为双面渲染：正/背面各一份顶点，背面法线翻转、winding 反向，两侧光照都正确。
- 3D 物体尺寸建议贴近真实尺度（约 0.1–10 m）；过大过小会降低求解稳定性。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`World3D`、`Body3D`、`Cloth`、`Cloth3D`、`Fluid`）的方法也列在这里。

- `applyAngularImpulse()`、`applyForce()`、`applyForceAt()`、`applyLinearImpulse()`、`clear()`、`clearBounds()`、`destroy()`、`destroyBody()`
- `draw()`、`drawDebug()`、`emit()`、`getAngle()`、`getAngularVelocity()`、`getAngularVelocityX()`、`getAngularVelocityY()`、`getAngularVelocityZ()`、`getBody()`、`getCapacity()`、`getCols()`
- `getDamping()`、`getDensity()`、`getFriction()`、`getGrabIndex()`、`getGravityX()`、`getGravityY()`、`getGravityZ()`、`getId()`、`getIterations()`
- `getLinearVelocityX()`、`getLinearVelocityY()`、`getLinearVelocityZ()`、`getMass()`、`getMeter()`、`getName()`、`getNearPressureStiffness()`、`getParticleCount()`、`getParticleMass()`、`getParticleSize()`、`getParticleVx()`
- `getParticleVy()`、`getParticleVx()`、`getParticleX()`、`getParticleY()`、`getParticleZ()`、`getPressureStiffness()`、`getQueryBodyId()`、`getQueryCount()`、`getRayHitBodyId()`、`getRayHitFraction()`
- `getRayHitNormalX()`、`getRayHitNormalY()`、`getRayHitNormalZ()`、`getRayHitX()`、`getRayHitY()`、`getRayHitZ()`、`getRestDensity()`、`getRestitution()`、`getRotW()`、`getRotX()`、`getRotY()`、`getRotZ()`、`getRows()`、`getSmoothingRadius()`
- `getSpacing()`、`getStiffness()`、`getType()`、`getViscosity()`、`getX()`、`getY()`、`getZ()`、`grabAt()`、`hasRayHit()`
- `interactAt()`、`isActive()`、`isAwake()`、`isBullet()`、`isFixedRotation()`、`isGrabbing()`、`isPinned()`、`isSensor()`
- `moveGrab()`、`newBody()`、`newBoxShape()`、`newCapsuleShape()`、`newCircleFixture()`、`newCloth()`、`newCloth3D()`、`newClothGPU()`、`newFluid()`、`newRectangleFixture()`、`newSphereShape()`、`newWorld()`、`newWorld3D()`、`pin()`
- `pinTopRow()`、`queryAABB()`、`rayCast()`、`releaseGrab()`、`reset()`、`setActive()`、`setAngle()`、`setAngularVelocity()`、`setAwake()`
- `setBounds()`、`setBullet()`、`setCollideWorld()`、`setColor()`、`setDamping()`、`setDensity()`、`setFixedRotation()`、`setFoldStiffness()`、`setFriction()`、`setGravity()`
- `setIterations()`、`setLinearVelocity()`、`setMeter()`、`setMaxFoldAngle()`、`setNearPressureStiffness()`、`setParticleMass()`、`setParticlePosition()`、`setParticleSize()`、`setPosition()`、`setPressureStiffness()`
- `setRestDensity()`、`setRestitution()`、`setRotation()`、`setSensor()`、`setSmoothingRadius()`、`setStiffness()`、`setType()`、`setViscosity()`、`testPoint()`
- `setSelfCollision()`、`unpin()`、`update()`、`updateFull()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/physics/`](../../../src/modules/physics/)
**相关测试：** [`test/box2d.cpp`](../../../test/box2d.cpp)、[`test/box3d.cpp`](../../../test/box3d.cpp)、[`test/softbody.cpp`](../../../test/softbody.cpp)
