# 脚本、并发、破坏与布料测试契约复审

本轮只读审查；未修改测试、构建配置、分类、历史审计或生产代码，未运行构建或测试。下面是正文取证及建议，并非已实施的删除。附录覆盖本组 22 个 CPP、669 个字面注册；大型文件按章节和 case 清点，再深读列出的候选及保留对照，不宣称已逐断言证明所有 case 等价。

## S1：getter 测试没有 getter 行为，可以退役

- `test/integration/script-api/script_api_graphics_scene_tests.cpp:3373` 的 `SimpleSquirrelTest.GetterTest` 只调用 `callScript("getterTest")`。
- `test/simplesquirrel.nut:48` 的 `getterTest()` 只有 `return true`，没有 getter、输入、输出或状态观察。`test/ScriptTest.h:26` 仍为这个 case 创建 VM、暴露模块、编译并执行整段脚本。
- 建议删除这个独立注册及无其他调用者的脚本空函数，记录其未提供 getter 证据；不应为了维持历史 case 数再补一个同名空 smoke。
- 保留 `SimpleSquirrel.DirectMemberConversionsAndLifetime`（同 CPP:3493）：它实际调用成员 getter/setter、检查转换和异常，并观察 native 实例的存活计数。保留实例字段与 metamethod 的优先级测试；这些是不同绑定规则。
- `SimpleSquirrelTest.RefTest`（:3289）所调用的脚本主要通过 `B.print()` 读取所保留的 `A`，随后返回 true。它不等同于上述纯空函数：嵌套引用存活是一个潜在的独有契约，应先改成明确的值和释放观察，再判断与其他 lifetime 测试是否重叠，不能一并删除。

## S2：并发聚合里仍混有本地 codec、值与构造 smoke

- `test/integration/concurrency/concurrency_graphics_tests.cpp:1949` 的 `network.NetWriterReaderRoundtrip` 仅在内存 buffer 中写读整数、浮点、字符串、字节；:1996 的 `network.NetReaderOverrunIsSticky` 检查读取越界后的本地状态。没有 socket、worker、事件投递或外部消费者。
- 同文件 :1482 的 `event.VariantMessage` 也是本地值操作；:3160 的 `thread.channel.pushPop` 没有第二条线程；:3153 的 `thread.create` 仅检查构造、名字与硬件并发数。
- 这些应放进现有所属 unit 合并文件；不要为了迁移再新增一串小 OBJ。构造前提可并入真实消费者测试，硬件查询的返回值约束属于本地契约。
- 必须保留 :1523 的分片/合帧、:2019 的大 TCP 完整发送、:2096 的大消息重组及 :3214 的真实阻塞唤醒。字节 codec 往返不能证明这些边界；也不能用 fake queue 替换真实 worker 的 join、停止、销毁或异常传播。

## S3：默认成功空跑的百万像素 benchmark 仍在 integration

- 同一并发文件 :2382、:2413 的两个 `pixelworld.benchmark_million_*` 在各自环境变量未设置时直接 `return`。开启后创建 1024×1024 像素场景、真实 JobSystem，重复采样并打印 P50/P95。
- 默认是无目标行为的成功注册；它不是默认集成回归已经验证了百万像素负载。应进入现有显式 benchmark 编译/运行通道。
- 保留 :2356 的真实串行/并行 thermal 一致性：相同 seed、真实 scheduler、跨多 tick 的逐字节 snapshot 对照。它提供 benchmark 的正耗时断言所没有的行为证据。减少 tick 之前必须说明热反应、跨 chunk 与调度边界何时被触发。

## S4：地形测试同时承担小功能契约、大尺寸吞吐与机器时间门槛

- `test/integration/script-api/script_api_graphics_terrain_probe_tests.cpp:548` 的 `procgen.terrain.performance.real1kRoundTripAnd1k4k8kRandomAccess` 分配并填充 1024² 地形与多张环境图，实际 bake/读取，再在 1k/4k/8k 稀疏目录上各采样 20 次，输出时间并检查 20 秒、1 秒、100 毫秒门槛。
- 不能称其“没有任何性能断言”：它确实有时间门槛。问题在于功能失败、资源复杂度失败和宿主性能失败被一个默认父 case 混为一体，且放在脚本 API 聚合中但正文没有 VM。
- 建议小型真实 bake→archive→stream 链路保留为集成代表；读字节数/驻留数量上界保留为确定性资源复杂度契约；尺寸/采样/机器时间门槛放明确的性能验收通道。大逻辑地址空间的稀疏读取证据不能被一个 tiny dense fixture 完全替代。
- 同文件 :80 的 `convolveAndScriptBinding` 先做 native 数值检查，后面脚本调用只检查 `.ok`。应拆清目的：native 数值矩阵由 unit 负责，脚本代表应验证传入对象实际被修改及回执投影，而非在脚本里重新遍历全部数值组合。这里是重新分配观察，不是把所有绑定方法按名字合并。

## S5：重复的 debugger 行为观察需要先确认 hook 契约

- `test/integration/script-api/script_api_graphics_dap_tests.cpp:1249` / :2322 都编译 caller→callee 脚本，在调用行下断点并 step-over，最终检查停在 caller 的下一行；后者另外检查堆栈深度。:1296 / :2290 同样重复 step-into 的核心观察。
- 前一组 hook 只转发 line，audit hook 同时转发 call/return；因此不是可以仅凭文本直接删除的两个完整 fixture。
- 当前实现 `src/engine/devtools/Debugger.cpp:592` 的 stepping depth 从真实 VM stack 读取，:758 的 step 判断也使用该深度。建议以 audit 路径承载完整 stepping 契约；如果 line-only hook 是受支持的兼容接入，就只保留一个明确命名的兼容代表，说明其独有故障，而不是两套 near-identical stepping 矩阵长期并行。
- 不删 `breakpointHitsInsideStepOver`、step-out、异常报告、条件求值失败、source-map 与 DAP transport 观察：它们不是“都用了同一 VM”就等价。

## S6：MCP 全目录路由 smoke 与真实工具行为不是一回事

- `test/integration/concurrency/concurrency_graphics_tests.cpp:625` 的 `everyDeclaredToolIsRouted` 先通过 stdio 列目录，再给 100 多个工具全部传 `{}`；判据只是响应不含 `unknown tool`。
- 这能证明 catalogue/dispatcher 闭包的一部分，不能证明参数正确、命中期望 provider、执行或发布了状态。工具数量增长还会自动扩大这一次 stdio session 的工作量。
- 建议本地注册/路由关系由直接 handler/目录契约测试负责，stdio 与 TCP 各保留代表性协议回合；真实 consumer 的行为继续由对应集成测试验证。不能拿这个 bulk smoke 替代所有工具行为，也不应为此另建一份平行工具清单。
- 对照 `script_api_graphics_mcp_tests.cpp:694` 的 execute→observe 会核对被拒绝与成功事务的关联结果；:804 的 observation session 会验证去重与关闭。这些具有 bulk route smoke 没有的独有观察。TCP 与 stdio 是不同 transport，不能简单删除其中一个。

## S7：Voronoi 输入/seed 矩阵与 World3D 接纳应分层

- `test/integration/house-collapse/physics_voronoi_fracture.cpp:49`、:85、:107 仅 cook 合成 mesh，验证体积、闭合、seed、预算和坏输入；:207 连续 cook 93 个 seed，只要求结果成功。它们不创建 World3D 或真实资源发布者，应归本地算法/回归 corpus，不需要以每次集成门禁证明所有 seed。
- :207 名字是 `houseSeedsRemainClosed`，但正文没有逐 seed 检查闭合/体积；只检查 cook 是否成功。应明确 cook 的成功不变量，或将名字收窄，不能因为名字含 closed 就声称独立观察了所有几何性质。
- 保留 :123 的 cook→物理实例→源 world 销毁→磁盘保存/加载→采样链；:219 的 admission 失败不泄漏 body/shape；:316 的 world-first 销毁。成功 cook 无法替代它们。
- `physics_collapse_animation.cpp:71` 的 content-key 字节敏感性是本地 hash 输入矩阵；:10、:56 的已保存播放寿命与 stale recorder 则是真实资源所有权，不能一起归为“序列化重复”。

## S8：布料基准与长稳态回归不应混同

- `test/integration/layered-cloth/physics_layered_cloth_gpu.cpp:384` 创建真实 GPU fixture，跑 124 帧、写 CSV、采 120 个耗时并打印分位数，没有性能回归门槛。将 timing/CSV 部分放显式 benchmark；保留小型真实 resident-state、有限值、pin/reset 观察。
- :467 的 one-simulated-minute case 则实际做 1800 次 1/30 秒模拟更新，每 120 帧检查有限值、floor、几何层间距，最后检查稳定间隙及 reset。这里是 60 秒模拟时间，不是测得 60 秒墙钟。它是有独有失败模式的长期数值稳定性回归，不应当作“纯 benchmark”删掉；可与快集成 lane 分开安排，不应把 1800 步机械砍成几步并声称覆盖不变。
- 保留 :204 的 hash overflow→scan、:263 的生产 GLSL point/face CCD、:292 的 edge degeneracy、:311 的 hash/scan 对照、:217 的真实 compute→draw 输出。CPU 参考或 getter 不能替代生产 GPU 内核行为。

## 不能按“没有真实 GPU/大量 mock”直接删除的任务

- `card-rts-payment/card_rts_payment_card_tests.cpp` 的 11 个 case 跨真实 Card、Economy、AtomicBatch、RTS/Action 或 Squirrel，观察扣款、card 状态、释放、失败补偿与 stale owner；成功、准备失败、提交后补偿不是同一个契约。
- `scene-editing/scene_transaction_observer.cpp:8` 验证 observer 抛异常后已发布状态仍成功且诊断可见，不能用一般 commit-success 替代。
- `script-api/procgen_native_proxy_reuse.cpp:9` 用很小的 probe 类型，但跨 native registry/handle generation/真实 VM GC 观察旧 proxy 失效及只释放新 owner 一次；不是纯值检查。
- `profiler-capture/profile_capture_tests.cpp:26`、:65 各有一次 8ms sleep，但一个观察报告中的 captured span，另一个观察 frame-begin 顺序不丢失 earlier work。它们不同。名称聚合的本地逻辑（:49）可下沉；若要去掉真实时间等待，应先有可注入时钟或保留一个真实时钟代表，不能把时间约束直接删掉。

## 本组文件清点

下面由当前源文件的字面注册数辅助清点，不是运行数量、删除数量或逐 case 等价证明。所有建议保留物理合并编译结构；本轮没有新增测试 OBJ、Unity 规则、选源宏或策略清单。

- `test/integration/card-rts-payment/card_rts_payment_card_tests.cpp`：11 个字面注册。Card/Economy/RTS/Action/AtomicBatch 与 VM 的支付、状态发布、stale owner、stage 释放和失败补偿。
- `test/integration/concurrency/concurrency_graphics_tests.cpp`：113 个字面注册。真实 worker/网络消息、MCP stdio、动画并行加载、render job graph、pixelworld/voxel 调度；混有本地 codec、值和 benchmark。
- `test/integration/house-collapse/physics_collapse_animation.cpp`：3 个字面注册。从真实物理实例记录、world 销毁后播放和磁盘恢复、stale recorder；另有本地 content hash。
- `test/integration/house-collapse/physics_collapse_dust.cpp`：3 个字面注册。solver→gas 保存/恢复、fog footprint 注入、非阻塞 owned worker 以及源对象先销毁。
- `test/integration/house-collapse/physics_destruction_graphics_depth.cpp`：1 个字面注册。renderer 当前提交的 depth token、有界有效期和 renderer 销毁后的陈旧检测。
- `test/integration/house-collapse/physics_voronoi_fracture.cpp`：11 个字面注册。本地 cook/几何输入和 seed 矩阵；另有真实 world 接纳、磁盘持久化、泄漏和销毁顺序。
- `test/integration/layered-cloth/physics_layered_cloth_gpu.cpp`：14 个字面注册。生产 GPU kernel/CPU reference、compute→draw、CCD/overflow/接触守恒、pin/reset、长期稳定与 timing。
- `test/integration/layered-cloth/physics_layered_cloth_script.cpp`：1 个字面注册。通过真实 ScriptTest VM 调用 layered 脚本的 native cloth 绑定。
- `test/integration/module-composition/module_composition_devtools_frame_capture_tests.cpp`：1 个字面注册。frame-capture MCP 的 provider-absent 与 schema/路由。schema 数量本身不是行为覆盖。
- `test/integration/profiler-capture/profile_capture_tests.cpp`：3 个字面注册。真实 profiler span→text report/frame capture、frame-begin 顺序以及同名 zone 本地聚合。
- `test/integration/scene-editing/scene_transaction_observer.cpp`：1 个字面注册。observer 异常不撤销已经发布的 scene transaction，且保留可观察诊断。
- `test/integration/script-api/procgen_everyday_material_optics.cpp`：1 个字面注册。native PBR material 的 optical/map 属性经真实 Squirrel getter 投影，与 native 值对照。
- `test/integration/script-api/procgen_native_proxy_reuse.cpp`：1 个字面注册。native proxy 地址重用、handle generation、旧 proxy 失效与真实 VM GC 的一次性释放。
- `test/integration/script-api/script_api_graphics_dap_tests.cpp`：68 个字面注册。DAP、debug hook/stepping/snapshot/条件表达式；还混入 Decal、Dialogue、Economy、Voxel、filesystem/image/inventory/Joystick 的脚本 facade。
- `test/integration/script-api/script_api_graphics_mcp_tests.cpp`：87 个字面注册。MCP TCP 与真实编辑/观察、日志、hot reload；module lazy binding、脚本 provider、生产/PCG、RPG 和 reload-state policy。
- `test/integration/script-api/script_api_graphics_scene_tests.cpp`：150 个字面注册。Runtime 错误/反射、Rx、Scene link、ScriptCompiler/module resolver、MVVM、文档脚本、raw binding、tactics/sensing/motion sink。
- `test/integration/script-api/script_api_graphics_ScriptECS_tests.cpp`：108 个字面注册。Database/inspector 双向绑定、ScriptECS engine store/schema generation/reload/snapshot；另有 async、领域 facade、ScriptProvider 发布/撤销。
- `test/integration/script-api/script_api_graphics_terrain_probe_tests.cpp`：88 个字面注册。terrain settings/算法/Workspace/provider 经 VM 暴露、失败回执；另混入无 VM 的大尺寸 terrain 性能与 Timer/Transaction/UI/Vehicle/Weapon 绑定。
- `test/integration/script-api/script_api_procgen_cave_generation_stages_tests.cpp`：1 个字面注册。脚本驱动 cave skeleton→field→surface→mesh，验证 field 所有权、stale Params 和不支持的 evolution。
- `test/integration/script-api/terrain_road_batch_vm.cpp`：1 个字面注册。真实 VM 调用 batch road→terrain 的生产策略与原子失败。
- `test/integration/script-api/terrain_road_write_vm.cpp`：1 个字面注册。VM 生成 owned road mesh→terrain height/splat/hole 写入与 undo/redo、work budget、stale handle 和错误参数投影。
- `test/integration/script-api/terrain_world_origin_vm.cpp`：1 个字面注册。VM 建立带 originY 的 world，世界高度归一化、撤销与 tile-origin 读取。
