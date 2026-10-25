# 集成测试前十个高开销文件审查

审查基准：`044e1ca5a`；使用 `integration-slow-runtime.json` 的 after 轮，
以及 `.local-debug/integration-20261026-after/` 的逐用例结果、注册信息和 JUnit。
本轮只读审查源码和已有测量，不修改测试，也没有重新运行测试。

这些时间是四并发运行时，各用例在各自进程中的经过时间之和，不是 CPU
采样时间，也不是文件单独运行的墙钟时间。前十个文件合计 500.425 秒，
占全部 1,142.860 累计进程秒的 43.79%；不能承诺删掉其中多少就能等量减少总墙钟。
下列优化是有源码依据的候选，尚未通过新的 A/B 证明收益。

## 统计口径需要先区分

前轮 collector 按集成目录的物理 CPP 选择用例，排除 bundle 和独立 profile，
但没有排除这些 CPP 内的 `benchmark` 标签。本次完整统计因此包括
`ClassicScenes.perf.maxFps`（2.748 秒）。
[Makefile](../../../Makefile#L552) 中普通 `make test` 在没有 FILTER、
`INCLUDE_BENCHMARKS=0` 时已经使用 `-LE benchmark` 排除它。
`EVENGINE_BUILD_TEST_BENCHMARKS=OFF` 排除的是 benchmark 目录源码，不能理解为
所有带 benchmark 标签的注册都不存在。迁走 maxFps 可以整理边界，不能算普通
默认 lane 的新增运行收益。

Make 默认还使用 `--repeat until-pass:2`；本次计时 collector 没有开启重试。
已知失败在本轮占 34.562 累计进程秒，普通 Make lane 的重试可能增加开销，
但没有本轮重试实测，不能直接将此数字翻倍并当作新的总耗时。

## 1. RenderSystem3D：80.126 秒，76 个用例

[源码](../../../test/integration/render-presentation/render_presentation_graphics_RenderSystem3D_tests.cpp#L2469)

- 最慢是 cloud motion 4.783 秒、optical cycle 4.256 秒、authored wisps
  2.931 秒，三例合计只占本文件 14.94%；大头并非一个失控长循环。
- cloud/optical 分别重新调用 `SkyWispsAsset::generate(2026)`，随后准备
  SkyRuntime/LUT。实际生成包括 cloud 512²、stars 1024²、moon 256² 和
  96×48 球面网格，见
  [SkyProceduralAsset.cpp](../../../src/modules/graphics/sky/SkyProceduralAsset.cpp#L64)。
  [SkyAtmospherePass.cpp](../../../src/modules/graphics/sky/SkyAtmospherePass.cpp#L177)
  还会烘焙基础和 optical 分层 LUT。这里不是等待十秒真实时间，时钟由测试注入。
- 可先将非资产生成契约改用最小自有 wisps 输入，保留至少一例真实资产生成到
  渲染的组合验证。不能让整个组只使用 mock 后声称覆盖了资产组合。
- cloud cycle 对 160×90 全图逐 RGB 调用约 43,200 次 `REQUIRE`；optical
  两轮逐 RGB 约 6,144 次。可遍历全部像素，聚合最大误差、非有限值和变化标记，
  最后少量断言；必须保持原阈值、全像素范围和 NaN 检查，不能稀疏采样替代。
- `volumetric.*` 20 例合计约 18.871 秒；质量/参数往返等小契约可局部共享设备。
  三次周期捕获、午夜跨越、负时间拒绝、散射/阴影/froxel 像素观察均应保留。

## 2. RenderImageAudit：67.292 秒，58 个用例

[源码](../../../test/integration/render-presentation/render_presentation_graphics_RenderImageAudit_tests.cpp#L569)

- 最慢三例仅占 7.23%。38 个 imageAudit 合计 44.996 秒，20 个 RenderScenes
  合计 22.296 秒，成本分散。
- 58 例 JUnit 中都能读到一次 `initWithWindow(total)`，可见初始化累计
  31.210 秒，约为该文件经过时间的 46.4%。这是固定初始化成本的证据，不是
  可无条件消除的成本；截断日志不能证明每例只有一次初始化。
- `pipelineConfigs` 已在一个场景中覆盖 13 个阶段：约 44 次提交、18 次捕获，
  包括五次 flicker 双帧观察。`materialsAndCamera` 也已共享场景检查 13 种状态，
  不应再按 13 次重复窗口初始化处理。
- `auditGpuFrame/auditGpu2D/capturePresented` 已在预热阶段关闭读回。
  上轮已删除成功路径 PNG 和 warmPresent 的额外 sleep，不能重复计算这部分收益。
- 可考虑同一场景的小契约共享设备，或者把纯参数算法边界归 unit；直接合并整份
  CPP 的所有用例会带来相机、灯光、ECS 和 RenderControl 残留，不能使用整文件
  bundle 代替契约设计。没有静态证据支持直接删除现有预热/flicker 帧。

## 3. gpu_driven：54.909 秒，51 个用例

[源码](../../../test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp#L685)

- 最慢是 lit-material spheres 3.216 秒、contact sheet 1.846 秒、vegetation
  update/season 1.766 秒。13 个 GpuDriven 例约 15.121 秒，9 个 decal 例
  约 11.487 秒，不是所有 GPU 例在测同一件事。
- `renderImportedPresetContactSheet` 对 40 套 128² preset 做烘焙、拼图，输出
  41 张 PNG 和 manifest；`renderImportedPresetsOnLitMaterialSpheres` 再烘焙
  40 套、上传 120 张通道纹理、创建 40 个球并提交四帧，主要终点是截图非空、
  PNG 保存成功。`renderGalleryPng` 也属于视觉产物生成。
- 优先将完整画廊移到手动/产物任务，常规只保留有代表性的真实
  import→upload→projection 像素契约。不能仅删 PNG 后将剩余“截图非空”描述成
  原画廊的视觉正确性验证，也不能未经审查丢掉不同 preset 的导入格式覆盖。
- [gdWarmPresent](../../../test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp#L1728)
  固定四帧，调用前已开启读回。可保留所有提交而仅观察末帧，避免无用 GPU 等待
  和回传。cull/HZB 依赖历史，不能直接将四帧变一帧。
- forward、cull、visibility resolve 的像素对照和可见实例/draw-count 是独有观察，
  当前失败也不能靠删除或合并提前终止隐藏。

## 4. backend_parity：52.823 秒，67 个用例

[源码](../../../test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp#L2592)

- `pixelWorldMillionPixelAtlas` 3.177 秒：实际生成 1024² 视口，半径 724 的
  paintCircle，首次上传 256 chunks，再修改相隔的两个 chunk。
  initHeadless 的可见样本为 311.7ms，规模工作显然不止设备初始化。
- 百万规模应留 benchmark；短契约仍需跨 chunk、视口外裁剪、首次同步、两处脏
  chunk 的增量上传及 revision/count。缩小场景不等于覆盖百万规模。
- `textureUpdateAndAlphaBlend` 中有 400 次 1×1 texture 更新，注释明确涉及
  descriptor 压力。可分离规模负载，常规保留短更新序列、实际红绿输出和 alpha
  混合观察；不能让短序列承担耗尽阈值证明。
- SSF 的 64² headless 投影、法线、颜色和遮挡小契约可按任务共享设备，renderer
  与输出仍独立。两个流体例分别有 120/60 步，合计 2.491 秒，可通过近接触初态
  缩短，但必须保留下落、SDF 不穿入、聚团有界等条件。
- 两个名称为 millionPoint/millionResident 的用例本轮因环境开关提前返回、
  零断言通过，不能把其内部 20/10 轮循环当作本轮慢因。

## 5. ClassicScenes：51.236 秒，56 个用例

[源码](../../../test/integration/render-presentation/render_presentation_graphics_ClassicScenes_tests.cpp#L1565)

- maxFps 2.748 秒属于前述 collector 口径；普通无过滤 Make 已排除，不能重复
  宣称默认测试会因迁移它而加速。
- flyThroughConfigs 已是每配置三次固定相机提交，五配置分别保留像素审计，
  最终 FullLit 读回仍供调用者使用。上轮已去掉默认墙钟漫游和成功 hero PNG，
  这些不是现存候选；15 个缺资产例本轮是 skip，不能按资产齐全推算收益。
- [Outline 的 warmPresent](../../../test/integration/render-presentation/render_presentation_graphics_ClassicScenes_tests.cpp#L3780)
  仍有逐帧 `SDL_Delay(8)`。可保留全部真实提交和 `darkenedPixels > 8`，去掉
  sleep。150×33ms 的人工预览已受 manual 开关保护，不计默认开销。
- IBL 多例反复创建相同窗口、cube、camera、environment，可将强度/清除恢复、
  metal/dielectric 分别按小契约共享初始化。当前已有 IBL/Cornell 失败，合并前
  必须保证一个子例失败不会使其他观察漏跑，不能把更早退出当作成功加速。

## 6. voxel_face_dir：44.451 秒，42 个用例

[源码](../../../test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp#L488)

- 3D cloth 碰撞 4.247 秒：48 粒子从 y=4 开始，以 gravity=-6 运行 180×1/60。
  质量动量比较 2.915 秒：24 粒子从 y=3 向 y=1.2 的 box 下降，两种密度各
  180 步。这两个例合计 7.161 秒，是该文件最清楚的长模拟成本。
- 可用靠近接触面的初态和少量短步触发同一跨系统接触；必须同时证明确实发生
  接触、全粒子不穿透、轻重物体速度差。不能拿 2D 例替代 3D，也不能拿大 dt
  或放宽距离阈值替代初态设计。每种密度应使用独立的 World3D。
- shaderReload 四例重复 160×120 Graphics 和 SPIRV 初始化；可具名共享设备，
  保留正常重载、坏 SPIRV 回滚、外来 shader 拒绝和 GLSL 编译失败。
- voxel 的 same-frame batch、跨 frame-slot atlas 更新、buffer growth 可共享
  fixture，保留各自 batch 隔离、layout/colour 和旧 batch 扩容观察。不能机械
  缩短用于遍历真实 frame slots 的帧数。8000 粒子例上轮已迁走，不再列为现存负担。

## 7. asset_import：43.709 秒，102 个用例

[源码](../../../test/integration/asset-import/asset_import_graphics_tests.cpp#L642)

- Mixamo preview 3.768 秒含 150×16ms sleep（名义等待 2.4 秒）；最后只检查
  drawnBones=70 和 Idle。可保留导入/pose/真实绘制与 Idle→Run→Jump→Idle
  状态观察，删人工观看等待；如继续缩模拟帧，要按状态机混合/exit-time 重新设计
  短契约，不能只把循环改成三次而丢掉状态转移。
- map preview 2.160 秒含 75×16ms sleep，业务终点主要是预先 collect 的
  116 个元素。应审查绘制是否有独有像素观察，区分 unit 数据契约和短渲染组合。
- [renderModelSmoke](../../../test/integration/asset-import/asset_import_graphics_tests.cpp#L1481)
  每次 60×16ms；rock、toon、sofa 分别耗 2.760/2.716/2.361 秒。默认只需
  少量有效姿态和原中心亮度/实际纹理观察；rock/toon 可共享同一模型导入，保留
  两种材质管线输出。不同格式/资产的导入失败与纹理覆盖不能被一次 cube 替代。
- Cesium 另有 60×16ms；fire preview 有 90×16ms 和八次预热。
  这些等待可以先与业务更新分开，不需要先动引擎 API。

## 8. procgen：35.440 秒，30 个用例

[源码](../../../test/integration/render-presentation/render_presentation_graphics_procgen_tests.cpp#L109)

- resident simulation 3.460 秒：六个 emitter，各容量 8192，150 帧；累计提交
  >800、存活/实例 >1000，再验证 gameplay collision 触发 GPU→CPU 回退。
  规模与积累过程应拆开：短契约用 burst/短生命周期形成目标状态，保留 resident
  handle、CPU alive=0、实际 indirect 输出和回退；长规模另留 benchmark。
- 预算场景 2.903 秒：24 emitter、120 帧，断言 12 模拟/12 跳过、总粒子
  <=900、实际渲染 >100。可以用少 emitter 和一次 burst 越过 budget 边界，
  不能仅删循环后保留旧人口阈值，也不能把预算 skip 与真渲染观察删掉。
- playback 2.842 秒、ribbon 2.147 秒同样有积累循环。可缩内容生命周期并按
  play/pause/seek/loop 与 trail/ribbon 独有观察共享初始化。
- `mainThreadCrossoverAtSixteenThousandParticles` 约 1.300 秒在断言耗时优势，
  是 benchmark 职责；普通集成保留数值/后端行为，不应断言性能拐点。
- 四个 resource 生命周期例可共享窗口、各资源独立作用域；procgen preview
  自动模式已每算法一帧，不能把 manual 分支当作常规长预览。
- waterfall 当前失败，flowDiff=0、foamDiff=0。不能靠减捕获帧或降低阈值处理。

## 9. vegetation_extras：35.353 秒，40 个用例

[源码](../../../test/integration/render-pipeline/render_pipeline_graphics_graphics_vegetation_extras_tests.cpp#L977)

- Grass 2.357 秒：960×540，实际 6200 dense/220 sparse；创建窗口约 105ms、
  Graphics 初始化约 470ms。四次预热提交前已打开 readback，但只检查最后图。
  可保留四次提交，前三次关读回，末次打开。
- 随后无条件保存 frame 两份、重新 pack atlas 并保存两份，共四张 PNG。
  可将这些输出和二次打包变成 manual/dump，保留最后图的绿/暗绿/亮绿计数。
  亮绿 >50000 有 CSM 回归价值，不能缩密度/分辨率后单纯下调阈值。
- GPU tensor 多个小例分别 initHeadless；fused 样本仅初始化就有 373ms。
  同任务可共享 device，各 Func/CompiledFunction 仍独立，保留 GPU 后端、shape
  和全元素 eager 误差；不能用 transformer 端到端替代 SDPA/conv 的独有 kernel。
- parallelCompilation 1.379 秒有 workers={1,4}、两次执行、80 dispatch/4096
  元素，可审查缩短链长；必须仍有四个独立编译任务、并行峰值、二次零编译、
  serial/parallel 一致及 cancel/retry。
- sessionShapesAndLifetime 刻意实际销毁 device，检查两种销毁顺序；不能为了
  共享初始化而并进长寿 device fixture。

## 10. file_io_graphics_tensor_llm：35.086 秒，31 个用例

[源码](../../../test/integration/file-io/file_io_graphics_tensor_llm_tests.cpp#L633)

- 六个 projectIndex 例合计 20.465 秒，六个 LanguageServer 例 7.434 秒，
  两组占本文件 79.51%。最慢 automaticIndex 8.027 秒、chained completion
  4.321 秒、shadow/dynamic rejection 3.631 秒。不是本机 LLM 推理慢：五个
  LLM 例合计仅 0.807 秒，缺资源提前返回，不能据此评价真实推理效率。
- automaticIndex 的 fixture(false,false) 不复制大项目，不能归咎于拷贝整个
  example。它在初建、rename、修改、删除后多次构图；每次 MCP query 还会
  refresh 检查。实际实现
  [refresh](../../../src/engine/devtools/McpProjectIndexTools.cpp#L43) 会扫描文件，
  变化时重建候选图并再次扫描确认；不变时会复用，不是每次请求都完整重建。
- [automaticGraph](../../../src/engine/devtools/ProjectIndexAutomatic.cpp#L156)
  在 prepare/project 中获取全 binding registry snapshot，并进行语义合同序列化。
  [snapshot](../../../src/engine/common/ScriptCompiler.cpp#L446) 确实复制并排序。
  [LSP completion](../../../src/engine/devtools/LanguageServer.cpp#L739) 也反复取
  snapshot。它们是需要分段计时的机制成本，当前没有证据将 8 秒精确分摊给某一项。
- 下一步应分别计 scan/parse、合同准备、graph、序列化、查询和 completion；保留
  真实 MCP→生成合同的组合例，纯语法遮蔽/推断角落可下放 unit 使用小合同。
  增量、rename、stale revision、删除观察仍需真实文件，不能全部 mock 掉。
- remoteSyncUnreachable 2.298 秒有固定 250ms sleep，随后 stop/join；真实
  HTTP manifest timeout 是 2000ms。总时长与该路径相符，但尚未独立测出 join
  占比。单纯检查“不等于 synced”可能只观察到初始 idle，不能删等待后假通过。
  失败状态机角落可用可控 transport，保留一例真实 loopback HTTP/关闭生命周期。
  download 中 80ms sleep 与依靠不同文件长度检测变更的策略无直接依赖，可单独验证删除。

## 建议实施顺序

1. 先删确定的纯观看/产物成本：asset_import 的人工 sleep、decal 画廊、Grass
   四份 PNG/二次 atlas 打包、Outline 小 sleep。保留或补齐真正的像素/状态观察。
2. 再改模拟初态：3D cloth 近接触、particles burst 与短生命周期；规模工作留
   benchmark，并明确短契约不证明长时间/大规模稳定性。
3. 再按单一契约共享设备、shader/模型输入；重点是 shader reload、IBL 小组、
   SSF 和 GPU tensor。保留独立资源作用域与两种真实销毁顺序，不整文件 bundle。
4. 对项目索引/LSP 先分段计时；ImageAudit 的合理多帧/多配置观察暂不机械削减。

以上均不要求拆 CPP、增加 Unity 规则或恢复来源选择宏。此次没有公共 API、
模块边界、ECS 或生命周期实现变更，也没有架构规则例外。

## 已实施的契约调整

这十个合并 CPP 保持原编译边界，未增加 Unity 或来源选择宏。

- RenderSystem3D：天空仍逐像素检查，聚合误差与 finite 结果后断言；三组
  volumetric 参数作为 `volumetric.sharedParameterContracts` 的具名子例共享设备，
  每个子例新建参数对象。实际天空生成资源未换成合成输入。
- RenderImageAudit：`skinnedStill` / `skinnedPose` 合入
  `graphics.imageAudit.skinnedPoseContracts`，共享设备和模型，显式重置各子例场景。
  原 bind pose、t=0、半周期和 flicker 观察保留。
- gpu_driven：四帧 warmup 只读回末帧。三套 decal 画廊完整移至
  `test/benchmark/graphics/graphics_decal_gallery_benchmark.cpp`；默认投射契约增加
  一个真实导入预设的三通道烘焙、上传与投射像素差检查，不代表四十种外观都被验证。
- backend_parity：400 次纹理更新和百万像素 atlas 原负载移至
  `test/benchmark/graphics/graphics_backend_resource_scale.cpp`；默认保留四次更新
  和四到六个 chunk 的增量上传、裁剪、revision、像素与 alpha 契约。SSF 两个子例
  共享设备，CPU/GPU renderer 独立。
- ClassicScenes：IBL 强度和移除恢复合为具名子例，每例重新建立场景；Outline
  去除自动 warmup 固定等待，保留原帧数及像素阈值。maxFps 未修改。
- voxel_face_dir：shader reload、voxel 提交按契约共享设备，保留各自输入和多槽
  帧观察。两套原长布料场景移至 `test/benchmark/physics/cloth_world_contact_benchmark.cpp`；
  默认从近接触初态分别模拟 24 / 30 个 1/60 秒步长，仍检查地面与质量动量差。
- asset_import：去除自动预览等待，地图/模型渲染两帧，保留原采样朝向；Mixamo
  48 个 1/60 秒步长明确观察 Idle、Run、Jump、最终 Idle，fixture 跳跃退出阈值
  缩为 .05；火焰预热八步再观察八步。短例不声称验证原观看时长的稳定性。
- procgen：resident/budget/ribbon/playback 分别缩至 8/8/12/24 步，使用计划 burst
  和短 lifetime 触发真实 quota/循环路径，保留 GPU、indirect、fallback、配额和
  像素断言。16384 粒子 CPU/GPU 性能比较原负载移至
  `test/benchmark/particles/particles_gpu_crossover_benchmark.cpp`。waterfall 原失败阈值保留。
- vegetation_extras：Grass 保留四帧，末帧读回；人工 PNG/二次 atlas 仅手动预览。
  四个 GPU tensor 子例共享设备，张量和编译函数独立。ONNX 并发编译维持四类程序、
  单/四 worker、并发峰值、缓存、取消及恢复观察，dispatch 从 80 缩至 16。
- file_io：六个 LSP 子例共享 server，在同一项目根使用独立文件 URI，协议 shutdown 最后执行；
  两个 auto-index 子例保留各自可变 fixture。远程同步用真实 loopback 接受连接后
  立即关闭，必须进入 `error:unreachable` 再恢复并验证文件内容；它验证 HTTP 传输
  失败恢复，不声称覆盖 TCP connection-refused。删除与不同文件长度检测无关的 80ms 等待。

所有旧身份的子例/benchmark/删除映射以生成的 `integration-audit.md` 为准；该审计
检查历史 3792 个身份均有去向。短契约与原规模 benchmark 的边界是明确的，不能将
父用例数量下降解释为相同规模负载仍在默认运行。

## 本轮验证结果

基线为 `044e1ca5a` 的已编译二进制与十个源码快照。使用 Windows Debug SHARED、
真实 Vulkan、隐藏桌面、四并发、`EVENGINE_VIEW_SECONDS=0.3`、
`EVENGINE_PERF_FRAMES=30`，按同十个物理 CPP 选择父用例，排除 benchmark、bundle
和 profile，不使用失败重试。运行前校验注册身份与实际二进制身份唯一对应。

- CTest 实际总耗时：129.14 → **110.91 秒**，减少 **14.12%**；包装器耗时另列于 JSON。
  清理前的第一次修改版为 106.52 秒。基线只有一轮，修改版两轮有波动，不能宣称
  统计显著性或把这一比例外推到整个集成套件。
- 父用例 552 → 529；累计进程时间 512.249 → 440.889 秒，十个文件各自的累计值
  均下降。累计值不是各文件独立运行耗时，也不是 CPU profiler 结果。
- 基线 511 通过、26 失败、15 跳过；最终 487 通过、27 失败、15 跳过。
  26 个旧失败映射到 25 个父用例（两个 IBL 失败合入同一父用例），全部仍可观察。
  另有一个 LSP 父用例因原来就失败的 completion `WARN` 改为 `REQUIRE` 而失败；
  新增导入贴花投射断言也失败，像素差为 0。后者是新增覆盖，不能声称基线已有
  等价导入投射验证，也不能将其归为已修复。原默认 solid 投射仍有既有像素警告。
- 短布料接触/动量、Mixamo 状态路径、地图/模型预览、粒子短契约、共享 skinned
  审计、volumetric 子例及 loopback 失败恢复均已实际执行；未降低已有失败阈值。
  LSP formatting 原来拼接 URI 缺双引号的问题已在测试 fixture 修复。
- 十个默认 OBJ：230,407,138 → 230,823,883 字节，**增加 0.18%**。
  仍为十个合并编译单元；没有 Unity、来源选择宏或新默认 OBJ。
  因而严格的“OBJ 零增长”尚未达到，不能以 PDB 的下降掩盖它。
- 原现场 PDB 540,209,152 字节；保存旧生成物、移除增量链接状态后重新链接，
  最终 PDB 为 417,402,880 字节（下降 22.73%）。这是现场生成物比较，基线 PDB
  未同步做干净重链接，不能把全部下降都归因于源码改变。
- 同参数、无编译缓存、四 worker 编译十个源码快照，顺序为旧/新/新/旧：
  旧版 15.896 / 15.312 秒，新版 16.005 / 15.274 秒；中位数为 15.604 / 15.639 秒。
  交换顺序后差异方向翻转，未观察到明确的编译提速；样本太少，也不能证明严格无回归。
  此实验不含配置、生产代码编译或链接，源码路径不同但 flags/生成头文件相同。
  详见 `integration-top10-build-cost.json`。首次失败的命令转义实验不计入结果。

新增四个 benchmark CPP 已在显式打开 benchmark 的正常目标中编译并发现；最终配置
恢复 benchmark OFF。删除了无调用的私有 helper、只被死代码使用的类型/include，
以及残留临时调试文件写入；有效测试身份未因这项清理改变。

`make check CI_BASE=010c77ce2` 在 MSVC wrapper 与 clang-format 环境下执行：562 个
脚本测试，3 失败、1 错误、5 跳过。四项问题均位于既有
`test_ci_third_party_cache` 的 Windows/Bash 路径处理；格式、分层、架构契约与测试
身份等检查通过。普通终端运行额外出现的 nmake 环境错误不作为源码回归。
后续死代码清理再次通过正常严格编译、策略检查、身份审计和差异空白检查。
没有生产 API、模块边界、持久化或 ECS 模型变更，没有架构规则豁免。

逐文件比较见 `integration-top10-runtime.csv`；JSON 保留完整 552/529 个父用例耗时、
失败映射和测量边界。原有外部子模块本地改动未重置，也未纳入提交；干净 CI 仍需验证。
