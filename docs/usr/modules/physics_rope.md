# Physics Rope 子模块

`physics_rope` 位于 `src/modules/physics/rope`，依赖 `physics` 与 `schema`，但不属于
Physics 核心。脚本入口为 `local ropes = eve.Rope()`；创建使用
`newRope3D(...)` 或严格 Schema 入口 `newRope3DFromJson(json)`。C++ 工具可先调用
`registerRope3DCreateSchema()` 注册 `physics:rope3d-create@1`。
模块的 `getName()` 返回脚本模块名。

Rope3D 提供 XPBD 距离/弯曲约束、自碰撞、塑性、动态长度、切断/修复、自动断裂、
解析碰撞体、World3D 网格/高度场连续碰撞、SDF 连续碰撞以及刚体双向冲量。World3D
和 DistanceField3D 是借用提供者，必须先解除再销毁。

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
