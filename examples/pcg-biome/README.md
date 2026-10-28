# PCG Biome

对标 UE PCG Runtime/Hierarchical Generation 的端到端示例：空间数据组合、稳定点属性、
多级 Cell 调度、生成/清理迟滞、方向视锥、时间预算，以及按 Cell 发布到 Scene 的实例批次。

```sh
make run/linux-debug GAME=examples/pcg-biome
```

- `W/A/S/D`：按格移动生成源，观察近景细节生成及远处 Cell 清理。
- 大格生成树木，小格生成草和岩石；道路 Spline 作为排除空间。
- 每个 Cell 使用独立 seed；失败请求可以用相同 seed 重试。
- `BiomeRules` 负责优先级、密度和加权资产选择，PointGraph 负责可缓存的后处理。
- `publishCellInstances` 把 PointSet 原子 reconcile 到独立 SceneHost，清理请求对应删除批次。
