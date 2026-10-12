# 流体：选择求解器并维护模拟/表现边界

**入口：** `eve.Fluids()`，槽位 `fluids`；三维完整脚本工作流见
[Fluid3D](../fluid3d.md)。历史二维入口可能在 Physics facade 中，先查当前 SDK 绑定，
不要假定 Fluid3D 是二维 Fluid 的同名替换。

## 任务一：创建液体与发射器

按 Fluid3D 示例从 volumeDefaults 取得受验证的默认描述，再 newVolumeSimulator。
创建 VolumeFluidEmitter，取得 volumeEmissionDefaults，设置 origin/direction、
prototype.life 等字段，在固定模拟 tick 调用 emitter.advance。所有易失败操作先读
result.ok，再取 value；以 admitted 数量和求解器状态确认真的注入了粒子。

## 任务二：表现与失效处理

求解器拥有粒子/相状态，屏幕空间 surface renderer 只消费输出。先完成模拟，再同步
表现；渲染器不能反过来改权威粒子位置。液体、气体、颗粒和 multiphase 的参数范围与
质量档按明确 schema 校验，不能把任意巨大粒子数当廉价 setter。

发射器不保留求解器引用，允许任一实例先销毁，但游戏必须停止后续 advance 调用。
计算在模拟线程，GPU/屏幕表现只能在对应设备 owner 线程。固定 dt、发射种子与命令顺序
决定复现条件；backend 比较需要同样输入与明确容差，不声称不同求解器天然逐位一致。

性能验证记录粒子数、solver step、邻域/约束成本与 surface 渲染成本，先预热再比较。
完整 Result 示例与对象所有权见 Fluid3D；源码见
[fluids](../../../src/modules/fluids/)，性能接线证据见
[fluids_volume_profiler.cpp](../../../test/fluids_volume_profiler.cpp)。

## 任务三：浅水方程交互水面

`eve.Fluids().newShallowWater(resX, resZ, cellSize)` 创建 GPU 优先的 Saint-Venant
高度场求解器（无加速器时回退 CPU）。权威状态是格子上的水深 `h` 与水平动量
`(hu, hv)`；`buildSurfaceMesh` / 着色只消费输出，不能回写求解器。

角色腿部用 `setColliderCapsule(label, x, y, z, halfHeight, radius, vx, vy, vz, …)`
每帧采样，再 `step(dt)`。左键/脚本冲击用 `addImpulse`。可运行示例见
[interactive-shallow-water](../../../examples/interactive-shallow-water/)。

## 补充绑定参考

参数与接收者见 [源码生成参考](fluids/api-reference.md)，使用前先确认本章的前置条件与生命周期。
