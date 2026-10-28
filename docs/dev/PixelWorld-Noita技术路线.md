# PixelWorld / Noita 技术路线

状态：实施中。本文定义“完整 Noita 技术路线”的能力边界和验收证据，当前原型不等于完成。

## 总体契约

- `PixelWorld` 是材料、温度、寿命和结构连通性的唯一权威 owner；渲染、物理、音频、网络只消费版本化投影。
- CPU 参考后端在相同 seed、输入命令流和 `SimulationTick` 下 bit-exact。并行 CPU 后端必须与参考后端 bit-exact；GPU 仿真若引入则明确标为 tolerance-bounded，不能静默替换回放后端。
- 64×64 Chunk 是调度、睡眠、脏区、存档、网络和 GPU 上传的共同粒度。跨 Chunk 写入必须唤醒邻域并参与同一 revision。
- 游戏脚本和未知回调只在 Tick 边界调用，不在 Chunk 锁内调用。Worker 仅处理 owning candidate buffer，提交按确定性 Chunk 顺序完成。
- 持久格式使用 `eve.pixelworld` schema/version；未知新版本拒绝，迁移和恢复失败不改变可观察世界。

## P0：确定性材料核心（已具备纵切，继续扩展）

- 稀疏 Chunk、负坐标、固定 Tick、命名 seed、粉末/液体/气体/固体/能量状态。
- 活跃/休眠 Chunk、跨边界唤醒、单 Tick 防重复更新。
- 权威 revision 和 `snapshotChangedChunks()` owning 增量投影。
- 数据驱动 `MaterialCatalog`：密度、黏度、导热率、热容量、燃点、熔/沸点、腐蚀、寿命、颜色和标签；加载时 schema 验证，运行中只用紧凑 ID。
- 数据驱动反应表：输入材料、邻域条件、温度/概率门限、产物、热量和事件；规则冲突按稳定 priority/id 排序。

验收：跨平台 golden snapshot；10k Tick 重放 hash 一致；边界交换物质守恒；未知材料/规则以结构化诊断拒绝。

## P1：热力学、化学与破坏

- 定点温度扩散、热源/冷源、点燃、熔化、冻结、蒸发、凝结。
- 油、木、火药、酸、熔岩、冰、烟等代表性链路；反应产物和放热守恒可诊断。
- 圆刷、射线、爆炸、腐蚀和布尔 stamp 统一为 Tick 边界 `PixelEditCommand`，支持命令日志、undo/replay。
- 静态材料连通域失支撑后生成碎片候选；碎片光栅从世界原子移除，失败不留下半提交状态。

验收：每条反应有正向/边界/失败测试；爆炸回放确定；大面积编辑只唤醒覆盖 Chunk 与一圈邻域。

当前结构破坏纵切：`extractUnsupportedFragments()` 在显式有限区域内以四邻域扫描
Solid，连接支撑线或区域外 Solid 的分量继续由世界拥有；浮空分量按左上顺序生成
owning `PixelFragment` bitmap，并在同一提交中从世界移除。`rasterizeFragment()` 先
验证 runtime world/epoch link、bitmap 和所有目标格，再一次提交；冲突、restore 或
clear 后的 stale link 均不产生部分写入。Physics adapter 与动态轮廓仍属于 P2。

## P2：刚体碎片与角色碰撞

- `PixelWorld` 产出轮廓/碎片 owning artifact，经 capability adapter 交给 Physics；Physics 不回写第二份材料真源。
- Marching Squares/轮廓简化生成静态碰撞；按 dirty revision 增量重建。
- 刚体碎片保存材料 bitmap 和 PixelWorld Link；碎片破裂或休眠时事务化栅格化回世界。
- 角色使用探针/扫掠碰撞和材料接触查询，避免每像素创建刚体或 ECS entity。

验收：世界先销毁、刚体先销毁、restore/hot reload 两种顺序均无 stale 引用；碰撞重建预算可观测。

当前动态碎片纵切：可选 L5 `pixelworld_physics` 把 owning `PixelFragment` 以确定性
top-left 贪心矩形分解为受显式 budget 限制的 Box2D fixtures。适配器跨帧只保存
`PhysicsLink`，不保存 World/Body 裸指针；休眠时将 bitmap snap 到最近 90°，先请求
PixelWorld 全量事务回填，成功后才销毁刚体。目标冲突会保留刚体，physics world
先销毁、body 先释放和 PixelWorld restore stale 三条生命周期路径均有测试。静态 terrain
现以带一格邻域采样的二值边界轮廓、共线简化及 Box2D chain/loop fixture 按 revision 增量
重建；边界修改会同时重建已投影的相邻 Chunk，避免残留 seam。角色圆形 probe 与连续
sweep 直接查询权威材料，带候选预算、确定性 tie-break 和精确圆角测试，不逐像素创建刚体。

## P3：规模化调度

- Chunk phase/color 调度，把无写冲突 Chunk 分批并行；跨边界写入 staging buffer，按 canonical 顺序提交。
- 活跃矩形、睡眠迟滞、空 Chunk 回收、世界 streaming 和相机兴趣区。
- SoA/packed cell 热数据，冷属性与事件分离；SIMD 温度/邻域计算。
- 性能预算：桌面 CPU 参考目标 1M 活跃像素 60 Hz，常规场景按活跃比例降本；基准必须报告硬件、活跃像素、反应数和 p50/p95。

验收：单线程/并行 snapshot hash 一致；TSAN；百万像素 benchmark；越界 Chunk streaming 压测。

当前 P3 纵切：L0 `PixelWorkScheduler` 只允许同步执行 index-owned candidate slots，
L1 `pixelworld_thread` 使用 JobSystem fork/join。热传导从同一 post-movement 只读状态
并行生成 owning contribution buffers，再按 canonical Chunk/坐标提交；4 worker 与参考
后端连续 100 Tick 的完整 version-3 snapshot 字节一致。Chunk 经过 3 Tick 无写迟滞才
睡眠，空 Chunk 回收会发布 revisioned tombstone，使渲染和静态碰撞删除旧投影。
运动阶段同样由 worker 对只读 Chunk 生成 index-owned、行序稳定的候选列表，再按原有
Chunk/行/列 canonical 顺序提交；提交时重新读取源状态并保留下落、两侧对角线及液体
横移的原 fallback 顺序，因此跨 Chunk 冲突结果不依赖 worker 调度。
Chunk 写入路径增量维护持久化 phase metadata（非空/可移动计数、每材质计数、温度范围；
温度极值被覆盖时延迟重算），每 Tick 只投影紧凑摘要：
无可移动材质时跳过运动，世界不存在可配对反应物时跳过反应，均匀且邻域同温时跳过
热传导且不创建空 worker task，phase transition 仅扫描同时含候选材质且温度范围可能
越过阈值的 Chunk。反应优化使用世界级快照判定，
不使用遍历中可能过期的逐 Chunk 邻域结论，以保持跨 Chunk 反应传播语义。

当前真实性能边界：AMD Ryzen 7 5700X、Debug、4 worker，1024×1024 均匀 stone、
256 个每样本强制唤醒的 Chunk、10 样本，优化前 p50 2760.26 ms / p95 2807.64 ms，
初始保守 phase summary 后 p50 319.325 ms / p95 326.232 ms；改为持久化 metadata、
温度阈值筛选并消除空 thermal worker 后 p50 0.909 ms / p95 1.756 ms。该工作负载
是 1,048,576 occupied pixels 但不是 1M 移动物质；它证明静态占用已低于 16.7 ms，
并不证明 1M active 目标。移动/反应负载仍需继续改为 packed Chunk hot data、细粒度
activity mask、无冲突运动候选和 SIMD。不得用睡眠空转或均匀固体 fast-path 宣称
1M active 达标。

移动压力基线：同机 Debug、4 worker，初始 1024×1024 packed sand（1,048,576 个
mobile pixels）连续 5 Tick，累计 visited 3,858,432、moved 777,142；并行候选初版
p50 1190.67 ms / p95 1246.21 ms，改用 Chunk 内直接扫描并在提交中复用 source 后
p50 784.636 ms / p95 906.146 ms；进一步按源 Chunk 批处理、同 Chunk 直接交换 cell
slot 且仅在边界唤醒邻居后，p50 253.206 ms / p95 259.187 ms。该结果明确证明真正
mobile 工作负载的逐像素候选随后改为每 Chunk 64×64 位 packed row mask，提交阶段以
count-leading/trailing-zero 保持原左右 canonical 次序；Catalog 同时维护事务热重载时重建的
material-state/位移矩阵，避免每次尝试重复解释 density/state。Clang Debug、4 worker、同一
5 Tick 工作负载两次无并发复测为 p50 169.540/170.316 ms、p95 175.814/176.425 ms，snapshot
和 moved/visited 计数保持不变。独立 Clang Release 构建曾在进入仿真前崩于
`MaterialCatalog::builtIn()`：根因是 `Result<T>` 的私有观察 token 受 `ZEROERR_NO_ASSERT`
控制对象布局，而 Release 库与保留断言的测试目标采用不同宏，形成跨目标 ODR/ABI 冲突。
观察 token 现保持固定布局，裁剪 Release 探针已验证 13 项 built-in catalog 与稳定指纹可正常
跨宏边界返回。`EVENGINE_BUILD_PIXELWORLD_BENCHMARK=ON` 现提供 renderer-independent 的
`pixelworld_benchmark [workers] [samples]`，每个计时样本都重新创建 1024×1024 packed sand，
保证每 Tick 确实访问 1,048,576 个 mobile pixels，初始化和 snapshot hash 不计入 Tick 时间。
AMD Ryzen 7 5700X、Clang 21.1.8 Release、4 worker 的两次 10 样本结果为
p50 11.078/13.055 ms、p95 12.821/14.753 ms，累计 visited 10,485,760、moved 976,960、
reactions 0；1/4 worker 产生相同 snapshot hash `5924589683504813959`。这证明隔离的纯移动
压力纵切已达到 16.7 ms 参考预算，但不代表热、相变和高反应密度同时开启的完整仿真已达标。
系统缺少 `g++` 且自动 sudo 无法安装，GCC 结果仍未验证。下一瓶颈转向 canonical reaction
commit、热传导 SIMD 和混合材质压力，需要颜色批次或确定性目标仲裁继续拆分。

同一 benchmark 还提供 `reaction-only`（acid/stone checkerboard）和 `mixed-reaction`
（fire/water checkerboard）模式。Catalog runtime cache 现为每个有序材质对保存 canonical
规则索引序列，热重载时原子重建；这避免每个邻居重复遍历全部规则，同时仍允许高优先级
温度规则不匹配后落到低优先级规则。4 worker、Release 的纯反应 5 样本从
p50 59.675 / p95 61.510 ms 降到 p50 45.805 / p95 49.319 ms；随后同 Chunk 反应读取和
metadata 写入改为直接 slot，跨 Chunk/可能创建 Air 的自定义规则仍走安全稀疏路径并在插入后
重取引用，进一步降至 p50 30.141 / p95 30.544 ms。反应数 2,615,830、snapshot hash
`6722933230463575247` 始终不变。热阶段随后把每次传热的 heap contribution 和百万项
坐标哈希/排序替换为每源 Chunk 的 local/right/bottom 固定 delta buffer，再归并到目标
Chunk 的 4096 项稠密 delta；计算预取 own/right/bottom Chunk 和 Catalog 热容/导热率表，
提交按互斥 Chunk 并行并跳过已归一化 cell 的重复除法。fire/water 混合压力由
p50 223.694 / p95 247.968 ms 降至稳定约 p50 90 ms / p95 98 ms（5 样本、
10,449,965 thermal transfers），snapshot hash `16111782154737202544` 不变。独立
hot/cold stone 热压力约 p50 36.1 / p95 38.0 ms；完整混合仿真仍未达到 16.7 ms，
不能用纯移动达标替代该目标。

## P4：渲染、视觉和 GPU 上传

- 每个变化 Chunk 一次紧凑材质/温度纹理上传，禁止逐像素脚本调用和逐像素 draw call。
- Chunk atlas/array texture、调色板 LUT、温度发光、火焰/烟雾扰动、法线/液面边缘和屏幕后处理。
- CPU readback 只用于 MCP snapshot/诊断；渲染缓存携带 source revision，可检测落后和重建。
- 粒子 VFX 是可丢失投影，不反向成为材料权威状态。

验收：MCP `eve_screenshot` 来自引擎 framebuffer；渲染 revision 追上世界 revision；1M 像素上传与绘制不产生 1M draw call。`pixelworld_graphics` 已将脚本侧绘制降为一张图集/一次 draw，并按 chunk revision 更新 CPU 图集；Vulkan/WebGPU 均实现原纹理子区域上传，不再为动态像素重建 GPU 纹理。Vulkan 批量接口把多个 dirty region 紧密打包进一次 staging/command submission；百万像素测试覆盖 256 个可见 Chunk，随后两个相距很远的修改只提交 2 个 region。真实桌面 MCP framebuffer 证据为 `/tmp/pixelworld_multiregion_mcp.png`，Validation 未报告 VUID。

## P5：世界生成、存档、回放和网络

- 确定性 biome/洞穴生成、材料 stamp、兴趣区 streaming、Chunk 压缩与增量存档。
- Snapshot v2 使用公共 `SnapshotEnvelope`、内容 hash、Chunk 压缩、N/N-1 migration 和损坏隔离。
- 输入命令日志 + 周期 checkpoint；分歧定位到 Tick/Chunk/revision。
- 联机默认同步命令与权威 Chunk 修正，不承诺把每个像素作为独立复制对象。

验收：旧存档迁移、截断/未知字段/坏 hash 不部分恢复；长回放 hash；断线重连 Chunk 校正。
当前 `pixelworld_replay` 已实现严格 sequence/Tick 命令日志、周期 world/Chunk
checkpoint、空世界位精确重放和首个 Tick/Chunk/revision 分歧报告。replay
codec 使用公共 `SnapshotEnvelope` 的 `pixelworld:replay-log` version 2，完整验证
hash 后事务化解码，接受缺少 Chunk 诊断的 version 1；坏 hash、截断、未知字段和
未知新版本不部分修改旧日志。`pixelworld_streaming` 已实现有界 Chunk 兴趣区、revision
增量、离区 tombstone、source epoch 全量重同步，以及带 Catalog/seed/revision/Tick/
edit-sequence 校验的事务化权威 Chunk correction；断线后增量校正已有组合测试。
`pixelworld:chunk-batch` SnapshotEnvelope version 3 对每个 Chunk 独立使用 none/zstd、
decoded-content hash 和有界解码，接受 version 1/2 legacy cell 并将缺失热能 residual 迁移为
0；坏外层/Chunk hash 均在 authority 写入前拒绝。可靠会话已实现有界发送/接收窗口、
单 Chunk 分片、丢包重传、乱序缓冲、冲突重复包拒绝、整 transfer 原子提交和累计应用层
ACK；发送端在 ACK 前保留 owning correction/tombstone，窗口或分片预算失败不推进 cursor。
该状态机可承载于 `UdpLink::Reliable`/可靠 RPC，拥塞、认证和加密继续由宿主网络层负责。
确定性世界生成纵切现由 `generatePixelWorld` 提供：version 1 有界请求使用全局整数坐标
hash 和插值地表生成 forest/desert/fungal/volcanic biome、无 Chunk 接缝洞穴、水穴与熔岩
特征，并在地形之后应用可跨 Chunk 的 owning row-major material stamps。生成器不持有或
修改世界，只返回 canonical `PixelChunkBatch`、逐 Chunk 统计和稳定 content hash；调用方
通过现有 `applyChunkBatch` 的 fingerprint/seed/revision/Tick/edit-sequence 校验事务式提交。
测试证明同 seed 重复生成 bit-exact、单 Chunk 与大区域中的同坐标 Chunk 完全一致、换 seed
改变 hash、跨 Chunk stamp 正确分片，未知 schema 在 authority 外即被拒绝且世界 snapshot 不变。
`eve.pixelworld.generation-request` version 1 JSON codec 已持久化全部生成参数与 owning stamp
cells；seed 以 canonical 十进制 uint64 字符串保存，避免 JSON number 精度丢失。解码严格拒绝
未知/缺失字段、未知版本、窄整数溢出、尺寸不匹配和超预算 stamp，并且不会接触 live world。

## P6：创作与诊断工具链

- 材料/反应 schema 编辑器、实时校验、调色板预览、热更新事务。
- Chunk 活跃度、睡眠、温度、反应、脏 revision、上传延迟和模拟预算 overlay。
- MCP：查询材料/区域、提交编辑命令、暂停/单步、snapshot/restore、性能采样和引擎 framebuffer 截图。
- 自动场景：沙水守恒、火灾、爆炸、酸蚀、碎片、百万像素压力与存档迁移。

验收：UI/CLI/MCP 复用同一领域操作；无绕过 authority 的调试写入口；每个自动场景同时产出数值断言和引擎截图证据。

当前进度：共享 `PixelWorldControlService`、权威暂停/显式单步、严格序列编辑、有界
Chunk 活跃度/温度诊断、事务化 snapshot/restore 已实现；MCP 通过模块中立的
`IPixelWorldAutomation` capability 暴露对应工具。每世界 256 条有界性能历史和可切换
Chunk/温度/耗时 overlay 已接入示例。真实桌面 MCP 已查询 live world、Chunk diagnostics
和 samples，并通过 `eve_screenshot` 生成 `/tmp/pixelworld_p6_mcp.png`（960x576）；未使用
Xvfb。材料 Catalog version 1 JSON codec、颜色/标签权威数据、validate/canonicalize MCP 工具，
以及 paused + optimistic fingerprint 的事务化 live reload 已完成；规则变更会推进 epoch 并
显式使旧 replay history 失效。`pixelworld_editor` 现提供独立的事务式 Catalog draft、材料
浏览/颜色预览/完整属性编辑、二元反应和相变规则增删改，以及命名 UI host 的
open/close/refresh 生命周期；脚本 `pixelworldEditor.openCatalog()` 是生产入口，示例把 UI
pass 合入最终帧。面板通过共享 control service 发布，不保存 world 指针或绕过 authority。
真实桌面引擎 MCP 已确认 `eve_pixelworld_catalog` 的 94 节点树，并由 `eve_screenshot`
生成 `/tmp/pixelworld_catalog_editor_mcp.png`（960x576）；未使用 Xvfb。

## 当前完成边界

已完成的是 P0 的稀疏确定性纵切、经验证的 owning `MaterialCatalog`、确定性
`MaterialReactionRule`、带 Catalog fingerprint、edit sequence、热能 residual 和 v1/v2/v3
读取迁移的 version 4 snapshot、
Chunk 增量投影和可交互示例。P1 已有固定阶段的整数热扩散、数据驱动
`MaterialPhaseRule`、水/冰/蒸汽、石头/熔岩、酸蚀、火药与淬火代表链路，以及
严格 sequenced 的绘制/加热/爆炸 `PixelEditCommand` 与事务 receipt。
P1 还具备失支撑 Solid 连通域的事务化提取和带 epoch stale detection 的 bitmap
回填；P4 已具备单 atlas draw、按 dirty Chunk 的多 region GPU 增量上传和百万像素
覆盖测试；P2 已有动态碎片 Physics adapter、fixture budget、休眠回填和双销毁顺序
测试。P3 已有并行热候选、bit-exact 对比、睡眠迟滞、空 Chunk tombstone 与真实性能
基线，但完整并行运动/反应、SIMD 和 1M active/60 Hz 尚未达到。P5 已有确定性命令日志、
checkpoint、首个分歧定位、兴趣区 streaming 和事务化权威 Chunk 校正。材料 Catalog 外部
schema JSON 加载与事务热重载、静态轮廓碰撞、角色 probe/sweep、整数热能 residual 守恒，
以及带应用提交 ACK/tombstone 保留的可靠 Chunk transport 状态机、专用可视 Catalog 编辑
面板均已完成。路线仍未完整交付：P3 的完整并行反应/SIMD 与高密度热+反应 1M/60 Hz
尚未达到；P5 已有 biome/洞穴/stamp 确定性纵切和生成请求持久化 codec，但增量按需生成
调度与生成器接入示例/编辑器仍需完成，不能据现有演示宣称完整路线已交付。
