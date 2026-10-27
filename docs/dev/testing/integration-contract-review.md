# 集成测试契约复审

本轮按“这条测试独有地证明了什么”审查当前集成目录，而非要求保留历史 case 数。只新增审查文档；没有删除、迁移或修改测试、生产代码、构建或分类元数据，也没有重新构建或测量运行速度。

## 范围与证据强度

当前目录有 138 个 CPP、可提取的 3597 个字面测试名（含条件分支）。这是当前目录清点，与历史 3792 个身份、全库分类数量及实际编译注册数不同口径。四份报告的文件/章节清点合计覆盖全部 138 个文件；对列为发现的候选、保留对照和依赖夹具做正文核查。大型聚合的全章节索引不等于已经逐断言证明全部测试等价，也不等于允许按某个百分比批量删除。

- [资产、文件、恢复与生命周期](review-asset-contracts.md)：10 个 CPP、510 个字面测试名。
- [模拟、导航与跨域组合](review-simulation-contracts.md)：77 个 CPP、1758 个字面测试名。
- [渲染、UI、音频与编辑](review-render-contracts.md)：29 个 CPP、660 个字面测试名。
- [脚本、并发、破坏与布料](review-script-concurrency-contracts.md)：22 个 CPP、669 个字面测试名。

## 已确认的问题

### 1. 有注册并不等于有行为证据

最明确的退役候选是 `SimpleSquirrelTest.GetterTest`：`script_api_graphics_scene_tests.cpp:3373` 调用的 `test/simplesquirrel.nut:48` 只有 `return true`，却仍启动 ScriptTest VM。实际 getter、参数转换、错误和 native 存活观察已经存在于同文件 :3493 的 `SimpleSquirrel.DirectMemberConversionsAndLifetime`。空注册没有独有 getter 证据，不应为了历史数量保留。

另外，`runtime_lifecycle_graphics_tests.cpp:646` 的 scancode roundtrip 只检查两个结果非空，没有检查回到原键；相关窗口建立失败时还可能直接 return。必须先修“没执行目标行为也能通过”的测试，再讨论压缩运行数量。必需 provider 的失败应成为失败；真正可选的 backend 应由明确配置/lane 区分，不能用静默成功替代执行。

### 2. 前一轮迁移没有清完本地算法、值和参数矩阵

仍有 HUD geometry、Variant/Message 构造、NetWriter/NetReader 的字节往返、UI settings JSON、RenderFlow 手工事件、Voronoi mesh/seed/预算等本地检查。它们可能是有价值的测试，但不因此需要集成门禁或一次次真实模块启动。

例如 `file_io_graphics_tensor_llm_tests.cpp:727` 仅验证 PcgPhotoModeValues 的 setter/snapshot/restore，还残留五次无断言观察的 `.local-debug/pvstage.txt` 调试写文件。应移入现有 UI unit 合并文件并去掉这些写入，保留类型、原子失败和 schema 语义；若要保留集成代表，应该观察保存设置如何影响真实 session/consumer。

### 3. 一些 GPU/VM 开销只服务于 getter 或重复格式检查

AO/AA/volumetric/outline 参数往返会创建真实窗口及 shader，却主要检查 setter/getter；Mixamo 多个消费者各自重新加载相同三个文件；多个 RoadPreset case 重复完整 create→edit→commit→undo→capture→restore→redo，只在投影字段上变化。

优先把参数/格式完整矩阵留在局部契约；集成保留真实创建/上传/像素结果、跨阶段回执、失败不发布或不泄漏的代表。若不同字段走独立适配通路，不能不经核对就删掉相应路由观察。确实属于同一契约且需要真实边界的剩余行，才考虑共享不可变夹具或 subtest，每行重置可变状态。

**subtest 不是冗余测试的保留理由。** 先判断是否有独有故障，再决定是否共享 setup；不能把十条重复端到端路径改成十个 subtest 就视为完成设计精简。

### 4. benchmark、长回归和默认成功空跑仍混在普通 lane

除前一轮已分离的两个 workload，还发现 pixelworld 百万像素、montage、mesh-VFX、GPU million-element 等环境门控 timing 注册：默认只 return，开启才执行采样。有些 dense terrain 检查又把大尺寸功能、资源复杂度和机器时间门槛混在一个 case。

这些需要显式归属及成本层次，不能把默认空跑称作已验证大负载。计时采样进入 benchmark；小型真实链路进入集成；确定性的读字节数、预算和不变式保留恰当的本地或资源契约。

与此同时，布料的 1800 步/60 秒模拟时间接触稳定性、物理长时堆叠和串并行 bit-exact 不等于纯 benchmark。它们有独有行为证据，可评估慢速回归通道，但不能简单缩短采样、只看最后一帧或换 mock 后声称覆盖不变。

### 5. 有重复候选，也有必须保留的相似测试

Debugger 的 step-over/step-into 有两套相近 VM 场景和核心断言，但 line-only 与 call/return hook 不同，应先确认是否存在受支持的兼容契约，再决定保留一个强观察版本或留下一个明确的兼容代表。

相反，Action 的 prepare-fail、commit-fail、success 分别观察没有 reservation、确实 rollback、确实 commit；native proxy 的地址重用与 GC、world-first 销毁、原子恢复失败、GPU overflow→scan、不同 framebuffer/backend 和 DAP/TCP/stdio transport 均有不同故障通路。不能仅因 setup 相似就删。

## 下一轮实施顺序

1. 先退役明确无行为的注册，移除无观察的 debug I/O，修正初始化失败静默通过与名称强于断言的问题。
2. 将确认的本地矩阵并入已有 unit 文件，将残留 timing workload 放现有显式 benchmark 通道；保持合并编译，不新增细碎 OBJ、Unity 或来源选择宏。
3. 对真实边界重复的任务，写清保留的成功、关键失败/回滚、释放与陈旧性观察；合并后不能丢失它们。
4. 最后才共享昂贵 fixture，执行选定的契约测试，并按相同契约工作量重新测量。历史审计同步逐项退役理由和保留证据，不再把 case 总数不能下降作为目标。

本轮是静态审阅结果，尚无新增删减数量或提速承诺。前一轮 628 个可比场景的运行数据仍只是那个子集；它既不代表全套 integration 耗时，也不证明本轮发现的剩余测试已合理。没有生产 API、ECS、持久化格式或跨域生命周期改动，因此未引入架构例外；只读报告不需要重复编译门禁。

后续脚本场景审查已落实 GetterTest 退役、契约分组及严格断言修复，具体范围和新测量见 [脚本场景契约精简](script-scene-contract-reduction.md)；原审查行号是修改前定位快照。
