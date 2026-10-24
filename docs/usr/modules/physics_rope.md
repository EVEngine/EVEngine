# Physics Rope 子模块

`physics_rope` 位于 `src/modules/physics/rope`，依赖 `physics` 与 `schema`，但不属于
Physics 核心。脚本入口为 `local ropes = eve.Rope()`；创建使用
`newRope3D(...)` 或严格 Schema 入口 `newRope3DFromJson(json)`。C++ 工具可先调用
`registerRope3DCreateSchema()` 注册 `physics:rope3d-create@1`。
模块的 `getName()` 返回脚本模块名。

Rope3D 提供 XPBD 距离/弯曲约束、自碰撞、塑性、动态长度、切断/修复、自动断裂、
解析碰撞体、World3D 网格/高度场连续碰撞、SDF 连续碰撞以及刚体双向冲量。World3D
和 DistanceField3D 是借用提供者，必须先解除再销毁。

## 从悬索到游戏更新

下例创建 16 粒子的悬索，首尾固定。创建放在 init，update 放在模拟阶段；独立演示中
用固定步长推进，不与正在运行的自动模拟重复更新。

```squirrel
local ropes = eve.Rope();
local cable = ropes.newRope3D(16, -2.0, 3.0, 0.0, 2.0, 3.0, 0.0);
cable.pin(0);
cable.pin(15);
for (local i = 0; i < 30; ++i) cable.update(1.0 / 60.0);
assert(cable.getParticleCount() == 16);
print("middle y=" + cable.getParticleY(8) + "\n");
```

第二个任务是攀爬绳/拖曳线：用 attach 建立附件，移动附件而非每帧重建绳。
约束、碰撞与切断改变的是模拟状态；绘制用 getSample* 采样，不把 getParticle* 的离散
节点直接当作平滑曲线。采样坐标与游戏 World3D 保持同一单位和变换。

World/SDF provider 存活期间才允许碰撞更新。退出关卡时先停止 update，再解除
setCollideWorld/setCollideSdf 的引用，最后释放 provider。newRope3D 的返回对象交给 VM
管理；没有脚本 destroy 方法，不能照搬 Body 的释放协议。JSON 构造错误会抛脚本错误，
不是返回一个可继续使用的空绳。源代码与断裂、边界条件证据见
[Rope.cpp](../../../src/modules/physics/rope/Rope.cpp)、
[physics_rope.cpp](../../../test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp)。

主要参数与观测：`update()`、`setGravity()`、`getGravityX()`、`getGravityY()`、
`getGravityZ()`、`setStretchCompliance()`、`setDistanceConstraintsEnabled()`、
`getDistanceConstraintsEnabled()`、`setBendCompliance()`、`setBendConstraintsEnabled()`、
`getBendConstraintsEnabled()`、`setMaxBending()`、`getMaxBending()`、`setPlasticity()`、
`getPlasticYield()`、`getPlasticCreep()`、`getBendPlasticity()`、`setMaxCompression()`、
`setDamping()`、`getDamping()`、`setParticleMass()`、`getParticleMass()`、`setRadius()`、
`getRadius()`、`setSelfCollision()`、`getSelfCollision()`、`setBounds()`、`clearBounds()`。

碰撞：`setCollisionFriction()`、`getCollisionFriction()`、`setCollisionRestitution()`、
`getCollisionRestitution()`、`setContinuousCollision()`、`getContinuousCollision()`、
`setCollideWorld()`、`getCollideWorld()`、`setCollideSdf()`、`getCollideSdf()`、
`addSphereCollider()`、`addPlaneCollider()`、`moveSphereCollider()`、`removeCollider()`、
`clearColliders()`。

拓扑与采样：`pin()`、`attach()`、`moveAttachment()`、`detach()`、`isAttached()`、
`applyForce()`、`setRestLength()`、`changeLength()`、`getRestLength()`、`calculateLength()`、
`cut()`、`repair()`、`isElementActive()`、`getElementForce()`、`setTearing()`、
`disableTearing()`、`getTopologyRevision()`、`getLastTornElementCount()`、
`getLastTornElement()`、`getParticleCount()`、`getParticleX()`、`getParticleY()`、
`getParticleZ()`、`getSampleX()`、`getSampleY()`、`getSampleZ()`、
`getSampleTangentX()`、`getSampleTangentY()`、`getSampleTangentZ()`。
