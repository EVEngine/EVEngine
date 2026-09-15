# Procgen Animation

`procgen_animation` 是程序化网格与骨骼动画之间的可选 L6 组合模块。运行时入口为
`eve.procgenAnimation`；基础 `procgen` 与 `animation` 模块仍可独立裁剪。

## Pcg Skinned MeshCombiner

用 `PcgSkinnedMeshPlan.appendSource(mesh, skin, transform, defaultMaterialId)` 按 renderer 顺序复制输入，随后调用
`eve.procgenAnimation.combinePcgSkinnedMeshes(output, plan)`。成功后 `output.copyMesh()` 返回拥有式组合网格，
`output.getSkin()` 返回与该网格顶点顺序匹配、由 output 拥有的 `AnimSkin`。output 下一次组合或销毁后，该 skin
借用指针失效。

joint 仅在 skeleton bone identity 与 16 元素 inverse bind 同时相同时复用；四个 influence 槽按 Pcg 原算法
重映射。组合和 skin 构造全部成功后才替换 output，失败不会留下只有 mesh 或只有 skin 的半完成状态。
`PcgSkinnedMeshPlan.getSourceCount()` 返回输入数并可用 `clear()` 清空；组合结果以 `isValid()` 查询是否同时
持有可用网格与 skin。
