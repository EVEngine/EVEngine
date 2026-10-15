# RTS：世界、阵营、命令与固定步进

**入口：** `eve.RTS()`，槽位 `rts`。RTS facade 组合领域服务并拥有 RTS ECS roots；
经济、群体运动、动作、战斗与生产仍各自拥有事实。渲染与 HUD 消费 inspectState，
不能另写一套位置或资源来绕过 RTS 命令。

## 任务一：建立最小阵营世界

以下可作为完整 main.nut，配置包含 `modules=["gfx","rts"]`。
它验证创建、步进和迷雾查询；完整可玩的单位/建造界面见下一节。

```squirrel
function rts_value(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
local simulation = null;
local faction = null;
local accumulated = 0.0;
eve_init = function() {
    simulation = eve.RTS();
    rts_value(simulation.configureScriptWorld(4, 4, 1.0, 0.0, 0.0));
    faction = rts_value(simulation.newFaction("00000000-0000-7000-8000-00000000fc01"));
    rts_value(simulation.scriptCellExplored(faction, 0, 0));
    rts_value(simulation.scriptCellVisible(faction, 0, 0));
};
eve_update = function(dt) {
    accumulated += dt;
    local steps = 0;
    while (accumulated >= 1.0 / 30.0 && steps < 8) {
        rts_value(simulation.stepScript(1.0 / 30.0));
        accumulated -= 1.0 / 30.0;
        ++steps;
    }
};
eve_render = function() { gfx.clear(); };
```

此处补步上限为每帧 8 次，保留积压时间；持续过载时要明确游戏自己的暂停/追赶策略。
不能为了追赶无限推进一帧，也不能声称不同 dt 下可变步进天然一致。
RTS 的持久主体 ID 是 UUID；例子的值用于独立演示，项目要给每个主体不同 ID。

## 任务二：生产单位并命令移动

运行 `eve run examples/rts-sandbox`。其 [config.nut](../../../examples/rts-sandbox/config.nut)
列出组合模块；[main.nut](../../../examples/rts-sandbox/main.nut) 的 resetGame 依次：

1. loadScriptContent 读取 [content.json](../../../examples/rts-sandbox/data/content.json)，检查 Result。
2. configureScriptWorld 创建网格，newFaction 建立阵营，addScriptResource 发放初始资源。
3. 配置导航障碍/成本，newBuilding/newResourceNode/newUnit 使用唯一主体 UUID 与
   `building:<id>`、`unit:<id>` 的内容逻辑 ID。
4. 玩家操作选择单位并调用普通命令；stepScript 推进真实生产和单位运动。
5. inspectState 生成 HUD/渲染投影，坐标通过游戏自己的 world→screen 映射。

不要直接在 inspectState 返回表上改单位位置来发命令，它是观察结果。生产队列扣款、
完成、取消与失败按 RTS/Production 域接口处理；不能又手动从 Economy 扣同一笔资源。
测试 [rts_script.cpp](../../../test/rts_script.cpp) 覆盖支付后产出和帧回调身份，
[rts_crowd_production.cpp](../../../test/rts_crowd_production.cpp) 验证组合。

已绑定 Crowd 的单位位置由 Crowd 拥有，ECS Motion 是观察投影。需要瞬移、移开生产出口的
坚守单位或进行其他显式放置时，使用 `relocateUnits([{subject=uuid,x=20.0,y=10.0}], maxChecks)`，
不要修改观察表或反向覆写 ECS 坐标。C++ 使用 UnitRelocationBatch；全批原子验证，
拒绝重复/失效主体、容纳单位、缺失投影、地形或身体重叠及工作预算耗尽。
成功保留原订单并清除派生导航路线；Crowd 身份保持不变，运动速度归零。
该接口为精确放置，不隐式挑选附近位置。完整语义见 [Crowd](crowd.md)。

## 重载、保存与失败

RTS facade 拥有当前 generation 的实体 roots。rts-sandbox 重载时显式重建 simulation，
不把 `persist sim` 当作所有原生实体可跨 generation 存活的保证；选择集合、UI 等投影
也需要按新主体重新连接。需要保留比赛时走该域快照/恢复契约，并验证 UUID 链接；
通用引擎调试 Snapshot 不等于玩家存档。

newFaction/newUnit 等返回 Result，既要检查 ok，也要只在成功后保存 value。内容缺失、
重复 UUID、失效阵营、无路可达、资源不足应显示领域拒绝原因。检查失败操作前后权威
状态不变，按同样命令序列测试冷启动、重载、恢复和完成生产。
源码：[RTS.cpp](../../../src/modules/rts/RTS.cpp)，完整运行示例与断言是上面的 sandbox
和 [rts_script_2.cpp](../../../test/rts_script_2.cpp)。

## 补充绑定参考

参数与接收者见 [源码生成参考](rts/api-reference.md)，使用前先确认本章的前置条件与生命周期。
# Crowd 仿真时钟

挂接 Crowd 后，RTS 传递同一 `SimulationStep.tick` 和整数 `Duration`，不再转换成 float 时长。
独立的新世界从 tick 1 开始正时长推进，之后连续递增；零时长仅同步当前 tick。
不要单独推进已挂接的 Crowd，或混用其兼容 float 时钟。
RTS 整体步进和独立 CrowdMotionSystem 都在更新输入前检查时钟；重复、跳过或混用时钟返回结构化失败。
这项准入检查不预留 tick，也不保证整个 RTS 多系统步进成为原子事务。
provider 由游戏拥有；RTS 使用不可保活的 lifetime witness 检测其销毁。
失效后清除派生 AgentRef 并返回明确失败；解除挂接或重新绑定由调用者显式执行。
双方都只在同一 owner thread 使用，witness 不授权并发销毁。


### 自动发布 Crowd 导航表面（原生）

游戏先挂接 `setNavigationProvider` 和 `setCrowdProvider`，再调用
`configureCrowdSurfaceTracking`。配置包含强类型 surface/movement 身份、运动类别与代价模型版本、
clearanceRadius，以及 maxInflationChecks。配置方法仅保存拥有型值，不复制地图；RTS::step 在任何
ECS 系统执行前检查地图变化并完成 Crowd 的 prepare/commit 发布。

未变化帧只比较 Pathfinder 的 O(1) NavigationChangeStamp 和已发布来源；不会复制整张地图。
设置相同代价等产生的变更提示仍会捕获一次，但内容版本相同则不重新投影。地图内容、provider
重绑、世界网格转换或显式类别配置变化都会触发新投影。Hex、方向通行门、低于 1 的正代价、
非法几何和 clearance 预算耗尽均返回失败，保留 Crowd 表面和仿真 tick，允许同 tick 修正后重试。

该跟踪器拥有当前单类别表面的投影配置；启用期间不要把 Crowd 地形当作第二份地图权威。
原有 Crowd 目标在同几何地图编辑后保留但变为 stale；通过导航请求重新建场，跟踪器不隐式同步
执行完整 Dijkstra。`clearCrowdSurfaceTracking` 停止自动跟踪并保留最后发布的表面。
Pathfinder 与它所绑定的 TileLayer 必须活到解除导航挂接；Crowd 的销毁由 lifetime witness 检测。
配置、缓存与运行时 token 不属于 RTS 持久化格式，重建/热重载时重新配置。


脚本世界同样可以启用跟踪：

```squirrel
assert(sim.configureScriptWorld(32,32,1.0,0.0,0.0).ok);
assert(sim.configureCrowdSurfaceTracking({surfaceId=1, movementId=1,
    movementRevision=1, costModelRevision=1, clearanceRadius=0.5,
    maxInflationChecks=1000000}).ok);
assert(sim.stepScript(0.01).ok);
```

身份与版本输入必须是正的精确 int64，maxInflationChecks 为精确整数；缺失必要字段、浮点身份、
非法半径与预算均返回失败并保留旧配置。额外字段忽略。clearCrowdSurfaceTracking() 停止跟踪。


### 共享静态目标 Flow 消费者

挂接 Crowd 并发布 imported movement projection 后，configureCrowdFlowNavigation({maxWorkPerTick,
maxStartsPerTick}) 可显式启用。脚本同名方法接受精确整数：work 为 1..1000000，starts 为 1..16。
当前 policy 覆盖有 Crowd 链接、没有 formationTarget/trafficRecoveryTarget 的 Move/AttackMove/Attack/
Escort/Patrol；其他订单沿原生导航，带威胁代价的补给路径迁移仍待完成。目标按 Crowd 表面包含格（floor）转换，
游戏应提供半径有效的 world 目标点。进入目标格后采用局部 Seek 完成精确位置到达。

eligible 单位跳过各自 A*，先收集数值目标，再按稳定准入序开始准备，并按准入序号轮转图工作；新低键请求不插队。
每 positive tick 最多开始指定准备数、最多消费指定 heap-pop/gradient work；同 tick 的零时长
生命周期同步不额外推进。相同目标格复用一个请求/lease，输入/owner 变化会重建派生作业。
同一导航服务实例的每个 owner/tick 只允许一次正时长 pump 尝试；失败也关闭该 tick 的配额。同 owner 下修复输入
不返还配额，Pending 请求等下一 tick；恢复生成新 owner 时属于新时间线。lastStarts 包含失败的
准备尝试，lastWork 对失败图工作按预留 quantum 上界记账；失败的内部计划取消，不能继续发布。
RTS 生命周期内只创建一次导航服务。configure 调整配额保留未完成计划和消费边界，等值配置
返回 NoOp；clear 关闭策略并释放作业，仍保留消费边界，重新启用也不能返还同 tick 配额。
关闭状态的 status 返回空观测；重新启用后累计计数继续，限额变更用于下一次允许的尝试。
这仍不是跨多个协调器的全局 CPU 预算。
受约束 movement group 在组阶段一次准备排序去重的槽位目标集，成员共用场；进入任一槽位格后
持续使用自己指定槽位的单目标末段请求，防止零梯度停住或错误到达。单目标末段也遵守相同
starts/work/cache 配额，未完成时等待；没有同步 A* 旁路。开放区域仍使用半径扫掠验证的移动槽位。
成员句柄代次、订单绑定和 Crowd owner/input epoch 控制派生复用；组投影替换失败保留旧投影。
当前容量为 64 个组、每组最多 1024 成员/目标；服务接受最多 10000 个目标键，并限制为 64 个同时保留工作态的构建器。更多目标按原准入顺序等待，表现为 Pending，不因超过 64 个目标拒绝整个 tick。超过请求队列上限仍显式失败；缓存被 lease 全部占用时保持 Pending 并报告 cachePressure。
末段阶段在恢复时重建，不承诺恢复前后逐位相同轨迹；完整编队阶段机与预约公平性仍待实现。
Pending、Unreachable 和类别/半径不支持均停止该导航驱动，保持原订单且 arrived=false；
最新墙体/身体约束继续执行。缓存压力可观察并保留已完成计划等待重试，不静默超预算。

getCrowdFlowNavigationStatus 返回 pendingGoals/readyGoals、lastWork/lastStarts、prepared/cacheHits
（字符串计数）和 cachePressure。clearCrowdFlowNavigation 释放派生工作，并在下一 motion sync
解除系统范围内代理 lease 后恢复显式兼容策略。配置与作业不属于持久化格式，重载后重新配置。
本路径尚未提供完整跨域 step 原子性；输入变更时解除旧派生 lease 后，后续准备可能失败。


共享场不可达通知继续使用 NavigationEvent，在整个运动投影成功后通过拥有型 notice 与
代次 handle 派发；同一订单只通知一次，地形恢复后清除诊断。事件回调可改变/销毁单位，
后续通知逐项重查句柄；不得重入 step 或销毁当前 RTS。准备/推进失败不发布本阶段 notice。
FieldObservation.owner 是进程内拥有者 epoch；恢复更新它，用于识别同地址 Crowd 的新运行态。


共享策略现已覆盖无 formation/recovery slot 的 Attack、Escort、Patrol：攻击目标使用当前实体
投影位置，护送使用已计算 guard 点，巡逻使用当前端点。导航、共享请求与到达判断复用同一
movementGoal 解析，不持有实体指针。目标在同格移动保留 generation，跨格请求新场，仍受
tick work/start 配额约束。SupplyMission 保留可见威胁代价覆盖的原生路径，尚不迁移到基础场。


原生 composition root 可先为外部 Crowd 注入 `CrowdWorkScheduler`，再作为 RTS provider 使用。
自包含脚本世界在 `configureScriptWorld` 后，可由原生调用方使用
`configureScriptCrowdExecution(CrowdExecution::Parallel, scheduler)` 为其内部 Crowd 注入同一个调度器；
该接口不创建线程池，调用方须持有 JobSystem 直到所有步完成。缺少 provider 或调度器时返回结构化失败。
串行模式仍可独立运行；选择模式仅作用于运行时，不进入快照。
RTS motion 仍只在 owner 线程收集 ECS 数值输入，Crowd 内部并行完成后统一写回位姿。
输出 frame 容量与拥有型订单投影在 advance 前准备，pending Flow 不在失败前修改 arrived；
所有位姿先提交，再判断观察其他单位的动态目标到达，避免依赖 ECS 遍历顺序。
worker/provider 失败沿 Result 返回，不提交 motion，并保留同 tick 重试入口。


RTS 也在 Crowd 推进前准备所有潜在 arrival idle 更新及选择掩码，推进后只提交实际到达的项。
成功提交不分配验证/输出工作空间，避免在 pose 已提交后因 arrival 更新批次分配失败而返回失败。


### 编队 AttackMove

原生 MovementGroupBatch.kind 支持 Move（默认）和 AttackMove，其余类型在订单修改前拒绝。最多 64 个组，每组 2..1024 单位。attackMoveGroupUnits 与 moveGroupUnits 参数一致，前者提交 AttackMove。单位进入 engagementRange 后停止自身行军并攻击，不让尚在行军的后排因编队 pacing 一同停下；双方身体接触仍由 Crowd 解算。

AttackMove 编队可消费共同槽位目标集的导航场，终点仍绑定各自订单。成员死亡、容纳或订单替换按既有 generation/epoch 规则退出。原生 RTSStateSnapshot v2 已保存完整订单 kind 和成员 order id；恢复支持两种编队订单，派生导航与 pacing 重建。没有新增持久化字段。

EVERTS_COMMANDS v6 使用既有 command kind 字段支持 AttackMove 编队；旧 v1-v5 输入照常读取，v5 中的 AttackMove 编队组合显式拒绝且不部分导入。纯 Move 编队继续导出 v5。原生线程契约仍为 owner-thread、非重入、无回调；批次只借用到调用结束，RTS 保留 generation handles 和订单身份。


NavigationGrid 的 origin 是格子 (0,0) 的世界坐标中心。RTS 到 Crowd 的适配会减去半格，转换为 CrowdField 的格子左上角原点；脚本地形和射线阻挡使用同一中心约定。

无逐格路径点的共享流场成员在窄口前预占入口，赢家使用格子中心引导通过；交通预约尚未释放时，组仍解除整体速度锁，避免先行单位停在出口。所有存活成员到达各自指定终点后，移动编队解散；AttackMove 订单继续保留接敌姿态。战斗射线被静态障碍挡住时，本 tick 的有效 engagementRange 归零，避免成员因看不见的目标停在墙外；武器定义的射程不变，下一 tick 重新验证。


### 单位近战攻击位置与后排等待

RTS 在两个移动后端之前，根据当前 Attack / AttackMove、有效战斗目标的完整 generation handle、单位圆形足迹和有效武器射程准备每 tick 有效的位置计划。首个支持范围是 Unit 对 Unit 的近战：有效射程不超过双方半径和的两倍，且已接近目标。远程武器和建筑目标继续使用现有武器接敌策略。

每个目标最多考察 64 个前排位置，圆周容量取决于最大参战半径；距离近者优先，距离相同按 SubjectRef 的 UUID 字节排序。每人选择最近的未占位置，静态地图半径扫掠检查不通过时进入后排等待带。后排保持实体接触并继续让位，CombatFireSystem 在等待准入时不消耗弹药或施加伤害。单位原订单和目标身份保持权威，位置投影不完成 Attack 订单。

Navigation 的 combatSlotTarget / combatSlotWaiting 是一次完整准备后发布的派生数据，每 tick 重建，不进入保存。目标死亡、销毁、同 ID 重建、换单、容纳和 restore 都无需修复第二套预约表。inspectState 的单位结果公开 combatSlotActive / combatSlotWaiting。RTS 在 owner thread 运行，无新增回调、锁、RNG 或持久化 schema。


大编队槽位准入额外比较 formation-local 空间顺序候选与原有贪心候选，再做有界距离优化。总路程相等时，以平方路程平衡作为次序；已有平移网格不因任意横排交叉生成长短不一的路线。受约束组中某个成员到原指定位置的完整半径扫掠已清晰、且未持有通行预约时，可走直接末段；其他成员仍保留共享场/预算，不按“碰到任意槽位”无条件切 Seek。

ScriptWorld 初始化启用 SensingWorld 现有空间索引（格宽为八倍导航格），仍由原 QuerySpec 过滤/排序。没有新建敌人身份表或使用 Crowd 邻居列表代替感知。外部注入的 SensingWorld 保留调用者配置。


### 2026-10-14 订单与感知热路径

`OrderComponent::current()` 缓存不变订单的派生投影，避免每个系统反复序列化和解析 JSON；通用队列及其 payload 仍是权威。当前 id 改变时重建，清空和恢复时失效，缓存不进入快照。私有组合接口交出可变队列后永久关闭该实例的投影缓存，避免借用者后续修改造成陈旧结果。接口仍返回独立拥有的 `OrderRecord`，不向调用者借出缓存指针。

感知空间索引的数字 id 直接指向 SensingWorld 自己拥有的稳定 map 节点；upsert 不改变节点地址，删除先撤销索引，再删除节点，恢复重建索引。没有保存来自外部提供者的临时指针。战争迷雾按完整代次索引 revealers；雷达和干扰只扫描具有相应能力的 source。Heightmap 仅在全图无遮挡且最大高度低于所有观察者的最低阻挡阈值时，省去逐格高度射线；每次 dirty compute 重算证明，墙体、高度和模式变化继续走完整计算。


同一帧的常规无乘员伤亡按完整代次批量清理引用和 roster，避免每个死者再扫描所有单位；先准备并验证 Crowd 数值移除，再更新所有关系，最后按原根顺序释放 sensing、weapon 和 entity。带乘员或建筑递归仍按既有生命周期执行。镜像保留稳定 Sensing 节点，用完整当前 roster 线性对账，只删除已离开的主体。
