# 逆运动学模块

**脚本入口：** `eve.IK()`

创建 2D/3D 骨架和 FABRIK Solver，设置目标并逐帧求解。

## 基本用法

```squirrel
local ik = eve.IK();
local skeleton = ik.newSkeleton2D();
local solver = ik.newSolver2D(skeleton);
solver.addTarget("hand", 240, 180);
solver.solve();
```

## 对象关系与调用时机

Skeleton2D/3D 保存骨骼层级；Solver2D/3D 引用骨架并保存目标、迭代参数；solve/step 修改求解后的骨骼姿态。渲染骨骼或模型需要游戏主动同步结果。

## 目标导向指南

### 让手臂追踪目标

创建 Skeleton2D 和骨骼链，建立 Solver2D，把末端骨骼绑定目标坐标；目标变化后调用 `solve()`，再读取骨骼位置/角度更新渲染。

### 控制求解稳定性

限制 iterations 和 tolerance，在目标不可达时接受最大伸展结果；连续动画使用 `step()` 逐帧收敛，瞬时编辑器操作使用 `solve()` 完整求解。

## 常见问题

- 创建 Solver 后销毁 Skeleton。
- 骨链长度为 0 或父子关系错误。
- 每帧高迭代求解大量角色，未做距离/可见性裁剪。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `addTarget()`、`bind()`、`clearConstraints()`、`clearTargets()`、`clearTrace()`、`createBone()`、`forwardKinematics()`、`getBoneCount()`
- `getChild()`、`getChildCount()`、`getForce()`、`getKeepTrace()`、`getLength()`、`getMaxIterations()`、`getName()`、`getOrientationX()`
- `getOrientationY()`、`getOrientationZ()`、`getParent()`、`getRootId()`、`getRotation()`、`getRotationPitch()`、`getRotationYaw()`、`getTargetCount()`
- `getTolerance()`、`getTraceSize()`、`getX()`、`getY()`、`getZ()`、`hasConstraints()`、`initStraightPose()`、`newSkeleton2D()`
- `newSkeleton3D()`、`newSolver2D()`、`newSolver3D()`、`setConstraints()`、`setForce()`、`setKeepTrace()`、`setLength()`、`setMaxIterations()`
- `setOrientation()`、`setPosition()`、`setRotation()`、`setTolerance()`、`solve()`、`step()`、`totalLengthTo()`、`updateRotations()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/ik/`](../../../src/modules/ik/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `ik`。
