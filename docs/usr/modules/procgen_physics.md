# Procgen Physics

`procgenPhysics <- eve.ProcgenPhysics()` 是程序化地形与 3D 物理之间的可选组合模块。
运行时裁剪配置不需要物理时可以从构建 profile 中移除该模块。

`procgenPhysics.newGtsTerrainColliderRuntime()` 创建 caller-owned
`GtsTerrainColliderRuntime`。`replace(lods,world,collisionLevel,originX,originY,originZ)` 使用指定 LOD 的
MeshBuild 为每个非空 tile 创建静态 `Body3D` 和凹三角网格 Shape3D。Body 的位置为 terrain origin 加 tile
pivot offset，因此碰撞面和 GTS renderable 使用同一世界变换。

`replace` 先完成全部候选 collider，随后一次替换旧批；失败时旧 body、tile 槽和 revision 保持不变。
`getBody(tile)` 返回借用的 live body，空槽、失效句柄或已销毁 world 返回 null。`getTileCount()` 包含空 tile，
`getRevision()` 返回已提交版本，`clear()` 销毁仍存活的 body 并返回数量。Runtime 只保存
`PhysicsWorldHandle` 与 `PhysicsBodyHandle`；World3D 先销毁时这些 link 自动变 stale。