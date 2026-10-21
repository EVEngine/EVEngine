# Crowd 实现进度与验证记录

目标：[具体实现方案](2026-10-13-crowd-large-battle-implementation.md)。
此记录区分已实现、待验证和剩余工作；不是完成声明。

## 批 1：基准与报告（进行中）

已实现：StepReport 搜索/接触/网格工作量和五个阶段纳秒耗时；脚本 Result 同步投影。
新增 `test/crowd_budget.cpp` 验证计数重置、无操作、输入拒绝与状态保持、
邻居截断不限制搜索工作量、过滤后的接触统计。独立 profile 测试目标已登记。

新增核心移动基准 `test/profile/crowd_benchmark.cpp`：
确定性网格、同向行军、预测采样、120 帧预热、默认 3600 帧测量，
输出配置、逐帧 CSV 和 p50/p95/p99。数量/帧数作为命令行参数，最大 100k/36000。
构建目标 `eve_crowd_benchmark`，不登记为时间敏感的 CTest。

后续补齐：多场景、密度统计、内存/分配统计；随 RTS 和表现批次增加完整战斗与渲染测量路径。
当前 runner 不能代表完整战斗容量，也没有执行优化前后性能比较。

已验证：测试 manifest、架构契约、严格 binding 覆盖、修改行 clang-format 18。
Windows MSVC 严格 Debug OBJECT 配置：eve_crowd_check 与 eve_crowd_benchmark 编译链接成功；
独立 Crowd CTest 51/51 通过（含新增 3 项），没有运行图形或全仓测试。
批 1 完整 make check 通过（含 258 项脚本测试）；后续修改需重新运行门禁。
核心移动基线：1000 代理，Debug OBJECT，固定 1/60 秒、120 帧预热后 3600 帧，
p50=81.3241 ms、p95=88.8779 ms、p99=92.8797 ms。原始数据在本工作树
`build/crowd-core-1000.csv`。默认 sepRadius=28/perceptionRadius=64，半径=0.5、间距=1.5；
这是未优化串行核心行军数据，不代表发布构建或完整战斗性能。

## 批 2：句柄、批处理与 RTS 消费者（进行中）

已接入间接 registry：RuntimeHandle<CrowdAgentTag> + owner 身份，swap-pop 更新间接索引，
删除先推进代次，代次耗尽退休，clear 不重置代次。追加前预留全部 SOA 与 registry 空间。
出生事务复制后保留原 owner，回执返回新 AgentRef；失败不发布暂存句柄。

新增 AgentUpdateBatch/AgentRemovalBatch、强类型 SteeringIntent、拥有型 AgentFrame，
原子验证重复/失效/错 owner/非法数值；支持全体与按请求顺序的选定快照。
createAgents 复用原 SpawnBatch 放置内核，旧 applySpawnBatch 为兼容 facade。
数值句柄与批次脚本绑定均使用标准 Result 投影，不把 packed uint64 转成浮点。

RTS Motion bridge 已迁移缓存句柄和批量输入/输出；正常帧保持 Crowd 的位置权威。
链接变更和容纳后移除旧投影，生产出生按 receipt 绑定并批量发布 displaced peers。
初始化历史 ECS 几何的首次绑定仍保留已有直接放置契约；生产出生走安全原子批次。
生产投影输出内存与绑定字符串在放置提交前准备，避免提交后新增分配失败边界。

验证：registry/batch/script/relocation 契约加入独立测试，最新 Crowd 60/60 通过。
新增独立 `test/rts_crowd_registry.cpp` 四项组合/脚本测试；迁移生产堵塞测试到显式放置。
完整 OBJECT 组合套件 128/128 通过（60 Crowd + 68 RTS/Crowd），包含编队、生产、恢复与支付重试。
最新构建在 `build/crowd-checked`，日志 `build/crowd-final-composition-tests.log`。
新增精确重新放置批次，拒绝身体/墙体/预算失败；保留身份与订单、清零速度并使派生路线失效。
CORE 与 RTS 脚本绑定均覆盖此操作。创建、更新、删除与重新放置分别提交，不合并为隐含交易。
新增明确的 crowd-core-only profile，选择 common/crowd 两个模块、仅 Squirrel 第三方组。
该 profile 的 Windows host-off SHARED 编译链接成功，60/60 实际 CTest 通过；
dumpbin 检查 EVFoundation.dll 仅导入 Windows/C++ 运行库，未导入图形、窗口或 RTS 引擎组。
eve_profile_smoke 构建成功，独立能力 present/absent 两个可执行探针均退出 0。
日志分别为 build/crowd-shared-tests.log、crowd-shared-imports.log、crowd-shared-profile-smoke.log。
上一轮完整 make check 已通过（258 项脚本测试）；当前时钟/策略改动需重新过门禁。可复用 RTS 工作区、
只发送变化字段与完整 named RNG streams 尚待验证或后续补齐。
已加入具名 ID 字节哈希和匿名创建序号组成的数值 tieKey；碰撞在创建准入明确拒绝，
匿名序号耗尽拒绝追加而不回绕，swap-pop/clear/快照遵循身份与缓存维护规则。
预测排序及完全重叠接触方向不再比较、拼接或分配字符串；wander 初相位由身份派生。
SHARED 核心 75/75、OBJECT/RTS 组合 144/144 通过；
日志 build/crowd-tie-final-tests.log、build/crowd-tie-composition-tests.log。
覆盖匿名 compact 后的接触方向、具名创建顺序独立性及恢复后的动态运动重放。
稳定键批的完整 make check 已通过，包含 258 项脚本测试，日志 build/crowd-tie-make-check.log。
provider 使用不可保活的 LifetimeWitness；不存在 capability 注册或每帧字符串查询。
RTS 推进/生产放置/重定位在解引用前检查，失效清除派生引用并明确失败；
解除绑定及销毁跳过失效 provider，不自动选择原生运动。witness 仅授权同一 owner thread 使用。
双方销毁顺序、恢复/clear 不终止 provider 身份、失效后的重定位拒绝均通过；
OBJECT 组合 146/146、host-off SHARED 核心 76/76，日志 build/crowd-lifetime-tests.log、
build/crowd-lifetime-shared-tests.log。本批生命周期增量尚待完整源码门禁。
已有 OwnedProviderLease 会保活 provider，适用于 owning capability 注册；此处支持游戏直接拥有的
stack/member Crowd，所以采用非保活直接绑定 witness，未引入第二套 capability 注册表。

## 后续批次

批 2 正在实施；批 3 已完成部分核心契约；批 4–9 尚未实现。目标保持完整实现范围，不以局部测试代替整体完成。

## 批 3：策略、时钟和预算（进行中）

InteractionPolicy 是唯一交互数据权威，预测/Boids 与身体接触独立筛选；出生、重定位使用接触规则。
旧 setter 映射两套筛选，旧 getter 返回接触字段，hold 更新保留独立筛选。
显式 CrowdStep 消费连续 tick 和整数纳秒时长，拒绝两种时钟混用；空世界也消费时间。
所有运动列与 wander 使用可复用回滚缓冲；预算失败及非有限输出不发布运动或时钟。
网格容量、邻居扫描、预测检查、接触与残留检查使用确定性操作预算；接触迭代数受限，残留显式报告。
脚本 advanceTick 严格整数输入、tick 字符串输出，帧快照附带同一 tick。
host-off SHARED 核心 70/70 通过，包含积分后残留预算失败的回滚，日志 build/crowd-clock-shared-final-tests.log。
补充非有限 steering 参数和查询几何的拒绝，避免非法浮点转换进入空间索引；核心 71/71 通过，
日志 build/crowd-finite-tests.log。此补充后的 OBJECT 组合和源码门禁尚待重新验证。
OBJECT/RTS 组合 138/138 通过（70 Crowd + 68 RTS），日志 build/crowd-clock-object-final-tests.log。
上一轮 make check 完成通过（258 项脚本测试）；后续非有限检查与 RTS 时钟迁移不在该证据范围内。
RTS Crowd bridge 已传递原始 tick/Duration，独立与整体 RTS 入口在输入变更前使用同一准入检查。
迁移连续正时长 tick 与当前 tick 的零时长同步；空关联集合也推进 Crowd 时钟。
迁移后的首次组合 134/140 通过，暴露空关联早退、旧零时长测试和 checkpoint 未恢复 Crowd 时钟。
前两项已修复；checkpoint 已接入 Crowd 运行态联合恢复。恢复后旧 AgentRef 失效并按名字重绑定。
零时长同步不重复消费 Effects 调度 tick；最新组合 142/142 全部通过，
日志 build/crowd-snapshot-final-tests.log（73 Crowd + 69 RTS）。源码门禁重新运行中。
新增错误 tick 在绑定更新前拒绝的测试；并行工作态和完整失败注入尚未完成。

## 批 4：运行态恢复（部分实施）

新增原生拥有型、不可变进程内 Snapshot；prepareRestore 先分配完整状态和新 owner，
commitRestore 无分配发布，拒绝空、已消费、其他目标及上一轮恢复的计划。
所有代理、策略、场与时钟共同恢复，旧数值引用失效，消费者按稳定名字重建绑定。
SHARED 核心 73/73 通过，日志 build/crowd-snapshot-tests.log，覆盖准备不发布、
错误计划不修改世界、恢复后继续同一时钟、重复恢复和旧引用失效。
此接口是进程内 checkpoint；版本化持久格式、迁移、导航 revision 和完整跨域失败原子性仍未完成。
导航 revision 已开始实施：地形/目标变化使旧方向场 Stale，默认停止采样；
最新地形继续用于墙体投影。重复相同单格写入不增加版本，版本耗尽拒绝回绕。
构建在副本上完成后发布，不把部分积分场标为 Ready。SHARED 核心 78/78 通过，
初始日志 build/crowd-revision-tests.log；状态观测补齐后 SHARED 核心 79/79、
OBJECT/RTS 组合 149/149 全部通过，日志 build/crowd-nav-observation-final-tests.log、
build/crowd-nav-observation-composition-tests.log。
getFieldStatus 原生/脚本读取 readiness、revision、builtRevision、唯一目标数；脚本 revision 为无损字符串。
StepReport 正时长、空世界与零时长均携带导航观测。重复目标不失效，坐标对分配后一次发布。
当前 revision 是 owner-local terrain+goals 合并版本；运动类别、表面身份、请求 token、
取消/增量构建及多场缓存尚未实施，不能宣称导航阶段完成。
生命周期批的完整 make check 已通过（258 项脚本测试），日志 build/crowd-lifetime-make-check.log；
导航 revision/观测增量需要重新运行门禁。
导航 resize 先准备全部缓冲，再发布尺寸/原点/版本；非有限几何和索引范围溢出在发布前拒绝。
相同配置为 no-op，不使已建场失效。SHARED 核心 80/80 通过，日志 build/crowd-resize-tests.log；
本增量的 OBJECT/RTS 组合仍待验证。既有 void 配置入口保留异常报告；正式请求 builder 尚未实现。
resize 的 OBJECT/RTS 组合 150/150 通过，日志 build/crowd-resize-composition-tests.log。
导航 revision/观测批的 make check 已通过（258 项脚本测试），日志 build/crowd-navigation-make-check.log。
增加 move-only FlowBuildPlan 和 prepareFlowBuild/commitFlowBuild：准备不发布，提交验证 owner/来源版本，
cancel 显式释放工作态；旧单目标入口改为兼容 facade，委托同一构建内核。
SHARED 核心 82/82、OBJECT/RTS 组合 152/152 通过，日志 build/crowd-flow-plan-tests.log、
build/crowd-flow-plan-composition-tests.log；此请求基础增量尚待完整源码门禁。
该计划目前同步计算完毕后返回 Ready；Pending/按工作预算推进/Unreachable/注册表/脚本 token 尚未实现。
方向生成已拆为单格内核，后续可将完整 gradient 阶段纳入确定性格数预算；
当前同步路径仍遍历全部格，未宣称分步请求已完成。大有限积分成本改用 hypot 归一化，
避免平方溢出导致方向丢失。SHARED 核心 83/83 通过，日志 build/crowd-gradient-tests.log；
本增量的 OBJECT 组合尚待验证。

分步构建已接入：prepare 返回 Pending，advanceFlowBuild 统一计入堆弹出与方向格预算；
完成前拒绝发布，来源 revision 变化在工作前取消；失败/取消/不可达/完成状态明确区分。
同步入口驱动同一工作态至完成，不保留第二个全量方向循环。
SHARED 核心 85/85、OBJECT/RTS 组合 155/155 通过，日志 build/crowd-incremental-final-tests.log、
build/crowd-incremental-composition-tests.log。覆盖预算切分一致、方向阶段计费、零预算、过期取消及 Unreachable。
prepare/commit 基础批 make check 已通过；本分步增量尚待重新过门禁。
请求注册表、脚本 token、表面/运动类别身份、持久化迁移与多场缓存仍未完成。
脚本请求注册表和 token 已接入；注册表是 Crowd 外层瞬态工作态，checkpoint 不复制，恢复清除。
schema 1 token 必须含 kind 与精确整数 owner/index/generation；未知版本拒绝，额外字段忽略。
保留最多 64 项记录；release 推进代次并回收槽，代次耗尽退休。响应在记录发布前准备。
query/advance/commit/cancel/release 均使用标准 Result 投影与同一 native plan 内核。
SHARED 核心 88/88、OBJECT/RTS 组合 158/158 通过，日志 build/crowd-request-final-tests.log、
build/crowd-request-composition-tests.log；新增测试覆盖类型/owner/代次、工作预算、容量回收与恢复失效。
严格绑定检查通过，日志 build/crowd-request-bindings-check.log。分步构建批 make check 已通过；
注册表增量需要重新过门禁。表面/运动类别身份、持久化迁移与多场缓存仍未完成。

原生 NavigationSurfaceSnapshot 已接入强类型 surface/movement ID、三类 revision 与 clearanceRadius。
prepare/commit 验证并复制拥有型数据；物理成本与预膨胀导航成本分离，导航不能解除物理墙体。
同身份/版本矛盾数据拒绝，版本回退拒绝，同样投影 no-op，旧发布计划不覆盖新状态。
超尺寸 Flow 代理停止导航驱动并报告 navigationClassMismatches；手动地形修改清除导入来源，恢复重新建立来源。
SHARED 核心 90/90、OBJECT/RTS 组合 160/160 通过，日志 build/crowd-surface-final-tests.log、
build/crowd-surface-composition-tests.log。请求注册表批完整 make check 已通过（258 项脚本测试）。
此表面增量尚待重新过门禁；真实 map adapter、每代理类别绑定、多场缓存与持久化仍未完成。

地图捕获与上层适配已接入：Pathfinder::captureNavigationGrid 产出不可变拥有型输入，
在暂存 Grid 上同步，内容相同复用版本，成本/方向掩码/hex 布局共同决定捕获版本。
RTS 的 projectCrowdSurface 保留四/八邻域及目标进入成本，按确定性候选检查预算生成圆足迹膨胀，
物理成本不膨胀；hex、方向门控和正成本小于 1 明确拒绝。map 不依赖 Crowd。
OBJECT 组合 164/164、无 map 的 SHARED 核心 90/90 通过，日志 build/crowd-map-cost-model-tests.log、
build/crowd-adjacency-shared-tests.log。表面边界批 make check 已通过（258 项脚本测试）。
本适配增量尚待门禁；适配器未自动挂接实时 RTS 更新，每代理类别绑定、多场缓存及持久化仍未完成。

## 后续两项目标：运行时表面跟踪与代理运动类别

RTS 已新增 configureCrowdSurfaceTracking/clearCrowdSurfaceTracking，原生与脚本均可显式启用。
配置只保存拥有型值；step 在任何 ECS 系统前检查地图 O(1) NavigationChangeStamp、provider 重绑、
世界几何与导入来源。未变化时不捕获/复制网格；同值编辑捕获后按内容版本跳过投影。
变更路径通过已有 owning snapshot + 有界 clearance adapter + prepare/commit 发布；非法地图、
不支持的 topology/gates/cost、预算耗尽或缺失 provider 拒绝步骤并保留 Crowd 表面/时钟。
同几何地图编辑使既有目标 stale；流场重建仍通过明确导航请求，不在普通 step 中隐式全量建场。

Crowd 的 SpawnRequest/AgentUpdate/AgentState/AgentFrame 新增 MovementBinding，SOA 列随
reserve、swap-pop、clear 与进程内快照一起维护。显式身份/版本必须同时为正；零/零为兼容选择。
Flow 匹配 class ID、class revision 和 clearance；不匹配暂停 Flow 方向并报告
navigationClassMismatches，零时长也返回当前诊断。局部接触/墙约束继续执行；Seek/Boids
不使用 Flow 匹配。脚本拒绝浮点身份、不完整表及半个零绑定；输出十进制字符串保持 uint64 精度。
RTS 常规批量同步不覆盖原生代理的显式类别。

SHARED core-only 最新 93/93 已通过，日志 build/crowd-tracking-shared-tests.log。
OBJECT 首次组合 169/170；新增零时长断言发现 advanceInternal 的早退遗漏诊断，已补齐统一计数。
最终 OBJECT 组合 171/171 已通过，包含 RTS 脚本跟踪端到端，日志 build/crowd-tracking-tests.log。
构建日志 build/crowd-tracking-build.log；核心 SHARED 构建日志 build/crowd-tracking-shared-build.log。
源码 make check 已通过，258 项脚本测试，日志 build/crowd-tracking-make-check.log。
最终增量另核验架构契约（88 项目录、无新违规）与严格 crowd/rts 绑定检查（217 bindings）；
日志 build/crowd-tracking-final-architecture.log、build/crowd-tracking-final-bindings.log。
本轮没有声明多场缓存、RTS A* 运动类别投影、自动建场、持久化 schema 或完整大规模战斗完成。
Pathfinder/TileLayer 维持明确的借用存活前置条件；必须先解除挂接再销毁或移动 provider。
Crowd 销毁继续由 witness 识别；跟踪缓存不拥有 provider，不持有数组跨度。

源码门禁基于当前分支与已前进的 origin/dev 的共同祖先
`a73c6ba8ea99e70b7d8236db1783ea54acb39b65`。直接对最新 dev 比较会把尚未合入本分支的
IStateProvider Result 迁移倒差计作新增 bool API；没有修改 baseline/allowlist 或无关接口。

## 多场缓存前置：规范化目标集

原生 prepareFlowBuild(span<FlowGoal>) 已加入 1..1024 个目标的验证、排序去重与拥有型复制，
单目标入口委托同一内核。完成状态检查全部目标：阻挡目标与可走目标混合不会因排序后的
第一个目标被阻挡而错误报告 Unreachable。规范化目标相同时不重复改变工作态的目标 revision。
新增用例对比反序、重复目标及不同 work 分片，验证最近目标方向、非法输入原子拒绝与全阻挡。
OBJECT 组合 172/172 与 SHARED core-only 94/94 已通过，日志
build/crowd-multigoal-tests.log、build/crowd-multigoal-shared-tests.log。
构建日志 build/crowd-multigoal-final-build.log、build/crowd-multigoal-shared-build.log；完整源码门禁已通过，
258 项脚本测试，日志 build/crowd-multigoal-make-check.log。
这仅落实文档 §8 缓存键的目标集维度；FlowFieldKey、共享缓存、lease、字节预算淘汰、
按代理选场、消费者迁移与持久化重建仍未实现，不能将这一前置工作记为完成多场缓存。
下一步先拆分导航输入版本与场发布版本：当前计划绑定 CrowdField::revision，目标变更与地形
变更共用该计数；直接复用它会使一个目标的提交取消其他仍有有效输入的请求。

## 多场缓存前置：导航输入与场发布版本拆分

CrowdField 新增 inputRevision。几何、物理代价、导入导航成本/规则变化推进输入版本；
目标集编辑仅使默认场 revision 变化。FlowBuildPlan 绑定 owner + inputRevision，其他目标
请求的提交不再把它取消。commitFlowBuild 在原子发布前验证输入并分配新的单调发布版本，
不会用较老的准备态把 revision 倒退。FieldObservation 与脚本 getFieldStatus 提供输入版本。
原生与脚本新增共存目标请求回归；真实代价变化仍取消旧请求，同值编辑维持输入版本。
setAllCellCost 顺带改为先分配成本数组再改输入 epoch，避免分配失败后只发布半个版本更新。
OBJECT 组合 174/174、SHARED core-only 96/96 已通过；日志 build/crowd-input-epoch-tests.log、
build/crowd-input-epoch-shared-tests.log。构建日志 build/crowd-input-epoch-build.log、
build/crowd-input-epoch-shared-build.log；本轮源码门禁 build/crowd-input-epoch-make-check.log 已通过（258 项脚本测试）。
共享缓存、带代次 lease 和按代理选场并未由这一版本拆分自动实现。

## 多目标共享缓存与原生 lease

CrowdFlowCache.cpp 接入 ready 字段复用、完整 key 比较、64 项上限、字节预算和确定性未租用淘汰。
key 包括输入 epoch、surface/class/cost 来源、规范化目标和显式算法版本。FlowLease 不保持 provider
存活；实际 pin 独立于 cache/checkpoint 的数据保留。缓存发布先验证压力并预分配元数据，然后
no-throw 移交字段；失败保留计划/缓存。acquire 不覆盖默认场。AgentUpdate::flow、SOA 列和
交换删除/清空路径已接入；Flow 热路径按代理选 immutable field，不查字符串 key。
快照保存缓存/代理绑定；恢复重建 owner，外部旧 lease 失效。sample 给出明确 reachability。

首轮 OBJECT 178/178 通过，日志 build/crowd-flow-cache-tests.log。补充无 lease checkpoint 记录
不 pin 当前淘汰的回归后 OBJECT 179/179、SHARED core-only 101/101 通过，日志
build/crowd-flow-cache-final-tests.log、build/crowd-flow-cache-shared-tests.log。
源码门禁发现 bool comparator 命名与内部借用指针生命周期文档缺项，已修正；
build/crowd-flow-cache-architecture.log 检查通过；完整源码门禁随后通过（258 项脚本测试）。
复核发现插值成本可能让阻挡格内部分位置误判 Reachable，已改为确切所在格的可达性
判据，并由 sample 与 Flow 驱动共享；新增阻挡格边缘与可走邻格回归。
最终修正 OBJECT 180/180、SHARED core-only 102/102 通过；日志
build/crowd-flow-cache-reachability-tests.log、build/crowd-flow-cache-shared-reachability-tests.log，
对应构建日志 build/crowd-flow-cache-reachability-build.log、
build/crowd-flow-cache-shared-reachability-build.log。最终增量架构检查
build/crowd-flow-cache-final-architecture.log 通过；完整缓存主批 make check 通过。
修正后的对象构建/测试 build/crowd-flow-cache-contract-build.log、
build/crowd-flow-cache-contract-tests.log（179/179）；SHARED
build/crowd-flow-cache-shared-contract-build.log、build/crowd-flow-cache-shared-contract-tests.log（101/101）。原生 cache 不是整个文档 §8 的完成声明：目前仍服务
当前运动类别投影，多类别投影共存、脚本 lease/token 接入、pending 去重/调度、RTS 多订单
共享场迁移、持久化逻辑请求恢复与整体战斗验证仍未完成。

## 脚本请求持有 lease 的第一条组合路径

既有 NavigationRequests::Entry 新增可选原生 lease。acquireFlowRequestLease 只消费完成的
计划并将 pin 留在已知请求记录，sampleFlowRequestLease 复用原生采样；release/restore 清空
记录时释放 pin。没有另建 registry 或第二个 token owner。新增 VM 回归覆盖完成前/重复 acquire、
reachable/unreachable、外来 owner、地形失效和释放后的 stale token。
OBJECT 组合 181/181、SHARED core-only 103/103 通过，日志
build/crowd-request-lease-final-tests.log、build/crowd-request-lease-shared-tests.log；
构建日志 build/crowd-request-lease-rts-build.log、build/crowd-request-lease-shared-build.log。
源码门禁发现新增字段 changed-lines 对齐问题，已格式化修复，
build/crowd-request-lease-make-check.log 已通过（258 项脚本测试）。
本增量不是独立脚本 lease 协议与代理绑定的完成声明；RTS 订单消费者也尚未迁移。

## 脚本代理选场接入

updateAgents 接入 flowRequest 与 useDefaultFlow，复制请求持有的原生 lease 到代理，整个批次
仍由原生 updateAgents 先验证后发布。释放请求不解除代理 pin；显式默认场选择才解除该绑定。
请求 token 校验统一到 NavigationRequests::resolve，使用 sq_rawget 读取字段，避免 `_get`
回调在解析期间重入/销毁注册表；新增 VM 回归验证 token delegate 不被调用、坏绑定批次不
发布 data、请求释放后代理继续导航、旧 token 不可重新绑定及显式解除。
首轮用例语义通过但预期坏输入触发 Result.expect 的 FATAL 日志，现已改为显式 Result
检查并返回参数失败。最终 OBJECT 182/182、SHARED core-only 104/104 通过；日志
build/crowd-script-lease-binding-final-tests.log、build/crowd-script-lease-binding-shared-final-tests.log。
对应构建日志 build/crowd-script-lease-binding-final-build.log、
build/crowd-script-lease-binding-shared-final-build.log；源码门禁
build/crowd-script-lease-binding-make-check.log 已通过（258 项脚本测试）。
RTS 订单共享场消费者、多目标脚本请求及独立 lease 身份仍未完成。

## RTS 共享静态目标消费者接线

导航阶段与共用威胁查询从超大 RTSSystems.cpp 移到 RTSNavigation.cpp；威胁计算仍为唯一内核。
显式 configureCrowdFlowNavigation 接入原生/脚本，对 linked、无 formation/recovery slot 的
Move/AttackMove 跳过逐单位 A*。Motion 收集目标后一次消费 start/work 配额，最后统一分发
当前输入的 lease。owner/input 变更重建派生作业，零时长同步不重复消费图工作；pending/
unreachable 不等于 arrived，订单保留。目标格内转局部 Seek，避免只停在格中心不能到达精确点。
已接入提交后不可达事件：notice 持有订单值与代次 handle，在未知回调前转移至独立拥有型
容器；失败阶段不通知，同订单不重复，解阻后清除诊断并重建请求。缺失 imported projection
在任何 RTS 系统前拒绝，修正配置后同 tick 可重试。
最终 OBJECT 186/186、SHARED core-only 104/104 通过；日志
build/rts-shared-flow-contract-tests.log、build/rts-shared-flow-core-shared-tests.log，构建日志
build/rts-shared-flow-contract-build.log、build/rts-shared-flow-core-shared-build.log。
本轮 source gate build/rts-shared-flow-make-check.log 已通过（258 项脚本测试）；架构目录 90 项与 changed-lines/
layering 检查通过。形成完整消费者迁移仍需编队槽位、动态目标与恢复/热重载等证据。
本增量不声称跨域整个 step 原子；输入重建解除派生 lease 后的后续准备可能失败。

## RTS 共享场生命周期组合验证

补充 checkpoint 恢复后 owner 更新/缓存重绑定、不可达回调销毁全部接收单位后的句柄重查、
缓存预算压力解除后复用已经完成的计划（不再次建场），以及 provider 销毁后从已有快照
重建并保留同一时间线的热重绑定。输入缺席时拒绝推进，无隐式运动回退。
这些是现有消费路径的生命周期取证，不代表持久化格式或所有动态/编队订单已经迁移。
最终 OBJECT 组合 190/190 通过，日志 build/rts-shared-flow-lifecycle-final-tests.log，
构建日志 build/rts-shared-flow-lifecycle-final-build.log。四条新增用例覆盖 checkpoint、回调销毁、
缓存压力与 provider 快照重建；核心 104 项回归同批仍通过。上一轮完整源码门禁已确认通过，
258 项脚本测试，日志 build/rts-shared-flow-make-check.log。无新增生产 API/持久化格式或架构例外。
这些证据证明当前静态无编队订单策略的恢复路径，不证明编队/动态目标迁移或完整大规模战斗完成。

## 动态目标共享请求迁移

共享资格新增 Attack/Escort/Patrol；movementGoal 为原生导航、Crowd 意图、共享请求与到达判断
唯一解析入口，读取当前目标/guard 点/巡逻端点并返回拥有型数值。相同目标所在格共用场，
跨格才新增请求；编队与 traffic recovery 保持显式原生路径。SupplyMission 的威胁代价覆盖
不能由当前基础 projection 代表，仍保留已有机制，未声称已迁移。
新增双攻击者回归对比格内移动不建新场、跨格准备一次新场及两单位 generation 一致。
最终 OBJECT 组合 192/192 通过，日志 build/rts-dynamic-flow-final-tests.log，构建日志
build/rts-dynamic-flow-final-build.log。新增用例验证 live Attack 目标格内/跨格以及 Patrol/Escort/
SupplyRelay 解析与显式原生路径策略。源码门禁 build/rts-dynamic-flow-make-check.log 已通过（258 项脚本测试）。
后续继续迁移编队路径并扩展动态目标事件与失效验证；未声明全范围迁移完成。

## 本次构建准备

初始化既有四个 Git 子模块；只读复用既有 win32-debug 第三方安装。
clang-format 18.1.8 安装在本工作树 build/check-tools，不更改系统配置。
首次沙箱内编译器检测中断；新的 build/crowd-checked 完成 C/C++ feature 检测，
禁用 compiler cache 以隔离环境问题。标准 MSVC wrapper 保持使用。
host-off + SHARED 的既有 profile 路径未接上 EVBacktrace/链接组，未宣称此组合通过；
本轮使用 host 配置开启、仅构建独立测试的 OBJECT 组合路线；SHARED 核心路线已通过。
现已将 EVBacktrace 的唯一声明提取到 cmake/host_backtrace.cmake，profile 也消费此声明；
SHARED Crowd 测试使用 manifest 派生的 owning group，程序输出在 DLL 同目录。
新增显式 test-source configure dependency，避免新用例仅编译而没有被 CTest 注册。
实际裁剪验证改用 crowd-core-only：minimal+host-off 仍选择 UI，却没有 Vulkan loader；
既有 headless 的显式 seed 不允许 Crowd。没有放宽 hostless 选择门禁，新增独立明确 profile。
修复链接组在无 host 时引用不存在的 EVScripts；host 配置继续链接原资源库。
独立能力探针自己编译 registry 源码，因此 SHARED 时明确声明其定义所有权，避免 dllimport 定义错误。
OBJECT 的 RTS 组合显式启用 font：原 minimal 图形文字路径的缺席行为未通过链接，
因此本轮不宣称 font-absent 可用，也未修改 graphics 的可选依赖契约。

## 本轮架构规则取证

R-BOUND-1/2：以 core-only 配置、DLL 实际 imports 和运行测试分别记录 L1/L2/L3；
不以 include 图替代链接/运行证据，也不声称完整 L4 战斗表现已完成。
R-FACE：Crowd/RTS 的 script 和 Link 变更登记在各自 shard，组合快照由脚本生成。
R-MECH-1/4：绑定时使用逻辑身份，常规帧使用已知 provider 和数值 generation 引用；
无新增每帧 capability 查询或 getModInst。RTS public header 用前置 Tag 与 common handle，
没有新加向 Crowd 的公共 include。
R-COST-1/2/4：批量复制/放置显式 builder 与成本说明；廉价身份查询保持直接；
没有默认参数区分成本档位。Result、原子失败和视图后结构变化以独立测试取证。
没有批准或采用新的架构例外；legacy 直接放置只保留已公开的兼容初始化语义。
configure 自动应用仓库既有 ECS default-table patch，子模块内容因此显示 dirty；
不是本次新增子模块修改，不提交该标记。

## 编队目标集基础能力与接入边界

RTS 内部共享场服务支持 1..1024 个拥有型目标值，按格坐标排序去重，使用完整规范化
目标集作为请求键。不同排列与重复槽位复用同一计划和 lease；单目标走同一内核。
steer 保留单位精确终点，且在修改 AgentUpdate 前验证终点格属于目标集。
无效集合不得新增任务，无效精确终点不得覆盖已有更新。
OBJECT 组合最终 193/193 通过，日志 build/rts-slot-goals-final-tests.log；
源码门禁 build/rts-slot-goals-make-check.log 已通过（258 项脚本测试，格式、分层与架构契约通过）。

此节记录基础能力增量时尚未改变 movement-group 消费路径，消费者接入见下节。多源场表示到任一槽位
的最短路，不保证到指定槽位的路径；抵达其他槽位后直接 Seek 可能穿越阻挡或停在零梯度格。
正式接入必须先确定槽位重分配或受限末段导航机制，覆盖错误槽位、断连目标集、窄通道、
成员销毁与订单替换。禁止用 centroid 代替有效槽位集，也不能仅凭相同 generation 宣称
编队完成。每单位重复目标集排序还需改为组级准备、数值句柄消费，并计入确定性工作预算。

## 受约束编队共享导航消费者与指定槽位末段

MovementGroup 保留唯一成员/订单权威；组阶段发布拥有型派生成员投影，按完整 ECS handle、
当前订单、Crowd owner/input epoch 查询。一次目标集排序用于整组，复用相等的不可变键；
单位请求和 steering 不再重复转换/排序整个组。输入重复成员时完整拒绝并保留旧投影。
Route 是拥有型、不可伪造的内部工作值，持有不可变目标集与输入 epoch；不保存 ECS/group 地址。
恢复改变 owner 后旧 Route 在修改 AgentUpdate 前拒绝。当前 View 验证单位，订单绑定排除撤单/
替换，generation 排除 entity slot 重用。没有新的持久化 group identity 或公共 ECS 模型。

受约束 Move 编队共享多目标粗路；进入任一槽位格后单调进入指定槽位单目标末段，不无条件
直线 Seek，也不离开错误槽位格后退回共享场。原始精确槽位及订单不改变，Pending/Unreachable
仍由统一运动内核处理，末段请求沿同一 starts/work/cache 配额。开放路径继续组级半径扫掠
验证后的移动槽位；约束路径不锁定全组速度。规划资格不读取上一帧 formationTarget，因此
模式开启后的组路径不会被旧派生投影触发各单位 A*。

最终 OBJECT 组合 198/198 通过，日志 build/rts-formation-flow-final-tests.log，构建日志
build/rts-formation-flow-final-build.log；新增五条独立用例在 test/rts_crowd_flow_groups.cpp。
它们覆盖共享绕障后到不同槽位、错误槽位末段持续复用、断连指定终点停止、重复成员原子拒绝、
旧订单/entity generation/恢复 epoch 失效。原有 193 项也同批回归通过。
SHARED 组合也为 198/198，通过日志 build/rts-formation-flow-shared-final-tests.log；
构建日志 build/rts-formation-flow-shared-build.log 与 rts-formation-flow-shared-core-build.log。
OBJECT/SHARED 最终用例均将开口收窄为单格通道，并断言相同 entity slot 的 generation 改变。
SHARED 配置显式启用 devtools：host CLI 引用该 provider，裸 minimal+c/rts/font 首次链接失败，
不宣称该缺席组合可用。组合测试 SHARED 从 Foundation 导入唯一 cmd registry，OBJECT 才单独
编译 registry 源码；消除了测试目标重复定义 dllimport 的构建错误。
本轮完整源码门禁 build/rts-formation-flow-make-check.log 已通过（258 项脚本测试；格式、分层和 91 项架构 catalogue 检查通过）。
架构 catalogue 91 项；GroupBinding 的 ownership/destruction/restore/stale 与消费者阶段已经登记。
当前服务最多 64 个活跃目标键，组最多 64、每组成员/目标最多 1024，容量超出显式拒绝；
没有容量公平队列、全准备成本配额、完整 Compress/Traverse/Reform 阶段机或精确持久化恢复。
这些仍属于实现文档的未完成要求，不以当前消费者测试缩减完整目标。

## 共享导航准入与图工作公平性

目标键排序仅服务查找。每个新目标键分配不复用的 uint64 准入序，配额内按最旧请求优先启动；
图工作使用最后完成 quantum 的准入序轮转，并冻结每轮准入上限；新到达请求必须等下一轮，
避免不断扩张的轮转尾部再次饿死旧请求，不依赖会被插入/删除移动的 vector 下标。
Owner/input 改变重置工作轮转边界，序号耗尽显式拒绝，不回绕。排序用 pump 内固定 64 槽临时
数组，借用 Job 指针不跨 mutation、帧或回调保存；没有新公共数据或持久化格式。
新增两条回归：新低键不能插队旧等待请求；每 tick 一个工作单位且低键不断变化时，
两个长期请求在 160 tick 内完成。保持启动/工作上限以及零 dt 不额外推进。
本增量仍保留最多 64 活跃目标键；不代表容量外排队、缓存发布公平性或全准备成本预算或失败重试配额已完成。
最终 OBJECT/SHARED 各 200/200 通过，日志 build/rts-flow-fairness-tests.log 与
build/rts-flow-fairness-shared-tests.log。完整 make check 已通过（258 项脚本测试），日志
build/rts-flow-fairness-make-check.log；最终代码独立契约复查日志 rts-flow-fairness-final-contracts.log 通过。
没有引入架构例外；仍未完成实现文档的全部范围。

## 失败 pump 的同 tick 配额封闭

pump 在开始前占用 owner/tick 尝试，失败也不退还该 tick 未用配额；同 owner 的输入 revision
修复会清除旧作业，但不重置尝试边界。新 restore owner 属于新时间线，允许重新准入。
准备前计入 starts；图工作先预留 quantum，成功按实际工作回减，失败保守保留上界并取消
部分内部计划，不允许其后继续发布。已完成计划遇到普通缓存压力仍保持原重试语义。
原生输入故障注入在一个目标已准备后缩小表面，触发下一目标失败；验证重复 pump 与同 tick
修复均不重新准备，下一 tick 正常恢复。没有测试专用生产钩子或新公共 API。
OBJECT/SHARED 最终均 201/201，通过日志 build/rts-flow-retry-tests.log 与 build/rts-flow-retry-shared-tests.log。
用例同时验证 restore 生成新 owner 后同 tick 可重新尝试，故障类别为 Rejected。完整源码门禁 build/rts-flow-retry-make-check.log 已通过（258 项脚本测试）。
本增量不证明所有分配故障路径，不是整个 RTS step 的跨域原子事务，也未实现全 CPU 准备成本预算。
配额封闭目前属于单个服务实例；跨 configure/clear 后新实例的同 tick 消费衔接仍是实施缺口。

## configure/clear 的消费边界衔接

RTS 在第一次配置时创建唯一导航服务，后续 configure 更新廉价配额而不重建服务或丢弃计划。
等值配置 NoOp。clear 释放 jobs/groups/notices 并关闭策略，保留 owner/input、tick 尝试边界与
累计诊断；重新启用也不退还同 tick 消费。消费端显式查询 enabled，不把保留对象当成启用状态。
Agent lease 清除在下一 motion sync 执行，包括 clear 后立即重新启用，避免旧 lease 残留。
新 owner 恢复仍属于新时间线；没有新的持久化格式、公共 ECS 模型或 provider 保活责任。

两个新增公开 RTS 回归覆盖：图工作后 agent update 失败，同 tick 提高配额/clear 再启用不能
额外工作；普通配置保留原计划，clear 后下一 tick 可重建；等值配置 NoOp；disabled 保留服务
仍允许 Crowd provider 缺席。OBJECT 最终 203/203 通过，日志 build/rts-flow-config-budget-tests.log；
SHARED 最终也为 203/203，日志 build/rts-flow-config-budget-shared-tests.log；完整源码门禁
build/rts-flow-config-budget-make-check.log 通过（258 项脚本测试、91 项架构 catalogue 校验）。之前“跨 configure/clear 消费衔接”的历史缺口由
本增量消除，范围为同一个 RTS 生命周期；仍不证明多个协调器共享全 CPU 预算。
没有新的架构例外；容量外排队、全准备成本、并行调度及其余实现文档范围继续保持未完成。

## 稀疏空间索引与可观察查询预算

提取 CrowdSpatialIndex，移除整体代理 AABB 的 dense 空格分配与遍历。拥有型成员/occupied-cell/
行目录均由固定顺序排序构建，桶内按现有 named/anonymous tie key 排序；signed int64 坐标转换
有范围验证。每次 rebuild 的 cell 尺寸按最大身体直径（至少 1）选取，不让远感知半径令所有
小单位落入同一巨大桶。行/cell 二分查询不按空世界面积遍历，半径过滤用 double 避免平方溢出。
clear/relocation/restore/失败 rollback 显式使派生索引失效；不保存代理指针或更改身份权威。

AdvanceBudget 增加 maxSpatialVisits，计入行/cell/二分工作，预算耗尽回滚，不软跳过接触。
StepReport 与两条脚本 report 投影增加 gridEntriesPeak/gridSortComparisons/spatialVisits。
maxGridCells/gridCellsPeak/cellsVisited 的语义更新为 occupied cells，手册和 catalogue 同步。
脚本新预算字段可省略，但提供时必须为精确整数；没有新增持久化格式。

新增四条回归：暴力枚举对照（含负坐标与远坐标）、桶内 tie 排序、相距 2e9 的群体只用两个
occupied cells、查询/cell/坐标预算失败后的位姿与时钟回滚、旧脚本 budget 兼容与精确整数验证。
最终 OBJECT/SHARED 组合各 207/207，通过日志 build/crowd-sparse-final-tests.log 与
build/crowd-sparse-shared-tests.log；core-only SHARED 108/108，日志 build/crowd-sparse-core-only-tests.log。
本轮完整源码门禁 build/crowd-sparse-make-check.log 已通过（258 项脚本测试、91 项架构 catalogue 校验、格式与分层通过）。
1000 代理/5 帧的小规模 Debug 单线程 core-motion 基准输出 build/crowd-sparse-benchmark.csv，
不是发布性能或完整战斗验收。身体尺度 cell 后每帧候选约 143724（此前巨大单桶约 3000000），
预测截断仍存在，时间值不用于算法决策。tile 层、混合尺度分组、完整构建成本预算、并行执行
和其余实现文档范围仍未完成；不以当前存储与查询验证缩减完整目标。

## 稳定接触 pair 与有界构建 oracle

接触数学内核移到 CrowdContactSolver.cpp；候选生成改为计数/前缀分配，整轮只读当前位置，
按 canonical 有序 tie-key pair 排序后累加修正，最后统一应用。桶/slot 枚举顺序不再决定接触
求和顺序。maxContactPairs 默认 1000000、范围 0..10000000，限制每轮活跃 pair buffer；
maxPairVisits 计入 scratch 初始化、计数、前缀、buffer 初始化、填充和排序，耗尽整步回滚。
contactPairsPeak/pairVisits 进入原生和两条脚本 report；脚本预算扩展可省略但必须为精确整数。
Rollback/restore 使派生 pair buffer 失效。没有新增持久化格式、上层依赖或不完整接触结果。

四条新增回归覆盖密集群组正反创建/恢复的位姿逐位一致、pair 容量与构建预算失败后位姿和
时钟保留、过滤后零 pair 不消耗 buffer 容量，以及脚本扩展兼容和精确整数验证。
OBJECT/SHARED 组合各 211/211，日志 build/crowd-pairs-tests.log 与 build/crowd-pairs-shared-tests.log；
core-only SHARED 112/112，日志 build/crowd-pairs-core-only-tests.log。完整源码门禁 build/crowd-pairs-make-check.log 已通过（258 项脚本测试，格式/分层/91 项架构 catalogue 校验通过）。当前仍为串行 oracle，incident 索引、并行 pair/归并和 scheduler lease
尚未实现；没有将这一前置内核声明成完整并行阶段或完整大规模战斗完成。


## 接触 incident 索引与固定顺序归并

每轮 canonical pair 生成后构建 incident CSR：N+1 个 offset、恰好 2P 个 pair 索引，
每个 agent 的索引沿 canonical pair 顺序填入。pair 内核仅写自己独占的 ContactDelta；
agent 归并只写自身修正列，保持原先减左/加右的浮点操作顺序，最后统一应用位姿。
最大穿透单独按 pair 顺序归并。当前执行仍为串行，实际 scheduler 和线程失败 join 尚未接入。

pair/delta/incident 的活跃存储为 P*(sizeof(ContactPair)+sizeof(ContactDelta)+2*sizeof(size_t))，
常见 64 位布局为约 44P 字节，另有 O(N) offset/count 暂存；这是活跃数量上界，vector 容量可能保留历史峰值。
暂存数量由 maxContactPairs 推导有界，初始化、计数、前缀、填充与归并通过 maxPairVisits
预扣工作；maxContactChecks 在 pair 数学计算前整批预检。预算失败及 restore 清空派生 CSR/delta，
不发布部分位姿。既有密集群体创建顺序/恢复一致性回归继续覆盖这条生产路径；新增验证为
实测完整预算减一失败后位姿保留、同 tick 精确预算重试得到相同结果。

OBJECT/SHARED 组合各 211/211，日志 build/crowd-csr-tests.log 与 build/crowd-csr-shared-tests.log；
core-only SHARED 112/112，日志 build/crowd-csr-core-tests.log。完整 make check 门禁退出成功，
日志 build/crowd-csr-make-check.log；额外显式格式复核通过，避免开头短暂 Git 锁及子模块目录干扰。
未新增公共 API、模块依赖或持久化格式，
适用单一事实 owner、确定性、原子失败、热路径数值索引和裁剪验证规则，无例外。


## 同步执行器 lease 与 JobSystem 接触并行

CrowdWorkScheduler 为原生 composition-root 接口；setWorkScheduler 显式选择 Serial/Parallel，
Parallel 缺 lease 拒绝，活动 advance 中禁止重配。两种执行都使用 256 项固定逻辑 partition，
当前接触 delta 与每 agent incident 归并进入相同内核；pair 构建/排序和速度/预测仍串行。
核心不 include thread；crowd/thread 卫星通过 manifest 声明 L1，工厂观察真实 shared JobSystem。
weak pool owner 在每次 submit/join 期间锁成 owning lease，在途释放外部 owner 安全，空闲不保活。
禁止 borrowed 裸指针空 deleter 或调度中 stop。快照不保留 scheduler；commit restore 保留当前配置。

JobSystem join 不向上传播 child hasFailed；适配器记录 partition 独占 exception slot，完整 join
后返回 Result 失败，不重跑串行。Crowd 将已 join 的失败送入现有 AdvanceGuard 回滚，包含部分
agent 已应用修正的情形。无脚本线程资源配置、新 ECS owner、持久化格式或热路径字符串查找。

五条新用例：无 provider/非法模式拒绝与默认串行、恢复后执行配置保留、300 agent 多 partition
串并行三 tick 位姿/速度及 pair report 一致、真实 worker 异常 join 与在途 owner 释放、部分 gather
失败不重跑且同 tick 重试与串行结果一致。OBJECT/SHARED 组合各 216/216，日志
build/crowd-scheduler-tests.log、build/crowd-scheduler-shared-tests.log；无卫星核心 SHARED 114/114，
日志 build/crowd-scheduler-core-tests.log；新增 crowd-thread-core-only profile 仅选择
common/thread/crowd/crowd_thread，SHARED 编译链接与运行 117/117，日志 build/crowd-scheduler-trim-tests.log。
12 profile 静态矩阵通过 build/crowd-scheduler-profile-matrix.log。最终当前状态的完整源码门禁
build/crowd-scheduler-current-make-check.log 退出成功：259 项脚本测试、22 项架构专用测试、
92 项 catalogue 校验、格式与分层通过。

架构取证器原先将 class JobSystem; 前向借用误报为新 ECS System；本轮只排除有分号的前向声明，
新增测试同时保留真正定义以及同一行前向声明+定义的门禁，不使用 baseline/allowlist。
补强后的生命周期用例通过线程 ID 与 5 秒测试 watchdog，保证后台 worker 确实参与，
并由首个后台 worker 注入异常与释放外部 owner；剩余 18 个 partition 全部完成。
expired/stopped provider 均不执行回调。最终 OBJECT 与独立 SHARED 相关三条用例各 3/3，
日志 build/crowd-scheduler-final-worker-object-tests.log、build/crowd-scheduler-final-worker-trim-tests.log。
watchdog 仅用于测试并发存活取证，不参与模拟决策。
适用 Result、ownership/lifetime、确定性、失败原子性、模块六面与可选 provider 裁剪规则，无例外。
实际期望/预测速度并行、LOD、完整性能/图形战斗验收与其余文档要求仍未完成。


## 预测速度固定 partition 与精确评分预检

邻居准备在 owner 线程完成，每个 agent 保留独占 owning 数值列表，按距离/tie-key 选择，
长度不超过 maxNeighbors；插入过程暂时至多 maxNeighbors+1 项，设置上限 128，保留容量可沿历史峰值。
评分采样数组与预算计数同源，当前每选中邻居恰好 45 次评分，held/zero-speed 行不消费预测。
maxPredictionChecks 在派发前精确预检整轮，计数与截断 report 仅 owner 写入。
速度加速度/预测评分/朝向通过已有 256 项逻辑 partition 执行；worker 只读旧位姿和速度，
仅写自身 next velocity/speed/heading。join 后统一交换速度列并积分，没有共享 float atomic。
串行走相同准备、评分与分区内核；期望方向（含 Boids）准备尚未并行。

Rollback/restore 清空派生预测列表；失败时原位姿、速度、朝向、wander phases 和 tick 保留。
新测试覆盖 300 个移动 agent 三 tick 串并行位姿/速度/朝向/预测与候选 report 逐位一致，
两 agent 的 90 次精确评分预算减一失败和同 tick 精确配额重试，以及部分速度完成后 worker
失败的整步回滚与 Boids wander 快照参考回放。接触 gather 失败注入调整为第三次 dispatch，
仍检验真实 gather 写入后的回滚，而非误测新增 velocity 阶段。
OBJECT/SHARED 组合各 219/219，日志 build/prediction-crowd-checked-tests.log、
build/prediction-rts-crowd-shared-tests.log；独立 Crowd+thread SHARED 120/120，日志
build/prediction-crowd-shared-tests.log。补强 wander 回归的 5 条线程用例通过 build/prediction-rollback-tests.log。
无卫星核心 SHARED 115/115，日志 build/prediction-core-only-tests.log。完整源码门禁
build/prediction-make-check.log 已通过：259 项脚本测试、22 项架构专用测试、92 项 catalogue
校验以及格式/分层/12 profile 矩阵通过。

未新增公共 API、持久化格式或模块依赖；适用单一 owner、数值热路径、同步 join、Result 原子失败、
注入时间和确定性规则，无例外。完整准备 CPU 预算、期望方向并行、LOD 与全战斗验收仍未完成。


## 有界完整期望方向 CSR 与固定分区数学

期望方向内核从 CrowdSimulation.cpp 移到 CrowdPreferred.cpp。owner 通过 count/prefix/fill
构建完整有效三力邻居 CSR（N+1 offset、K 个数值 slot）；没有复用截断预测列表。
保留旧 max(sepRadius,perceiveRadius) 查询范围与 float 三力判据，去掉确定不贡献启用三力的索引。
缩小查询半径会丢失 double 距离略大、float 运算恰好落在边界的贡献，新增精确回归对此取证。
完整 Boids/Seek/Wander 数学与最终期望方向通过固定 256 项 partition 执行，不写共享计数器。
Flow 的 navigationField 仍为 owner-thread 局部借用；先采样成方向/减速三个拥有型数值，再交 worker。
因此没有 Field 裸指针跨任务、未知 callback 或上层依赖。

maxPreferredNeighbors 默认 1000000，允许 0..10000000，限制每子步有效完整邻居活跃条目；
maxPreferredVisits 计入速度工作列、count/prefix/init/fill、Flow 数值准备和三力条目计算。
底层遍历/比较同时继续计入 maxNeighborVisits/maxSpatialVisits。容量/工作耗尽整步拒绝且同 tick 可重试，
不截断三力。preferredNeighborsPeak/preferredVisits 在原生及 advance/advanceTick 两条脚本报告投影中可见；
脚本新增预算可省略但提供时须为精确整数。Rollback/restore 清空 CSR 与拥有型 Flow 派生输入，
保留当前执行器配置。容器可保留历史容量，活跃条目上界不等于 resident-byte 硬上限。

三条新核心回归覆盖容量与精确工作预算失败/重试、旧脚本兼容/严格整数及两条报告、float 边界；
一条新线程回归覆盖 300 个 Boids/Seek/Flow/Hold 混合 agent 三 tick 串并行状态及报告逐位一致。
已有失败回放同时覆盖 preferred/velocity 两阶段；接触 gather 注入改为第四 dispatch，仍验证实际归并失败。
公共布局改变后，相关 Crowd/RTS 以及直接头/测试 fixture 消费 TU 已触发重编译。
OBJECT/SHARED 组合各 223/223，日志 build/preferred-crowd-checked-tests.log、
build/preferred-rts-crowd-shared-tests.log；独立 Crowd+thread SHARED 124/124，日志
build/preferred-crowd-shared-tests.log；无卫星核心 SHARED 118/118，日志 build/preferred-core-only-tests.log。
本轮完整源码门禁 build/preferred-make-check.log 已通过：259 项脚本测试、22 项架构专用测试、
92 项 catalogue 校验、格式/分层与 12 profile 矩阵通过。

遵循 Result、单一事实 owner、固定数学顺序、同步 join、owner-only 借用、数值热路径及 optional-provider
裁剪契约，无例外。全预测准备初始化与 rollback 复制的成本配额、tile/混合尺度、LOD、完整战斗消费者
及性能/真实渲染验收仍未完成，不以本轮阶段并行回归缩减目标。


## RTS 并行消费者与位姿发布边界

新增 test/rts/thread/CrowdMotion.cpp，经现有可选模块与组合测试入口注册，不创建新 System。
真实 JobSystem 的 300 单位 Move/AttackMove/Hold 消费路径逐 tick 对照串行 motion、heading、arrival
与 Crowd 速度；过期 provider 和确定重叠场景的部分 gather 失败均保持两边位姿与同 tick 重试。
SharedFlow pending + 过期 provider 回归取证发现并修复了提前写 arrived=false 的错误：只在成功 motion 后发布。

CrowdMotionSystem 在 advance 前预留 selected frame 容量，避免成功推进后才为状态复制分配；
输入阶段 move 保存拥有型订单投影，notice 和 arrival 不再重新读取/复制订单。
位姿和 heading 全部写回后才算动态目标 arrival。新回归以两种实体创建顺序验证 Attack 观察的是
完整新目标位姿，消除逐单位 pose/arrival 交错发布的遍历顺序依赖。
没有 Field/ECS 指针交给 worker；没有新增公共 API、持久化格式、provider 或模块依赖。

OBJECT/SHARED 组合各 228/228，日志 build/rts-publication-object-final-tests.log、
build/rts-publication-shared-final-tests.log。完整源码门禁 build/rts-publication-make-check.log 已通过：
259 项脚本测试、22 项架构专用测试、92 项 catalogue 与格式/分层/12 profile 矩阵通过。
适用 Result、owner-only ECS、拥有型投影、单一事实 owner、同步 join 与失败发布边界规则，无例外。
这批测试不证明最后 arrival-intent updateAgents 批次发生分配失败时的全事务回滚；该晚提交边界仍需
后续 canonical prepare/commit 或等价完整取证。供给/threat 等剩余消费者、LOD、完整性能及渲染验收仍未完成。


## 预备更新 token 与关闭 arrival 晚提交分配

prepareAgentUpdates 校验身份/参数/重复/lease 并复制 owning 更新值；move-only AgentUpdatePlan
只持有非复用 owner 身份与值，不持有 Crowd、slot 或临时 span。commitAgentUpdates 对全部引用和
零/一选择掩码预检，再解析当前 slot 并应用，成功消费 token；失败不写入且保留 token。
即使未选择的引用失效也整批拒绝。压缩可重解析，restore 失效，provider-first 安全。
旧 updateAgents 为明确 compatibility-only facade，委托同一个 canonical 内核，没有廉价旁路。
RTS 在 advance 前准备全部潜在 idle 更新、输出容量与掩码，成功 motion 后只提交实际到达项。

真实 OBJECT operator-new 探针揭示了两个 Debug 分配问题，而不是绕过失败：
MSVC vector 默认构造的 iterator proxy 在 noexcept 中分配；准备 workspace 与 token 数据采用可抛异常的
零长度构造，使第一次真实故障进入 Result catch。随后探针发现成功 Status 的空诊断 vector 有三次分配。
Status 成功值现在无诊断容器，错误诊断为不可变共享 owning 存储；公开查询形状和诊断顺序不变。
只读空观察由 common 库 init-on-use 并在库启动预热；私有公共值布局改变后，src/test 全部 CPP 消费者
已触发重编译，OBJECT 677 个构建步骤与 SHARED 全消费者构建日志提供证据。

新增 token 所有权/掩码原子拒绝/压缩/失效/恢复/销毁顺序回归，并引入共同 Result 既有契约及新诊断寿命测试。
Result CHECK_THROWS 用例按标准发现器策略用 exit status 判断，Crowd 仍保留 fatal-output 检查。
实际 probe 输出 commit allocations: 0; ok: 1，同时真实 preparation 分配失败保留原状态；
成功 Status 创建/复制/移动也测得零分配。探针仅适用 OBJECT，不以它冒充 DLL allocation interception。
最终 OBJECT 242/242（含真实分配探针）、SHARED 241/241、core-only SHARED 131/131，日志
build/arrival-final-crowd-checked-tests.log、build/arrival-final-rts-crowd-shared-tests.log、
build/arrival-final-crowd-shared-tests.log。完整源码门禁 build/arrival-final-make-check.log 通过：
259 项脚本测试、22 项架构专用测试、93 项 catalogue 与格式/分层/12 profile 矩阵通过。

遵循 canonical Result、prepare-token-commit 成本形状、不可变 owning 诊断、单一事实 owner、owner-thread
提交、全引用 stale 检查、销毁/restore、公共家族导出与全布局消费者重建要求，无例外。
此次关闭的是已识别的 arrival 成功提交分配窗口；不代表全 RTS step 的其他系统事务、LOD、tile/混合尺度、
供给/threat 剩余消费者、完整性能与渲染验收已完成。


## 2026-10-14 核心交付路线修正与首次完整战斗

按用户要求停止扩展基础设施前置清单。实施方案第 11 节改为完整战斗 → 编队/交通 → 规模测量；完整功能路线仍保留，外围产品化独立后续阶段。已有接口正确性契约不豁免。

新增独立原生 runner `test/profile/rts_battle_benchmark.cpp` 与显式 target `eve_rts_battle_benchmark`。通过现有 RTS::stepScript 消费地图、Crowd、索敌、武器、伤害和死亡清理，不新增战斗权威状态或公共 API。稳定身份、固定 0.05 秒 tick、确定性出生布局，无随机输入；仿真及观察均为 owner thread。输出 step 总时间，事件观察时间排除在 step 时间外；总时间尚未分解为各系统。

首次 SHARED Debug 构建成功；128 单位、160 tick 实际运行通过，产生 309 次 weapon_fired、309 次 damage、75 次死亡，剩余 53 单位。日志 `build/core-battle-128.csv` 与 `build/core-battle-128.err`。runner 以伤害事件 killed 字段统计死亡，同时检查实际单位数量下降，任一条件缺失返回失败。

这是完整移动/战斗闭环的首次证据，不代表共享导航、窄口编队、攻击槽、Release 规模性能或真实渲染完成。没有新增 ECS System、持久化 schema、跨模块接口或生命周期例外；沿用现有根、组件、Provider、Result 及单一权威 owner。下一步围绕同一战斗路径补齐行为并测量生产配置，避免继续独立增加机制。


同一 runner 扩大至 1000 单位、80 tick 也已通过：1545 次开火及伤害、364 次死亡、剩余 636 单位；日志 `build/core-battle-1000.csv` 与 `build/core-battle-1000.err`。Debug 后段 tick 约 1 秒，不能宣称千人实时或万级性能完成。下一步先取得 Release 分项测量，再决定热点优化；该结果不支持预先归因于某个系统。


复用既有 common profiler，runner 现在额外输出 Crowd::advance zone 与剩余 step 时间。该 zone 实际包围 advanceWorking，剩余时间包含 Crowd 的准备/提交、RTS 同步和其他系统，不能全部归因于战斗或导航。Profiler 聚合和事件观察在 step 计时外，zone 收集开销在计时内。128/160 再跑得到相同 309/309/75 与剩余 53；日志 `build/core-battle-128-profile.csv`。后段工作态 Crowd 约 10 ms，而剩余 step 约 70–75 ms，说明只优化 Crowd 数学内核不足以解决当前总成本；下一步需进一步分解，不能把这个剩余桶解释成某一系统。


同一 runner 增加 shared/corridor 显式场景开关。shared 使用既有 surface tracking 与有预算 Flow 服务，记录 prepared 与 cacheHits（后者不是单位共享消费计数）；128/160 通过，473 次开火及伤害、117 次死亡、剩余 11，构建 16 场、2 次缓存命中、无 pending，日志 `build/core-battle-128-shared.csv`。不同移动路线与死亡数量使它不能和开阔地默认场景直接比较优化倍数。

corridor 在地图放置仅留一格出口的整列墙，并启用武器障碍阻挡，共享导航及现有交通系统参与。64/240 通过，117 次开火及伤害、28 次死亡、剩余 36，8 场、7 次缓存命中、无 pending；日志 `build/core-battle-64-corridor.csv`。这证明带窄口的实际战斗链路可以运行，不证明完整编队 Compress/Traverse/Reform、所有单位公平通过或攻击槽已经完成。


## 千人共享导航真实阻塞修复

1000/80 shared 场景首次在进入战斗 tick 时失败，错误为 Shared-goal request capacity exhausted。修复将轻量目标队列和昂贵构建器分开：最多 10000 个目标键，仍最多 64 个 Pending/Ready/Unreachable 工作态构建器，启动与工作配额不变，等待项保留准入序号并显式 Pending。pump 的临时排序指针仅在 jobs 不变的函数阶段使用，分配失败转 Result，预算 latch 保持既有失败语义。

回归进一步发现 ready cache 也有独立 64 项限制，与字节预算未耗尽时的多目标战斗不符。缓存项上限改为 10000，原字节预算、lease pin、稳定淘汰顺序与压力失败原子性保留；动态 eviction bitmap 按实际项数准备。公有签名、类布局、跨模块依赖和持久化格式均未变化，成本注释和 catalogue 已同步。

新增三项回归：70 个目标全部完成；140 个等待目标在极小工作预算下仍仅准备 64 个构建器；70 个 pinned 场保留且缩减预算失败不修改缓存。SHARED 244/244、OBJECT 245/245、无 RTS/thread 的 Crowd core-only SHARED 132/132 均通过，日志 build/core-battle-capacity-tests.log、build/core-battle-capacity-object-tests.log、build/core-battle-capacity-core-tests.log。OBJECT/SHARED 构建通过；完整源码门禁 build/core-battle-capacity-make-check.log 通过。

最终 1000/80 shared 原生战斗退出成功：1991 次开火、1991 次伤害、474 次死亡、剩余 526 单位；126 个场已准备，末尾 pending=0。日志 build/core-battle-1000-shared.csv 与 .err。cacheHits=0 是合法结果：该计数记录重新 acquire 的缓存命中，不计多个单位重复消费同一 Job 的 owning lease。runner 的共享导航验收已修正为存在真实构建、活动请求全部完成以及真实战斗/清理，不以 cacheHits 非零代表共享消费。

千人数据为功能验证，部分运行与构建/测试并发，不能用该日志对比优化收益；当前 Debug 总耗时仍明显偏高，未宣称实时性能。完整编队阶段、攻击槽、Release 5k/10k 性能与实际渲染仍是后续核心缺口。

本批遵循 Result、拥有型/带代次关系、owner-thread 执行、单一事实 owner、显式成本与配额、不可驱逐持有者、失败原子性、接口共享测试和裁剪配置验证。未新增 ECS System、公共签名、持久化 schema、provider 或架构例外。


## 2026-10-14：AttackMove 编队、窄口终点、近战准入与正式 Release 验证

本批保留原先的核心顺序，增加 AttackMove 编队准入和同一 group/epoch 生命周期。原生 RTSStateSnapshot v2 无新增编码字段；命令日志只在 AttackMove 编队组合出现时导出 EVERTS_COMMANDS v6，旧 Move 编队继续导出 v5，伪装成 v5 的新组合原子拒绝。交战成员不锁住仍在前进的成员。

统一 RTS 的格子中心原点与 Crowd 的左上角原点：适配减去半格，脚本地形/射线也使用中心坐标。Flow 在静态障碍边缘对齐圆形足迹入口；没有路径点的共享成员在真实穿越入口前预占，持续预约期间解除整体速度锁。完整窄口旧版验证日志 `build/core-group-corridor-32.csv`：32/800，76 次开火及伤害、18 次死亡、14 存活，全到指定位置、group=0、pending=0。后续策略又有修改，最终版本需重新跑同一场景，不能沿用该数字作为最终版本证明。

Seek 对符合接触掩码、可推动且非 Hold 的 Idle、相对近零速成员及停稳终点邻居采用实体让位；接触和墙约束保留。无接触/不可推动/Hold 继续预测避让。测试显式设置 Idle，不能把默认 Flow 代理当作 Idle。

近战 Unit 对 Unit 位置归 RTS 单一 owner，每 tick 准备完整位置计划后发布 Navigation combatSlotTarget/combatSlotWaiting，不进入保存。前排最多 64 个候选，按距离和 UUID 字节排序，独占位置并检查异半径完整足迹；后排使用等待带且原武器系统禁火。销毁、换单、容纳、同 ID 重建自然失效；不建持久预约平台。短射程 64/240 实战已产生 242 次开火与伤害、58 次死亡、6 存活，日志 `build/core-battle-64-melee.csv`；该运行与构建并发，仅作功能证明。

正式 Release/OBJECT 通过真实 Release 第三方依赖构建。独立测试目标沿用主测试的断言启用策略，避免 REQUIRE 中的初始化被编译掉。发现并修复 Result 跨策略移动标记：观察、转移、源失效在所有配置一致更新，只有检查按断言策略消失；两 TU 的 interop 回归覆盖无断言 producer 与断言 consumer。库值布局不变，全部相关 TU 强制重编译。

千人编队首次暴露槽位分配局部最优：同总路程的横排交叉令 pacing 等待任意长路线。新增 formation-local 空间候选与等距离平方路程比较，保留更优候选再做既有有界优化。512 人平移网格回归验证每人等长、不横排反转；不改输入数据回避缺陷。当前 Release/OBJECT 261/261 通过，日志 `build/core-spatial-grid-tests.log`。最终 SHARED、core-only 和 make check 需在后续最终改动后重验。

原始千人共享 Debug-ABI /O2 模式 1000/80 已有 2071 次开火和伤害、491 死亡、509 存活，后 30 tick 中位数总 step 901.39 ms，Crowd 工作态 131.15 ms、CombatFire 135.31 ms，其余 627.53 ms；日志 `build/core-battle-1000-indexed.csv`。该构建不是正式 Release，不能用于宣称实时性能。5000 次旧测量均未完整完成，不能作规模通过证据。

增加原生总 CrowdMotion（含导航/准备/发布）与 CombatFire 分项计时；Crowd working 是前者的子集，不能重复相加。目标验证使用完整 table/type/id/generation tick-local 索引，取代每次扫描全目标；感知和目标过滤权威未替换。

新增 `examples/crowd-battle`，通过正常 EveScript 生命周期运行真实 RTS 战斗，读取 authoritative pose/health 渲染简化方块。支持开阔、单格窄口、近战模式，不用 eve_eval 或脚本改血量。布局检查通过，实际 smoke 和当前版本 engine-owned PNG 待验证。


### 当前版本核心实战与规模证据

空间槽位候选解决了平移网格的路线反转，261 项 Release/OBJECT 模块契约通过：`build/core-sensing-index-tests.log`。感知启用已有 SpatialHash2D，依旧走原过滤、排序、武器和伤害路径。1k/5k/10k 160 tick 的火力/伤害/死亡在启用索引前后完全相同，说明不是关闭战斗或丢单位换吞吐。

最终 Release/OBJECT grouped 场景（无渲染、默认调度、dt=0.05、没有并发构建）结果：1000 单位 3483 开火/伤害、868 死亡、132 存活；5000 单位 17501 开火/伤害、4372 死亡、628 存活；10000 单位 34987 开火/伤害、8743 死亡、1257 存活。prepared 分别 1/4/2，末尾 pending 全为 0。完整逐 tick CSV 为 `build/core-index-release-1000.csv`、`...5000.csv`、`...10000.csv`，对应 `.err` 无错误。

前 20 tick 均未死亡，满员负载中位数总 step 为 136.27 / 770.08 / 1664.18 ms；Crowd working 为 30.42 / 147.43 / 293.75 ms；CrowdMotion（包含前者、导航/准备/发布）为 41.33 / 207.75 / 414.28 ms；CombatFire（含候选查询、过滤、射击/伤害）为 27.13 / 160.81 / 339.75 ms；其余 RTS 为 68.13 / 402.25 / 912.99 ms。后三十 tick 的单位数已降到 132、629→628、1257，不能以其 18.04 / 91.76 / 188.40 ms 代表满员性能。没有把整个剩余桶叫做 Fog 或 ECS。现有数据支持规模功能边界，不支持千人/万级实时帧率；60 FPS 和更细感知/FOV/同步预算优化保留为性能阶段。

同输入优化前 5k 满员 CombatFire 中位数 761.67 ms、10k 首段约 5.8 秒，既有感知索引显著减少候选扫描；最终仍有整帧热点，未宣称全面完成性能优化。旧 Debug-ABI /O2 与正式 Release 的场景/断言/单位存活数量不同，不报告两者加速倍数。

最终 32/800 grouped-corridor：100 次开火/伤害、25 死亡、7 存活，移动 group=0，所有存活单位到原指定终点，31 个场、pending=0，日志 `build/core-final-grouped-corridor.csv`。最终 64/240 melee：234 次开火/伤害、57 死亡、7 存活，8 场、pending=0，日志 `build/core-final-melee.csv`。两项为并发构建期间的功能重验，不作性能比较。

真实图形闭环：`scripts/capture_example.ps1` 通过当前 Release 的正常 run 生命周期，120 frame，由 gfx.saveFramePng 读取 swapchain 输出 `build/core-crowd-battle.png`；已查看 832×640 实帧，包含两军位置、真实健康条和单格窄口。日志 `build/core-crowd-battle-capture.log`。不是桌面截图，不是万级图形性能证据。正式 smoke 的 LF 临时副本在 Git Bash 运行，持续 6 秒、最小 2 秒、无错误标记通过，日志 `build/core-crowd-battle-smoke.log`；原 tracked shell 的 CRLF 未改动。

当前无 RTS/thread 的 core-only SHARED 139/139 通过（包括跨策略 Result），日志 `build/core-final-trim-tests.log`。最终 RTS SHARED 与源码门禁结果见下方收口记录。


最终开发 SHARED 260/260 通过，日志 `build/core-final-shared-tests.log`；OBJECT/Release 261/261、core-only SHARED 139/139 构成当前接口和 provider absent 的实际编译/链接/运行证据。两种联接路线包含相同核心语义；OBJECT 额外保留全局 allocator 提交探针。目录与 UTF-8、Result 不得丢弃、代次生命周期、无第二份状态 owner、同步线程边界、无新向上依赖、临时占位的完整准备再发布、公开相对成本及六面 catalogue 已按本批改动落实。没有架构例外，也没有触碰原先 dirty 的 external/ECS.hpp 变更。

最终 `make check CI_BASE=a73c6ba8ea99e70b7d8236db1783ea54acb39b65` 退出 0，源码门禁及 259 项脚本测试通过，日志 `build/core-final-make-check.log`；基线为当前 HEAD 与 origin/dev 的共同祖先。完整战斗、编队/窄口/攻击槽行为、1k/5k/10k 规模测量和针对已测热点的优化构成本轮核心交付。万级仿真功能已验证，满员实时性能尚未达到；后续性能阶段和完整扩展设计不在此处标为完成。以上为本地证据，未声称跨平台 CI 通过。


## 2026-10-14 性能优化：100 ms 目标

本轮目标按同一 10000 单位满员 grouped 战斗、dt=0.05、Release/OBJECT、无渲染的总 `RTS::stepScript` 衡量，同时记录满员步的中位数、P95、最大值。不能用 Crowd 子集、死亡后的较少单位、减少更新频率或停止战斗代替目标。

已实施：订单不可变投影缓存及完整失效；视野绑定和 revealer 索引；无雷达/干扰能力时避免无效扫描；Sensing 自有稳定节点数字索引；空间查询按首个相交网格去重，省掉每次查询集合分配；无供应者时跳过无效受援扫描；不变 revealer 不触发重算；有完整无遮挡/高度证明的 height-ray 快路径；停稳状态复用和有界最近集合插入剪枝；非负评分下界剪枝。未新增调度平台或降低物理解算质量，所有新缓存均为派生状态，不改变持久格式。

回归包括负坐标/跨多个网格的空间查询对照、视野源有序删除/移动/清空、相同 order id 的恢复/清空/替换失效、Sensing upsert/erase/restore/move、快视野与完整高度射线逐格比对及 terrain edit 失效。近处网格优先遍历与全扫描的差分实验结果一致，但在本场景过滤大量停步成员后性能反而退步，因此已撤回该实验实现及其专用测试。最终规模计时和门禁结果将在本节收口记录。


批量死亡清理属于既有 `simulation.cleanup`：范围为 RTS 自己拥有的 Unit；不持有 ECS View，读 Identity/Containment/Crowd/Weapon，按完整 table/type/id/generation 一次更新 Unit 的容纳/战斗/护卫/供给引用、Building 的建设/驻军/rally/capture、ResourceNode workers、Faction members 和 Player selection。数值 Crowd removal 与所有计划存储先验证，再发布引用清理和根 roster 更新；最后仍走既有逐根 sensing/script/weapon/entity release 顺序。载具乘员和建筑递归使用原生命周期路径。没有新持久状态、时间源、RNG 或事件协议；回归覆盖提供者存在/缺失、非法重复 AgentRef 的失败前不变状态，以及父载具删除乘员。


最终无并发构建复测（`build/perf-casualty-1000.csv`、`...5000.csv`、`...10000.csv`）：160 tick、dt=0.05，与原 grouped 输入一致。前 20 tick 全员存活，总 step 中位数为 24.853 / 142.012 / 296.649 ms，P95（20 样本 nearest-rank）为 26.579 / 151.799 / 313.074 ms，满员最大值为 37.848 / 203.125 / 415.905 ms。完整 160 tick 的最大值为 41.433 / 214.724 / 463.702 ms。万单位满员中位数较原 1664.18 ms 降约 5.6 倍；同场景批量清理前的 2886.18 ms 峰值降至 463.702 ms。100 ms 在千单位全程通过，5k/10k 总单步尚未达标，不能以 Crowd working 的 8.467 / 39.193 / 75.513 ms 子集宣称总 step 达标。

开火/伤害/死亡/最终存活分别为 3483/3483/868/132、17501/17501/4372/628、34987/34987/8743/1257，均与原基准一致；prepared 为 1/4/2，pending 均为 0。最终 32/800 grouped-corridor 仍为 100 开火/伤害、25 死亡、7 存活、移动组清空且到原终点；64/240 melee 仍为 234 开火/伤害、57 死亡、7 存活。日志为 `build/perf-casualty-corridor.csv`、`build/perf-casualty-melee.csv`。stderr 中的 profiler 报告是阶段计时，全部 runner 退出 0。

OBJECT/Release 269 项测试已通过，包括新生命期/失败注入回归，日志 `build/perf-casualty-release-tests.log`。最终 SHARED、精简配置和源码门禁结果见下方；以上没有宣称跨平台 CI 通过。剩余满员成本主要为 CombatFire 查询/镜像、邻居准备和 RTS 多系统扫描；100 ms 总目标继续保留。


本轮最终收口：SHARED 268/268（`build/perf-complete-shared-tests.log`）、OBJECT/Release 269/269（`build/perf-casualty-release-tests.log`）、无 RTS/thread 的 core-only SHARED 139/139（`build/perf-complete-core-tests.log`）全部通过。`make check CI_BASE=a73c6ba8ea99e70b7d8236db1783ea54acb39b65` 退出 0，259 项脚本测试及源码质量/架构门禁通过，日志 `build/perf-complete-make-check.log`；该 SHA 仍为合法共同祖先，没有新 baseline 或 allowlist。Result、唯一状态 owner、完整代次校验、生命周期失效/恢复、提供者存在与缺失、失败原子性、View 外结构变更、线程/回调边界、相对成本及模块 catalogue 已按本次范围验证；没有架构例外。100 ms 总目标只在本场景千单位全程达到，5k/10k 保持未达标状态。

### 2026-10-14 继续定位：空间索引更新开销

同一万单位 grouped 场景的第 2 tick，上一轮剖析显示邻居准备 65.940 ms（3 个子步），CombatFire 总计 70.564 ms，其中对象镜像 19.616 ms。镜像中的 Sensing upsert 会复制整个 Subject，并对所有坐标重新删除/插入空间索引条目，即使位置没有改变或仍在相同格子内。

本轮保留两个局部改动：Subject 在原有稳定节点内移动赋值，仅位置变化时更新空间索引；SpatialHash2D update 原位更新 bounds，同格子更新保留原桶容量，并通过旋转维持原先的 update-to-end 查询顺序。元数据仍每次更新，空间查询排序、身份、标签、阵营过滤、碰撞与战斗规则不变。扩展已有差分测试，覆盖负坐标、跨多桶、同桶连续更新的结果及顺序，以及坐标不变但标签/阵营变化的情况。所有权和生命周期仍由原 Sensing / Spatial 实例负责，没有新增持久状态、公共接口或架构例外。

候选邻居的最大堆实验虽然保留相同最近 K 个对象与最终排序，但实际邻居准备约 96 ms，慢于原约 66 ms，已撤回；完整战斗开火/伤害/死亡计数仍一致。仅局部同步减少约 2 ms 的初测不能作为整体达到 100 ms 的证据。主要剩余成本仍是完整预测邻域扫描、逐单位候选查询以及 RTS 多系统扫描。

最终保留版本：Release 269/269、SHARED 268/268、core-only SHARED 139/139 通过（build/perf-final-*-tests.log）；make check 退出 0，259 项脚本测试通过，日志 build/perf-index-make-check.log。独立万单位复测 build/perf-isolated-index-10000.csv：前 20 tick 满员中位数 353.11 ms、P95 396.49 ms、满员最大 489.33 ms、160 tick 最大 542.37 ms。相较上一轮 296.649 ms 的历史测量没有整体提速证据，不能把索引局部初测当作完整 step 的提升。万单位仍未达到 100 ms。开火/伤害 34987、死亡 8743、剩余 1257，以及 prepared=2、hits=0、pending=0 与基线完全相同。通道与近战验收计数也一致。最终采样 tick 2：邻居准备 68.935 ms、CombatFire 73.971 ms（镜像 20.406 ms）、CrowdMotion 129.203 ms、MovementGroups 24.461 ms；Crowd 内部时间是 CrowdMotion 子集，不能重复相加。当前保留改动减少索引条目和桶的重建，不宣称总体性能达标或跨平台 CI 通过。

### 2026-10-15 向量化/缓存核查与四线程实测

RTS 使用 `ecs::View<Unit,...>` 读取领域组件；Crowd 将位置/速度等高频数值放在独立 SoA。Release `/O2` 不等于热点已有效 SIMD：邻居筛选的离散编号、过滤分支和固定顺序最近 K 个插入都限制自动向量化。曾在每格排序目录复制位置以减少随机读取，万单位邻居准备仍约 69 ms，完整步更慢；实验已撤回，不留下第二份位置事实。

在既有 `CrowdWorkScheduler` / `crowd/thread` JobSystem 适配器上，并行准备预测邻居：固定 256 单位逻辑分区、每分区独占邻居缓冲与工作计数，worker 全部 join 后按分区号归并。串行路径保留；RTS 自包含脚本世界可通过原生 `configureScriptCrowdExecution` 注入调用方持有的调度器，缺失提供者返回 Result，调度器不保存到快照。RTS ECS View 和战斗结算仍由 owner 线程执行。预算超限或 worker 失败在发布位姿前返回失败，保持原有 advance 回滚边界。

同一 Windows Release OBJECT 万单位、160 tick grouped 战斗，连续无并发构建比较：串行前 20 tick 满员中位数 294.80 ms、P95 316.25 ms、满员最大 415.11 ms、全程最大 469.57 ms；四线程分别为 240.61 / 268.93 / 331.12 / 416.73 ms。第 2 tick 邻居准备约 67.15 → 14.86 ms，Crowd advance 总计约 91.88 → 24.95 ms；总步仍远高于 100 ms。开火/伤害 34987、死亡 8743、剩余 1257，flow prepared=2、hits=0、pending=0，两种模式完全一致。独立 Crowd 1 万单位基准 P50 19.36 → 6.80 ms，但其输入不同于完整 RTS 场景，不能代替总步证据。完整结果：`build/perf-rts-{serial,parallel}-10000.csv` 与 `build/perf-crowd-{serial,parallel}-10000.csv`。

最终验收：Release OBJECT 271/271（build/perf-parallel-final-release-tests.log）、SHARED 270/270（build/perf-parallel-final-shared-tests.log）、不加载 RTS 的 core-only SHARED 139/139（build/perf-parallel-final-core-tests.log）通过；`make check CI_BASE=a73c6ba8ea99e70b7d8236db1783ea54acb39b65` 退出 0，259 项脚本测试与架构/源码门禁通过（build/perf-parallel-make-check.log），没有新增基线或豁免。四线程 5 千单位配对复测：前 20 tick 满员中位数 168.29 → 142.83 ms、P95 222.76 → 184.76 ms；160 tick 开火/伤害 17501、死亡 4372、剩余 628、flow prepared 4，两模式一致（build/perf-rts-{serial,parallel}-5000.csv）。100 ms 目标在 5k/10k 的完整 RTS 步仍未达到；这里只宣称使用 4 worker 的 Crowd 阶段加速和对应完整步的有限收益，不声称战斗/ECS 系统已并行。

### 2026-10-15 战斗空间格子候选复用

`SensingWorld` 的圆形查询按所覆盖的 `SpatialHash2D` 格子范围复用候选 ID；原查询继续逐攻击者做精确圆形相交、阵营/标签过滤、优先级排序及结果发布。缓存为线程局部、有限额的派生数据（最多 256 个范围、每范围 512 个 ID、合计 32768 个 ID），索引变更即失效，世界或索引身份改变时清空；不跨任务传递主体指针。战斗的 ECS View 顺序、开火和伤害提交仍由 owner 线程执行。独立测试 `test/sensing_spatial_cells.cpp` 对比缓存与新建世界，覆盖同格不同圆心、负坐标和边界、位置/元数据更新、删除、移动赋值及完整查询调试 JSON；手工重编后的 Release 测试可执行文件通过。

相同 Windows Release OBJECT、grouped、4 worker、160 tick 运行，128 单位及 5k/10k 的开火、伤害、死亡和最终单位数与优化前一致。5k 前 20 tick 完整步 P50 121.48 → 110.58 ms，P95 142.92 → 121.77 ms，`CombatFire` P50 33.51 → 29.09 ms；全程均为开火/伤害 17501、死亡 4372、剩余 628（`build/spatial-{before,after}-5000.csv`）。10k 两组无并发构建的配对结果：完整步 P50 271.56 → 230.67 ms、P95 388.01 → 276.33 ms，`CombatFire` P50 73.16 → 60.81 ms；反向顺序配对为完整步 P50 247.24 → 236.82 ms、P95 275.95 → 260.40 ms，`CombatFire` P50 70.15 → 61.41 ms（`build/spatial-final-{before,after}-10000.csv`、`build/spatial-final2-{after,before}-10000.csv`）。最终缓存身份修正后单次 10k 复测前 20 tick 完整步 P50 240.51 ms、P95 319.08 ms、`CombatFire` P50 61.53 ms，160 tick 汇总仍为开火/伤害 34987、死亡 8743、剩余 1257（`build/spatial-accepted-after-10000.csv`）。

该优化有可重复的净收益，但 5k/10k 完整步仍超过 100 ms。`CombatFire` 是整个系统的计时，不能等同于候选查询耗时；下一轮若考虑战斗读阶段分块或 SIMD，应先对镜像、候选收集、过滤/排序和结算分别计时，并保留完整 RTS 步与战斗语义验收。当前 CMake 重新配置在本机 WebP SIMD 探测中挂起，本轮改用既有构建的 MSVC Release 参数手工重编相关目标和独立测试；完整 CTest 未在这次代码变更后重跑。

### 2026-10-15 战斗阶段拆分与镜像快路径

在 10k grouped、4 worker 的完整 RTS 场景中临时打开逐查询 profiler：满员 tick 的 `CombatFire` 约 65.65 ms，其中镜像 18.95 ms、Sensing 空间候选收集 18.94 ms、精确过滤及排序 7.73 ms、结果发布 0.65 ms，其余战斗处理约 19.38 ms。逐查询探针每 tick 产生约 3 万个 zone，明显增加开销，取证后已移除；这些是阶段定位数据，不能直接与无探针的性能数值相加。把空间范围缓存的 `std::map` 换成哈希表只使探针下的候选收集约 18.94 → 18.03 ms，无探针的 5k/10k 完整步均没有净收益，已撤回。

镜像阶段每步对存活主体反复 `upsert`。元数据未变且没有 zones 时，现在只更新坐标及必要的空间索引，避免重复解析标签/可见阵营 CSV 和重建集合；元数据变化或已有 zones 时沿用原完整替换语义。新增测试覆盖坐标更新、重复/空 CSV 项和 zones 重置。无探针 10k、160 tick 两次复测前 20 tick `CombatFire` P50 为 55.38 / 55.86 ms，原缓存版本为 61.53 ms；完整步 P50 为 230.13 / 239.51 ms，原版本为 240.51 ms。5k 的 `CombatFire` P50 为 26.98 ms，原版本 29.09 ms，但完整步 112.15 ms 对比原 110.58 ms，显示总步噪声或其他阶段抵消。所有运行的战斗汇总仍分别为 10k 开火/伤害 34987、死亡 8743、剩余 1257；5k 开火/伤害 17501、死亡 4372、剩余 628。原始数据：`build/combat-phase*.{csv,err}`、`build/spatial-upsert-fast*.{csv,err}`。10k 完整步距 100 ms 仍很远，下一步应从 CrowdMotion、群组更新和其他 RTS 系统的累计成本入手，不能只并行化战斗查询。
