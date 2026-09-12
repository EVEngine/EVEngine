# Cloth 布料子模块

`physics_cloth` 是依赖 `physics`、`graphics` 与 `gpgpu` 的可选卫星模块。它独立拥有 2D、3D 与 GPU 布料运行时；刚体、流体项目可以只启用 `physics`，不必链接布料求解器和 GPU 布料实现。

```nut
local clothModule = eve.Cloth();
local cloth2d = clothModule.newCloth(18, 12, 14.0, 48.0, 36.0);
local cloth3d = clothModule.newCloth3D(16, 12, 0.4, -3.0, 3.2, -2.0);
local clothGpu = clothModule.newClothGPU(40, 30, 8.0, 40.0, 30.0);
```

`newCloth` 创建 CPU 2D 网格，`newCloth3D` 创建支持 XPBD、体积/压力、tether、skin/backstop、attachment、碰撞过滤和撕裂的 CPU 3D 网格，`newClothGPU` 创建 Vulkan compute 2D 网格。实例由调用者持有，结束使用时调用 `destroy()`；模块不保留跨帧的实例裸指针。

完整参数、交互和材质调节示例见 [Physics 模块](physics.md) 的 Cloth 章节。

## 公共 API

- 生命周期与运行：`update()` 推进一步模拟，`reset()` 恢复初始状态，`draw()` 绘制当前网格，`destroy()` 释放实例。
- 网格与粒子查询：`getCols()`、`getRows()`、`getSpacing()`、`getOriginX()`、`getOriginY()`、`getOriginZ()`、`getParticleCount()`、`getParticleX()`、`getParticleY()`、`getParticleZ()`、`getTriangleCount()`、`getDistanceConstraintCount()`、`getTetherConstraintCount()`、`getSkinConstraintCount()`、`getAttachmentCount()`。
- 基础求解参数：`setGravity()` 配合 `getGravityX()`、`getGravityY()`、`getGravityZ()`；`setStiffness()`/`getStiffness()`、`setIterations()`/`getIterations()`、`setDamping()`/`getDamping()`、`setParticleSize()`/`getParticleSize()`、`setParticleMass()`/`getParticleMass()`。
- 粒子控制：`setParticlePosition()`、`setParticleInverseMass()`/`getParticleInverseMass()`、`pin()`、`unpin()`、`pinTopRow()`、`isPinned()`、`applyForce()`。
- 交互：`grabAt()`、`moveGrab()`、`releaseGrab()`、`isGrabbing()`、`getGrabIndex()` 和 `interactAt()`。
- 边界与碰撞：`setBounds()`/`clearBounds()`、`setCollideWorld()`/`getCollideWorld()`、`setSelfCollision()`/`getSelfCollision()`、`setCollisionMaterial()`、`getCollisionFriction()`、`getCollisionRestitution()`、`setCollisionFilter()`、`getCollisionCategoryBits()`、`getCollisionMaskBits()`。
- 弯折和 XPBD 材质：`setFoldStiffness()`/`getFoldStiffness()`、`setMaxFoldAngle()`/`getMaxFoldAngle()`、`setStretchCompliance()`/`getStretchCompliance()`、`setShearCompliance()`/`getShearCompliance()`、`setBendCompliance()`/`getBendCompliance()`。
- Tether：`setTetherScale()`/`getTetherScale()` 和 `setTetherCompliance()`/`getTetherCompliance()`。
- 封闭体积：`setPressure()`/`getPressure()`、`setVolumeCompliance()`/`getVolumeCompliance()` 和 `getCurrentVolume()`。
- Skin/backstop：`setSkinConstraint()`、`updateSkinReference()`、`clearSkinConstraint()`、`hasSkinConstraint()`。
- Attachment：`attachParticle()`、`updateAttachment()`、`detachParticle()`、`isAttached()`。
- 撕裂：`setTearThreshold()`/`getTearThreshold()`、`setMaxTearsPerStep()`/`getMaxTearsPerStep()`、`tearConstraint()`、`getTornConstraintCount()`。
- 空气动力学：`setWindVelocity()` 配合 `getWindVelocityX()`、`getWindVelocityY()`、`getWindVelocityZ()`；`setAerodynamics()` 配合 `getAirDensity()`、`getDragCoefficient()`、`getLiftCoefficient()`。
- 后端能力：`getBackendName()` 返回当前后端，`supportsFeature()` 用于在启用高级约束前进行显式能力检测。
- 外观：`setColor()` 设置调试和示例绘制颜色。
