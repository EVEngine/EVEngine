# Scene Physics Composition

`scene_physics` is the 3D runtime composition satellite exposed as `scenePhysics`.
`bindGeneratedColliders(world3D)` routes later procgen collider publications into the
same caller-owned gameplay `World3D` used for stepping and queries. Scene and procgen
remain independent producers/consumers; the physics artifact provider owns only its
published bodies and shapes.

`isGeneratedColliderWorldBound()` reports whether the shared collider world is still
live. `unbindGeneratedColliders()` removes an empty live binding; it is a no-op when
already unbound and raises a script error when committed or staged publications still
exist.

Binding requires an empty collider provider. Unbinding also requires no committed or
staged publications. Destroying the caller-owned world makes existing collider records
observably stale rather than retaining a raw world pointer. Clear publications before
an orderly unbind.

## 游戏场景接入步骤

加载阶段先创建 caller-owned World3D，绑定 scenePhysics，再发布场景/程序化生成的
碰撞工件。update 由游戏唯一 owner 推进这个 world；射线查询也使用它，不能另建一个
仅供可视化的 world 再误认为角色能撞上地形。

切换关卡先撤销已提交和 staged 的 collider publication，确认 provider 为空后 unbind。
已经发布时直接 unbind 会抛脚本错误；先销毁 world 会令记录 stale，不能把 stale 记录
重新绑定到新 world 当成恢复。新的关卡需重新发布，并检查绑定状态。
源码：[ScenePhysics.cpp](../../../src/modules/scene/physics/ScenePhysics.cpp)。
