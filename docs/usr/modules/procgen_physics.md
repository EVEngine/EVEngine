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

## 地形加载与热更顺序

先用 Procgen 的 GTS 流程生成 lods，再创建游戏唯一的 World3D，最后创建 collider runtime。
碰撞 level 是明确选择的 LOD，不能每帧随相机 render LOD 抖动。replace 的成功 value 是
已提交 revision，先检查 ok 再让角色进入地形；失败继续保留旧碰撞及旧视觉的配对版本。

地形编辑任务在修改完成后批量生成候选 lods 和 collider，不在每个笔刷采样点重建整个
物理世界。getBody 返回借用对象，只在对应 runtime revision 与 world 都有效时使用。
退出时先停模拟/查询，检查 clear 的 Result，再释放 runtime、lods 和 world。
不靠脚本 GC 的随机顺序清理物理对象。

签名与原子替换实现见
[ProcgenPhysics.cpp](../../../src/modules/procgen/physics/ProcgenPhysics.cpp)。
