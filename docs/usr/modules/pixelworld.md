# 像素物质世界（PixelWorld）

`pixelworld` 是与渲染解耦的二维 falling-material 仿真模块。每个像素保存权威材料状态，世界按 64×64 chunk 稀疏分配，并在固定 tick 下确定性推进。它适合制作可挖掘地形、沙、水、油、火焰和蒸汽等 Noita 风格交互。

```squirrel
local pixels = eve.PixelWorldModule();
local world = pixels.newWorld(42);
world.paintCircle(80, 10, 8, "sand");
world.paintCircle(100, 10, 8, "water");
world.setMaterial(80, 70, "stone");

eve_update = function(dt) {
    world.step(); // 一次调用严格推进一个整数 simulation tick
};
```

内置材料名为 `air`、`stone`、`sand`、`water`、`oil`、`fire`、`steam`、
`wood`、`ice`、`lava`、`smoke`、`gunpowder` 和 `acid`。当前规则覆盖粉末
下落、不同密度流体交换、液体横向流动、气体上升、燃烧、酸蚀、熔岩淬火
以及水/冰/蒸汽和石头/熔岩相变。

底层不再以分支硬编码这些定义。`MaterialCatalog::create()` 接受 owning
`MaterialDefinition`、`MaterialReactionRule` 和相变规则表，事务化验证连续 ID、唯一名称、
正热容量、颜色、规范化标签、规则 ID 和全部材料引用，再按 `priority/id` 固化规则顺序。每个世界
拥有自己的不可变 Catalog；未知材料通过 `setMaterialChecked()` /
`paintCircleChecked()` 返回结构化 `NotFound`，不会静默写成空气。

材料资产使用 JSON schema `eve.pixelworld.material-catalog` version 1。文档完整携带材料
状态、物理/热参数、`displayRgba`、标签、二元反应和相变规则；读取会拒绝未知/缺失字段、
错误类型、窄整数溢出、超预算数组和悬空材料引用，并返回 canonical JSON 与确定性
fingerprint。颜色和标签由 Catalog 唯一拥有，渲染器不再维护硬编码颜色副本。

每 Tick 的 CPU 参考顺序固定为：材料运动、定点热传导、相变、邻域化学反应、
气体/能量上升。热传导先从同一 pre-phase 状态计算 owning delta，再按坐标排序
提交，因此不依赖 `unordered_map` 迭代顺序。`StepStats` 分别报告
`temperatureTransfers`、`phaseChanges`、`reactions`、`parallelTasks`、
`chunksReclaimed` 和 `cellsChanged`。

Chunk 连续 3 个 Tick 未被写入后才睡眠。完全为空的睡眠 Chunk 会被回收，之后
`snapshotChangedChunks()` 返回 `removed=true`、空 cells 的 revisioned tombstone；
增量渲染、碰撞和网络消费者必须用它删除旧投影。

C++ 可向 `advanceScheduled()` 传入同步 `PixelWorkScheduler`。运动候选和热传导
owning delta 按 Chunk 并行计算；热 delta 完成确定性归并后，各 worker 只提交自己拥有的
Chunk，统计按 canonical index 归并。可选 `pixelworld_thread` 提供 JobSystem adapter。
它不改变 seed/tick 的 bit-exact 契约。

可选 `pixelworld_replay` 记录按 Tick 排序的 canonical edit command 和周期 checkpoint，
可在空世界中位精确重放，并报告首个 Tick/Chunk 分歧。它只拥有输入与摘要，不拥有
第二份可变地形；详见 [PixelWorld 确定性重放](pixelworld_replay.md)。

可选 `pixelworld_streaming` 按有界 Chunk 兴趣区产生增量 owning batch。
`applyChunkBatch()` 是存档/网络权威校正的 canonical 写入口，同时验证 Catalog、seed、
revision、Tick、edit sequence 和 Chunk 内容，失败不部分修改世界。详见
[PixelWorld 兴趣区 Streaming](pixelworld_streaming.md)。

`generatePixelWorld()` 提供 C++ 的 version 1 确定性世界生成入口。请求包含 seed、有限
Chunk 区域、地表振幅、水位、洞穴阈值和 owning material stamps；输出 forest/desert/
fungal/volcanic biome、洞穴与材料特征组成的 canonical `PixelChunkBatch`、逐 Chunk 统计和
稳定 content hash。生成器不修改世界；将 batch 交给 `applyChunkBatch()` 才会通过现有
Catalog/seed/revision/Tick/edit-sequence 检查事务提交。噪声只使用全局整数坐标，因此
单独生成一个 Chunk 与在大区域中生成相同 Chunk 的 cells 完全相同，跨 Chunk stamp 也不会
产生接缝。未知 schema、超大区域、无效 stamp 和不兼容 Catalog 在生成前拒绝。
`encodePixelWorldGenerationRequestJson()` / `decodePixelWorldGenerationRequestJson()` 使用
`eve.pixelworld.generation-request` version 1 保存与恢复请求；未知字段和版本严格拒绝，
64 位 seed 不经浮点 JSON number，解码结果完整拥有 stamp cells 且不修改 live world。

PixelWorld 是材料状态的唯一 owner。模块不逐像素创建 ECS entity，也不依赖 Graphics 或 Physics；渲染器、碰撞轮廓和刚体切割应作为只消费查询/快照的 adapter 接入。C++ 的 `advance(SimulationTick)`、`saveSnapshot()` 和 `restoreSnapshot()` 返回必须检查的 `Result`；恢复会先完整验证候选世界，失败不改变现有状态。

`PixelWorldControlService` 是编辑器、CLI 与 MCP 共用的 main-thread-affine 控制面。每个
世界在构造时自动注册，析构时注销，move 会原子重绑定借用指针；控制面不会保留已销毁
世界。暂停状态属于 PixelWorld 权威状态，因此普通 `advance()`、`advanceScheduled()` 和
`step()` 都不能绕过暂停；显式工具单步调用同一私有推进实现，并限制为每次 1–1024 Tick。
控制面还提供严格序列编辑、有界 Chunk 诊断和事务化 snapshot capture/restore。

启用 DevTools/MCP 时可用 `eve_pixelworld_worlds`、`eve_pixelworld_pause`、
`eve_pixelworld_step`、`eve_pixelworld_edit`、`eve_pixelworld_diagnostics`、
`eve_pixelworld_samples`、`eve_pixelworld_snapshot_capture`、
`eve_pixelworld_snapshot_restore`、`eve_pixelworld_catalog_builtin`、
`eve_pixelworld_catalog_validate` 和 `eve_pixelworld_catalog_apply`。每个世界只保留最近 256 条 Tick 性能样本，查询上限
同样为 256，世界析构时同步清除。MCP 只通过公共
`IPixelWorldAutomation` capability 转发到上述控制面，不直接持有或修改世界。

Catalog authoring 采用“导出内置模板 → 编辑 → validate/canonicalize → pause → apply”的
事务流程。实时 apply 使用当前 fingerprint 做乐观并发检查，并要求材料数量、ID 和名称稳定，
因此已有 cell 的语义不会漂移；成功会重建全部 Chunk 元数据、推进 world epoch 并明确报告
`replayHistoryInvalidated=true`。验证失败、指纹过期、不兼容或未暂停都不会部分修改世界。
增加、删除或重命名材料需要创建新世界或显式迁移存档，不属于 live reload。

当前 snapshot schema 为 `eve.pixelworld` version 4。v2 起写入材料 Catalog 的确定性
fingerprint，恢复到不同 Catalog 时事务化拒绝，避免相同数值 ID 被解释成不同材料。
v3 额外保存最后接受的 edit sequence；v4 为每个 cell 保存规范化热能残差。内置 Catalog
可读取 version 1/2/3；自定义 Catalog 拒绝缺少 fingerprint 的 v1 数据，v1/2 迁移后的
edit sequence 为 0，v1/2/3 迁移后的热能残差为 0。
未知版本、尾随和截断数据均拒绝且不改变现有世界。

热扩散以整数能量而非分别截断两个温度增量。每个非空气 cell 的权威能量为
`temperature * heatCapacity + thermalRemainder`，其中 residual 始终位于
`[0, heatCapacity)`；每对邻居提交大小相反的能量候选，因此在未触及 int16 温度边界时
严格守恒。`StepStats::thermalEnergyTransferred` 和 `thermalEnergyClamped` 分别报告传递量
与温度表示范围造成的饱和损失。Snapshot、Chunk correction、archive 和 replay digest
均包含 residual，避免恢复或联机校正后产生亚温度级确定性漂移。

`applyEdit(PixelEditCommand)` 是绘制、加热和爆炸的 canonical C++ 写入口。命令
sequence 必须严格等于上一条 + 1；半径、坐标、材料和爆炸强度在任何写入前完整
验证。成功返回 `PixelEditReceipt`，包含 revision 前后值和修改/移除/加热像素数；
拒绝不会改变 cell、revision 或 sequence。爆炸使用材料 `blastResistance` 和整数
径向衰减，未被移除的像素仍可接收径向热量。

## 结构碎片

`extractUnsupportedFragments(region, supportY, minimumCells)` 是结构脱落的 canonical
simulation-thread 操作。它只扫描显式的 inclusive 有限区域，保留连接 `supportY`
或区域外 Solid 的连通分量，并将浮空分量原子转换为 owning `PixelFragment` bitmap。

`rasterizeFragment(fragment, originX, originY)` 是全有或全无提交。来自其他世界、被
`clear()`/snapshot restore 失效的 fragment、错误 bitmap、坐标溢出或任一目标格被占用
都会返回结构化失败且不写入。Physics/gameplay 可以持有 bitmap，但不得将其当作第二份
可变地形权威状态。

## API 快查

- 模块：`newWorld()`。
- 世界写入与推进：兼容 facade `setMaterial()`、`paintCircle()`、`step()`、`clear()`；
  C++ 新代码使用带结构化失败的 `setMaterialChecked()`、`paintCircleChecked()` 和 `advance()`。
- 世界查询：`getMaterial()`、`getSeed()`、`getRevision()`、`getTick()`、`getChunkCount()`、`getActiveChunkCount()`。
- 脚本增量诊断：`getChangedChunkCountSince(revision)` 返回该 revision 之后需要重新投影的 Chunk 数。
- 脚本破坏 facade：`explode(x,y,radius,strength,heat)` 自动分配下一 sequence，并返回移除像素数；`getLastEditSequence()` 可用于诊断。
- 可选 `pixelworld_graphics` 卫星模块提供 `newRenderer(originX,originY,width,height)`；
  `renderer.sync(world,gfx)` 仅转换 revision 更新后的 64x64 chunk，并维护一张 RGBA8
  最近邻图集。渲染时对 `renderer.getTexture()` 调用一次 `drawTexturedRect()`，避免逐像素
  Squirrel 查询和 draw call。dirty Chunk 通过批量 texture-region API 增量上传；Vulkan
  将同帧 regions 合并为一次 staging buffer 和 command submission。
- `renderer.drawDiagnostics(world,gfx,scale)` 绘制 active/sleep/最高温度 Chunk 边框和
  最近 60 Tick 耗时图；它只消费有界诊断与性能样本，不持有新的材料状态副本。
- C++ 增量投影：`snapshotChangedChunks(sinceRevision)` 按确定性顺序返回 owning Chunk
  副本，用于渲染纹理上传、碰撞轮廓重建和网络增量；投影不会成为第二权威状态。

二进制 snapshot API 当前仅向 C++ 开放；脚本侧后续应通过统一 `ByteData` Result 投影接入，而不是增加 `lastError` 兼容入口。

示例：[`examples/pixelworld`](../../../examples/pixelworld/)。

源码：[`src/modules/pixelworld/`](../../../src/modules/pixelworld/)。测试：
[`test/pixelworld.cpp`](../../../test/pixelworld.cpp) 与
[`test/pixelworld_control.cpp`](../../../test/pixelworld_control.cpp)、
[`test/pixelworld_generation.cpp`](../../../test/pixelworld_generation.cpp)。
