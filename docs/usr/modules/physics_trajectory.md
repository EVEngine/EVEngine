# Physics Trajectory 子模块

`physics_trajectory` 位于 `src/modules/physics/trajectory`，依赖 `physics`，可裁剪。
它把骨骼挂点上的碰撞球/胶囊与 `World3D` 的连续扫掠组合起来，对标 UE5 Physics
Asset 与动画 Notify 的球体/胶囊轨迹检测。

## 职责边界

- **权威碰撞**：仍由 `World3D`（Box3D castSphere / castCapsule）负责。
- **本模块**：持有骨骼碰撞体目录、每个 subject 借用的 `IAttachmentPointSource`、
  攻击窗口的 arm/disarm 状态，以及 previous→current 的确定性扫掠与命中去重。
- **不依赖** `animation` / `avatar`：挂点通过 `common/AttachmentPoint.h` 的
  `IAttachmentPointSource` 解析（Avatar 已实现；测试可用固定挂点表）。

## 用法（C++）

```cpp
eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world3d);

eve::physics::trajectory::BoneColliderDefinition hand;
hand.colliderId = "hand.R";
hand.boneName   = "hand.R";
hand.kind       = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
hand.radius     = 0.25f;
runtime.registerCollider(hand).throwIfError();
runtime.bindPoseSource(subject, attachmentSource).throwIfError();

// 攻击窗口 Enter
runtime.arm(subject, "hand.R").throwIfError();
// 每帧在动画 pose 更新之后
auto frame = runtime.advance(tick);
// 攻击窗口 Exit
runtime.disarm(subject, "hand.R").throwIfError();
```

### 形状

| Kind | 挂点 | 说明 |
| --- | --- | --- |
| Sphere | `boneName` + `localOffset` | Physics Asset 风格碰撞球 |
| Capsule（单骨） | 同一骨采样 `localOffset ± (0, halfHeight, 0)` | 局部 +Y 轴胶囊 |
| Capsule（双挂点） | `boneName` + `endBoneName` | 武器刃/双 Socket 扫掠 |

### 过滤与去重

- `categoryBits` / `maskBits` / `ignoredBodyId` 仅在单次扫掠期间临时施加，不改写世界默认查询过滤。
- 同一 `(subject, colliderId, bodyId, shapeId)` 在 disarm 前只报告一次命中。

### 生命周期

World 通过 weak lifetime + `PhysicsWorldHandle` 观察；先销毁 `World3D` 再
`advance` 返回 `StaleHandle`。Pose source 是借用指针，必须先 `clearPoseSource`
或保证其寿命覆盖绑定。命中结果只发布进程内 `PhysicsBodyHandle` /
`PhysicsShapeHandle`，不持久化。
