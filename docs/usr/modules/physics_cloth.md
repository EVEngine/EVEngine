# Cloth 布料子模块

`physics_cloth` 是依赖 `physics`、`graphics` 与 `gpgpu` 的可选卫星模块。它独立拥有 2D、3D 与 GPU 布料运行时；刚体、流体项目可以只启用 `physics`，不必链接布料求解器和 GPU 布料实现。

```nut
local clothModule = eve.Cloth();
local cloth2d = clothModule.newCloth(18, 12, 14.0, 48.0, 36.0);
local cloth3d = clothModule.newCloth3D(16, 12, 0.4, -3.0, 3.2, -2.0);
local clothGpu = clothModule.newClothGPU(40, 30, 8.0, 40.0, 30.0);
```

`newCloth` 创建 CPU 2D 网格，`newCloth3D` 创建支持 XPBD、体积/压力、tether、skin/backstop、attachment、碰撞过滤和撕裂的 CPU 3D 网格，`newClothGPU` 创建 Vulkan compute 2D 网格。C++ 工厂返回调用者所有的对象；模块不保留跨帧的实例裸指针。
脚本 `newCloth3D` 返回 VM 所有的实例，最后一个脚本引用释放时删除原生对象。
`destroy()` 只停止该对象的工作，不替代释放所有脚本引用；GC 可回收其原生存储。
2D/GPU 工厂的既有资源合同不因本次 3D 所有权修复改变。

完整参数、交互和材质调节示例见 [Physics 模块](physics.md) 的 Cloth 章节。

## 公共 API

- 模块识别：`getName()` 返回模块名（每个 `eve.*` 模块类都有它），便于脚本确认 `eve.Cloth()` 交回来的是哪个模块。
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

### 3D 脚本工厂所有权

`newCloth3D` 的参数、返回实例和失败抛异常行为保持兼容；内部使用标准 owned VM 实例发布，
创建或发布失败不会泄漏候选对象。多个脚本变量引用同一包装器时，不会提前删除或重复删除。
原生调用者从 VM 借出的 Cloth3D 指针只在对应脚本 owner 存活期间有效；跨调用消费者必须
保留 owner 或先解除绑定。退出时停止 update，解除 world/SDF 等借用，再释放 provider。
烘焙录制只复制拥有型帧，录制后释放最后源引用不会影响已记录帧、冻结资产或播放器。
此修复不新增 ECS System、事件、provider 或工具协议；工厂分配成本按网格顶点/约束数量增长。
