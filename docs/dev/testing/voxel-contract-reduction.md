# 体素渲染集成契约精简

2026-10-25。当前合并文件 `test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp` 保持一个物理 CPP/OBJ，不增加 Unity 规则或来源选择宏。体素部分从 63 个独立进程用例收敛为 10 个父用例、21 个具名 subtest。全文件从 100 个父用例降到 43 个：另退役四个没有绘制结果断言的预览。

## 保留的真实边界

1. `faceDirAliasesDraw`：六个方向各一个 subtest，比较 canonical/alias 提交的像素，并检查 VoxelWorld 自动选择和提交同方向的结果。窗口、图形设备、atlas 和已重网格 world 共用一次初始化。
2. `visibleWorldSubmission`：face cull、range、frustum、正/负 chunk origin，以及自动 `drawVisible` 与逐 batch 手动提交的实际像素一致性。每个 subtest 有独立 world，共享图形初始化。原来的同名 manual parity 用例只手动绘制，没有真正比较两条路径；现在比较九个采样点并要求中心确实为蓝色。
3. `depthNearOccludesFar`：近绿墙移除后显露远蓝墙，保留真实生成、提交、遮挡和重网格后的可见变化。
4. `atlasBoundaryIndices`：首索引、宽图集行边界、最大 packed 索引三个 subtest，验证具体采样颜色；另保留 registry→mesh→GPU 的材质路由 subtest，检查同一注册方块顶绿侧红的实际输出，而非只检查 CPU tex 索引和画面亮度。不再将大量色卡/尺寸作为独立 GPU 场景反复执行。
5. `sameFrameBatchIsolation`：同帧两个 origin、两张不同 atlas，分别观察两个屏幕位置。不能仅凭中心亮度就声称所有 batch 均有效。
6. `frameSlotAtlasUpdates`：八帧切换 atlas 与布局，采用 00/11 颜色时序并逐帧检查绿/蓝。相较简单奇偶切换，该时序可在二槽复用时改变同槽的预期颜色，也覆盖三槽重复访问；不声称穷举任意 swapchain 配置。
7. `instanceBufferGrowth`：先以小数据访问 draw slots，再以 256 个实例重用后续 slot，检查先前绿色 batch 与后续蓝色 batch 同时保留。Vulkan 实现每个 draw 有独立 slot，因此同帧 1→256 本身不是同一 buffer 的扩容；测试通过跨帧重用触发小→大的分配。WebGPU 初始 arena 较大，同样工作量只证明提交隔离，不作为其 arena 扩容证据。
8. `remeshPublication`：同一 world 执行 empty→solid→纹理改变→clear，观察 batch 数与像素变化，清空后与原始空画面对比。
9. `vertexAoDarkensPixels`：保留真实 AO 属性上传和 shader 明暗比检查。
10. `submissionValidationAndRecovery`：缺少 begin、非法方向、空输入、null atlas、零 tilesPerRow 五个 subtest，每行后都验证正常提交能够恢复。缺少 begin 时 Vulkan 抛异常、WebGPU 返回 no-op，按已有后端语义检查，不修改生产接口或静默跳过。

父用例各有一个 VoxelContractFixture，具名 subtest 即时执行，不使用全文件 bundle。需要改变 world 的行创建自己的 world；跨帧资源发布契约刻意保留连续状态。所有新增断言辅助函数、fixture 和局部检查 lambda 显式接收当前 zeroerr TestContext，必要初始化与像素断言失败必须令用例失败。

负向检查曾实际发现：已有 `openGfxWindow` 在无效尺寸下的 context-free REQUIRE 会跳出测试，却给出退出码 0，导致外层 fixture 校验根本没有机会执行。因此这十个契约的专用 fixture 在当前 TestContext 下校验尺寸、创建窗口与图形对象、检查 setWindowSettings；没有调用上述 context-free 窗口 helper，也没有修改共享公共 helper。窗口仍按父用例 RAII 关闭，保留已有视口环境覆盖及 no-render lane 限制。

## 有意退役的范围

47 个旧体素身份作为重复单面/色卡/规模/展示变体退役；另 12 个旧身份保留为具名 subtest，4 个保留为父用例。其余 6 个父用例以实际契约重新命名。退役不代表原来每条场景断言都有逐字等价替代，全部身份及原因仍见 [历史账本](integration-audit.md)。没有把 63 个旧场景全部机械包成 subtest。

两个大体素场景 `sceneHillsIsland`、`sceneWalledTown` 退出常规集成。mesher 的矩形数量、几何排列与大量 atlas 色卡不再各自携带真实图形初始化；这里没有新增另一套默认单元/性能场景来继续执行它们。

同时退役以下四个预览，因为其最终断言只观察模拟或查询数值，删除 draw/present 仍然会通过：

- `softbody.render.clothAndFluid2DPreview`：只检查粒子位置/数量。
- `softbody.render.cloth3DPreview`：只检查一个粒子的 Y。
- `softbody.gpu.renderClothPreview`：只检查一个粒子的 Y，且有 provider 不可用时直接返回的路径。
- `spatial.quadtree.queryPreview`：只检查 hitFrames；已有 `test/unit/spatial/spatial_unit.cpp` 的精确 ID/查询/移除断言更适合此职责。

原有 softbody 碰撞/动量、流体排斥、GPU 约束测试，以及 `test/unit/physics_softbody/softbody_unit.cpp` 的重力、固定点、抓取/释放测试仍保留。CPU cloth/fluid 绘制仍有 RenderImageAudit 调用。此次可能减少 Cloth3D/ClothGPU 绘制函数的偶然“不崩溃”检查；不把模拟断言当作它们的像素、上传或资源生命周期的等价证明。

`stylize.render.cylinderStyleGallery` 保留四种 style 的实际像素变化观察，删除默认五次 PNG 导出及手动展示循环。其他 32 个非体素存活用例正文保持不变。文件中不再包含 SDL_Delay；没有缩短仍保留的物理算法/碰撞/约束用例的模拟步数。

## 验证与测量方法

使用 Windows x64、MSVC Debug SHARED，保持逐用例独立进程、CTest 4 并发、每例超时 120 秒，固定 `EVENGINE_VIEW_SECONDS=0.3`、`EVENGINE_PERF_FRAMES=30`。每轮记录 JUnit，所有预期身份必须出现一次，失败或跳过不作为正常耗时样本；构建、发现及负向检查不计入测量。

修改前重新实测当前 100 个父用例三轮，均全部通过：墙钟 35.491 / 29.964 / 32.967 秒；全文件用例进程累计 138.375 / 116.674 / 128.693 秒；其中 63 个体素用例累计 84.512 / 65.428 / 72.545 秒。中位数分别为 32.967、128.693、72.545 秒。

这是本次新测的定向基线，与之前完整集成运行的该文件 91.664 秒不同，不能混用两种并发邻居和机器状态的数据。修改前后三轮顺序测量用于描述本机成本变化，不是 ABBA 对照实验；保留所有正式样本，不承诺同等幅度的跨平台提升。

最终修改后三轮均为 43/43 通过，无失败、跳过或超时：

- 第一轮：墙钟 13.635 秒；全文件用例进程累计 50.680 秒；体素累计 11.810 秒。
- 第二轮：墙钟 12.722 秒；全文件累计 47.907 秒；体素累计 11.044 秒。
- 第三轮：墙钟 13.269 秒；全文件累计 50.320 秒；体素累计 11.768 秒。

本机中位数变化：体素累计 **72.545 → 11.768 秒（-83.8%）**；全文件累计 **128.693 → 50.320 秒（-60.9%）**；4 并发墙钟 **32.967 → 13.269 秒（-59.8%）**。累计值包含重复进程/图形初始化，不能当作该文件的串行墙钟，也不代表全部集成测试只需 13.269 秒。

新增 fixture 的无效尺寸负向检查、1×1 视口的像素辅助断言负向检查均以预期失败码 1 退出。后者真实触发 `pixel.b > pixel.g` 失败，证明关键辅助断言能令进程失败。四个带 subtest 的父用例还逐一独立进程直接执行，完整输出确认六面 6 行、可见性/提交 6 行、atlas/registry 4 行、提交恢复 5 行共 21 行全部执行，无跳过。负向及直接执行不计入上述三轮耗时。

最终源码为 2579 行（原 4437 行），include 指令 93 条（原 121 条）；32 个其他存活用例正文不变。标准完整 Debug 构建和最终 graphics 目标构建均成功。没有新增编译单元，未重新测量全库冷编译/PDB 成本，因此这里不声称额外的编译性能百分比。完整机器数据、逐例耗时、二进制与源码哈希见 [JSON](voxel-contract-reduction.json)，本机日志保留在 `.local-debug/voxel-contract-reduction/`。

最终完整源码门禁通过：在 MSVC 开发环境执行 `make check CI_BASE=20394d720951b2aa13f035dd9b16104c3de3096e`，退出码 0；511 项脚本回归运行完成，3 项跳过，无失败。该固定基线是本工作起点，避免已前进的 `origin/dev` 将未改动的旧代码纳入差异。格式、模块/测试发现、断言策略、绑定、架构契约和 ruff 均通过；未扩大断言 baseline。历史账本为零未解释缺口。

运行数字仅来自本机 Windows/Vulkan。WebGPU 的提交前置条件按实现差异处理，尚未在本次任务中原生运行 WebGPU 或其他平台；没有报告跨平台 CI 成功。共享状态用例若在帧中断言失败，父用例会失败，随后子用例的诊断可能受未完成帧影响；没有通过重新打开窗口来掩盖失败。

本次未改变生产 API、ECS、持久格式、模块边界或跨域生命周期；无架构规则例外。图形失败路径、子用例上下文、原生构建与源码质量检查均在本次验证范围内。
