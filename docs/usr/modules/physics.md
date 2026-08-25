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

// 精确形状重叠查询，结果通过 getQueryCount/getQueryBodyId 读取。
world3.querySphere(0, 1, 0, 0.5);
world3.queryCapsule(0, 0.5, 0, 0, 1.5, 0, 0.3); // 两端球心 + 半径
world3.queryBox(0, 1, 0, 2, 1, 3, qx, qy, qz, qw); // 中心、完整尺寸、旋转

// 连续扫描可避免高速物体跨帧穿过薄墙；返回最早命中的 body id。
local hit = world3.castCapsule(0, 0.5, 0, 0, 1.5, 0, 0.3, 8, 0, 0);
local boxHit = world3.castBox(0, 1, 0, 2, 1, 3, qx, qy, qz, qw, 8, 0, 0);
if (hit >= 0) {
    local safeFraction = world3.getShapeCastFraction();
    local nx = world3.getShapeCastNormalX();
    local colliderId = world3.getShapeCastShapeId();
    local colliderTag = world3.getShapeCastShapeTag();
}

// 点到当前过滤层中最近碰撞表面的距离、位置和朝向目标点的法线。
local nearest = world3.closestPoint(listenerX, listenerY, listenerZ, 20);
if (nearest >= 0) {
    local distance = world3.getClosestDistance();
    local surfaceX = world3.getClosestX();
    local colliderId = world3.getClosestShapeId();
}
```

### 3D 符号距离场地图

`DistanceField3D` 保存规则网格顶点上的符号距离（负值为实体、正值为空间），适合体素地形、
程序化洞穴和预烘焙距离场。世界坐标查询使用三线性插值；碰撞后可读取最小间隙、接触位置和
由中心差分估计的表面法线。

```squirrel
// 16³ 个网格顶点，间距 0.5m，原点在 (-4,-4,-4)，地图外视为空间。
local sdf = physics.newDistanceField3D(16, 16, 16, 0.5, -4, -4, -4, 1000);
// 批量数组按 x 最快、其次 y、最后 z 的顺序排列；长度必须等于 getSampleCount()。
sdf.setDistances(bakedDistances);
sdf.setDistance(8, 8, 8, -0.5); // 也可局部更新单个样本
// 流式地图只替换变化区块；区域数组同样是 x -> y -> z 顺序。
sdf.setDistanceRegion(chunkX, chunkY, chunkZ, chunkW, chunkH, chunkD, chunkDistances);
local mapRevision = sdf.getRevision(); // 每次成功修改只递增一次，失败保持不变

if (sdf.checkSphere(playerX, playerY, playerZ, 0.4)) {
    local penetration = -sdf.getCollisionDistance();
    local nx = sdf.getNormalX();
    // Surface 是距离场表面投影；ShapeContact 是球/胶囊表面上的对应接触点。
    local terrainY = sdf.getSurfaceY();
    local shapeY = sdf.getShapeContactY();
}

// 端点是胶囊两端半球的球心，可为任意方向。
if (sdf.checkCapsule(ax, ay, az, bx, by, bz, radius)) {
    local hitY = sdf.getCollisionY();
}

// 连续碰撞检测：高速移动也会返回沿 delta 的最早接触点，避免跨帧穿过薄墙。
if (sdf.castCapsule(ax, ay, az, bx, by, bz, radius, dx, dy, dz)) {
    local safeFraction = sdf.getCastFraction();
    local travelDistance = sdf.getCastDistance();
    local startedPenetrating = sdf.didCastStartInside();
}

// 角色移动：返回是否受阻；把最终位移同时应用到胶囊两个端点和角色 Transform。
sdf.setMoverUp(0, 1, 0);
sdf.setMoverSlopeLimit(50); // 最大可站立坡度
sdf.setMoverSkinWidth(0.002); // 与表面保留的小间隔，降低浮点抖动
sdf.setMoverGroundSnap(0.15); // 移动后向下吸附到可行走表面，0 表示关闭
sdf.setMoverStepHeight(0.35); // 自动跨越不高于该值的台阶，0 表示关闭
local blocked = sdf.moveCapsule(ax, ay, az, bx, by, bz, radius, dx, dy, dz);
playerX += sdf.getMoverDeltaX();
playerY += sdf.getMoverDeltaY();
playerZ += sdf.getMoverDeltaZ();
local grounded = sdf.isMoverGrounded();
```

距离场至少为 `2 × 2 × 2`，`cellSize > 0`。`fill(distance)` 可快速重置整个网格；
`getWidth/Height/Depth`、`getCellSize` 和 `getOriginX/Y/Z` 可用于校验地图资源。批量替换会先
校验全部数据，因此无效长度或 NaN/Infinity 不会导致地图只更新一部分。`setDistanceRegion`
和 `fillRegion` 适合按区块流式加载或卸载大型地图，越界区域同样不会产生部分写入。胶囊轴按半个网格单元的最大步长采样，
Sweep Cast 沿运动方向使用四分之一网格单元的最大步长并对首次命中做二分细化。因此中段穿入而
两个端点均在实体外，或物体单帧跨过障碍时也能检出；地图分辨率仍应覆盖最薄的可碰撞特征。

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

`Physics` 保存 2D 像素/米比例并创建 World / World3D / DistanceField3D / Cloth /
Cloth3D / Fluid。World 管理 Body 与 Fixture；World3D 管理 Body3D、Shape3D 和 Joint3D。
布料、流体和距离场可按需组合；`Cloth.setCollideWorld(world)` /
`Cloth3D.setCollideWorld(world3)` 可将布料接到刚体世界做碰撞。2D 碰撞消息为
`begincontact` / `endcontact`；3D 普通碰撞为 `begincontact3d` / `endcontact3d`，Sensor
重叠为 `begintrigger3d` / `endtrigger3d`，显著撞击为 `hit3d`。3D 消息的六个整数参数依次是
A/Sensor Body ID、B/Visitor Body ID、A/Sensor Shape ID、B/Visitor Shape ID、两个 Shape Tag；
Hit 的浮点反馈从 World3D 当帧缓冲读取。查询结果对应**上一次查询**写入的缓存。

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

### 精细控制 3D 刚体

`Body3D` 提供逐刚体的线性/角阻尼、重力倍率、睡眠开关和睡眠阈值。角色可用
`setGravityScale(0)` 暂停重力，水下物体可配合较高 `setLinearDamping()`；阻尼和睡眠阈值
必须是有限非负数，重力倍率允许为负。`getMass()` 返回 Shape 密度和体积计算出的千克质量，
`getLocalCenterX/Y/Z()` 与 `getWorldCenterX/Y/Z()` 可用于施力、相机跟随和特效定位。

动态刚体还可用
`setMassProperties(mass, centerX,centerY,centerZ, inertiaXX,inertiaYY,inertiaZZ,
inertiaXY,inertiaXZ,inertiaYZ)` 覆盖自动质量、局部质心和关于质心的对称惯性张量；这适用于
车辆配重、偏心道具以及碰撞外形不等同于质量分布的复合体。质量必须为正，惯性张量必须有限且
正定。六个惯性分量可由 `getInertiaXX/YY/ZZ/XY/XZ/YZ()` 读取；调用
`resetMassProperties()` 会重新按当前 Shape 的密度和几何计算全部质量属性。添加/删除 Shape 或
切换 Body 类型也会触发 Box3D 自动重算，因此应在这些结构变更完成后再设置自定义质量属性。

```squirrel
player.setLinearDamping(3.0);
player.setAngularDamping(6.0);
player.setGravityScale(1.0);
player.setSleepEnabled(false);

// 直立角色：锁定水平外的位移轴按项目需要配置，并禁止所有自转。
player.setMotionLocks(false, false, false, true, true, true);
```

`setMotionLocks(linearX, linearY, linearZ, angularX, angularY, angularZ)` 原子设置六个自由度，
对应状态由 `isLinear*Locked()` / `isAngular*Locked()` 查询；`setFixedRotation(true)` 仍可作为
锁定三个角轴的快捷方式。`applyTorque()` 施加持续一帧的世界空间力矩，
`applyLinearImpulseAt()` 在指定世界坐标施加瞬时冲量，因此可同时改变线速度和角速度。

车辆、炮塔和布娃娃通常在自身坐标系中工作。`localToWorldPoint()` / `worldToLocalPoint()` 转换
位置，`localToWorldVector()` / `worldToLocalVector()` 只旋转方向；它们均返回三元素数组。
`getLocalPointVelocity(localX,localY,localZ)` 与 `getWorldPointVelocity(worldX,worldY,worldZ)`
返回指定刚体点的世界速度，其中包含角速度产生的切向速度，适合轮胎滑移、接触音效和平台载人。

`applyLocalForce()`、`applyLocalLinearImpulse()` 的力/冲量与作用点都使用刚体局部坐标；
`applyLocalForceToCenter()`、`applyLocalLinearImpulseToCenter()` 是质心快捷方式，
`applyLocalTorque()` 和 `applyLocalAngularImpulse()` 同样会把局部轴旋转到世界空间。所有新接口
拒绝 NaN 和无穷值，避免无效数进入求解器。

移动平台应使用 `kinematic` Body，并在物理更新前调用
`setTargetTransform(x,y,z,qx,qy,qz,qw,timeStep)`；引擎会计算到达目标所需的线速度和角速度，
从而让接触物体获得正确的平台速度。`timeStep` 必须大于零，四元数会归一化。

### 3D 距离关节与旋转铰链

`world3.newDistanceJoint(bodyA, bodyB, anchorAX,anchorAY,anchorAZ,
anchorBX,anchorBY,anchorBZ, length, collideConnected)` 用两个世界空间锚点创建绳索/拉杆约束。
刚性模式保持固定长度；`setDistanceSpring(true,hertz,damping)` 启用弹簧，
`setDistanceLimits()` 设置伸缩范围，`setDistanceMotor()` 可驱动长度变化。实时长度由
`getDistanceCurrentLength()` 读取。

```squirrel
local rope = world3.newDistanceJoint(ceiling, crate,
    0, 5, 0,       // ceiling 世界锚点
    0, 2, 0,       // crate 世界锚点
    3.0, false);
rope.setDistanceSpring(true, 4.0, 0.7);
rope.setDistanceLimits(true, 2.5, 3.5);
```

`world3.newRevoluteJoint(bodyA,bodyB, anchorX,anchorY,anchorZ, axisX,axisY,axisZ,
collideConnected)` 创建世界空间轴向的门轴/转轴。轴会归一化；零长度或非有限轴会抛出异常。
角度和角速度使用弧度：`setRevoluteLimits()` 配置转角范围，`setRevoluteMotor()` 配置目标转速
及最大扭矩，`setRevoluteSpring()` 可将转轴拉回目标角度。

```squirrel
local hinge = world3.newRevoluteJoint(frame, door, 0, 1, 0, 0, 1, 0, false);
hinge.setRevoluteLimits(true, -1.57, 1.57);
hinge.setRevoluteMotor(true, 1.0, 40.0);
```

`newPrismaticJoint()` 使用同样的世界锚点和世界轴参数创建滑轨。连接体只能沿轴平移且不能
相对旋转；`setPrismaticLimits()` 适合行程边界，`setPrismaticMotor()` 适合电梯、活塞和自动门，
`setPrismaticSpring()` 可制作悬挂或缓冲器。当前位置、速度和电机力分别由
`getPrismaticTranslation()`、`getPrismaticSpeed()`、`getPrismaticMotorForce()` 读取。

```squirrel
local lift = world3.newPrismaticJoint(frame, platform, 0, 0, 0, 0, 1, 0, false);
lift.setPrismaticLimits(true, 0, 6);
lift.setPrismaticMotor(true, 2, 5000);
```

`newSphericalJoint()` 创建球窝/点对点关节，常用于肩、髋和吊挂物。传入的世界轴定义圆锥中心
和扭转轴；`setSphericalConeLimit()` 限制摆角，`setSphericalTwistLimits()` 限制扭转，
`setSphericalMotor()` 用三轴角速度和最大扭矩驱动。当前圆锥角与扭转角可实时查询，用于
动画混合或关节受伤判定。

```squirrel
local shoulder = world3.newSphericalJoint(torso, arm, 0, 1.5, 0, 0, 1, 0, false);
shoulder.setSphericalConeLimit(true, 1.0);
shoulder.setSphericalTwistLimits(true, -0.5, 0.5);
```

`newWheelJoint(chassis,wheel, anchor, suspensionAxis, wheelAxis, collideConnected)` 是车辆专用
复合约束。悬挂轴决定轮毂上下行程和转向轴，轮轴决定自转方向；两轴不能平行。
`setWheelSuspension()` 配置弹簧与阻尼，`setWheelSuspensionLimits()` 限制压缩/回弹行程，
`setWheelSpinMotor()` 驱动车轮，`setWheelSteering()` 以弹簧伺服追踪目标转角，
`setWheelSteeringLimits()` 限制最大舵角。

```squirrel
// Y 向悬挂，Z 向轮轴。
local wheel = world3.newWheelJoint(chassis, frontLeft,
    -0.9, -0.4, 1.2,
    0, 1, 0,
    0, 0, 1,
    false);
wheel.setWheelSuspension(true, 5.0, 0.7);
wheel.setWheelSuspensionLimits(true, -0.25, 0.15);
wheel.setWheelSpinMotor(true, 20.0, 800.0);
wheel.setWheelSteering(true, steeringAngle, 8.0, 0.9, 1200.0);
wheel.setWheelSteeringLimits(true, -0.6, 0.6);
```

`getWheelSpinSpeed()` / `getWheelSpinTorque()` 可用于轮胎音效和牵引控制，
`getWheelSteeringAngle()` / `getWheelSteeringTorque()` 可驱动视觉轮毂和反馈方向盘。

所有关节均提供稳定 ID、连接 Body ID、`getConstraintForce*()`、
`getConstraintTorque*()` 和分离误差，适合调试约束、声音与破坏判定。销毁任一连接 Body 或
World 会使 Joint3D 包装器安全失效；也可显式调用 `joint.destroy()`。默认连接体互不碰撞，
可在创建时或通过 `setCollideConnected(true)` 开启。

需要可破坏结构时，用 `setForceThreshold(newtons)` 和
`setTorqueThreshold(newtonMetres)` 配置关节应力事件。任一阈值被超过的物理帧会发送
`jointstress3d`，四个整数参数依次为 Joint ID、Body A ID、Body B ID、关节类型代码
（0 Distance、1 Revolute、2 Prismatic、3 Spherical、4 Wheel）。同一帧的精确求解器力和扭矩从
`getJointStressForceX/Y/Z()`、`getJointStressTorqueX/Y/Z()` 读取；事件缓冲会在下一次
`world3.update()` 前清空。

```squirrel
rope.setForceThreshold(2500);
rope.setTorqueThreshold(1000);

world3.update(dt);
for (local i = 0; i < world3.getJointStressCount(); ++i) {
    local id = world3.getJointStressJointId(i);
    local fx = world3.getJointStressForceX(i);
    local fy = world3.getJointStressForceY(i);
    local fz = world3.getJointStressForceZ(i);
    local force = sqrt(fx * fx + fy * fy + fz * fz);
    // 按 ID 找到游戏侧关节；达到项目断裂强度时调用 joint.destroy()。
}
```

事件本身不会自动销毁关节，游戏可以根据材质、耐久度或短时峰值过滤决定是否断裂，避免
单帧冲击造成不可控破坏。

### 制作触发区域

把 Fixture / Shape3D 设为 sensor。2D 监听 `begincontact` / `endcontact`；3D 监听独立的 `begintrigger3d` / `endtrigger3d`，或逐帧读取 `getBeginTrigger*` / `getEndTrigger*` 缓冲。Sensor 不产生碰撞推力，适合传送门、拾取物和区域检测。Trigger API 明确区分 Sensor 与 Visitor，不依赖底层碰撞器顺序。调试时调用 `world.drawDebug(gfx)` 核对 2D 形状。

### 组合 3D 碰撞体

一个 Body3D 可以挂多个具有独立局部变换的 Shape3D，用于角色头部/躯干/脚底、车辆车身或
武器碰撞区。`setLocalTransform(px, py, pz, qx, qy, qz, qw)` 会原子设置相对 Body 的米制
位移和四元数旋转；也可分别调用 `setLocalPosition()` / `setLocalRotation()`。四元数会自动
归一化，零长度或非有限参数会抛出异常。

```squirrel
local body = world3.newBody("dynamic", 0, 2, 0);
local torso = body.newCapsuleShape(1.2, 0.35);
torso.setTag(1);
torso.setLocalPosition(0, 0.9, 0);

local head = body.newSphereShape(0.3);
head.setTag(2);
head.setLocalPosition(0, 1.8, 0);

local footSensor = body.newBoxShape(0.5, 0.1, 0.5);
footSensor.setTag(3);
footSensor.setLocalPosition(0, 0.05, 0);
footSensor.setSensor(true);
```

局部 Transform 更新会安全重建底层 Box3D 几何，但 wrapper 的稳定 Shape ID、Tag、碰撞过滤、
材质、Sensor 状态和 Hit Event 偏好保持不变；由重建产生的 End Contact/Trigger 仍携带正确
快照。Box 支持完整局部旋转，Capsule 的轴随局部四元数旋转，Sphere 只使用局部平移。

Primitive 尺寸也可在运行时原子修改：Box 使用 `setBoxSize(width, height, depth)`，Sphere 使用
`setSphereRadius(radius)`，Capsule 使用 `setCapsuleSize(height, radius)`；Capsule 的 `height`
是两个半球中心之间的线段长度，因此总高度为 `height + 2 * radius`。`getKind()` 返回
`"box"` / `"sphere"` / `"capsule"` / `"convexHull"`，对应 Primitive 尺寸可通过 `getBoxWidth/Height/Depth()`、
`getRadius()` 和 `getCapsuleHeight()` 读取。错误类型、非有限值、非正尺寸会在修改前抛出异常。

```squirrel
// 蹲伏：保持脚底位置不变，同时缩短角色胶囊。
playerCapsule.setCapsuleSize(0.7, 0.35);
playerCapsule.setLocalPosition(0, 0.35 + 0.35, 0);

// 站起前先用 queryCapsule 检查头顶空间，再一次性恢复尺寸。
if (world3.queryCapsule(ax, ay, az, bx, by, bz, 0.35) == 0) {
    playerCapsule.setCapsuleSize(1.4, 0.35);
}
```

尺寸重建与局部 Transform 使用相同的配置保持规则，并自动重新计算动态 Body 的质量和惯性。
如果需要同时修改尺寸与局部位置以固定脚底，建议先做空间查询，再连续完成两项修改，避免在
物理步进之间留下中间状态。

### 任意凸包碰撞体

`Body3D.newConvexHullShape(vertices, maxVertices)` 从扁平的局部空间 XYZ 数组建立凸包，适合
斜坡、岩石、道具和低多边形动态物体。至少需要四个有限且不共面的点；`maxVertices` 为
`4–254` 的简化预算，常用 `16–64`。输入点不必按面排序，也不需要三角形索引。

```squirrel
local wedge = body.newConvexHullShape([
    -2, -0.5, -2,   2, -0.5, -2,
    -2, -0.5,  2,   2, -0.5,  2,
    -2,  0.5, -2,   2,  0.5, -2
], 32);
wedge.setTag(20);
wedge.setFriction(0.8);
```

运行时可用 `setConvexHullVertices(vertices, maxVertices)` 原子替换源点；退化、非有限或格式错误
的输入会在旧碰撞体被修改前抛出异常。稳定 Shape ID、Tag、材质、Layer、Sensor、Hit Event、
单向配置和局部 Transform 均保持不变。`getConvexHullPointCount()` 返回源点数，
`getConvexHullMaxVertices()` 返回当前预算。

凸包始终填充点集内部，不能表达洞穴、门框或内凹地形。动态凹物体应拆为多个凸包并挂到
同一个 Body3D；大型静态环境使用下一节的 Triangle Mesh Collider。不要把数万渲染顶点直接
交给单个凸包。凸包源点上限为 100000，但实时内容应先离线简化，以控制烘焙和窄相成本。

### 静态三角网格碰撞体

`Body3D.newTriangleMeshShape(vertices, indices)` 创建可表达空洞和内凹结构的静态网格碰撞器。
`vertices` 是局部 XYZ 数组，`indices` 每三个值组成一个三角形；从可碰撞一侧观察，顶点应按
逆时针排列。网格只能挂在 `static` Body 上，创建后该 Body 也不能改为 dynamic/kinematic。

```squirrel
local level = world3.newBody("static", 0, 0, 0);
local floor = level.newTriangleMeshShape([
    -10, 0, -10,   10, 0, -10,
     10, 0,  10,  -10, 0,  10
], [0, 2, 1, 0, 3, 2]); // +Y 一侧可碰撞
```

默认会以 `0.001m` 容差焊接邻近顶点并识别共享边，减少相邻三角形接缝处的幽灵碰撞。
`newTriangleMeshShapeFull(vertices, indices, weld, tolerance, identifyEdges, medianSplit)` 可控制
烘焙：规则地形网格可开启 `medianSplit` 加快 BVH 构建；非规则关卡通常保留 SAH 默认值。
运行时对应 `setTriangleMeshData()` / `setTriangleMeshDataFull()`，并保持稳定 ID、Tag、Layer、
材质、Sensor、Hit Event、单向配置和局部 Transform。局部 Transform 会烘焙到网格顶点。

引擎会在调用 Box3D 前验证数组格式、索引范围、有限坐标、焊接容差和零面积三角形；失败时
旧碰撞器保持不变。`getTriangleMeshVertexCount()` / `getTriangleMeshTriangleCount()` 返回源数据
规模。当前硬上限为 100 万顶点、200 万三角形；生产关卡仍应分块并离线简化，以改善流式加载、
BVH 重建和宽相更新成本。需要移动的复杂物体应使用复合凸包，而不是移动 Triangle Mesh。

### 高度场地形碰撞体

规则地形使用 `newHeightFieldShape(countX, countZ, cellSizeX, cellSizeZ, heights)`。高度按
`z * countX + x` 行优先排列，单位为米；网格从 Body 局部原点向 `+X/+Z` 展开。简单版本自动
从数据求量化范围，完整版本可显式指定共享范围和三角形朝向：

```squirrel
local terrainBody = world3.newBody("static", originX, originY, originZ);
local terrain = terrainBody.newHeightFieldShapeFull(
    257, 257, 1.0, 1.0, heights,
    -256.0, 256.0, false
);
```

相邻地形块应使用完全相同的 `globalMin/globalMax`，这样边界顶点经过 16 位压缩后仍能精确对齐，
避免接缝。所有源高度必须位于该范围内；引擎不会静默裁剪。`clockwise=false` 默认从上方碰撞，
设为 true 会反转可碰撞侧。

`setHeightFieldHeights(values)` 可全量更新，`setHeightFieldRegion(x, z, width, depth, values)`
可更新矩形采样区；修改会原子重建压缩数据，同时保持稳定 Shape ID、Tag、Layer、材质、Sensor、
Hit Event 和单向配置。可通过 `getHeightFieldHeight()`、`getHeightFieldCountX/Z()`、
`getHeightFieldCellSizeX/Z()` 和 `getHeightFieldGlobalMin/Max()` 读取源元数据。

HeightField 只能挂在 static Body 上，定位和旋转使用 Body Transform。Box3D 不提供 HeightField
子形状 Transform，因此该类型的 `setLocalPosition/Rotation/Transform` 只接受单位变换并在其他
输入时报错。每个地形块通常使用独立 Body。区域更新目前会重建整块压缩数据，频繁雕刻应使用
较小分块并把多次编辑合并到一帧；单块上限为 1600 万采样点。

### 单向 3D 平台

`Shape3D.setOneWay(nx, ny, nz, planeOffset, margin, minNormalDot)` 为碰撞器配置局部空间的
单向接触平面。法线指向允许站立的一侧并会自动归一化；`planeOffset` 是平面沿该法线相对
Shape 原点的距离。以中心位于原点、高度为 `0.5m` 的 Box 平台为例，其上表面偏移为 `0.25m`：

```squirrel
local platform = world3.newBody("static", 0, 0, 0).newBoxShape(10, 0.5, 10);
platform.setOneWay(0, 1, 0, 0.25, 0.05, 0.2);

// 暂时恢复双面碰撞；原配置直到再次 setOneWay 前保持在 wrapper 中。
platform.disableOneWay();
```

`margin` 为允许侧的位置容差，通常使用 `0.02–0.08m`；`minNormalDot` 限制接触外法线与平台
法线的夹角，常用 `0.1–0.3`，可避免侧边接触阻挡从下方穿越。局部/Body 旋转和平移后，平面
会在每次物理步进前更新。过滤发生在 Box3D 的线程安全 pre-solve 阶段，不调用脚本。Sensor
期间不产生求解接触，恢复普通 Shape 后单向配置仍生效；`isOneWay()` 可查询状态。

单向规则只影响刚体求解，Ray、Overlap、Closest Point 和 Shape Cast 仍把几何视为双面；游戏
逻辑可根据命中法线和移动方向自行忽略查询结果。极高速运动建议同时使用 Shape Cast 预测，
因为底层连续碰撞的 pre-solve 回调在极端速度下可能暂停运动，而不是可靠穿过平台。

### 物理体拾取与视线

- `fixture.testPoint(x, y)` / `shape3.testPoint(x, y, z)` — 点是否在形状内
- `world.rayCast` / `world3.rayCast` — 段上最近命中，返回 body id（未命中 `-1`）
- `world3.rayCastAll(x1, y1, z1, x2, y2, z2, maxHits)` — 收集指定容量内最近的全部射线命中
- `world.queryAABB(x, y, w, h)` — 2D 轴对齐盒（原点+宽高）
- `world3.queryAABB(minX, minY, minZ, maxX, maxY, maxZ)` — 3D AABB（角点）
- `world3.querySphere(x, y, z, radius)` — 精确球体重叠查询
- `world3.queryCapsule(ax, ay, az, bx, by, bz, radius)` — 精确任意朝向胶囊重叠查询
- `world3.queryBox(x, y, z, width, height, depth, qx, qy, qz, qw)` — 精确有向盒重叠查询
- `world3.castSphere(x, y, z, radius, dx, dy, dz)` — 沿位移连续扫描球体
- `world3.castSphereAll(..., maxHits)` — 有界收集最近的全部球体扫描命中
- `world3.castCapsule(ax, ay, az, bx, by, bz, radius, dx, dy, dz)` — 连续扫描胶囊体
- `world3.castCapsuleAll(..., maxHits)` — 有界收集最近的全部胶囊扫描命中
- `world3.castBox(x, y, z, width, height, depth, qx, qy, qz, qw, dx, dy, dz)` — 连续扫描有向盒
- `world3.castBoxAll(..., maxHits)` — 有界收集最近的全部有向盒扫描命中
- `world3.closestPoint(x, y, z, maxDistance)` — 查询过滤层内最近碰撞表面、法线和距离

形状扫描返回最早命中的 body id；接触点、法线和 `[0,1]` 位移分数分别通过
`getShapeCastX/Y/Z`、`getShapeCastNormalX/Y/Z` 和 `getShapeCastFraction` 读取。
Box3D 形状扫描忽略起始重叠，因此直接使用 cast 组合移动逻辑时，应先用
`querySphere` / `queryCapsule` 处理已经发生的穿透。

三个 `cast*All` 与 `rayCastAll` 使用相同的 `[1,4096]` 容量、近邻保留和确定排序规则。
返回结果通过 `getShapeCastResultCount()`、`getShapeCastResultBodyId()`、
`getShapeCastResultShapeId()`、`getShapeCastResultShapeTag()`、`getShapeCastResultX/Y/Z()`、
`getShapeCastResultNormalX/Y/Z()` 和 `getShapeCastResultFraction()` 读取。旧的单命中 `cast*`
使用容量 1 的同一实现，因此 `getShapeCast*` 始终与结果 index 0 一致。

```squirrel
local count = world3.castCapsuleAll(ax, ay, az, bx, by, bz, radius,
                                    attackDx, attackDy, attackDz, 16);
for (local i = 0; i < count; ++i) {
    local hurtboxTag = world3.getShapeCastResultShapeTag(i);
    local fraction = world3.getShapeCastResultFraction(i);
    // 宽体穿透攻击、相机防穿或预测路径响应。
}
```
重叠查询的返回值和 `getQueryBodyId()` 仍按唯一 Body 统计；当一个 Body 含有多个碰撞器时，
使用 `getQueryShapeCount()` / `getQueryShapeId()` 读取全部命中 Shape。Shape ID 在其生命周期内
稳定，`setSensor()` 导致底层碰撞形状重建时也不会改变。Ray、Shape Cast 和最近点查询分别通过
`getRayHitShapeId()`、`getShapeCastShapeId()`、`getClosestShapeId()` 返回具体碰撞器。
`Shape3D.setTag(integer)` 可保存游戏定义的碰撞器用途（如头部、弱点、攀爬面）；对应的
`get*ShapeTag()` 或 `getQueryShapeTag(index)` 无需额外 ID 映射即可读取，Tag 同样跨 Sensor 重建保留。

Triangle Mesh 或 HeightField 命中还可读取具体面索引：最近 Ray 使用
`getRayHitTriangleIndex()`，Ray All 使用 `getRayResultTriangleIndex(i)`；Shape Cast 对应
`getShapeCastTriangleIndex()` 和 `getShapeCastResultTriangleIndex(i)`。Primitive 或未命中返回
`-1`。该索引可用于弹孔、脚步声、地形编辑和导航面定位。

Triangle Mesh 返回 Box3D 烘焙后的三角形索引，BVH 烘焙可能重排源面；HeightField 返回压缩
网格的三角形索引。索引在碰撞数据不变时稳定，但调用 `setTriangleMeshData*()`、
`setHeightFieldHeights()` 或 `setHeightFieldRegion()` 重建后应重新查询，不能把旧 face index 当作
长期资源 ID。需要关联渲染面或材质时，应在烘焙阶段保存显式映射。

`rayCastAll` 返回缓存数量，结果用 `getRayResultCount()` 和 `getRayResultBodyId()`、
`getRayResultShapeId()`、`getRayResultShapeTag()`、`getRayResultX/Y/Z()`、
`getRayResultNormalX/Y/Z()`、`getRayResultFraction()` 逐项读取。`maxHits` 必须在 `[1,4096]`；
缓存只保留最近的 N 项，并在遍历中用当前最远结果裁剪 broad-phase，不会先无限收集再截断。
结果按 fraction 升序排列，相同 fraction 按稳定 Shape ID 排列。`rayCast()` 复用相同缓存并将
容量设为 1，因此旧的 `getRayHit*` 与新的 index 0 始终表示同一个最近命中。

```squirrel
local count = world3.rayCastAll(muzzleX, muzzleY, muzzleZ, endX, endY, endZ, 8);
for (local i = 0; i < count; ++i) {
    local bodyId = world3.getRayResultBodyId(i);
    local materialTag = world3.getRayResultShapeTag(i);
    local distanceFraction = world3.getRayResultFraction(i);
    // 穿透射击、遮挡层或沿线材质处理。
}
```

`World3D.update()` 每次调用会清空上一帧并收集新的事件。普通碰撞通过
`getBeginContactCount()` / `getEndContactCount()` 和对应的 `BodyA/BId`、`ShapeA/BId`、
`ShapeA/BTag` getter 读取；双方按稳定 Shape ID 排序，结果不依赖 Box3D 内部遍历顺序。
Trigger 使用 `getBeginTriggerCount()` / `getEndTriggerCount()`，并提供明确命名的
`Sensor*` 与 `Visitor*` getter。事件保存稳定 ID/Tag 快照，所以由移动、过滤器修改、
Sensor 重建或销毁引发的 end 事件仍可安全读取。`clearContactEvents()` 可提前清空全部事件缓冲。

Begin/End 适合状态切换，持续贴地、轮胎抓地或墙面摩擦则可调用
`queryBodyContacts(bodyId, maxPoints)` 查询当前仍在 touching 的 manifold point，容量范围为
`[1,4096]`。结果统一以被查询 Body 为自身：`getContactPointShapeId/Tag()` 是自身 Shape，
`getContactPointOtherBodyId()` 与 `getContactPointOtherShapeId/Tag()` 是对方；法线始终从自身
指向对方，不受 Box3D 内部 A/B 顺序影响。

`getContactPointX/Y/Z()` 返回自身表面上的世界点，`getContactPointNormalX/Y/Z()` 返回重定向
后的法线，`getContactPointSeparation()` 返回有符号间距（负数为穿透）。
`getContactPointNormalImpulse()` 是最终子步冲量，`getContactPointTotalNormalImpulse()` 是整次
World step 全部子步的累计冲量，`getContactPointNormalVelocity()` 是预求解法向相对速度
（负数表示接近），`isContactPointPersisted()` 表示该 feature 是否从上一物理步延续。
结果按自身 Shape ID、对方 Shape ID 和 feature ID 确定排序后截断。

```squirrel
local count = world3.queryBodyContacts(playerBody.getId(), 16);
for (local i = 0; i < count; ++i) {
    local upDot = world3.getContactPointNormalY(i); // 默认 Y-up 时，自身→地面通常为负
    local supportImpulse = world3.getContactPointTotalNormalImpulse(i);
    local surfaceTag = world3.getContactPointOtherShapeTag(i);
    // 识别支撑面、落地强度、摩擦材质或车轮抓地。
}
```

这是按需诊断/玩法查询；大量对象的常规状态切换仍应优先消费 Begin/End/Hit 事件，避免每帧为
所有 Body 提取 manifold。

需要碰撞音效、落地强度或撞击伤害时，在相关碰撞器调用
`shape.setHitEventsEnabled(true)`，并可用 `world3.setHitEventThreshold(speed)` 设置全局最低接近
速度（m/s）。Hit 默认按 Shape 关闭以避免不需要的事件开销；开关偏好跨 Sensor 重建保留，
但 Sensor 本身不会生成物理撞击。每帧用 `getHitCount()` 遍历，通过 `getHitBodyA/BId()`、
`getHitShapeA/BId()` 和 `getHitShapeA/BTag()` 识别对象；`getHitPointX/Y/Z()` 返回世界接触点，
`getHitNormalX/Y/Z()` 返回由稳定 A 侧指向 B 侧的法线，`getHitApproachSpeed()` 返回正的预求解
接近速度，`getHitNormalImpulse()` 返回所有接触 manifold 在全部子步累积的总法向冲量。

```squirrel
crateShape.setHitEventsEnabled(true);
world3.setHitEventThreshold(2.0);
world3.update(dt);
for (local i = 0; i < world3.getHitCount(); ++i) {
    local strength = world3.getHitNormalImpulse(i);
    local speed = world3.getHitApproachSpeed(i);
    // 按 strength / speed 选择音量、粒子规模或伤害。
}
```

角色控制器可直接调用 `world3.moveCapsule(...)`。它将穿透恢复、多接触平面约束和连续
扫描组合在最多五轮求解中；最终安全位移通过 `getMoverDeltaX/Y/Z` 读取，主要阻挡法线、
参与约束的平面数和迭代数可通过 `getMoverNormalX/Y/Z`、`getMoverPlaneCount`、
`getMoverIterations` 获取。调用方应把返回位移加到角色位置，而不是直接应用原始输入位移。

用 `setMoverUp(x,y,z)` 配置重力反方向，用 `setMoverSlopeLimit(degrees)` 设置最大可行走
坡度。每次移动后，`isMoverGrounded()` 表示是否接触可行走表面，
`getMoverGroundDot()` 返回接触法线与 up 的最大点积，可用于落地、跳跃和斜坡动画状态。

### 3D 碰撞分层

`Shape3D` 与 2D `Fixture` 一样提供 `setCategoryBits`、`setMaskBits` 和
`setGroupIndex`。查询侧通过 `world3.setQueryFilter(categoryBits, maskBits)` 配置，之后的
ray cast、overlap、shape cast 和 capsule mover 都使用该过滤器；调用
`resetQueryFilter()` 恢复接受所有低 32 位 category。切换 sensor 时过滤设置会被保留。

### 连续碰撞与求解器调优

`World3D` 默认启用连续碰撞。高速动态物体撞击静态场景时应保持
`setContinuousCollisionEnabled(true)`；对于还需要动态物体之间 CCD 的子弹、弹片等物体，
同时对相应 `Body3D` 调用 `setBullet(true)`。

```nut
world3.setContinuousCollisionEnabled(true)
world3.setMaximumLinearSpeed(250.0)
world3.setRestitutionThreshold(1.0)

// 接触刚度 Hz、阻尼比、最大穿透修正速度（m/s）
world3.setContactTuning(30.0, 10.0, 3.0)
world3.setContactRecycleDistance(0.01)
world3.setWarmStartingEnabled(true)
```

较高的接触刚度会更快消除穿透，但也可能带来抖动；降低阻尼会让修正更有弹性。
`setMaximumLinearSpeed` 是全世界速度安全上限，适合约束异常力或错误脚本产生的极端速度。
接触点复用可以改善静止堆叠的帧间稳定性，设为 `0` 可关闭。Warm Starting 对稳定堆叠和关节很重要，
通常只应在求解器诊断或确定性对比时关闭。所有调优参数都可通过对应 `get*` / `is*` 方法读取。

### 高级表面材质与传送带

除摩擦、反弹和密度外，`Shape3D` 还支持滚动阻力、局部切向速度及应用材质 ID：

```nut
local belt = floorBody.newBoxShape(12.0, 0.5, 2.0)
belt.setFriction(0.9)
belt.setTangentVelocity(3.0, 0.0, 0.0) // 局部 +X 方向传送带
belt.setMaterialId(1001)                // 脚步声、粒子或游戏材质表索引

local tire = wheelBody.newSphereShape(0.45)
tire.setRollingResistance(0.04)
```

切向速度会随 Shape 的朝向旋转，并投影到接触平面，因此旋转传送带形状即可改变输送方向。
滚动阻力只对球体和胶囊体产生物理效果。材质 ID 保留完整低 32 位，可作为有符号脚本整数往返；
切换 Sensor 或运行时重建几何时，高级材质参数都会保留。Ray Cast 可通过
`getRayHitMaterialId()` / `getRayResultMaterialId(i)` 读取实际命中的表面材质；Shape Cast
对应使用 `getShapeCastMaterialId()` / `getShapeCastResultMaterialId(i)`，并与三角形索引一起
支持地面音效、轮胎反馈、弹孔和粒子效果的材质分派。

每个 Shape 还能独立指定摩擦和反弹的混合策略：

```nut
ice.setFrictionCombineMode("minimum")
bumper.setRestitutionCombineMode("maximum")
```

可选值为 `default`、`average`、`minimum`、`multiply`、`maximum`。当接触两侧模式不同，
按 `maximum > multiply > minimum > average > default` 的优先级选择，行为与常见商业引擎的
Physics Material 规则一致。`default` 保持 Box3D 原生行为：摩擦取几何平均，反弹取最大值。
混合模式编码在内部材质元数据中，修改应用材质 ID 或重建 Shape 都不会丢失。

### Triangle Mesh 多材质

静态 Triangle Mesh 可以为每个源三角形选择最多 255 个材质槽：

```nut
mesh.setTriangleMeshMaterialIndices([0, 0, 1, 1])
mesh.setTriangleMeshMaterial(
    0, 0.8, 0.0, 0.02, 0.0, 0.0, 0.0, 100, "average", "default")
mesh.setTriangleMeshMaterial(
    1, 0.05, 0.1, 0.0, 0.0, 0.0, 0.0, 200, "minimum", "maximum")
```

参数依次为槽位、摩擦、反弹、滚动阻力、局部切向速度 XYZ、应用材质 ID、摩擦混合模式和
反弹混合模式。材质槽可运行时更新而无需重建 BVH；更改逐三角形映射时会原子重建碰撞网格。
Ray Cast 和 Shape Cast 返回的是实际命中三角形的材质 ID，而非统一的 Shape 默认值。

### 运行时接触覆盖

临时穿透角色、队友免碰撞、抓取物体忽略玩家碰撞等场景，可以覆盖指定 Body 对或 Shape 对，
无需改写双方长期使用的 Layer/Mask：

```nut
world3.setBodyPairCollisionEnabled(player, carriedObject, false)
world3.setShapePairCollisionEnabled(characterShape, oneSpecificDoorShape, false)

// 放下物体或门重新实体化时恢复
world3.setBodyPairCollisionEnabled(player, carriedObject, true)
```

`isBodyPairCollisionEnabled` 和 `isShapePairCollisionEnabled` 可读取当前覆盖状态。修改会立即刷新
现有接触并同时作用于普通碰撞、CCD 和 Sensor 重叠；Layer/Mask 仍先执行，因此启用覆盖不会绕过
原有碰撞层过滤。规则使用稳定 Body/Shape ID，无论参数顺序如何结果一致，并在对象销毁时自动清理。

### 径向爆炸与内爆

`World3D.explode` 按物体朝向爆心的投影面积施加真实几何冲量，而不是简单修改速度：

```nut
local affected = world3.explode(
    blastX, blastY, blastZ,
    2.0, 6.0, 35.0, LAYER_DESTRUCTIBLE)

for (local i = 0; i < affected; ++i) {
    local bodyId = world3.getExplosionResultBodyId(i)
    local dvx = world3.getExplosionResultDeltaVelocityX(i)
}
```

前三个物理参数依次为满强度半径、额外线性衰减距离和每平方米冲量；负冲量产生内爆。
爆炸只影响通过 `maskBits` 的动态球体、胶囊和凸形状；静态/运动学 Body、Triangle Mesh 与
Height Field 不会获得冲量。结果按稳定 Body ID 排序，并缓存实际造成的线速度和角速度变化，
便于伤害、镜头震动、联网同步和调试。

每个 `Shape3D` 可用 `setExplosionScale(scale)` 调整响应：`1` 为默认，`0` 完全免疫，
大于 `1` 可表现轻质碎片或弱点。复合 Body 上各 Shape 的冲量会累积；倍率会在 Sensor 切换、
尺寸调整及网格重建时保留。`getExplosionScale()` 可用于编辑器属性面板和运行时检查。

### 世界诊断与性能面板

`World3D` 暴露无需暂停模拟即可读取的统计数据：

```nut
local bodies = world3.getBodyCount()
local contacts = world3.getContactCount()
local memoryBytes = world3.getMemoryByteCount()
local physicsMs = world3.getProfileStepMs()
local solveMs = world3.getProfileSolveMs()
```

计数器还包括 Shape、Joint、Island、Awake Body/Contact、复用接触点，以及静态/动态宽相树高度。
`getProfilePairsMs`、`getProfileCollideMs`、`getProfileBulletsMs` 和 `getProfileSensorsMs`
可定位宽相、窄相、CCD 与 Sensor 开销。`getBoundsMin*` / `getBoundsMax*` 返回当前世界宽相边界，
适合编辑器取景、流式场景分区和异常物体越界检测。时间单位均为毫秒。

发起者需要排除自身时，调用 `setQueryIgnoredBodyId(body.getId())` 会跳过该 Body 的全部复合
Shape；`setQueryIgnoredShapeId(shape.getId())` 只跳过一个碰撞器。两项排除可同时生效，并统一
应用于 Ray、AABB/精确 Overlap、Sphere/Capsule/Box Cast、Closest Point 和 Capsule Mover。
传入 `-1` 可单独关闭一项，`clearQueryIgnores()` 同时清除两项但不改变 Layer Filter。
最近命中在相同 fraction 时按稳定 Shape ID 决胜，避免 broad-phase 遍历顺序导致结果漂移。

```squirrel
const LAYER_WORLD  = 1;
const LAYER_PLAYER = 2;
const LAYER_TRIGGER = 4;

groundShape.setCategoryBits(LAYER_WORLD);
triggerShape.setCategoryBits(LAYER_TRIGGER);
triggerShape.setSensor(true);

// 角色移动只碰撞世界，不被 trigger 阻挡。
world3.setQueryFilter(LAYER_PLAYER, LAYER_WORLD);
world3.setQueryIgnoredBodyId(playerBody.getId());
world3.moveCapsule(ax, ay, az, bx, by, bz, radius, dx, dy, dz);
```

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
- `getCollideWorld()`、`getDamping()`、`getDensity()`、`getFoldStiffness()`、`getFriction()`、`getGrabIndex()`、`getGravityX()`、`getGravityY()`、`getGravityZ()`、`getId()`、`getIterations()`
- `getLinearVelocityX()`、`getLinearVelocityY()`、`getLinearVelocityZ()`、`getMass()`、`getMeter()`、`getName()`、`getNearPressureStiffness()`、`getParticleCount()`、`getParticleMass()`、`getParticleSize()`、`getParticleVx()`
- `getMaxFoldAngle()`、`getParticleVy()`、`getParticleVx()`、`getParticleX()`、`getParticleY()`、`getParticleZ()`、`getPressureStiffness()`、`getQueryBodyId()`、`getQueryCount()`、`getRayHitBodyId()`、`getRayHitFraction()`
- `getRayHitNormalX()`、`getRayHitNormalY()`、`getRayHitNormalZ()`、`getRayHitX()`、`getRayHitY()`、`getRayHitZ()`、`getRestDensity()`、`getRestitution()`、`getRotW()`、`getRotX()`、`getRotY()`、`getRotZ()`、`getRows()`、`getSmoothingRadius()`
- `getOriginX()`、`getOriginY()`、`getOriginZ()`、`getSelfCollision()`、`getSpacing()`、`getStiffness()`、`getType()`、`getViscosity()`、`getX()`、`getY()`、`getZ()`、`grabAt()`、`hasRayHit()`
- `interactAt()`、`isActive()`、`isAwake()`、`isBullet()`、`isFixedRotation()`、`isGrabbing()`、`isPinned()`、`isSensor()`
- `moveGrab()`、`newBody()`、`newBoxShape()`、`newCapsuleShape()`、`newCircleFixture()`、`newCloth()`、`newCloth3D()`、`newClothGPU()`、`newFluid()`、`newRectangleFixture()`、`newSphereShape()`、`newWorld()`、`newWorld3D()`、`pin()`
- `pinTopRow()`、`queryAABB()`、`rayCast()`、`releaseGrab()`、`reset()`、`setActive()`、`setAngle()`、`setAngularVelocity()`、`setAwake()`
- `setBounds()`、`setBullet()`、`setCollideWorld()`、`setColor()`、`setDamping()`、`setDensity()`、`setFixedRotation()`、`setFoldStiffness()`、`setFriction()`、`setGravity()`
- `setIterations()`、`setLinearVelocity()`、`setMeter()`、`setMaxFoldAngle()`、`setNearPressureStiffness()`、`setParticleMass()`、`setParticlePosition()`、`setParticleSize()`、`setPosition()`、`setPressureStiffness()`
- `setRestDensity()`、`setRestitution()`、`setRotation()`、`setSensor()`、`setSmoothingRadius()`、`setStiffness()`、`setType()`、`setViscosity()`、`testPoint()`
- `setSelfCollision()`、`unpin()`、`update()`、`updateFull()`

+### 3D 高级接口补充索引

以下方法补充了 3D 碰撞查询结果、接触事件、关节状态、距离场采样、材质参数和世界诊断的脚本索引。坐标分量方法按 `X/Y/Z` 成组使用；索引类 getter 的有效范围由对应的 `*Count()` 返回值决定。

- `areHitEventsEnabled()`、`didMoverHitWall()`、`getAngularDamping()`、`getAngularSeparation()`、`getAwakeBodyCount()`、`getAwakeContactCount()`、`getBeginContactShapeAId()`、`getBeginContactShapeATag()`
- `getBeginContactShapeBId()`、`getBeginContactShapeBTag()`、`getBeginTriggerSensorBodyId()`、`getBeginTriggerSensorShapeId()`、`getBeginTriggerSensorShapeTag()`、`getBeginTriggerVisitorBodyId()`、`getBeginTriggerVisitorShapeId()`、`getBeginTriggerVisitorShapeTag()`
- `getBodyAId()`、`getBodyBId()`、`getBoundsMaxX()`、`getBoundsMaxY()`、`getBoundsMaxZ()`、`getBoundsMinX()`、`getBoundsMinY()`、`getBoundsMinZ()`
- `getBoxDepth()`、`getBoxHeight()`、`getClosestBodyId()`、`getClosestNormalX()`、`getClosestNormalY()`、`getClosestNormalZ()`、`getClosestShapeTag()`、`getClosestY()`
- `getClosestZ()`、`getCollideConnected()`、`getCollisionX()`、`getCollisionZ()`、`getConstraintForceX()`、`getConstraintForceY()`、`getConstraintForceZ()`、`getConstraintTorqueX()`
- `getConstraintTorqueY()`、`getConstraintTorqueZ()`、`getContactDampingRatio()`、`getContactHertz()`、`getContactPointCount()`、`getContactPointNormalZ()`、`getContactPointShapeTag()`、`getContactPointY()`
- `getContactPointZ()`、`getContactPushOutSpeed()`、`getContactRecycleDistance()`、`getDepth()`、`getDistanceLength()`、`getDynamicTreeHeight()`、`getEndContactShapeAId()`、`getEndContactShapeATag()`
- `getEndContactShapeBId()`、`getEndContactShapeBTag()`、`getEndTriggerSensorBodyId()`、`getEndTriggerSensorShapeId()`、`getEndTriggerSensorShapeTag()`、`getEndTriggerVisitorBodyId()`、`getEndTriggerVisitorShapeId()`、`getEndTriggerVisitorShapeTag()`
- `getExplosionResultCount()`、`getExplosionResultDeltaAngularVelocityX()`、`getExplosionResultDeltaAngularVelocityY()`、`getExplosionResultDeltaAngularVelocityZ()`、`getExplosionResultDeltaVelocityY()`、`getExplosionResultDeltaVelocityZ()`、`getForceThreshold()`、`getFrictionCombineMode()`
- `getGravityScale()`、`getHeightFieldCellSizeZ()`、`getHeightFieldCountZ()`、`getHeightFieldGlobalMax()`、`getHitBodyAId()`、`getHitBodyBId()`、`getHitEventThreshold()`、`getHitNormalY()`
- `getHitNormalZ()`、`getHitPointY()`、`getHitPointZ()`、`getHitShapeAId()`、`getHitShapeATag()`、`getHitShapeBId()`、`getHitShapeBTag()`、`getInertiaXY()`
- `getInertiaXZ()`、`getInertiaYY()`、`getInertiaYZ()`、`getInertiaZZ()`、`getIslandCount()`、`getJointCount()`、`getJointStressBodyAId()`、`getJointStressBodyBId()`
- `getJointStressKind()`、`getJointStressTorqueY()`、`getJointStressTorqueZ()`、`getLinearDamping()`、`getLinearSeparation()`、`getLocalCenterY()`、`getLocalCenterZ()`、`getLocalRotW()`
- `getLocalRotX()`、`getLocalRotY()`、`getLocalRotZ()`、`getLocalX()`、`getLocalY()`、`getLocalZ()`、`getMaterialId()`、`getMaximumLinearSpeed()`
- `getMoverGroundSnap()`、`getMoverNormalY()`、`getMoverNormalZ()`、`getMoverSkinWidth()`、`getMoverStepHeight()`、`getNormalY()`、`getNormalZ()`、`getOutsideDistance()`
- `getPenetrationDepth()`、`getQueryCategoryBits()`、`getQueryIgnoredBodyId()`、`getQueryIgnoredShapeId()`、`getQueryMaskBits()`、`getRayHitShapeTag()`、`getRayResultNormalY()`、`getRayResultNormalZ()`
- `getRayResultY()`、`getRayResultZ()`、`getRecycledContactCount()`、`getRestitutionCombineMode()`、`getRestitutionThreshold()`、`getRevoluteAngle()`、`getRevoluteMotorTorque()`、`getRollingResistance()`
- `getShapeCastBodyId()`、`getShapeCastNormalY()`、`getShapeCastNormalZ()`、`getShapeCastResultNormalY()`、`getShapeCastResultNormalZ()`、`getShapeCastResultY()`、`getShapeCastResultZ()`、`getShapeCastY()`
- `getShapeCastZ()`、`getShapeContactX()`、`getShapeContactZ()`、`getShapeCount()`、`getSleepThreshold()`、`getSphericalConeAngle()`、`getSphericalTwistAngle()`、`getStaticTreeHeight()`
- `getSurfaceX()`、`getSurfaceZ()`、`getTangentVelocityX()`、`getTangentVelocityY()`、`getTangentVelocityZ()`、`getTorqueThreshold()`、`getTriangleMeshMaterialCount()`、`getTriangleMeshMaterialFriction()`
- `getTriangleMeshMaterialId()`、`getTriangleMeshMaterialIndex()`、`getTriangleMeshMaterialRestitution()`、`getTriangleMeshMaterialRollingResistance()`、`getWorldCenterZ()`、`hasClosestPoint()`、`hasShapeCastHit()`、`isAngularXLocked()`
- `isAngularYLocked()`、`isAngularZLocked()`、`isContinuousCollisionEnabled()`、`isLinearXLocked()`、`isLinearYLocked()`、`isLinearZLocked()`、`isSleepEnabled()`、`isWarmStartingEnabled()`
- `sample()`、`sampleNormal()`、`setDistanceLength()`、`setSleepThreshold()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/physics/`](../../../src/modules/physics/)
**相关测试：** [`test/box2d.cpp`](../../../test/box2d.cpp)、[`test/box3d.cpp`](../../../test/box3d.cpp)、[`test/softbody.cpp`](../../../test/softbody.cpp)
