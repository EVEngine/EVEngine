# 逆运动学模块

**脚本入口：** `eve.IK()`

创建 2D/3D 骨架和 FABRIK Solver，设置目标并逐帧求解；支持链式求解（固定链根）、
3D 极向量（磁铁/弯曲方向）与末端方向覆盖。

## 基本用法

```squirrel
local ik = eve.IK();
local skeleton = ik.newSkeleton2D();
local solver = ik.newSolver2D(skeleton);
solver.addTarget("hand", 240, 180);
solver.solve();
```

只解算肩到手的局部链（骨架根与其它分支保持不动）：

```squirrel
// 先创建完整骨架，再通过 getChild/getParent 获取最终 bone id
local shoulder = skeleton.getChild(0, 0);
local hand = skeleton.getChild(shoulder, 0);
solver.addTarget(hand, 240, 180);
solver.solveChain(shoulder, hand);
```

## 对象关系与调用时机

Skeleton2D/3D 保存骨骼层级；Solver2D/3D 引用骨架并保存目标、迭代参数；
solve/solveChain 完整求解，step/stepChain 按帧软收敛；求解后读取骨骼位置/角度同步渲染。

## 目标导向指南

### 让手臂追踪目标

创建 Skeleton2D 和骨骼链，建立 Solver2D，把末端骨骼绑定目标坐标；目标变化后调用 `solve()`，再读取骨骼位置/角度更新渲染。

### 控制求解稳定性

限制 iterations 和 tolerance，在目标不可达时接受最大伸展结果；连续动画使用 `step()` 逐帧收敛，瞬时编辑器操作使用 `solve()` 完整求解。

### 只解局部链（固定链根）

`solveChain(rootBoneId, tipBoneId)` 只求解 root..tip 之间的骨骼：链根保持原位，
骨架根和其它分支不受影响（对应 Godot `root_bone`/`tip_bone` 与 Unity ChainIK 的 Root/Tip）。
只参与位于该链上的目标。`stepChain(root, tip, dt)` 是它的逐帧版本。

### 控制 3D 弯曲方向（极向量）

`setPole(x, y, z, weight)` 把链中间骨骼拉向一个极点，用于控制手肘/膝盖的弯曲方向
（对应 Godot `magnet`/`use_magnet` 与 Unity TwoBoneIK 的 Hint）。仅对 `solveChain`/`stepChain`
生效；weight 越接近 1 弯曲越贴近极点。`clearPole()` 关闭，`hasPole()`/`getPoleWeight()` 查询状态。

### 覆盖末端方向

`setTipRotation(boneId, yaw, pitch, weight)` 在求解后把末端骨骼的局部朝向混合到指定
yaw/pitch（对应 Godot `override_tip_basis` 与 Unity 的 Target Rotation Weight），
常用于脚掌贴地、手掌朝向。`clearTipRotation()` 关闭。

### 部分影响（混合）

`setInfluence(0..1)` 控制求解结果与求解前姿态的混合比例：1 为完全求解（默认），
0 为保持原姿态，中间值用于动画过渡（对应 Godot `influence` 与 Unity 约束的 Weight）。

## 常见问题

- 创建 Solver 后销毁 Skeleton。
- 骨链长度为 0 或父子关系错误。
- 每帧高迭代求解大量角色，未做距离/可见性裁剪。
- `createBone()` 每次都会按广度优先重排骨骼 id：先建完整个骨架，再通过
  `getChild()`/`getParent()` 重新取得 id，不要沿用创建时返回的旧 id。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `addTarget()`、`bind()`、`clearConstraints()`、`clearPole()`、`clearTargets()`、`clearTipRotation()`、`clearTrace()`、`createBone()`、`forwardKinematics()`、`getBoneCount()`
- `getChild()`、`getChildCount()`、`getForce()`、`getInfluence()`、`getKeepTrace()`、`getLength()`、`getMaxIterations()`、`getName()`、`getOrientationX()`
- `getOrientationY()`、`getOrientationZ()`、`getParent()`、`getRootId()`、`getRotation()`、`getRotationPitch()`、`getRotationYaw()`、`getTargetCount()`
- `getTolerance()`、`getTraceSize()`、`getX()`、`getY()`、`getZ()`、`getPoleWeight()`、`getTipRotationWeight()`、`hasConstraints()`、`hasPole()`、`initStraightPose()`、`newSkeleton2D()`
- `newSkeleton3D()`、`newSolver2D()`、`newSolver3D()`、`setConstraints()`、`setForce()`、`setInfluence()`、`setKeepTrace()`、`setLength()`、`setMaxIterations()`
- `setOrientation()`、`setPole()`、`setPosition()`、`setRotation()`、`setTipRotation()`、`setTolerance()`、`solve()`、`solveChain()`、`step()`、`stepChain()`、`totalLengthTo()`、`updateRotations()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/ik/`](../../../src/modules/ik/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `ik`。
