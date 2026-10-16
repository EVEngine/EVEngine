# Crowd 大规模战斗具体实现方案

日期：2026-10-13。状态：2026-10-14 本轮核心功能与规模测量已完成；实时性能仍有缺口，完整扩展设计继续实施。未在进度记录确认的类型、API、文件和测试仍为拟定名称。
范围依据：[功能范围与缺口](2026-10-13-crowd-large-battle-scope.md)。
本文记录核心交付与后续扩展；未落地接口仍为设计。2026-10-14 起以以下核心交付顺序为准，原技术章节作为实现参考，不再作为逐项前置清单。

后续实现状态与验证见[实现进度](2026-10-13-crowd-implementation-progress.md)；
本文中的拟定 API 只有在进度记录确认落地后才视为可用。

## 1. 已确定的实施选择

- 保留 Crowd 的紧凑 SOA，增加间接句柄表；不改成每代理对象或第二套 ECS。
- 第一阶段保留预测速度采样和共同子步；先优化身份、空间索引、出生与同步。
- 优先并行期望速度和预测速度，接触求解仍先串行；验证后并行接触候选计算。
- 共享场与最新地形快照分离，所有缓存有 revision；地图仍是权威 owner。
- Crowd 不读阵营、武器或 RTS 订单；战斗模块把规则转成移动意图和两套交互过滤。
- 正式批量接口为唯一 inward 实现，旧 slot/字符串接口是明确标注的兼容 facade。
- 不默认引入 ORCA，不默认启用 GPU 仿真，不默认降低近战解算精度。

## 2. 数据与身份

### 2.1 句柄表和 SOA

复用 `common/RuntimeHandle.h`：拟定 `AgentHandle = RuntimeHandle<CrowdAgentTag>`。
handle.index 指向 registry，不是 SOA dense slot。registry 项为 generation、denseIndex、occupied；
SOA 保存 denseToRegistry。swap-pop 时只更新被搬动项的 denseIndex，外部句柄不变。
删除先 bump generation，耗尽则永久 retire，不能 wrap；clear 不重置代次。

`AgentRef` 包含拥有者实例身份和 AgentHandle。跨域链接必须使用 AgentRef，
解析先检查 owner、再检查 generation；避免不同 Crowd 的同 index/generation 被误认。
owner 身份在构造时分配，进程内不复用；provider 重建产生新身份。
RTS 的 provider 引用还必须检查其有效期，不能为了验证 owner 而解引用已经销毁的对象。

命名代理在创建时解析稳定身份并赋予不可变数值 tieKey；邻居等距排序和精确重叠方向
使用 tieKey，不使用字符串或 dense slot。tieKey 持久化，保持恢复后的解算顺序。
命名 ID 查找只在绑定/恢复/脚本边界发生。匿名可持久代理在创建时获得 PersistentId；
历史稳定字符串保留为 LogicalId/兼容身份，不把它强行当成 UUID。

### 2.2 输入与输出

拟定类型：`AgentCreateBatch`、`AgentUpdateBatch`、`AgentRemovalBatch`、`AgentFrame`。
更新项携带 AgentRef 和强类型字段变更；不使用字符串字段名或可空 raw pointer。
创建与移除为原子批次；同一更新批次重复引用代理视为 Conflict，调用方先合并可替代的 setter。
创建、移除、传送和推进是独立操作，不能自动合并或丢弃。

核心 API 形状：

```cpp
// 拟定签名；结构化 Result、Doxygen 和 export 在实施时完整声明。
Result<CreateReceipt> createAgents(const AgentCreateBatch&);
Result<UpdateReceipt> updateAgents(const AgentUpdateBatch&);
Result<RemovalReceipt> removeAgents(const AgentRemovalBatch&);
Result<AgentState> readAgent(AgentRef) const;
Result<FrameCopyReceipt> copyAgentFrame(AgentFrame& destination) const;
Result<StepReport> advance(const CrowdStep&);
```

批次 owns 输入值；所有引用只在调用期间借用，调用后不保留 span。
输出复用调用方预留的 AgentFrame 存储，包含 tick/revision、ref、位置、速度、朝向和阻塞状态。
小状态读取直接返回拥有型值；不为了廉价 getter 添加 token。
不提供能跨推进存活的 SOA raw view；若以后提供借用读视图，类型必须编码 phase 生命周期。

既有便捷 setter 继续同线程直接更新期望参数，等值更新不重建派生数据；
批量 setter 使用同一个验证/应用内核。旧 add/remove/get(slot) 保留原有 slot 语义，
不擅自把返回整数换成 handle，也不继续用于新 RTS 热路径。

## 3. 推进管线与失败边界

`CrowdStep` 使用已有 `SimulationTick` 和时间类型，携带显式工作预算。
预期 tick 必须匹配；成功一次推进才递增 tick，拒绝不得消费时间或输入。
旧 `advance(float)` 经本地 tick 适配到同一内核，同一个实例不能混用两种时钟权威。

一个 tick 的顺序固定：

1. 验证 dt/tick、配置、句柄、场引用与预算；reserve 所需工作内存。
2. 捕获只读参数/位置快照，生成 work state；不复制字符串、静态场或全部配置。
3. 按共同子步构建邻域，算局部目标/流场对应的期望速度。
4. 基于共同快照求预测速度，执行速度、加速度限制并计算新位置/朝向。
5. 处理最新地形边界，再执行接触候选、修正归并、墙体约束的固定迭代。
6. 统计残余接触、搜索完整性、堵塞和预算质量，准备输出。
7. 一次发布运动列、时间、tick 和 report revision；RTS 随后批量同步 ECS。

使用可复用 working buffers，发布前失败丢弃 work state；不回滚外部 ECS。
worker 失败后 join 所有已提交任务，再返回失败，不能把仍在写入的缓冲发布。
时间、wander 状态、降频缓存与统计同样参与工作态提交，不能只回滚坐标。
失败 Result 只表示没有推进；成功报告另有 `SolveQuality`，可为完整、预测截断、接触未收敛等。

**预算区分**：固定 operation-count 决定可重复的解算行为；毫秒耗时仅用于测量，
不能按 wall clock 任意终止并改变游戏结果。预测搜索耗尽可显式接受质量降级；
硬接触候选枚举预算耗尽则拒绝整个 tick，不能把未检查代理当成无接触。
迭代已完整覆盖候选但未收敛时，可发布并报告残余；仍不承诺任意密度零重叠。
精确残余统计也消耗预算，不完整时报告 Unknown/Incomplete，不能给出虚假的零。

外部输入/出生批次的提交不属于 advance 的回滚范围；上层组合在进入 Crowd 前校验，
若跨 ECS/Crowd 创建需要原子性，使用 §7 的准备/提交协议。

### 转向契约修正

当前 `CrowdSimulation.cpp` 限制的是 heading 对速度方向的追随，速度本身可以更快改变方向。
普通步兵第一版保持此行为并明确命名为 heading turn rate。
需要运动方向转弯上限的类型引入显式 locomotion policy；避让候选必须在可达速度集合内选择，
不能先求无限制速度再旋转/裁剪却继续声称保留避让保证。

## 4. 两套交互过滤与战斗接入

拟定 `InteractionPolicy` 包含 avoidanceLayer/Mask、contactLayer/Mask、pushability、hold。
第一版两套过滤都沿用双方接受才成立的对称语义，保持可解释和稳定；
旧 layer/mask 同时映射两套。单向主动避让以后单独定义，不悄悄改旧语义。

友军：预测和接触均启用。敌军近战：接近期间由战斗策略选择预测范围/过滤，
身体接触始终启用；战斗模块用攻击槽给目标位置，Crowd 不选择目标。
仅关闭敌军预测可能导致大量接触，因此须结合接敌减速、队列位置和攻击槽准入。
地形墙体不受身体层过滤影响。hold 禁止主动移动和被代理推挤，不免除地形约束。
物理命中代理负责 raycast/弹道查询，不再重复解算普通士兵的身体相互挤压。

击退是显式外部运动命令，定义冲量/受限速度与优先级；传送是独立放置事务，
使用出生同一几何验证内核。死亡移除代理，尸体是否保留阻挡由战斗域另提交障碍表示。

## 5. 空间索引与有界工作

把现有按代理整体 AABB 分配的 dense grid 提取成内部 `CrowdSpatialIndex`。
第一步保留算法并加 counters；下一步采用固定尺寸 tile + 活跃 cell 列表，
按整数坐标排序形成稀疏目录，避免两群相隔很远导致遍历巨大空网格。
坐标转换使用经过范围校验的 int64，负坐标和大坐标都有测试。
桶内按 tieKey 排序，查询 cell 顺序固定，查询不依赖 unordered_map 的迭代顺序。

当前实施：已提取 CrowdSpatialIndex，以 occupied cell + 有序行目录代替整体 AABB dense grid。
身体尺度选择 cell 尺寸，signed int64 转换拒绝越界；查询用行/cell 二分查找，跳过空区域。
gridEntriesPeak/gridSortComparisons/spatialVisits 与原计数共同记录存储规模、排序和查询开销；
maxSpatialVisits 耗尽回滚整个推进，不遗漏接触。tile 分层、半径/速度分组目录和完整排序预算
仍待实现；当前改动不证明所有混合尺度场景达到所需性能。

预测先按半径/速度类别构建保守查询范围；各类保存最大半径/速度，跨类仍枚举。
分别统计 cellsVisited、candidatesVisited、neighborsKept 和 candidatePredictions。
搜索预算限定已访问 cell/代理，而不只是最近邻数量；越限标记查询不完整。
优先访问更近 cell 并记录覆盖范围，但截断最近邻选择不再宣称全局最近。

接触查询使用几何保守范围，不复用可能截断的预测邻居列表。
接触候选 pair 以有序 tieKey 对唯一标识，每轮按快照重新生成。
采用计数/前缀分配的有界 pair buffer；预估过量或枚举耗尽返回结构化诊断，
不追加无界内存、不跳过碰撞。排序/生成成本也纳入 report。
当前串行实现已统一为计数/前缀分配和稳定 pair key 顺序：maxContactPairs 控制活跃 buffer，
maxPairVisits 记录并限制 scratch 初始化、计数、前缀、填充、排序及 incident 归并；峰值与构建工作公开到 report。
候选生成期间位置保持只读，修正全部求和后统一应用。正反创建顺序的密集命名群组及恢复回放
有逐位一致回归。incident CSR 已实现：N+1 个 offset、2P 个按 canonical pair 顺序填充的索引；
pair delta 独占写入，每 agent 以相同浮点操作顺序归并。可选并行调度已接入接触 delta 与 agent 归并，固定 256 项逻辑 partition；串行走相同数学内核。
JobSystem 适配器在完整 join 后报告 worker 失败，advance 回滚。完整准备成本预算、降频与
完整性能场景仍待实施，不能将接触阶段证据作为完整并行阶段的完成证据。

## 6. 并行与降频

### 6.1 执行器

核心接口 `CrowdWorkScheduler` 已放在 `crowd/`，同步执行固定逻辑 partition 后 join。
`setWorkScheduler` 注入共享 lease，默认 Serial，缺失 provider 的 Parallel 选择明确拒绝。
已实现 `crowd-thread-core-only` 独立裁剪 profile；快照不持有 scheduler，恢复保留当前配置。
`crowd/thread/` 卫星用已有 `thread::JobSystem` 适配，模式参考 PixelWorld，
但不复制其隐式失败重跑策略：失败 join 后返回 Result，回滚 work state。
串行实现与并行实现执行相同 partition；scratch 按 partition 编号复用，
不能假设 JobSystem 暴露稳定 worker ID，也不能并发使用当前单一 avoidanceNeighbors。

机制四轴：一个注入调度器、高频调用、调用方已知、阶段屏障需要完成全部工作；
因此 composition root 注入接口，热路径直接调用，不每帧查 capability。
调度器以有生命周期保护的共享 lease 注入，Crowd 不拥有底层线程池；
其失效由适配器在开始调度前报告，所有在途调用完成前禁止释放 JobSystem。
串行为独立合法配置；并行请求无 provider 时显式拒绝，不默默退为串行。
实际 manifest 层级/group 由依赖图确认后声明，不在本设计臆定 layer 数字。

### 6.2 接触并行

当前期望方向、加速度/预测速度评分/朝向已走固定 partition，每项只写自己的列。
完整三力邻居 CSR 和 Flow 数值由 owner 准备，有界容量与工作配额耗尽整步拒绝。
预测邻居按 agent 独占，owner 在 dispatch 前精确预检全部评分预算，join 后统一交换速度列。
串行与并行共用同一采样数组和数学内核；全预测准备初始化 CPU 配额、tile 层与性能验收仍待完成。
下一批 pair 候选在只读快照上并行计算修正，按 pair key 排序。
为每 agent 建 incident-pair 索引，按固定 pair 顺序求和，每 agent 独立写 correction。
串行和并行走相同数学内核与相同归并顺序；没有浮点 atomic，也不按线程完成顺序加总。
一轮全部修正应用后统一重建索引与墙体处理。额外 pair/incident 内存有显式上限。

### 6.3 预测降频

设计 `Full` 与 `SparsePredictive` 两档起步；稀疏档只缓存避让建议，不缓存最终运动。
相位由 tieKey 分散，每隔固定 tick 重新求预测；每子步仍做积分、墙体和身体接触。
意图/速度类别变化、周围密度变化、危险相向接近、交战标记和边界 proximity 立即升级。
升档判断使用当前索引上的保守安全查询；本身工作量必须计入预算。
缓存建议失效时必须重算，不能把旧方向用于新目标；存档必须保存影响继续运行的降频相位/缓存，
或在保存与恢复两端都明确重置并只承诺容差恢复。第一版采用保存缓存状态以支持同构建精确续跑。

共同子步先保留。局部时间步/运动岛仅在混合速度基准证明收益后设计，
包含 swept AABB、跨岛接触、岛合并和同步时刻，不能先按类型拆开推进而漏交互。

## 7. 出生与 RTS 投影

### 7.1 局部出生事务

原 `SpawnBatch` 与三种策略继续作为唯一放置语义，内部换成 journal，不新增另一种廉价出生捷径。
先锁定调用期间的 Crowd revision 和最新障碍快照，再按空间查询扩展接触连通区域。
局部工作态包含新代理和被影响旧代理的位置；用 staged 索引查询每轮的新邻接，
新增受影响代理时扩展 journal，未动代理作为只读边界。预算耗尽即丢弃。
最后检查完整半径地形、接触、最大位移、容量、ID 和 journal revision。
预分配 registry/SOA/名字映射/receipt 所需内存后，进入不分配、不回调的提交阶段。
分配失败在提交前报告；不能先移动邻居再因名字映射分配失败返回。
极密集场景可能 journal 扩展全场，应如实记录 touchedAgents 而不是承诺局部固定成本。

跨 ECS 创建：RTS 先准备对象/武器但不发布，再准备放置计划；
两方都完成所有可失败的准备后，在同一 owner phase 无回调提交。
计划 token 绑定世界 revision，不能跨推进应用；任一失败释放暂存对象并保留生产任务可重试。
提交中不可避免的 ECS 分配须移到 prepare，若现有 ECS 无此能力，本 PR 保持既有失败清理契约，
不得声称跨域完全原子，也不得增设假回滚。当前代次分配不回滚的限制继续公开。

生产重试用确定性 tick backoff + 出口占用变化唤醒 + 轮转公平队列，
每 tick 限制准备/提交批数；不使用 wall clock。所有支付与任务结算沿原 RTS 权威路径。

### 7.2 RTS Motion bridge

`CrowdMotionSystem` 在绑定期建立 EntityHandle 到 AgentRef 映射，缓存上次发布参数，
在每 tick 收集 owned 数值输入并一次调用批更新；随后推进一次、批量取状态、同步 ECS。
当前每步 `setAgentPosition` 改为显式首次绑定/传送路径，普通帧不从 ECS 把旧坐标写回。
链接单位以 Crowd 为运动权威；RTS Motion 为已发布投影；未链接单位沿现有原生运动。
出生 displacement 同样用 receipt 的数值 ref 批量同步，不再反复查稳定字符串。

ECS 系统契约沿现有 `SystemContract` 登记：
类型闭包为 RTS Unit；View 复用现有 Identity/Motion/Navigation/Orders/Crowd/Combat/Containment/
Supply/Morale/Tactics/Command/Effects；读取订单、链接与速度修饰，写 Motion、Crowd 投影和到达状态。
Crowd 结构创建/移除在 View 收集完后执行；任何 ECS 结构变化推迟到专用生命周期 phase。
View 指针只在当前 owner 调用内使用，不交给 worker，不保存到下一帧。
服务为已绑定 Crowd 与导航 provider；Crowd 不向 RTS 发回调，后置事件在提交后发布。

销毁顺序：Unit 先销毁则生命周期队列删除代理；Crowd 先销毁则 provider lease 失效，
RTS 清除派生 ref 并报告 unavailable，按显式运动配置停住或转为原生路径。
不能悄悄切换。容纳移除代理，下车经放置事务重新绑定；恢复后旧 ref 一律失效。

## 8. 导航场的请求、缓存与失效

地图通过 adapter 提供不可变 `NavigationSurfaceSnapshot`：surface ID/revision、格子尺度、
原点、通行成本和按运动类别膨胀后的阻挡。adapter 在上层组合域，不让 Crowd include map。
最新 snapshot 的静态几何约束与流场 revision 分开使用。

拟定 `FlowFieldKey` 包含 surface identity/revision、排序去重目标集、movement class、
代价模型 revision 和构建算法 version；引用内容需校验，不能只用可能碰撞的 hash。
`requestFlowField(FlowRequest)` 返回 `FlowBuildToken`；
`pumpFlowBuilds(WorkBudget)` 按节点/边操作数推进；
`flowBuildStatus(token)` 为纯读取；`acquireFlowField(token)` 返回带代次 lease。
状态为 Pending/Ready/Unreachable/Cancelled/Failed，无返回 false 后再查 lastError 的协议。
这是一条唯一构建内核，旧同步 build 以兼容 facade 驱动同一请求直到结束，公开其成本。

地形改变使旧场 Stale，取消过期在建请求；新场完成后边界一次替换。
默认 stale 场不再作为有效导航，相关代理停止导航驱动且继续身体/墙体解算；
选择使用旧方向须显式策略并报告 stale，而且最新墙体仍约束运动。
不可达不能等同已到达；RTS 保持订单并记录导航诊断。
缓存按字节预算淘汰无 lease 的条目；使用确定性的引用/访问 tick 次序和稳定 key 打破平局。
运行时计时只影响诊断，不影响淘汰的逻辑次序。

P0 完成 revision 与失效，P1 才引入多场缓存，P2 扩展分块/分层构建。
第一版全量 Dijkstra，不在未验证时声称增量修改可以只更新局部而保持全局最优。

当前 RTS 已准入共享请求按不复用的准入序启动，并以固定每轮准入上限轮转图工作。
后来的低目标键不插队启动；持续加入的新请求不能使旧请求永远得不到下一轮 quantum。
OBJECT/SHARED 各 200 项组合回归覆盖该机制，包括每 tick 一个工作单位的持续请求变更。
请求元数据与构建工作态分开：最多 10000 个目标键、64 个保留工作态的构建器；ready 缓存最多 10000 项并继续受字节预算约束；超出构建容量的目标按准入顺序 Pending。缓存全部被 pin 时仍需消费方释放才能发布；完整准备成本预算
仍须继续实施，不把启动次数/heap-pop/gradient 配额等同于全部 CPU 成本预算。
单个导航服务实例内已经封闭一次 owner/tick pump 尝试：失败不退款，同 owner 输入修复
等待下一 tick；restore 新 owner 可以按新时间线尝试。准备尝试计入 starts，失败图 quantum
按预留上界记账并取消内部计划。原生输入故障与恢复回归见实现进度台账；不宣称覆盖全部内存故障。
RTS 生命周期内的 configure/clear 复用同一服务并保留消费边界，不通过重建服务退还配额；
普通配额调整保留计划，clear 释放计划后可重新启用。关闭状态不参与共享规划/组投影/运动，
保留的少量消费数据不要求 Crowd provider 存在。公开 RTS 的失败 step 重试回归覆盖上述行为。

## 9. RTS 群组与攻击槽

扩展现有 `MovementGroup`，不在 Crowd 建第二套群组：
route revision、anchor progress、布局参数、member order binding 和通道阶段。
Assemble/Cruise/Compress/Traverse/Reform/Arrive 描述行为阶段，由已有移动槽位、clearance、交通预约和到达状态派生；核心不另建持久阶段机。
通道内按稳定 slot rank 排列纵队，释放后有滞回地恢复原布局，避免每帧来回压缩。
追加命令与 AttackMove 组沿现有命令日志更新 schema，死亡/撤单/容纳释放成员。
通行预约增加等待 tick 与方向队列；占用者先安全出桥，再轮换方向，防止高优先级无限饥饿。
目的地区域按完整足迹准入，不对每个单位都发同一目标点。

### 9.1 编队共享导航的接入顺序与正确性边界

现有内部服务已经支持排序去重的槽位格目标集，单目标请求也走同一内核；这不是编队消费者
已完成的证据。多源 Dijkstra 求的是到任一槽位的最短路，不能证明可抵达原先分配的槽位。
禁止在抵达其他槽位后无条件切换直线 Seek；两点之间可能仍有阻挡。

第一步在 RTS MovementGroup 保留唯一成员/订单权威，在组阶段生成拥有型、不可变目标集。
成员使用派生组请求 handle，包含 owner、index、generation 和订单绑定，不能保存 group 指针
或 Crowd include。成员销毁、容纳、撤单、订单替换、restore 都使派生绑定失效；每次组阶段
从有效成员重建。先关闭 ECS View，再提交派生更新；请求准备失败不得发布半个组。
目标集转换和排序每组一次，单位热路径只读数值 token；工作预算涵盖准备与图构建。

第二步将共享场用于通道行进，维持稳定 slot rank 与原指定终点。在任一目标格附近进入
Reform，优先保留原槽位；只有半径扫掠检查通过时才允许直接驶向该槽。否则向同一预算服务
提交该单位指定槽位的单目标末段请求，Pending 时保留订单并等待，Unreachable 时显式报告。
末段请求可能达到成员数，必须遵守 starts/work/cache 配额并公开压力，不能绕过配额同步 A*。
后续槽位重分配属于独立优化，需要稳定排序、唯一预约和可达性验证，不能隐式交换终点语义。

开放区域继续消费经半径扫掠验证的移动槽位；受限路径解除整体速度锁，避免阻止先行单位出桥。
组阶段与导航阶段顺序必须显式登记：输入同步 → 导航请求 → 组投影 → 统一请求 pump →
Crowd 批量推进 → 到达/事件。进入新阶段后原 lease 在下一次批量更新释放；未知回调在提交后执行。

验收包含：目标集顺序/重复不影响键；精确终点不属于目标集时更新完全不变；不同单位到达
不同槽位；最近槽位不是指定槽位且两者之间隔墙；目标集存在断连分量；窄通道无整体停滞；
成员销毁/替换命令/restore 不访问旧 handle；末段请求预算压力恢复后无需重建已完成计划。
当前实施进度：受约束 Move / AttackMove 编队已接入共享槽位场，进入任一槽位格后保持单目标末段阶段；
开放区域继续用组级扫掠检查后的移动槽位。组派生表保存完整 ECS generation handle 与订单值，
每组一次规范化；运动调用返回拥有型不透明 Route，捕获 Crowd owner/input epoch，不保存组或
ECS 地址，不建立新的跨帧组身份。这里的 Route 是拥有型工作值，不是可持久化的 group handle。
全目标集相等才复用不可变键，末段状态仅在同成员/订单/输入键下保留。
OBJECT 组合 198 项通过，包含绕障到不同槽位、错误槽位末段、断连终点、重复成员拒绝、
订单替换与 entity slot 重用失效、Crowd 恢复旧 Route 失效。证据见实现进度台账。
这未完成整个阶段机、窄通道公平性、末段缓存压力专项、精确恢复或全准备成本预算验收。

攻击槽归 RTS/战斗域：首个核心实现为 Unit 对 Unit 近战，按完整目标 generation 每 tick 重建可达独占位置及等待带，一次完整准备后发布；不保存另一张预约表。远程武器与建筑目标保留既有策略。
槽半径由武器/单位足迹决定；近战前排准入，后排分配等待带；目标移动重新验证。
候选索敌复用 sensing 的候选 provider 和过滤管线，不把 Crowd 邻居列表当敌人列表。
Crowd 的位置快照可由上层 adapter 投影到 sensing 空间候选；不能建立第二套目标身份。
伤害、命中、技能和士气沿当前 combat/weapon/action 路径；本方案不替换那些权威系统。

## 10. 保存、脚本、表现与工具

拟定运行快照 schema `EVECROWD_RUNTIME` v1，与已有 CrowdDocument authoring schema 分离。
保存代理稳定 ID/tieKey、位置/速度/heading、意图、交互、配置、tick/时间、命名 RNG 状态、
降频缓存和逻辑场请求；不保存 owner/registry handle/dense slot、任务、指针或派生空间索引。
未知必需 version 拒绝，未知可选字段忽略并保留可观测诊断；迁移是显式纯数据转换。
恢复先验证全量引用与数值，暂存全部状态并准备导航/链接，再原子替换；
旧 handles 与 tokens 全部失效，generation allocator 不重置。
同构建精确续跑以相同地形 revision 和场数据可重建为前提；不满足则显式报告重导航，
不能把恢复后路径不同称为精确续跑。跨平台只承诺测试确认的容差。

脚本公开批量 Result 表与 owning handle 对象；owner/index/generation 分字段序列化到工具边界，
不把 uint64 packed handle 转成 float。现有 Binding Contracts 和手册同步更新。
运行时不增加每单位脚本回调；编辑器使用批量诊断快照和 renderer-neutral overlay。

表现适配只读前后两个已发布 AgentFrame 做插值，映射到 graphics/animation 现有实例与角色系统。
动画状态/武器挂点属于上层，Crowd 只提供 speed/heading/locomotion 与重要性提示。
先交付批量状态消费和现有实例路径的真实帧，再按独立图形方案引入 GPU 动画/共享和表现 LOD。
网络 adapter 消费稳定身份和已发布状态；复制频率不改变 server Crowd tick。

## 11. 修正后的核心交付顺序（2026-10-14）

核心目标是让现有 RTS/战斗模块通过 Crowd 完成大规模地面战斗移动。第一版使用平面圆形代理、CPU 仿真和已有预测采样器。现有句柄、批处理、共享场、空间索引与并行内核直接复用；基础设施不再独立扩展成新的先行项目。

### 交付一：贯通完整战斗

使用现有 RTS、地图、索敌、武器与伤害实现，建立可重复的两军场景。场景包含批量出生、编队行军、共享导航、障碍绕行、接敌、攻击和死亡清理。先跑通小规模完整闭环，再扩大至 1k/5k/10k。禁止以单向移动圆点代替战斗验收。

已有起点：显式构建目标 `eve_rts_battle_benchmark`（启用 crowd profile checks 且包含 RTS/host 的构建）。例如：

```powershell
cmd /c 'cmake\with-msvc.cmd cmake --build build/rts-crowd-shared --target eve_rts_battle_benchmark -j 4'
& build/rts-crowd-shared/eve_rts_battle_benchmark.exe 128 160
& build/rts-crowd-shared/eve_rts_battle_benchmark.exe 128 160 shared
& build/rts-crowd-shared/eve_rts_battle_benchmark.exe 64 240 corridor
```

runner 直接使用 RTS::stepScript 和规范化内容提供者，要求真实开火、伤害、死亡与单位清理。默认场景为开阔地两军 AttackMove；shared 启用现有共享导航；corridor 增加单格窄口与武器障碍阻挡。默认/shared/corridor、grouped/grouped-corridor 和 melee 均有运行证据；最终核心证据见进度文档 2026-10-14 收口。规模为无窗口实际战斗，图形为 128 单位引擎实帧；不宣称万级实时渲染。计时区分总 step、CrowdMotion（导航/准备/发布在内）、其子集 Crowd working、CombatFire（索敌/武器/伤害在内）与剩余 RTS；子集不能重复相加，未单独测到的细项不伪分配。

阶段证据必须记录实际调用路径、推进 tick 数、接敌/伤害/死亡结果、失败状态，以及移动与战斗耗时。图形验收使用引擎自身截图；原生无窗口 runner 可先验证仿真，但不能代表渲染完成。

### 交付二：补齐战斗中的移动行为

围绕同一场景补齐编队槽位、窄口压缩与通过后重组、目的地容量、攻击槽和后排等待。保留出生、避让、交通功能范围；逐项用可运行场景验收，不先构建通用交通平台。修复阻塞、互相躲避导致无法接敌、重复抢槽和订单失效等实际问题。

行为完成条件：军队能到达各自指定位置；窄口持续有进展；双方能接敌并攻击；死亡及撤单释放占用；未完成单位有可解释的等待或失败状态。

### 交付三：规模测量与必要优化

在同一场景测量 1k/5k/10k，分别报告移动、导航、索敌、武器/伤害、ECS 同步及渲染成本与运行配置。性能数字区分 Debug/Release、串行/并行和无窗口/实际渲染。

只有测量定位瓶颈后才增加预测降频、混合尺度索引或更细预算。保留身体接触和墙体约束；不能通过穿透、丢单位或停止战斗换取吞吐。30k/100k 仅用于扩展边界调查，不阻塞核心交付。

#### 已实现：空间格子复用战斗候选

RTS 已为 Sensing 开启 `SpatialHash2D`，现有 `CombatFireSystem` 的单位和建筑索敌都按圆形范围查询空间索引。Sensing 现按圆形查询覆盖的空间格子范围复用候选 ID，同一格子范围内不同圆心仍分别做精确圆形过滤、阵营/标签过滤和原有排序。缓存有条目数、单条候选数和总候选数上限；空间索引改变时失效，世界或索引身份改变时清空。缓存只保存派生 ID，不保存跨帧主体指针，也不改变 Sensing 的结果和调试查询语义。

战斗镜像仍是位置和标签的唯一事实；每个攻击者仍以自己的圆心、半径、阵营、标签及武器规则做精确过滤。命中、弹药、伤害、死亡、事件和订单变化继续由仿真 owner 线程按原 ECS View 顺序提交。此次没有并行化战斗结算，也没有新增第二套战斗查询 API；若继续分块计算读阶段，必须先单独计量镜像、候选收集、过滤/排序和结算，并证明提交顺序、连续击杀改选目标及显式攻击失败语义不变。

缓存验收覆盖同格范围不同圆心、跨格子/负坐标、位置与元数据更新、删除和世界移动；缓存世界与重新构建世界的结果、排名和调试信息一致。128/5k/10k 战斗的 160 tick 开火、伤害、死亡及最终单位数一致。5k 完整步前 20 tick 中位数 121.48 → 110.58 ms，10k 两组无构建干扰的配对中位数 271.56 → 230.67 ms 和 247.24 → 236.82 ms；详见进度文档。尚未单独量出候选阶段，不能把 `CombatFireSystem` 总时间全归因于查询；5k/10k 完整步均未达到 100 ms。

### 后续独立阶段

大世界分块/分层导航、完整存档迁移、网络复制/回放产品化、编辑器工具、动画共享/VAT、GPU 仿真、多层桥梁、车辆和飞行均保留为后续路线，不属于本轮核心完成条件。已有生命周期、Result 和失败原子性契约继续遵守；本轮不新增通用机制，除非核心场景暴露具体缺陷。

完整设计与核心交付分别记录状态。核心完成必须同时具有完整战斗、编队/交通行为和规模证据；局部测试数量不能替代这些证据。

## 12. 文件组织、契约与验证

拟定小 TU：`CrowdRegistry.cpp`、`CrowdBatch.cpp`、`CrowdSpatialIndex.cpp`、
`CrowdFlowCache.cpp`、`CrowdSnapshot.cpp`、`CrowdScheduler.cpp`；类型头只含本域类型与 common 基础。
现有 Simulation 按期望速度/积分/接触节拆开，纯移动先单独 PR，不夹入算法改动。
RTS 大文件的改动进入模块自己的运动/战斗专用 TU，不继续增长共享热点。
thread/graphics 等卫星放在 Crowd host 下，所有模块声明只经 manifest fragments 组合。

六面契约更新在 `scripts/architecture_contracts/crowd.json` 与 RTS/thread 等各自 shard：
capability in/out 为注入 scheduler/导航适配与输出适配的实际声明；
ECS/Link 为 AgentRef 生命周期和 RTS projection；事件仅为提交后诊断/领域通知；
script 为新批量/句柄/快照方法；tool protocol 沿既有工具，不另造绕过权限的控制通道。
无新增能力的面明确为空；禁止把规划中的接口登记成已落地证据。

规则适用：R-BOUND 的实际依赖与裁剪、R-FACE 的六面声明、R-MECH 的四轴与热路径、
R-COST 的批次/token/成本文档、状态 owner、Result、ECS scope、时间 RNG、持久格式和生命周期。
设计没有申请例外；实际实施有无法满足项时先补底层契约，不静默放宽。

每批新增独立模块测试文件并通过现有 test manifest 入口注册；
接口实施包括 Doxygen ownership/lifetime/thread/reentrancy 与 Windows export。
头布局改动触发所有相关 TU 重新编译，不能用旧对象的链接成功当证据。
运行 `make check`；构建 Windows SHARED 与 OBJECT 路线并跑相关 domain，完整跨平台套件交 CI。
crowd-only 与 crowd+thread 的 trimmed profile 需 compile/link/runtime 证据；
provider absent 的行为按配置验证，L4 的可观察输出另外记录，不用 include 图冒充裁剪证据。

下述为文档初稿时的验证记录；当前实施与各批构建/测试证据见
[实施进度](2026-10-13-crowd-implementation-progress.md)，不以初稿记录代表当前状态。
初稿验证仅为源码/规范核对和设计文档检查；所有 API/性能/并行与失败语义尚待上述测试证明。
两份文档的 UTF-8、代码块配对和尾随空白已核对；`git diff --check` 无错误。
`make check` 已尝试，但 PATH 缺少 clang-format，完整门禁未完成；未执行编译或运行时测试。


实施补充：RTS arrival idle 更新已迁移到 prepareAgentUpdates/commitAgentUpdates。
准备 token、selected frame 和零/一掩码在推进前完成；提交重新验证身份并解析 slot，不分配成功路径工作空间。
旧 updateAgents 为明确的 compatibility-only facade。成功 Status 不再构造 Debug STL diagnostics vector，
错误诊断为不可变共享值，公开查询形状不变。OBJECT 分配故障与零分配探针、SHARED 语义测试分别取证，
不能以 EXE allocator 替换声称覆盖 DLL。完整状态与日志见实施进度，整体实现仍未完成。
