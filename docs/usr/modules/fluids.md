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
`(hu, hv)`；`buildSurfaceMesh` / 场纹理 / 着色只消费输出，不能回写求解器。

`copyFieldToImage(image, velocityScale, depthContrast=0)` 把格子打成 RGBA8 **数据**场纹理
（宽=`resX`，高=`resZ`）：**R**=水深（`depthContrast<=0` 时为 `h/maxDepth`，否则 crest heat），
**G/B**=水平速度（0.5 静止），**A**=速率掩码。成品水面应把仿真网格接到透明
`gfx.newWater()` 着色器（折射 / 深浅 / foam），场纹理只作 debug/图例，不要当湖面 albedo。
`copyShadedAlbedoToImage` / `copyNormalToImage` 另写可视化反照率与斜率法线。
`buildSurfaceMesh` 可选顶点色使用同一套 R/G/B/A 通道语义。

角色腿部用 `setColliderCapsule(label, x, y, z, halfHeight, radius, vx, vy, vz, …)`
每帧采样，再 `step(dt)`。左键/脚本冲击用 `addImpulse`。可运行示例见
[interactive-shallow-water](../../../examples/interactive-shallow-water/)。

## 补充绑定参考

参数与接收者见 [源码生成参考](fluids/api-reference.md)，使用前先确认本章的前置条件与生命周期。

## 任务四：把岸坡接入浅水求解

`field.setBed(beds)` 接收 `resolutionX*resolutionZ` 个有限床面高度，按 Z 行、X 列排列。
它复制数组，失败不修改床面。随后调用 `field.fillSurface(surfaceY)`，按
`max(surfaceY-bed,0)` 初始化水深并清零动量，高于水位的岸地保持干燥。
两者返回 Result；初始化和重置时执行，不能每帧重灌床面/水位。
`fillDepth(depth)` 保留原有统一水深语义，不适合在斜坡上建立水平静水面。

```squirrel
requireOk(field.setBed(beds), "shore bed");
requireOk(field.fillSurface(0.55), "level lake");
```

高度与水平动量仍由 ShallowWaterField 单独拥有。CPU 与 GPU 使用相同的静水重构和
侧向压力修正，支持干湿前沿；固定输入与步长的后端对比按容差验证，不承诺逐位相同。
`buildSurfaceMesh` 的显示增益相对于最后一次填充的每格参考水深，保持斜坡静水面的水平。

`interactive-shallow-water` 共用同一 `pondBed` 生成沙床和物理床面；水边的可见范围读取
真实水深，泡沫根据浅水流速、速度收敛和表面坡度生成。泡沫是表现代理，未模拟气泡、
翻卷或三维飞溅；动量回传人物也不在这个单向耦合例子的范围内。

`field.syncSurfaceMesh(gfx, mesh, visualHeightGain)` 是设备线程上的同步表现桥：先创建 Graphics
网格，再按帧把当前场同步到该网格。调用中临时借用 gfx/mesh，不保存指针，不修改物理状态；
返回 Result，参数或上传失败须检查。成本与网格顶点数线性相关，明显高于点查询，按呈现帧
摊销一次；内部拥有临时几何缓冲，避免 `buildSurfaceMesh` 的脚本数组导出/再导入成本。
需要 CPU 网格数据的工具仍使用 `buildSurfaceMesh`，两者分别承担数据导出与渲染同步。
