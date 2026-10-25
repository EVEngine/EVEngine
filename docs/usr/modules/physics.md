# 物理模块（Box2D + Box3D + 布料 + 流体）

**脚本入口：** `eve.Physics()`

创建 World、Body、Fixture、Joint2D、Mechanism2D（Box2D 2D 刚体与机构）、World3D / Body3D / Shape3D / Joint3D / Mechanism3D（Box3D 3D 刚体与机构），以及可交互的 `Cloth`（2D Verlet 布料）、`Cloth3D`（3D Verlet 布料）与 `Fluid2D`（2D 粒子流体）。2D 脚本使用像素坐标并按 meter 换算；3D 使用米（Box3D 原生单位）；2D 布料/流体在像素空间模拟，3D 布料在米制空间模拟。

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

### 2D 关节与机械机构

`World` 拥有 `Joint2D` / `Mechanism2D`。锚点、长度与线速度使用像素空间（与 Body 一致）；角度与角速度使用弧度。支持 distance / revolute / prismatic / weld / wheel / motor / gear。

```squirrel
local hinge = world.newRevoluteJoint(frame, door, 100, 60, false);
hinge.setRevoluteLimits(true, -0.5, 1.2);
hinge.setRevoluteMotor(true, 2.0, 40.0);

local rail = world.newPrismaticJoint(frame, platform, 200, 100, 1, 0, false);
rail.setPrismaticLimits(true, -40, 40);
rail.setPrismaticMotor(true, 30, 500); // 像素/秒，像素力

local weld = world.newWeldJoint(bodyA, bodyB, 150, 80, false);
weld.setWeldSpring(0.0, 1.0); // 0 Hz 为刚性

local gear = world.newGearJoint(hinge, rail, 1.0);
gear.setGearRatio(2.0);
```

在关节原语之上，`Mechanism2D` 提供转轴、棘轮与曲柄滑块。销毁机构会销毁其关节；棘轮单向锁定在每次 `world.update()` / `step()` 前自动同步。

```squirrel
local shaft = world.newShaft(bearing, rotor, 100, 100, false);
shaft.setDrive(8.0, 60.0); // rad/s, N·m

local ratchet = world.newRatchet(frame, wheel, 100, 100, 1, 200.0, false);
ratchet.setDrive(3.0, 40.0); // 仅在自由方向驱动

local cs = world.newCrankSlider(
    frame, crank, rod, slider,
    0, 0,       // 曲柄转轴锚点（像素）
    40, 0,      // 曲柄销
    120, 0,     // 滑块销
    1, 0,       // 滑轨轴
    false);
cs.setDrive(4.0, 80.0);
local stroke = cs.getSliderTranslation(); // 像素
```

`getDriveAngle()` / `getSpinSpeed()` 读取驱动铰链状态；曲柄滑块另有
`getSliderTranslation()`。`getDriveJoint()` 等可取回底层 `Joint2D`。

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

跨模块共享采样走 `eve::ISdfField3D`（`physics::DistanceField3DSdf` 适配器）。可移植资产用
`eve.sdf.field3d` v1 blob：`physics.sdfField3DBlobFromDistanceField(field)` 导出，
`physics.distanceField3DFromSdfBlob(blob)` 导入（与 fluids `MeshSdf` /
`fluids.sdfField3DBlobFromModel` 互通；场外策略导入后变为常量 `outsideDistance`）。

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
local clothModule = eve.Cloth();
local cloth = clothModule.newCloth(18, 12, 14.0, 48.0, 36.0); // cols, rows, spacing, origin
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
local clothModule = eve.Cloth();
local cloth3 = clothModule.newCloth3D(16, 12, 0.4, -3.0, 3.2, -2.0); // cols, rows, spacing, origin(X,Y,Z)
cloth3.setGravity(0, -9.8, 0);       // 米 / s²，+Y 向上
cloth3.setCollideWorld(world3);      // 与 Box3D 非 sensor shape 碰撞
cloth3.setParticleSize(0.12);        // 自碰撞半径 / 碰撞厚度
cloth3.setParticleMass(0.1);         // 碰撞动量交换的粒子质量（kg）
cloth3.setStretchCompliance(0.0001); // XPBD 拉伸柔顺度（m/N，0 为旧 PBD）
cloth3.setShearCompliance(0.0002);   // XPBD 剪切柔顺度
cloth3.setBendCompliance(0.001);     // XPBD 距离弯曲柔顺度
cloth3.setTetherScale(1.0);          // 启用到固定粒子的测地线 tether
cloth3.setTetherCompliance(0.0001);  // tether XPBD 柔顺度
// 闭合网格可用 setPressure(1.0) 保持烘焙体积，>1 充气；开放布料会忽略。
cloth3.setVolumeCompliance(0.00001);
cloth3.setWindVelocity(4, 0, 1);     // 世界空间空气速度（m/s）
cloth3.setAerodynamics(1.225, 1.1, 0.2); // 空气密度、阻力系数、升力系数
cloth3.setMaxFoldAngle(130);         // 相邻三角面最大折叠角（度），默认 120
cloth3.setSelfCollision(true);
cloth3.setBounds(-4, -1, -3, 8, 5.5, 6);  // 原点 + 宽高深
// 鼠标拾取（射线与 y=2 平面求交后）：
// local idx = cloth3.grabAt(x, y, z, 0.5);
// if (idx >= 0) cloth3.moveGrab(x, y, z);
cloth3.applyForce(1.0, 0, 0);         // 均匀风
cloth3.interactAt(x, y, z, 1.1, -14); // 指针场：正吸引 / 负排斥（与 Fluid2D 相同语义）
cloth3.update(dt);
cloth3.draw(gfx);                     // 需要处于 3D 帧内（gfx.render3D 之后）
```

`setWindVelocity` + `setAerodynamics` 使用当前变形后三角形的面积、法线和布料相对风速计算双面阻力/升力；因此折叠、转向后的受力会随表面姿态变化。两个系数都设为 `0` 即关闭气动力。`applyForce` 仍是一帧性均匀加速度，适合爆炸/阵风脉冲；持续风应使用新 API。

### ClothModel（C++）

`ClothModel` 是可复用的静态布料资产，`Cloth3D` 是独立的运行时状态。模型负责验证并烘焙粒子、三角形、结构/弯曲距离约束和相邻三角面的折叠关系；它不持有渲染对象、碰撞世界或逐帧位置。规则网格和任意三角网格最终都进入相同运行时路径：

```cpp
auto modelResult = eve::physics::ClothModel::grid(18, 12, 0.5f, 0.f, 5.f, 0.f);
if (!modelResult) {
    // 读取 modelResult.error() 的结构化诊断。
    return;
}

auto cloth = physics.createCloth(modelResult.value());
cloth->setWindVelocity(2.f, 0.f, 1.f);
cloth->update(dt);
```

任意网格使用 `ClothModel::fromTriangles(positions, indices, inverseMasses)`；`positions` 是紧密排列的 XYZ，`inverseMasses` 中的 `0` 表示固定粒子。输入索引越界、退化三角形、负质量或非流形边会整体拒绝，不会发布半成品模型。模型可序列化为严格版本化的 `eve.cloth-model/1` Definition，并由 `asset/physics` 的 `EvpackClothModelLoader` 从 `.evpack` 加载；未知字段和未知版本都会拒绝。Squirrel 的规范入口是独立的 `eve.Cloth()` 子模块，`newCloth3D(...)` 在内部走同一个 `ClothModel` 网格烘焙路径。

### Obi Cloth 6.4 核心覆盖边界

以下矩阵按参考包 `ObiCloth_v6.4.unitypackage` 内的 actor、blueprint、constraint batch、attachment 与 renderer 源码核对。这里的“80%”指 15 组核心运行时/资产能力按“完整=1、部分=0.5、缺失=0”计分达到 12/15；不是宣称 API、Unity Inspector 或 Burst/Oni 后端逐项兼容。

| 能力组 | 状态 | EVEngine 对应实现 / 边界 |
|---|---:|---|
| Blueprint/任意网格资产 | 完整 | `ClothModel`、`eve.cloth-model/1`、`.evpack` loader、严格验证 |
| 拉伸/剪切距离约束 | 完整 | 分类型 XPBD compliance，保留零 compliance 旧 PBD 行为 |
| 弯曲 | 部分 | bend distance + 二面角 fold clamp；尚无 Obi 的 plastic yield/creep |
| 气动力 | 完整 | 变形三角形面积/法线、双面 drag/lift、相对风速 |
| Tether | 完整 | 从固定粒子测地烘焙、scale、XPBD compliance |
| Volume/pressure | 完整 | 封闭流形检测、静止体积、XPBD 保形与充气 |
| Skin/backstop | 完整 | 动画参考点/法线、skin radius、backstop sphere、compliance |
| Particle attachment | 部分 | 动态世界目标、软/硬 attachment、更新/解绑；尚无跨 actor/刚体局部锚点对象 |
| 刚体世界碰撞 | 完整 | `World3D` 非 sensor shape probe、动态体动量交换 |
| 自碰撞 | 完整 | 粒子空间哈希 + 非相邻三角面厚度约束 |
| Collision filter | 完整 | cloth category/mask 与 Shape3D category/mask 双向过滤 |
| 摩擦/回弹 | 完整 | cloth/shape 合并材质、切向速度摩擦、法向回弹 |
| Runtime tearing | 部分 | 阈值、单步预算、手动撕裂及权威表面拓扑更新；尚无 Obi 式顶点分裂/容量池 |
| Skinned/proxy renderer | 缺失 | 可逐帧更新 skin reference，但没有独立的 barycentric proxy renderer |
| GPU backend | 部分 | `supportsFeature` 明示能力；GPU 目前是 2D grid compute，3D 高级约束走 CPU，不静默伪装等价 |

运行时可用 `getBackendName()` 与 `supportsFeature(name)` 做显式后端选择。CPU 稳定 feature 名包括 `arbitrary_topology`、`xpbd_material`、`tether`、`volume_pressure`、`skin_backstop`、`attachment`、`world_collision`、`collision_filter`、`friction`、`self_collision`、`aerodynamics`、`runtime_tearing`。`getTriangleCount`、`getDistanceConstraintCount`、`getTetherConstraintCount`、`getSkinConstraintCount`、`getAttachmentCount` 与 `getTornConstraintCount` 提供编辑器/调试面板所需的只读数据。

高级材质与约束脚本 API：`setStretchCompliance` / `getStretchCompliance`、`setShearCompliance` / `getShearCompliance`、`setBendCompliance` / `getBendCompliance`；`setTetherScale` / `getTetherScale`、`setTetherCompliance` / `getTetherCompliance`；`setPressure` / `getPressure`、`setVolumeCompliance` / `getVolumeCompliance`、`getCurrentVolume`；`setParticleInverseMass` / `getParticleInverseMass`；`setSkinConstraint`、`updateSkinReference`、`clearSkinConstraint`、`hasSkinConstraint`；`attachParticle`、`updateAttachment`、`detachParticle`、`isAttached`；`setCollisionMaterial`、`getCollisionFriction`、`getCollisionRestitution`、`setCollisionFilter`、`getCollisionCategoryBits`、`getCollisionMaskBits`；`setTearThreshold` / `getTearThreshold`、`setMaxTearsPerStep` / `getMaxTearsPerStep`、`tearConstraint`。

模型可通过 `toJson()` / `fromJson()` 保存和恢复，规范标识为 `eve.cloth-model/1`。v1 使用严格未知字段策略：缺少字段、额外字段和未知版本都会被拒绝；约束不作为第二份真相保存，而是在加载并验证粒子与三角拓扑后确定性重建。规则网格额外保存并校验网格元数据，以保持旧求解器的约束顺序和有限迭代行为。

网格位于 XZ 平面、顶行默认钉住；`pin` / `unpin` / `pinTopRow`、`grabAt` / `moveGrab` / `releaseGrab`、`reset`、`setColor` 等与 2D 布料一致。3D 布料的折角约束基于相邻三角面的二面角：折叠超过 `maxFoldAngle` 时会绕共享边旋转两个三角面、把折角精确开回极限值，避免布料折成死褶或扭曲过高。3D 自碰撞除粒子级近邻外还带**三角面级**处理：非相邻三角面保持至少 2×`particleSize` 厚度，顶点穿透三角面会被沿面法线推出，布料不会在粒子间隙中互相穿过。

示例：[`examples/softbody3d/`](../../../examples/softbody3d/)（窗帘 + 平台/球体碰撞 + 自碰撞 + 折角限制）

### 体积软体 3D（形状匹配）

`SoftBody3D` 是真正的体积软体，而不是加厚的布料。规则粒子格的每个体素单元
构成一个重叠八粒子 shape-matching cluster；相邻 cluster 共享粒子，因此局部体积和
朝向会恢复，同时仍允许挤压、扭转和回弹。该实现独立编写，只借鉴了 Obi Softbody
公开展示的组件分层与材料参数概念。

```squirrel
local body = physics.newSoftBody3D(4, 4, 4, 0.38, 0.9, 2.2, -0.6);
local renderer = physics.newSoftBody3DRenderer(body);
body.setGravity(0, -9.8, 0);
body.setDeformationResistance(0.78); // 0 很软，1 强形状恢复
body.setIterations(6);
body.setParticleRadius(0.11);
body.setPlasticity(0.32, 0.18, 0.25, 0.45); // yield, creep, recovery, max
body.setCollideWorld(world3);        // 借用 World3D；world 必须活得更久
body.update(dt);
renderer.setColor(0.96, 0.36, 0.20, 1.0);
renderer.draw(gfx);
```

运行时可用 `getDeformationResistance()`、`getParticleRadius()`、
`getPlasticYield()`、`getPlasticCreep()`、`getPlasticRecovery()` 和
`getMaxDeformation()` 回读材料参数；`getCols()`、`getRows()`、`getLayers()`
描述体积格分辨率。

`getVolumeRatio()` 返回当前 cell 总体积与初始体积之比，可用于测试、调试 UI 和
破坏判定。C++ 应优先使用 `SoftBody3D::create()` 的
`Result<std::unique_ptr<SoftBody3D>>`；`Physics::newSoftBody3D()` 是为脚本 VM
所有权模型保留的兼容工厂。求解由注入的 `dt`/`SimulationStep` 驱动，在同平台同
设置下提供容差有界确定性，不读取墙钟或随机源。当前版本使用 CPU cluster solver；
尚未包含任意网格体素化、蒙皮权重烘焙或 GPU backend。

创建配置由 `SoftBody3DDefinition` 统一拥有。其稳定 schema id 为
`physics:softbody3d`，当前版本为 1；`toValue()` / `fromValue()` 用于持久化边界，
版本 1 拒绝未知字段和未来版本。由于这是首个发布版本，没有旧版本迁移路径。
`SoftBody3D::create(definition)` 会先完整验证候选，再一次性创建运行时对象，不会在
验证失败时发布部分状态。编辑器侧使用
`physics_editing::softBody3DDefinitionSchema()`，其中包含全部 20 个可编辑字段的
默认值、数值范围、步长、单位、分类和说明；默认值直接取自
`SoftBody3DDefinition`，不是第二份配置真源。首次创建时还会幂等注册同版本的 Eve
Schema，工具可通过 `SchemaRegistry::resolve("physics:softbody3d", 1)` 查询，并用
注册表执行通用 JSON 校验；聚合粒子数等跨字段约束仍由强类型 `validate()` 负责。

软体实现按 physics 宿主包内 satellite 拆分：`physics/softbody` 只包含 definition、
固定步长求解和窄碰撞接口；`physics/softbody/graphics` 负责动态表面网格；
`physics/softbody/editing` 负责属性 schema。通用固定步长协议位于
`physics/backend`。主 `physics` 模块仅保留 `World3D` 接触适配、脚本绑定和旧工厂，
因此自定义无渲染组合可以只构建 `EVPhysics_softbody`，不拉入 graphics 或 editing；
现有预设 `headless` / `server` profile 并不默认启用软体。

颜色、动态 Mesh 缓存和绘制生命周期全部由 `SoftBody3DRenderer` 所有；它只借用
`SoftBody3D`，销毁软体前应调用 `renderer.setBody(null)`。核心 `SoftBody3D` 的公共头
不包含或前置声明任何 Graphics/Mesh 类型。`renderer.getBody()` 返回当前借用对象；
`physics.newSoftBody3DRenderer(body)` 创建由脚本 VM 拥有的 renderer。

真实网格的离线生成入口使用 `SoftBodyModelDefinition`，稳定 schema id 为
`physics:softbody-model`、版本为 1。它记录 `sourceMesh`、surface/volume 采样模式、
三种分辨率、各向异性上限和位置平滑率；未知字段、未来版本、空资源引用以及同时关闭
surface/volume 采样都会被事务性拒绝。该能力统一使用 Model 命名。
`physics/softbody/cook` 将资产模块的 `CanonicalMeshData` 转为拥有全部数组的 cooked
`SoftBodyModel`。v1 已支持按源顶点采样、保留三角形表面、生成直接粒子绑定以及基于一环邻域的
重叠 shape clusters；体素 surface/volume recipe 当前返回明确的 `Unsupported`，不会隐式改用顶点模式。

### 布料 GPU（2D，compute shader 加速）

`newClothGPU(cols, rows, spacing, originX, originY)` 创建与 `Cloth` 同接口的 GPU 布料：Verlet 积分和距离约束全部跑在 Vulkan compute shader 里（每粒子一个线程 + 双缓冲 Jacobi 约束迭代），每帧回读位置用于绘制。适合大批量粒子：

```squirrel
local clothModule = eve.Cloth();
local clothG = clothModule.newClothGPU(40, 30, 8.0, 40.0, 30.0); // 1200 粒子
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
local fluid = physics.newFluid2D(600);
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
Cloth3D / Fluid2D。World 管理 Body、Fixture、Joint2D 与 Mechanism2D；World3D 管理 Body3D、Shape3D、Joint3D 与 Mechanism3D。
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

`newWeldJoint(bodyA,bodyB,anchor,collideConnected)` 在共享世界锚点处焊接两刚体，并保持
创建时的相对姿态；`setWeldLinearSpring()` / `setWeldAngularSpring()` 可配置柔度
（0 Hz 为刚性）。`newMotorJoint()` 驱动两体相对线速度/角速度，适合舵机、传送带和
相对位姿伺服。`newParallelJoint(bodyA,bodyB,axis,…)` 用弹簧约束两体局部 Z 轴与给定
世界轴平行，常用于保持转轴直立。`newFilterJoint(bodyA,bodyB)` 仅禁用两体碰撞，不
产生约束力，适合机构零件互穿过滤。

### 机械机构：转轴、曲柄滑块、棘轮

在关节原语之上，`Mechanism3D` 提供常用机械装配。世界拥有机构及其创建的关节；
销毁机构会销毁其关节。棘轮的单向锁定在每次 `world3.update()` / `step()` 前自动同步。

```squirrel
// 转轴：支座 + 转子铰链，可选驱动电机
local shaft = world3.newShaft(bearing, rotor, 0, 0, 0, 0, 0, 1, false);
shaft.setDrive(8.0, 120.0); // rad/s, N·m

// 棘轮：direction=+1 只允许正向自由转动，反向由 engagementTorque 锁止
local ratchet = world3.newRatchet(frame, wheel, 0, 0, 0, 0, 0, 1, 1, 200.0, false);
ratchet.setDrive(3.0, 40.0); // 仅在自由方向驱动

// 曲柄滑块：机架 + 曲柄 + 连杆 + 滑块
local cs = world3.newCrankSlider(
    frame, crank, rod, slider,
    0, 0, 0,        // 曲柄转轴锚点
    0, 0, 1,        // 铰链轴（平面机构）
    0.4, 0, 0,      // 曲柄销
    1.2, 0, 0,      // 滑块销
    1, 0, 0,        // 滑轨轴
    false);
cs.setDrive(4.0, 80.0);
local stroke = cs.getSliderTranslation();
```

`getDriveAngle()` / `getSpinSpeed()` 读取驱动铰链状态；曲柄滑块另有
`getSliderTranslation()`。`getDriveJoint()` 等可取回底层 `Joint3D` 做应力阈值或调试。

所有关节均提供稳定 ID、连接 Body ID、`getConstraintForce*()`、
`getConstraintTorque*()` 和分离误差，适合调试约束、声音与破坏判定。销毁任一连接 Body 或
World 会使 Joint3D 包装器安全失效；也可显式调用 `joint.destroy()`。默认连接体互不碰撞，
可在创建时或通过 `setCollideConnected(true)` 开启。

需要可破坏结构时，用 `setForceThreshold(newtons)` 和
`setTorqueThreshold(newtonMetres)` 配置关节应力事件。任一阈值被超过的物理帧会发送
`jointstress3d`，四个整数参数依次为 Joint ID、Body A ID、Body B ID、关节类型代码
（0 Distance、1 Revolute、2 Prismatic、3 Spherical、4 Wheel、5 Weld、6 Motor、
7 Parallel、8 Filter）。同一帧的精确求解器力和扭矩从
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

1. 通过 `eve.Cloth()` 子模块调用 `newCloth(cols, rows, spacing, originX, originY)`（2D 像素）或 `newCloth3D(cols, rows, spacing, originX, originY, originZ)`（3D 米）— 默认钉住顶行。
2. `setBounds` 限制摆动范围；`applyForce` 可作风场。
3. `grabAt` / `moveGrab` / `releaseGrab` 做鼠标拖拽；`pin` / `unpin` 控制固定点。
4. `setSelfCollision(true)` 开启自碰撞（3D 含三角面级）；`setMaxFoldAngle` / `setFoldStiffness` 控制折角限制（3D 为相邻三角面二面角）。
5. `setCollideWorld(world)` / `setCollideWorld(world3)` 让布料与刚体碰撞（按质量比例动量交换）；`interactAt` 做指针吸引/排斥（与流体一致）。
6. `draw(gfx)` 绘制约束网与质点（2D）或三角网格（3D，需在 `gfx.render3D()` 之后调用）。

3D 版把 2D 接口按维度推广：`setGravity(gx,gy,gz)`、`setBounds(x,y,z,w,h,d)`、`getParticleX/Y/Z`、`grabAt(x,y,z,r)`、`applyForce(fx,fy,fz)`。

### 可交互流体

1. `newFluid2D(capacity)` 后 `setBounds` 建容器。
2. `emit` 喷射粒子；`interactAt` 做吸引/排斥搅拌。
3. 可调 `setSmoothingRadius` / `setRestDensity` / `setPressureStiffness` / `setViscosity`。
4. `draw(gfx)` 按密度着色绘制粒子。

3D 绳索现为可独立裁剪的 `physics_rope` 子模块，详见 `physics_rope.md`。

几何破碎（预切几何集合 / 连接图 / 场驱动）为可独立裁剪的 `physics_destruction`
子模块，详见 [`physics_destruction.md`](physics_destruction.md)；表现卫星见
[`physics_destruction_graphics.md`](physics_destruction_graphics.md)。

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

## 绑定与行为说明

### 3D 高级接口补充索引

以下方法补充了 3D 碰撞查询结果、接触事件、关节状态、距离场采样、材质参数和世界诊断的脚本索引。坐标分量方法按 `X/Y/Z` 成组使用；索引类 getter 的有效范围由对应的 `*Count()` 返回值决定。


### 关节与机械机构补充索引

下列方法覆盖 2D `Joint2D` / `Mechanism2D` 与 3D `Joint3D` / `Mechanism3D` 的脚本绑定；
与上文机构示例中的 `setDrive` / `getDriveAngle` / `getSpinSpeed` / `getDriveJoint` /
`getSliderTranslation` 一并使用。

## 等待流送地形后激活刚体

`TerrainLoadGravityState`、`beginTerrainLoadGravity(state,body,terrainFound)` 和
`advanceTerrainLoadGravity(state,body,terrainLoaded,dt,activationDelay)` 对应 Pcg
`RigidbodyWaitForTerrainLoad`。调用者先用世界流送索引判断刚体位置是否落在某个 terrain scene 内；存在时
begin 保存原 gravity scale 并临时设为 0，不存在时直接进入 completed 且不修改刚体。每帧注入当前 regular
load state 和 dt；首次观察到 loaded 后启动延时，延时到期时恢复原 gravity scale，并返回 `value=true`
一次，调用者可据此启用自己拥有的附属组件。

这项状态机有意保留 Pcg `Invoke` 的语义：延时一旦排定，之后短暂的 unloaded 观察不会取消激活。状态与
Body3D 都由调用者持有，函数不保存指针、不调用未知回调；无效对象、负数或非有限时间会在修改前失败。
`isMonitoring()`、`isActivationScheduled()`、`isCompleted()` 和 `getDelayElapsed()` 可用于存档外的运行诊断。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。

**源码：** [`src/modules/physics/`](../../../src/modules/physics/)
**相关测试：** [`test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp`](../../../test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp)、[`test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp`](../../../test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp)、[`test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp`](../../../test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp)

## 补充绑定参考

参数与接收者见 [源码生成参考](physics/api-reference.md)，使用前先确认本章的前置条件与生命周期。
## 场效果、空气动力学和轮胎

`physics/forces/ForceModels.h` 提供无状态、可组合的实时力模型；同一计算可用于 2D XY
平面和 3D。脚本先实例化 `eve.Physics()`，随后使用下列 `eve.*` 类和函数。
全部输入采用 SI：米、秒、kg、N、Nm、弧度。2D 刚体的位置/速度需除以
`world.getMeter()`；`applyForceSI2D` 在施力时自动转换回像素单位。

| 值对象 | 字段及含义 |
| --- | --- |
| `ForceVector` | `x`, `y`, `z`，默认零。2D 使用 `z=0`。 |
| `FieldRegion` | `kind`（global/sphere/rectangle/box）、`center`、`radius`（sphere 负数无限范围）、`halfExtents`、`edgeWidth`（内侧线性衰减带宽）。 |
| `DirectionalField` | `region`, `acceleration`，按质量换算为力的方向加速度场。 |
| `DirectionalForceField` | `region`, `force`，恒力场（N），不随刚体质量变化。 |
| `RadialField` | `region`, `strength`（正吸引/负排斥，m³/s²）, `softening`（正的核心半径）, `maxAcceleration`（m/s²）, `maxForce`（N）。两个上限均为有限非负数，零表示不施力；默认取数值类型最大值。 |
| `BuoyancyModel` | `gravity`, `fluidDensity`, `displacedVolume`（实际浸没排水体积 m³）。 |
| `DampingModel` | `linear`（kg/s）, `quadratic`（kg/m）；系数必须非负。 |
| `AngularDampingModel` | 非负 `linear`（kg·m²/s）、`quadratic`（kg·m²），以相对角速度计算阻尼力矩。 |
| `WingModel` | 世界空间单位正交 `chord`/`normal`，`density`, `area`, `liftSlope`, `zeroLiftAngle`, `stallAngle`, `baseDrag`, `aspectRatio`, `efficiency`。 |
| `FrictionModel` | `staticCoefficient >= kineticCoefficient >= 0`。 |
| `TireModel` | `radius`, `inertia`, `friction`, `longitudinalStiffness`, `corneringStiffness`, `rollingResistance`, `minimumSlipSpeed`。 |
| `TireInput` | `longitudinalVelocity`, `lateralVelocity`（接地点相对地面的接触坐标系速度），`angularVelocity`, `normalLoad`, `driveTorque`, `brakeTorque`, `longitudinalInverseMass`, `lateralInverseMass`（非负接触有效逆质量 1/kg；零表示规定车身速度）。 |

这些对象由脚本 VM 拥有。C++ 对应对象按值拥有数据。嵌套向量/区域建议先构造，再赋值
给模型字段；不要假定 getter 返回可长期修改的引用。所有调用同步完成，不保留输入或刚体。

| 脚本函数 | Result.value |
| --- | --- |
| `sampleDirectionalConstantForce(model, position)` | 世界空间 `{x,y,z}`，N，不使用质量。 |
| `sampleDirectionalForce(model, position, mass)` | 世界空间 `{x,y,z}`，N。 |
| `sampleRadialForce(model, position, mass)` | 软化的反平方吸引/排斥力；中心为零，范围外为零。 |
| `sampleCircleImmersion2D(center, radius, thickness, surfaceY)` | `{volume,fraction,centroid}`，圆截面浸没量和浮心。 |
| `sampleRectangleImmersion2D(center, width, height, thickness, surfaceY)` | 同上，轴对齐矩形。 |
| `sampleSphereImmersion3D(center, radius, surfaceY)` | 同上，球体。 |
| `sampleBoxImmersion3D(center, size, surfaceY)` | 同上，轴对齐盒体，size 为完整 XYZ 尺寸。 |
| `sampleBuoyancyForce(model)` | `-fluidDensity * displacedVolume * gravity`。 |
| `sampleDampingForce(model, relativeVelocity, mass, dt)` | 线性+平方阻力的隐式单步等效力。 |
| `sampleAngularDamping2D(model, relativeAngularVelocity, inertia, dt)` | 标量力矩 Nm；正转动惯量 kg·m²、正步长 s。 |
| `sampleAngularDamping3D(model, relativeAngularVelocity, principalInertia, dt)` | 惯量主轴系中的 `{x,y,z}` 力矩 Nm；三个主惯量都必须为正。 |
| `sampleWingForce(model, relativeVelocity)` | `{force, angleOfAttack, liftCoefficient, dragCoefficient}`。 |
| `sampleFrictionForce(model, tangentVelocity, demandedForce, normalLoad)` | 静止时抵消静摩擦限度内的切向外力；滑动时反向动摩擦。 |
| `stepTire(model, input, dt)` | `{longitudinalForce,lateralForce,slipRatio,slipAngle,angularVelocity,reactionTorque,resistanceTorque}`。 |
| `applyForceSI2D(body, fx, fy, px, py)` | 无值成功，在世界坐标施力点施加力并唤醒刚体。 |
| `applyForceSI3D(body, fx, fy, fz, px, py, pz)` | 无值成功，支持偏心力矩。 |

函数统一返回 Result 表，必须先检查 `ok`。非有限数、负质量/密度/载荷、非正时间、
无效轴、数值溢出等返回 `InvalidArgument`；失效刚体返回 `StaleHandle`。
施力只接受有效、启用的动态刚体，并在所有检查完成后修改。采样是纯计算，可以在任意
线程使用不变输入；施力只在物理 owner 线程、固定 step 前执行，不能在回调中重入。
没有新增 ECS System、对象注册表或跨帧借用。长时间保存刚体身份仍使用已有 PhysicsLink。

### 计算与边界

- 方向场模拟恒定加速度；真实排水浮力单独计算。浮力的排水体积和浮心由几何/水面
  调用者提供，2D 也必须指定有厚度的物理体积。它不自动切割碰撞体或求水面。
- “黑洞”是游戏用软化引力，公式为 `m * strength * delta / (r² + softening²)^(3/2)`，
  不包含相对论、事件视界或自动销毁。多个场按固定顺序加和；负强度给出斥力。
- 阻尼使用 `c=linear+quadratic*|v|`、`F=-c*v/(1+c*dt/m)`，避免单独阻尼使速度
  在一个时间步内穿零反向。速度是物体减介质速度；可以表达风、水流或其他介质。
- 机翼按弦向/法向截面的相对流速计算；翼展方向气流不产生截面力。升力方向垂直气流，
  阻力方向逆气流。使用 `q=ρv²/2`、线性升力斜率、连续失速衰减，诱导阻力系数
  `Cl²/(π*aspectRatio*efficiency)`。力应作用于气动中心；其点速度必须包含机体角速度。
  这是可调游戏模型，不是实测翼型、完整飞机、超声速或 CFD 模型。
- 摩擦模型要求调用者已经投影到接触切平面。它不是碰撞约束求解器；不要对已有
  Box2D/Box3D 接触摩擦重复施加。同样不要同时为同一物体重复启用等效 solver 阻尼。
- 轮胎按轮平面的纵向/横向接触速度求 slip，纵横合力限制在 `μN` 摩擦圆内。
  `reactionTorque=-Fx*radius`；驱动、反作用、制动和滚阻共同更新轮速。零载荷无接地力，
  但仍响应驱动/制动。刹车和滚阻本身不导致反转。计算不修改 `TireInput`；调用者在
  检查成功后提交一次 `angularVelocity`，将接触坐标力转到世界空间作用于接地点。
  悬架、地面查询、法向载荷、轮胎接地点、转向坐标与车身状态由车辆调用者提供。
  刚性大或惯量小时需缩短固定子步；不承诺任意 dt 下的轮胎能量稳定性。

时间由调用者注入，无全局时钟和 RNG；相同输入/顺序的浮点容差契约为相对 `1e-5`、
绝对 `1e-6`，不承诺跨平台位一致。当前接口只提供进程内值计算，没有新增持久化格式。
模型参数和轮速若需存档，应进入调用方版本化 schema，恢复后重新采样，不能保存待施加力
再重复提交。world step 失败时调用者不得重复累计已提交的力，应恢复/重建自己的完整仿真步。

公式依据：[NASA 升力](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/lift-equation/)、
[NASA 诱导阻力](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/induced-drag-coefficient/)、
[MathWorks 轮胎转动与组合滑移](https://www.mathworks.com/help/vdynblks/ref/combinedslipwheel2dof.html)。
失速曲线和摩擦圆饱和是本实现明确选择的实时近似，并非复制该产品的完整轮胎模型。

示例：[physics-force-lab](../../../examples/physics-force-lab/README.md)。
测试：`physics.forces.*` 覆盖纯模型、真实 2D/3D 刚体、单位转换及 Squirrel Result 投影。

## 2D 机械关节

`Joint2D` 已提供独立的 owning wrapper。C++ 使用 `Joint2D::create(world, bodyA, bodyB, settings)`
和 `Joint2D::createGear(world, first, second, ratio)`，返回 `Result<unique_ptr<Joint2D>>`。
脚本使用 `eve.createJoint2D(world, bodyA, bodyB, settings)` 和
`eve.createGearJoint2D(world, first, second, ratio)`；检查 Result.ok 后保存 value。
VM GC 释放 wrapper 会移除 backend 约束，必须在需要约束存活期间持有 value。

`eve.JointPoint2D()` 的 x/y 是米；`eve.Joint2DSettings()` 的 `kind` 支持
`revolute`、`prismatic`、`distance`、`pulley`、`wheel`、`weld`。
`anchorA`/`anchorB` 是世界空间施约束点，`axis` 为滑轨/悬架方向；
`groundA`/`groundB` 为滑轮固定支点；`length` 是距离约束长度；`ratio` 是滑轮长度比例；
`springHertz` 和 `dampingRatio` 设置距离、车轮、焊接的软约束；
`collideConnected` 设置相连刚体是否仍碰撞。

`Joint2D.getId()` 返回 world 内持久拓扑编号（空实例返回 0），`Joint2D.isValid()` 查询存活；`setMotor(enabled,speed,maximumForce)`、
`setLimits(enabled,lower,upper)`、`destroy()` 都返回 Result。
电机支持 revolute/wheel（rad/s、Nm）和 prismatic（m/s、N）；限位支持 revolute/prismatic。
`sample(dt)` 返回 Result.value，包含 `anchorA`、`anchorB`、`reactionForce`、`reactionTorque`、
`coordinate` 与 `secondaryCoordinate`。coordinate 分别为转角、平移、距离、齿轮比或滑轮 A 段长度；
滑轮 B 段长度位于 secondaryCoordinate。dt 为刚完成的正物理步长，用于冲量转反作用力。

齿轮通过两条已有 revolute/prismatic 关节约束 `coordinate1 + ratio*coordinate2`；
两条关节的 bodyB 必须是不同的动态刚体。源关节先销毁会先销毁从属齿轮；
齿轮先销毁不影响源关节。body/world 先销毁使剩余 wrapper 失效，不能继续访问 backend。
所有操作和析构限物理 owner 线程、solver 回调外。非法参数/错误 world 在创建前拒绝；
不支持的操作返回 Unsupported，已销毁对象返回 StaleHandle，锁定 world 返回 PreconditionViolation。

2D world snapshot 使用 schema v3，保存七种关节的稳定编号、端点、局部锚点、
转轴参考角、滑轨/车轮局部轴、电机、限位、弹簧、滑轮参考绳长和碰撞策略。
齿轮以源关节编号保存依赖，同时保留四个端点的创建参考姿态，用于重建原始传动相位；
这些是不可变的构造参数，不是第二套运行时 transform。恢复先在隔离世界创建普通关节，
再创建齿轮，因此输入顺序不影响依赖解析。未知字段、非法参数或缺少源关节会拒绝，
不会改变当前 world。未注册到引擎的 backend 关节不能静默保存。

v2 迁移为没有关节的 v3；v1 已超出在线迁移窗口，返回 UnknownVersion。
成功恢复会使旧对象和运行时 handle 失效，C++ 用 `World::resolveJoint2D(id)`
解析新关节，再通过 PhysicsJointLink 保存运行时引用。恢复创建的关节 wrapper 由 world 持有；
原先 `create` 返回的 wrapper 仍由调用方/VM 持有，恢复后处于失效状态。
求解器的 warm-start 冲量缓存不保存，不能承诺逐位一致的重放。
脚本调用 `eve.resolveJoint2D(world,id)` 返回 Result，value 为 `Joint2DReference`，
提供与 Joint2D 相同的 getId/isValid/sample/setMotor/setLimits/destroy 方法，
也能作为 createGearJoint2D 的源关节。该对象只持有 world 弱生命周期与代际 Link，
每次调用重新解析；再次恢复、源关节销毁或 world 销毁后返回 StaleHandle。
回收引用不会销毁关节，显式 destroy 才移除 solver 约束；失效引用的 destroy 也返回 StaleHandle。
由 create 返回的 owning Joint2D 则维持重复 destroy 成功的原有语义。
2D v3 同时保存 fixture 密度、摩擦、恢复系数、sensor、category/mask/group 和 tag，
以及刚体线性/角阻尼、重力倍率、刚体与 world 休眠开关。质量与惯量由恢复后的 fixture
密度和形状计算；恢复前校验过滤位宽和质量数值范围，错误输入不会触发 solver 断言。
v2 没有这些字段，迁移时显式采用原恢复路径的默认值：密度 1（chain 为 0）、摩擦 0.2、
恢复系数 0、非 sensor、category 1、mask 65535、group 0、空 tag；阻尼 0、重力倍率 1、
刚体允许休眠、world 禁用休眠。这些默认值不能还原旧存档从未记录的自定义设置。
底层直接设置的自定义质量、待积分外力和求解缓存不在当前格式中；3D 机械状态仍属于
计划 A 的后续工作，不能将此阶段视为完整物理存档交付。

### 3D 齿轮与滑轮耦合

`eve.createGearCoupling3D(world, firstJoint, secondJoint, settings)` 和
`eve.createPulleyCoupling3D(world, bodyA, bodyB, settings)` 返回 Result；成功时
`value` 是 VM 独占的 `Coupling3D`。必须保存它，GC/VM 释放会移除约束，源刚体和关节继续存在。
世界、源关节或源刚体先销毁，会使该包装器失效。所有操作与销毁在物理世界所属线程进行，
不得在求解回调或 World3D.step 期间调用。工厂验证参数类型、同世界归属、存活状态和有限数值；
失败返回结构化诊断，不修改刚体。创建涉及端点查找和分配，应在初始化时进行。

- `eve.CouplingPoint3D()`：x/y/z，单位米。
- `eve.CouplingLimits3D()`：positionCorrection（默认 0.2，范围 0–1）、
  maxCorrectionSpeed（默认 10）、maxForce（默认 1000000），后两项要求有限正数。
- `eve.GearCoupling3DSettings()`：ratio（默认 1，有限非零）和 limits。
  两个源可为转轴或滑轨，保持 q1 + ratio*q2 的创建时相位；混合时 ratio 含长度单位。
- `eve.PulleyCoupling3DSettings()`：localAnchorA、localAnchorB 是刚体局部连接点，groundA、groundB 是固定世界导向点，
  ratio 为正，length 是加权总绳长，关系为 lA + ratio*lB = length。
  tensionOnly 默认为 false；设为 true 时绳索只拉不推，允许松弛。limits 控制修正和力上限。

`Coupling3D.getId()` 返回世界内编号；`isValid()` 查询存活；
`setEnabled(enabled)` 返回 Result，禁用期间停止施力但继续跟踪相位；
`destroy()` 返回 Result，重复销毁成功；`sample()` 返回 Result，其 value 包含
coordinateA、coordinateB、error、impulse、active 和 saturated。
impulse 是最近一次成功物理步的广义冲量，saturated 表示达到了力上限。
错误类型的工厂输入或方法接收者安全拒绝；无效接收者 getId 返回 0、isValid 返回 false。

实现采用原生求解之前的速度投影和有界相位修正，后端拒绝本步时回滚投影。
角度展开要求每步相对转动小于 pi；采用小固定步长。误差修正可能使瞬时速度比例略偏离理想值。
本模型不计算齿面接触，原生接触与电机组合精度仍在验证。3D v3 存档包含耦合参数和多圈相位，
恢复时先重建原生关节与运行时句柄，再解析源引用。

3D 读档创建的 Body3D、Shape3D 和 Joint3D 包装器由世界持有；失败候选全部释放，成功时统一接管。
通过查找接口取得的是借用对象，不能 delete 或重新交给 owning 智能指针。
它们的借用有效期截止于对象销毁、下一次成功恢复或世界销毁；跨读档应重新解析句柄。
调用者原先创建并持有的包装器在成功恢复后失效，但仍由原调用者释放。

3D 世界存档现使用 schema v3，保存五种原生关节的完整局部连接坐标与 parameters 对象：
电机开关/速度/最大作用力、弹簧开关/频率/阻尼/目标、限位、碰撞连接标志、约束调节和破坏阈值。
距离关节另保存静止长度和弹簧力区间；球形关节保存锥角、扭转、目标四元数及电机角速度；
车轮保存悬挂、旋转电机和转向设置。数值字段使用固定长度数组，开关使用布尔值。
未知、缺失、越界、错误类型和非法四元数在候选世界中拒绝，失败不改变当前世界。
v2 不含 parameters，迁移为构造默认参数；v1 继续沿用依赖当前拓扑的迁移规则。
此格式不保存求解器热启动缓存。couplings 数组保存稳定编号、源对象编号、参数、启停状态、
上次成功冲量及饱和标志；齿轮保存参考相位和多圈坐标，滑轮保存连接点、绳长和松绳设置。
旧版迁移为空耦合集合。C++ 可用 World3D::resolveCoupling3D(id) 取得 ResultRef 借用，
恢复的耦合包装器由世界持有；借用不能删除，成功恢复后必须重新查询。脚本通过 eve.resolveCoupling3D(world,id) 获取恢复后的观察引用。

World3D 步进期间拒绝存档与恢复（包含 provider 回调内重入）。恢复的新运行时句柄和索引
在候选世界中准备，成功提交时统一替换；候选准备或后端恢复失败不会消耗当前世界的句柄序列。


`eve.resolveCoupling3D(world,id)` 返回 Result，成功 value 为 `Coupling3DReference`，提供
getId、isValid、sample、setEnabled 和 destroy；GC 只释放这个观察对象，不移除世界中的约束。
引用同时校验世界和该约束实例的弱生命周期，成功读档、源销毁或约束销毁后立即失效，
不会因为相同编号被重新创建而重新指向新对象。此时 getId 保留原编号，isValid 返回 false，
其他方法返回 StaleHandle；需要重新查询。通过有效引用 destroy 会移除约束，重复调用引用的
 destroy 返回 StaleHandle。工厂返回的 owning Coupling3D 则仍支持重复 destroy 成功。
所有操作限物理所属线程，禁止在步进回调中改变约束；错误类型输入安全拒绝。

3D v3 刚体的 dynamics 保存 linearDamping、angularDamping、gravityScale、sleepThreshold 和
allowSleep，世界层也保存 allowSleep。阻尼与休眠阈值要求有限非负，重力倍率允许有限负数。
恢复先完成形状和关节，再设置速度与休眠，避免创建关节时的唤醒覆盖存档状态。
v1/v2 缺少这些字段时显式使用固定后端的默认刚体参数，世界休眠开关沿用旧恢复路径的 false；
旧版意外携带这些新字段会被拒绝。

3D v3 形状记录的 localTransform 保存局部平移与单位四元数，共 7 个数值；恢复会重建碰撞几何，
再恢复刚体速度。未知形状字段、非法数组、非单位四元数及超 float 范围输入均拒绝。
v1/v2 使用旧格式的单位局部变换。恢复后的形状事件索引按持久编号重建，稀疏编号不会变成创建顺序编号。
3D v3 还保存形状密度、表面摩擦/反弹/滚动阻力、切向速度、64 位材质标识与碰撞掩码、
分组、标签、传感器和事件开关、单向接触平面、爆炸响应及网格逐三角形材质槽。
恢复在独立候选世界中执行；密度提交前检查质量、惯量及求逆的有限范围，非法输入不改变原世界。
世界级按 Body/Shape 配对的碰撞忽略规则尚未纳入这一保存范围。

### C++ 批量 SI 外力

`physics/forces/ForceBatch.h` 提供 `ForceLoad` 和一次性 `ForceBatch`。
每条请求包含 `PhysicsLink`、世界坐标作用点（米）、力（牛顿）和额外力矩（牛顿米）。
2D 使用 XY 力和 Z 力矩，直接进入后端 SI 单位，结果不依赖 world 的像素比例。

先用 `ForceBatch::prepare(world, loads)` 校验并复制请求，再检查 Result 后调用
`batch.commit(world, step)`。准备不改变世界；提交再次解析所有句柄，按当前质心计算偏心力矩，
将同一刚体的合力与合力矩统一提交。静态/禁用/失效/跨世界目标、非有限值和 float 范围溢出
会在任何施力前拒绝整批。批次只持有数值与句柄，删除刚体或恢复世界会使旧目标失效。
移动会转移提交权；成功提交后再次提交被拒绝。调用须在仿真 owner 线程、solver 回调之外。

这一步不推进仿真；随后 solver 失败时回执保持 pending，使用原步重试，已提交批次不可重新提交。
后端既有外力仍按其累计规则保留，因此不能与不受限的 raw backend 外力累计混用。
准备和提交都以请求数量承担 O(n log n) 聚合成本及线性存储；按固定仿真步组织批次。
脚本场集合可以创建并提交该批次；通用脚本外力批次与车辆接入仍在后续计划中。

径向场先计算软化反平方衰减与区域边缘淡出，再同时限制力和加速度；两个上限取更严格者。
中心点返回零力，正负 strength 保持吸引/排斥对称。极小软化半径使用对数幅值计算，
使限幅在中间数值溢出之前生效。方向恒力场与方向加速度场使用不同类型，避免单位混用。

`FieldRegion.kind` 可取 `"global"`、`"sphere"`、`"rectangle"`、`"box"`。
`sphere` 使用 radius，负半径表示全局；二维 XY 切片自然得到圆形。
`rectangle` 使用 halfExtents 的正 X/Y 半尺寸，Z 半尺寸必须为零，并忽略采样点 Z；
`box` 使用三个正半尺寸，均为世界轴对齐。center 和尺寸单位为米。
edgeWidth 是内侧淡出宽度，不超过最小有效半尺寸；按距最近边界的距离线性淡出，
边界及范围外权重为零。零宽度表示硬边界。未知 kind 和非法尺寸返回失败 Result。
这套范围共同用于方向加速度、方向恒力和径向场；对象过滤与场集合仍待接入。

### C++ 场集合与对象过滤

`physics/forces/FieldSet.h` 的 `FieldDefinition` 包含方向加速度、方向恒力或径向场，
以及 `FieldFilter.includeAny/excludeAny` 两个 64 位类别掩码。
`FieldTarget` 包含 `PhysicsLink` 与调用方指定的 categories：至少命中一个 includeAny 位，
且没有命中任何 excludeAny 位，才受该场影响。类别为零不匹配任何场；排除优先。
这些类别用于场效果，不读取或修改碰撞过滤规则。

`FieldSet::build(definitions)` 校验并复制全部定义，包括暂时没有匹配对象的场。
创建后集合不可修改，按定义顺序计算并叠加，允许跨步复用。
`set.prepare(world, targets)` 读取动态刚体当前质量与 SI 质心，在质心采样所有匹配场，
返回一个待提交的 `ForceBatch`；然后检查 Result 并调用 `commit(world)`。
重复目标、失效句柄、静态或禁用刚体、非法输出会拒绝整批，准备不会唤醒刚体。
提交前再次校验目标，合力和力矩均为零时不唤醒。字段集合本身不持有 world 或 body 指针。

准备捕获当前质量、位置和类别，须在改变这些状态或推进仿真之前及时提交；
场按质心作用，不代替按浸没体积采样的浮力。构建成本按定义数量线性增长；
每步准备成本为场数乘目标数，再加目标聚合成本。脚本集合接口见下节。

### 脚本场集合与一次性提交

所有函数位于 `eve`，返回必须检查 `ok` 的 Result 表。
`FieldSetBuilder()` 与 `FieldTargets()` 由 VM 持有；FieldSet/ForceBatch 只能从成功 Result 获取。
类别掩码必须是无符号十进制字符串（0 至 18446744073709551615），不能用浮点数替代。

| 函数 | 行为 |
| --- | --- |
| `addDirectionalField(builder, model, includeAny, excludeAny)` | 校验后追加方向加速度场的副本。 |
| `addDirectionalConstantField(builder, model, includeAny, excludeAny)` | 校验后追加恒力场副本。 |
| `addRadialField(builder, model, includeAny, excludeAny)` | 校验后追加径向场副本。 |
| `buildFieldSet(builder)` | value 为不可变 FieldSet，后续修改 builder 或 model 不影响它。 |
| `addFieldTarget2D(targets, body, categories)` | 保存 2D PhysicsLink 与类别副本，不持有 body。 |
| `addFieldTarget3D(targets, body, categories)` | 保存 3D PhysicsLink 与类别副本，不持有 body。 |
| `prepareFieldForces2D(set, world, targets)` | value 为按当前状态准备的 ForceBatch，不施力。 |
| `prepareFieldForces3D(set, world, targets)` | 同上，针对 3D 世界。 |
| `commitForceBatch2D(batch, world, tick, nanoseconds)` | 一次性提交 2D 批次，返回 `ForceReceipt`；重复或错误世界提交失败。 |
| `commitForceBatch3D(batch, world, tick, nanoseconds)` | 一次性提交 3D 批次，返回 `ForceReceipt`；重复或错误世界提交失败。 |

添加失败不改变构建器/目标列表。重复目标在 prepare 时拒绝。
构建器和目标列表可复用；需要替换配置时新建容器。FieldSet/ForceBatch 不保留世界裸指针，
销毁或恢复后的旧目标通过代际句柄检查拒绝。GC 只释放配置/批次，不会施力或销毁刚体。
全部世界操作在 owner 线程执行；准备后应及时提交，之后再推进固定仿真步。

### 规则几何浸没与浮心

C++ `BuoyancyGeometry` 和上述四个脚本函数按水平液面 `y <= surfaceY` 计算，约定 +Y 向上。
返回实际排水体积 volume（m³）、体积分数 fraction 和世界坐标浮心 centroid（m）。
2D 的厚度必须显式为正，center.z 必须为零；矩形/盒体只支持世界轴对齐。
圆和球不受旋转影响。干燥时体积和比例为零，浮心返回原中心作为占位值。
非法尺寸、非有限输入及不可表示的体积/浮心返回 InvalidArgument。

将 volume 填入 BuoyancyModel.displacedVolume，计算浮力后在 centroid 处施加，
即可获得偏心力矩。几何函数是纯计算，不读取或修改刚体，也不会自动匹配 collider。
圆弓形和球冠使用解析积分，近乎干燥的圆使用小角展开避免相减损失精度。
任意旋转多面体裁切、波浪液面和流体动力求解不在这四个原语接口的范围内。

### 角阻尼与转动惯量

角阻尼使用 body-minus-medium 相对角速度（rad/s），不是线速度。
令 c = linear + quadratic * |relativeAngularVelocity|，每个惯量主轴上的等效力矩为
`-c * omega / (1 + c * dt / I)`，其中 I 为对应转动惯量。
在没有其他力矩的单步积分中，它减小相对旋转能量且不会单独造成旋转反向。
2D 返回绕 Z 的标量力矩。3D 输入速度与主惯量必须在同一主轴系；输出力矩也在该系，
调用者须旋转到世界系后通过 ForceLoad.torque 提交。一般非对角惯量不能直接把矩阵对角项当主惯量。
介质旋转较快时，阻尼可以加快刚体的世界角速度，但仍减小相对于介质的旋转。
该模型不包含陀螺项或其他力矩，也不会自动修改 backend damping 配置。

### 多翼面、尾翼与舵面组合

C++ `WingSurface` 以刚体局部坐标描述气动中心 center（米）和 WingModel 的单位 chord/normal。
`WingAssembly::build(surfaces)` 验证并复制这些值，生成不可变集合。
`prepare(world, bodyLink, deflections, wind)` 读取刚体实际姿态，转换翼面方向和气动中心，
在各中心读取包含 `omega cross r` 的点速度，减去世界风速后计算气动力。
结果是一个 ForceBatch；检查成功后统一提交，偏心力矩按实际质心累加。

每个翼面都有对应的有限偏转角（rad），固定翼面也提供零。
正角将 chord 向 normal 旋转，同时旋转 normal，不只是修改升力数值。
通过多个局部位置和独立角度，可组合机翼、尾翼、升降舵和差动副翼。
2D 要求局部中心、chord/normal、wind 的 Z 为零；3D 自动使用完整刚体旋转。
集合不拥有刚体，不保存控制输入指针；准备后在改变姿态/控制/风或步进前提交。
配置验证按翼面数线性增长，每步采样与批次准备也按翼面数付费。
该截面模型不包含翼面间尾流、下洗相互作用或 CFD。

| 脚本函数 | 行为 |
| --- | --- |
| `addWingSurface(builder, model, center)` | 向 WingAssemblyBuilder 追加已验证的 WingModel 与局部气动中心副本。 |
| `buildWingAssembly(builder)` | Result.value 为 VM 拥有的不可变 WingAssembly。 |
| `addWingDeflection(controls, angle)` | 按翼面顺序向 WingControls 追加弧度偏转角。 |
| `setWingDeflection(controls, index, angle)` | 更新现有角度；越界或非有限值返回失败。 |
| `prepareWingForces2D(assembly, world, body, controls, wind)` | Result.value 为尚未施力的 2D ForceBatch。 |
| `prepareWingForces3D(assembly, world, body, controls, wind)` | Result.value 为尚未施力的 3D ForceBatch。 |

脚本用 `eve.WingAssemblyBuilder()` 和 `eve.WingControls()` 创建配置容器，
控制角数量必须与翼面数量一致；采用已有 `commitForceBatch2D/3D` 提交。
所有操作返回 Result。添加失败不改变构建器；无效翼面、错误维度、失效目标或任一采样失败
不会部分施力。模型和中心使用值副本，VM 回收集合或未提交批次不会改变物理世界。

### 轮胎低速与接触冲量

轮胎现采用隐式滑移冲量，轮速和车身反作用使用同一纵向冲量。
输入 longitudinalInverseMass/lateralInverseMass 描述接触切线方向的对角有效逆质量；
零表示该方向的车身速度由调用方规定，正值则预测该接触力引起的速度变化。
接地点偏离质心时，有效质量应包含刚体转动响应，不能一律使用质量倒数。
一般接触响应的交叉项不在这两个标量输入中，车辆适配仍需显式处理。

每步以预测自由轮速冻结滑移分母/刚度，隐式求纵向和侧向冲量，再限制联合摩擦圆。
制动与滚阻作为有界旋转阻力，使用固定最多 64 次的单调求解，在容量允许时锁止轮速。
起步驱动和滚阻都在当步反馈到接触力，不延迟一帧。
resistanceTorque 是实际施加的有符号制动/滚阻扭矩，reactionTorque 为 -Fx*radius；
结合 driveTorque 可核对输出轮速的角冲量一致性。slipRatio/slipAngle 表示预测步末滑移。
这仍是固定步长的游戏轮胎模型，需要通过子步收敛验证精度；数值不可表示或锁止求解
未达到容差时返回失败，不发布部分结果。模型不自动推进车身或提交轮速。

### 外力批次的步进回执

`ForceBatch::commit(world, SimulationStep)` 成功返回 `Result<ForceReceipt>`，批次被消费。
所有尚未完成的批次必须绑定同一个 tick 和纳秒步长（大于 0，最多 50 ms）。
世界拒绝不同 tick 或步长的步进，拒绝重复提交已消费的批次。
后端失败保留已提交的力和 `Pending` 回执；重试原来的 `world.step(step)`，不要重新施力。
只有该步成功后，世界才将回执改为 `Applied`。轮胎调用方应届时发布预测轮速。
多个批次共享该世界步的完成状态，查询不调用脚本，不保留世界或刚体指针。

成功 restore 或 destroy 将未完成回执变为 `Cancelled`；失败 restore 保持回执和原状态。
已完成的回执不随后续世界销毁改变。回执可以比世界活得更久，但只能在仿真线程查询。
回执证明世界步成功，不证明提交后被调用方主动销毁的目标仍存在；发布车辆状态前仍须解析 PhysicsLink。
快照拒绝存在待处理批次的世界；回执是瞬时状态，不进入持久化格式。
批次只负责自己的提交，不允许绕过 World 直接步进、清除外力或关闭 Box2D 自动清力；
也不要与无界的原始后端外力累加混用。

脚本的 `tick` 和 `nanoseconds` 使用无符号十进制字符串，避免浮点时间取整或 64 位精度丢失。
以下函数都返回结构化 Result 表：

| 函数 | 行为 |
| --- | --- |
| `forceReceiptStatus(receipt)` | `.value` 为 `pending`、`applied` 或 `cancelled`。 |
| `stepPhysicsWorld2D(world, tick, nanoseconds, velocityIterations, positionIterations)` | 使用精确时间推进 2D 世界，返回求解结果。 |
| `stepPhysicsWorld3D(world, tick, nanoseconds, subStepCount)` | 使用精确时间推进 3D 世界，返回求解结果。 |

```squirrel
local submitted = eve.commitForceBatch3D(prepared.value, world, "1", "10000000");
assert(submitted.ok);
local advanced = eve.stepPhysicsWorld3D(world, "1", "10000000", 4);
assert(advanced.ok);
assert(eve.forceReceiptStatus(submitted.value).value == "applied");
```


### 合并后的后端支持边界

2D 像素坐标工厂与 SI Joint2D 工厂共享一套关节注册表、句柄及销毁规则。
Motor 关节也参与 2D 快照；兼容 C++ destroy() 抛出失败，规范 destroyChecked() 返回 Result。
World 拥有求解器关节及恢复生成的包装对象，普通工厂包装对象由调用者或 VM 拥有。

3D 通用外力、力场和翼面通过后端接口支持 Box3D/Jolt。
Coupling3D 齿轮/滑轮耦合当前仅支持 Box3D，Jolt 创建时返回 Unsupported。
Box3D 使用包含耦合与完整原生参数的 v3 快照；Jolt 保留上游 portable v2 快照。
两种格式不承诺跨后端互换，也不保存求解器热启动缓存。

### module-interface: World3D projectile sweep integration

`World3D.queryToken()` returns an opaque process-local token for an optional projectile query binding.
`CombatCarrierRuntime.bindWorld3D(token, radius, categoryBits, maskBits, ignoredBodyId)` checks a Result and
creates a physics-owned query session; `clearWorld3D()` removes it only while the pool is empty.
See [weapon World3D integration](weapon.md) and the runnable `world-sweep-ballistics` example.
The `IWorldSphereSweepFactory` capability is published by physics and consumed only at binding time.
The owning query session resolves authoritative world handles on the simulation thread and never stores a raw world pointer.
Successful world restore increments the query epoch; stale bindings, including stale excluded bodies, fail explicitly.
No new ECS System or tool protocol is introduced. Physics collision results retain body/shape provenance,
while gameplay entity mapping remains with the game. The existing backend provides the shape query;
filter parameters are scoped, results are owning, and overflow is an error rather than a truncated hit list.

## 烘焙与重放

烘焙属于 Physics。physics.bake() 提供 loadRigid/loadDeform/loadParticles(path,recipe)，
physics.bakeRecording() 提供 beginRigid/beginDeform。它们是宿主服务，不是独立模块。
布料和绳索通过 source.captureBake(recording,seconds) 采集，由各生产方模块拥有实现；
粒子使用 particles.bakeRecording() 的 begin/finish/newArchive，不归 Physics 管理。

纯资产、轨道、采样和播放状态位于 physics/backend/bake/，归属已有 physics_backend。
Box2D 的具体推进桥接归属 Physics 宿主，backend 本身不依赖求解器。
只保留一个跨域的 physics_bake_graphics 模块，原生与脚本呈现代码在同一模块内。

原生呈现器只拥有冻结资产和缓冲。createMesh(player,graphics)、
upload(player,graphics,mesh)、draw(player,graphics,material) 在调用期间借用播放器。
脚本 prepareDeform(player,radius)/prepareParticles(player,ids) 保留输入播放器的 VM 拥有引用，
控制方法与绘制使用同一份时间、速率、暂停和 placement。独立实例必须显式调用并检查
player.prepareInstance()；不要通过多个包装器对同一个播放器重复 advance。
材质资源仍按绘制借用，必须存活到 Graphics 完成相关帧。

旧 eve.PhysicsBake()/eve.PhysicsBakeRecording()/eve.ParticleBakeRecording() 仅兼容，
内部使用同一实现，不再注册 ModuleManager 或出现在模块启动列表。
持久格式和迁移版本不变；不覆盖已存在资产，缓存检查完整 provenance。
准备、加载、保存必须在帧循环之外执行，并检查每个 Result。

physics-bake-only 和 physics-bake-script-only 仅选择 common + physics_backend，
前者排除 VM 投影；physics-bake-render 组合呈现而不引入实时求解器。
