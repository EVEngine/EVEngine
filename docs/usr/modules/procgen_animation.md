# Procgen Animation

`procgen_animation` 是程序化网格与骨骼动画之间的可选 L6 组合模块。运行时入口为
`procgenAnimation`；基础 `procgen` 与 `animation` 模块仍可独立裁剪。

## Pcg Skinned MeshCombiner

用 `PcgSkinnedMeshPlan.appendSource(mesh, skin, transform, defaultMaterialId)` 按 renderer 顺序复制输入，随后调用
`procgenAnimation.combinePcgSkinnedMeshes(output, plan)`。成功后 `output.copyMesh()` 返回拥有式组合网格，
`output.getSkin()` 返回与该网格顶点顺序匹配、由 output 拥有的 `AnimSkin`。output 下一次组合或销毁后，该 skin
借用指针失效。

joint 仅在 skeleton bone identity 与 16 元素 inverse bind 同时相同时复用；四个 influence 槽按 Pcg 原算法
重映射。组合和 skin 构造全部成功后才替换 output，失败不会留下只有 mesh 或只有 skin 的半完成状态。
`PcgSkinnedMeshPlan.getSourceCount()` 返回输入数并可用 `clear()` 清空；组合结果以 `isValid()` 查询是否同时
持有可用网格与 skin。

## 合并多个角色部件

先准备已生成的 MeshBuild、AnimSkin 和 PcgMeshTransform，保持顶点与 skin 的对应。
创建 `eve.PcgSkinnedMeshPlan()` 与 `eve.PcgSkinnedMeshResult()`；对每个部件调用
appendSource 并检查 Result.ok，随后调用 combinePcgSkinnedMeshes(output,plan) 并检查
结果。没有有效部件时不能把空 output 交给渲染器。

成功后获取 copyMesh 的拥有式副本，再使用 getSkin 的借用指针创建/更新渲染资源。
重新组合前先停止使用旧 skin 的消费者；热更重建 mesh、skin 和 GPU 资源的对应关系。
plan.clear 只清输入计划，不代表已创建的 GPU 资源也被释放。

第二个任务是换装：先完整构造候选 plan，失败则继续显示旧 output，成功后才换角色部件。
不同骨架身份或 inverse bind 不同的 joint 不强行共用；不能仅凭同名骨骼判断可合并。
参数及事务提交顺序见
[ProcgenAnimation.cpp](../../../src/modules/procgen/animation/ProcgenAnimation.cpp)。
