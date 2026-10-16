# Crowd 大规模战斗功能范围与实现路线

日期：2026-10-13。状态：基于当前源码和公开一手资料的范围建议；性能目标待基准验证。
本次只新增设计文档，不变更运行时接口、模块依赖或持久格式。

后续设计：[具体实现方案](2026-10-13-crowd-large-battle-implementation.md)。

> 2026-10-14 执行修正：本文件保留完整功能路线；本轮交付聚焦两军完整战斗、编队/交通行为与 1k/5k/10k 测量。保存、网络、编辑器、大世界和高级表现为后续独立阶段，不再作为核心前置条件。具体顺序以[实施方案第 11 节](2026-10-13-crowd-large-battle-implementation.md#11-修正后的核心交付顺序2026-10-14)为准。

## 1. 定位与结论

Crowd 应成为**可预算、可观测的海量地面单位连续移动与拥挤解算基础**。
它支持 RTS 军队、割草游戏敌群、城镇人群等调用方，但不拥有战斗规则。
首个完整支持面为平面圆形代理；地形高度可由适配器投影，但多层导航、飞行和车辆运动学不纳入第一阶段。

大规模战斗由四个协作部分组成：

1. 导航提供路线、可达性、通道和共享方向场。
2. Crowd 计算期望速度、预测避让、运动限制、接触与边界修正。
3. RTS/战斗领域决定命令、编队、阵营、攻击槽、包围、撤退、伤害和死亡。
4. 图形/动画领域消费批量状态，执行实例化、动画共享/烘焙、剔除和表现 LOD。

规模化路线是 CPU 数据导向仿真与并行、共享导航、显式预算，再配合 GPU 批量表现。
GPU 权威仿真属于后续实验方向；先测量 CPU 阶段、同步和渲染各自成本，再决定是否值得迁移。

## 2. 外部实现对比与可借鉴点

### UE5：Mass 的分层比某个避让算法更值得借鉴

[MassGameplay 官方说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-gameplay-in-unreal-engine)
把表示、生成、仿真 LOD 和复制 LOD 分开；表示可在 Actor、ISM 和无表示之间切换。
仿真 LOD 可按 chunk 组织并改变更新频率。应借鉴预算和表示独立的设计，
而不是为每个普通士兵保留完整 Actor 生命周期。网页标注 ISM 动画支持仍有实验性限制。

[Mass Avoidance](https://dev.epicgames.com/documentation/unreal-engine/mass-avoidance-overview-in-unreal-engine?lang=en-US)
是力驱动的实体避让系统，输入包含邻居、导航边缘和移动目标；并非 ORCA。
资料将其标为实验功能，因此不能把使用 Mass 自动等同于完整战斗移动或无死锁保证。

[Animation Budget Allocator](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine)
按 CPU 时间预算降低骨骼更新频率，并选择是否插值；
[Animation Sharing](https://dev.epicgames.com/documentation/unreal-engine/animation-sharing-plugin-in-unreal-engine?lang=en-US)
提供动画共享机制。EVEngine 应把这些能力放在动画/图形域，Crowd 只输出运动状态与重要性输入。

### Unity：Entities/Jobs/Burst 与 Entities Graphics 是基础设施

[Entities 官方文档](https://docs.unity3d.com/Packages/com.unity.entities@1.3/manual/index.html)
及[官方 ECS 示例](https://github.com/Unity-Technologies/EntityComponentSystemSamples)
提供数据导向实体和任务处理的参考；
[Entities Graphics](https://docs.unity3d.com/cn/2023.2/Manual/com.unity.entities.graphics.html)
支持实例化网格和 LOD。它们需要导航、AI、动画和战斗逻辑的组合，不能单独视为军团战斗系统。
EVEngine 已有 ECS 与 SOA，应复用并完善批处理，避免再建立一套通用实体框架。

[Megacity Metro](https://github.com/Unity-Technologies/megacity-metro)
适合参考实体与网络协作；它是多人射击示例，不是万级士兵战斗基准，
其玩家数不能换算成 Crowd 的单位容量或性能。

### 导航与避让项目/插件

- [A* Pathfinding Project Pro：Local Avoidance](https://arongranberg.com/astar/documentation/stable/localavoidance.html)
  采用 ORCA，并把核心仿真与 Unity 对象包装分离；
  [FollowerEntity](https://arongranberg.com/astar/documentation/stable/followerentity.html)
  可使用 ECS 避让组件。可借鉴期望速度输入、邻居上限、优先级和导航约束的组合。
  文档明确区分局部避让与全局寻路；快速改变速度的假设也不适合直接套到车辆。
- [Recast/Detour](https://github.com/recastnavigation/recastnavigation)
  分离网格生成、运行时查询、TileCache 与 DetourCrowd。
  [DetourCrowd 源码](https://github.com/recastnavigation/recastnavigation/blob/main/DetourCrowd/Source/DetourCrowd.cpp)
  可参考路径请求调度、走廊维护和速度采样；它不是开箱即用的万级战斗方案。
- [RVO2 作者项目](https://gamma.cs.unc.edu/RVO2/)与[代码](https://github.com/snape/RVO2)
  提供 ORCA 与 OpenMP 并行的参考。数学保证依赖其运动和环境假设；
  接入加速度、转向限制、邻居截断和离散地形后，必须重新验证保证适用范围。
  它不替调用方分配目的地容量、攻击槽或窄口通行顺序。
- [Mass Community Sample](https://github.com/Megafunk/MassSample)
  适合参考 query 访问声明、processor、延迟结构变更和并行组织，
  属于社区学习项目，不能作为完整商业战斗能力证明。

### 海量角色表现项目/插件

- [GPU Instancer Crowd Animations](https://wiki.gurbu.com/index.php?title=GPU_Instancer:CrowdAnimations)
  提供批量动画表现参考；[厂商 FAQ](https://wiki.gurbu.com/index.php?title=GPU_Instancer:FAQ)
  说明间接实例化与 compute 路径及平台/着色器限制。它解决表现成本，不能证明战斗逻辑吞吐。
- [Unity Animation Instancing](https://github.com/Unity-Technologies/Animation-Instancing)
  是骨骼角色实例化技术参考；导入前需验证目标后端、动画混合、武器挂点、阴影和受击表现，
  不直接作为 EVEngine 的运行时依赖。

以上只核对公开文档和部分公开源码，没有安装商业插件或运行跨引擎同场景基准。
网页 stable/current 路径可能滚动；实际移植必须固定提交/版本并复核许可证。
本方案不使用厂商演示人数作跨引擎性能排名。

## 3. 当前仓库已具备什么

核对入口：`src/modules/crowd/`、`src/modules/rts/RTSCrowdMotion.cpp`、
`docs/usr/modules/crowd.md`、`test/crowd*.cpp`、`test/rts_crowd*.cpp`。

- `CrowdInternal.h`：紧凑 SOA、swap-pop、命名身份映射、计数排序空间网格和复用工作缓冲。
- `CrowdField`：八邻域 Dijkstra 积分场、连续梯度方向、双线性采样和多个目标。
  一个多目标场表示流向代价最小的目标，不代表每组各自独立的导航场。
- `CrowdAvoidance.cpp`：预测速度采样、加速度约束、优先级和邻居截断。
  当前是启发式采样，不是 ORCA；独立 Crowd 默认未启用预测。
- `CrowdStep.cpp`：子步推进、最多十二轮接触修正、墙体处理；报告残余接触、穿入量与预测工作量。
- `CrowdInteraction.cpp`：坚守、可推挤程度、双向接受的交互层过滤。
- `CrowdSpawn.cpp`：拒绝、附近可用点、推挤邻居三种原子出生策略，显式计算/位移预算。
- RTS：订单投影、出生重试、容纳/释放链接、静态编队槽位、移动组节奏和移动锚点；
  窄通道预约与退让恢复已有基础。现有文档明确移动组仍需扩展有序列队压缩、
  共用路线转弯槽位、追加组命令和 AttackMove 组。

现有 `crowd.sim.massiveAgentsSmoke` 只证明大量代理的短程有效状态，
没有完整战斗、持续密集拥堵、帧时间分位数和动画渲染证据。
`maxAgents=100000` 是容量设置，不能视为十万战斗单位的实时性能承诺。

## 4. 功能范围

### Crowd 核心必须完成

1. **身份与批处理**：数值型带代次运行句柄；稳定身份在保存/恢复边界解析。
   批量创建、移除、目标/运动策略更新和拥有型批量输出；每步统一提交结构变化。
   消除 RTS 热路径重复字符串解析，但保留现有便捷接口作为兼容入口。
2. **导航消费与共享场**：显式期望速度/局部目标输入；共享场按目标集合、地形 revision、
   半径/通行类别建立身份与缓存。构建/更新用请求 token 和工作预算，旧 revision 可观测。
   CrowdField 保持模块内数学工具；地图权威、全局路径与 NavMesh 烘焙留在导航/地图域。
3. **局部避让与接触**：可观测预测邻域、速度/转向约束、静止障碍和边界；
   保留采样器为基线，用 ORCA 对照测试决定是否替换。不得仅因名称更先进就切换算法。
4. **战斗移动策略参数**：分开声明预测避让关系与身体接触关系。
   当前单套 layer/mask 同时过滤预测和接触，不足以表达“双方接敌，但身体不能穿透”。
   暴露通用交互参数；阵营敌友判断、攻击距离与攻击槽由战斗域转换成参数。
   接敌停止、排队包围由调用方提供局部目标；Crowd 报告拥挤/阻塞，不自行换敌人。
5. **密度、阻塞与预算**：密度/占用查询、持续无进展诊断、预测截断、接触迭代耗尽、
   出生拒绝原因和阶段耗时。区分输入拒绝与成功推进后的解算残余。
   超限策略由显式配置决定，不能悄悄穿墙、取消身体碰撞或丢单位。
6. **并行执行与仿真重要性**：先以共同只读快照并行求速度，接触修正采用确定性归并/分区方案；
   工作缓冲按 worker 隔离。远端稀疏单位可降低预测频率，近战/高速/密集边界立即升级。
   降低预测频率不等于暂停接触/墙体约束，也不能只根据摄像机判断服务器仿真精度。
7. **生命周期与恢复**：配置、代理状态、目标和命名 RNG 流的版本化保存；
   导航场、空间网格、跨域链接作为派生数据重建；失败恢复不发布半状态。

### 协作范围必须有生产接入，但权威状态属于其他模块

- **RTS**：行进编队、槽位分配、窄口队列、通行公平性/防饥饿、退让恢复、
  AttackMove/追加组订单、目的地区域容量和整组到达。
- **战斗领域**：阵营、索敌/目标缓存、近战攻击槽、远程射线/弹道、AOE 查询、
  伤害、技能、士气、尸体阻挡规则。普通士兵不强制配完整刚体；身体移动由 Crowd 解算，
  命中查询由战斗/物理域使用独立表示，避免两套位置解算互相覆盖。
- **图形/动画**：批量实例数据、GPU skinning/VAT、动画共享、可见性/阴影 LOD、
  近处英雄精细动画及武器挂点。渲染插值只写显示位置，不回写 Crowd 权威状态。
- **网络/回放**：外部固定 tick 驱动和输入日志；复制重要性与带宽预算由网络域管理。
  第一版承诺同构建固定输入可重复；跨平台逐位确定性未验证，不能默认宣传锁步支持。
- **editing/editor**：场/密度/速度/接触/堵塞/预算可视化，压力场景与运行配置制作。
  放在既有 `crowd/editing` 和 `crowd/editor` 卫星，runtime 不依赖编辑器。

### 后续扩展范围

分块/分层导航、NavMesh corridor 适配、多层桥梁、非圆形足迹、车辆非完整约束、
空中三维避让，以及 GPU 仿真。分别做最小场景和共享契约验证后引入，
不通过默认参数把便宜平面模式与昂贵通用模式揉成同一个隐含成本接口。

## 5. 当前代码的优先性能调查

以下是源码结构显示的风险，不是已经测量的瓶颈排序：

- 子步长度使用全体最小半径和最大速度；单个极小/高速代理可能增加全场子步数。
  先测混合单位，再考虑独立运动类别/岛与边界交互，不可直接各跑各的而漏掉跨类接触。
- 每轮接触迭代重建空间网格；需要记录各轮收敛与 grid-build 成本。
- 避让查询半径受全局最大速度/半径影响；`maxNeighbors` 只限制保留邻居和预测候选，
  不限制空间搜索遇到的代理数。必须新增 visited-candidate 统计和可观测搜索预算。
- 邻居等距排序比较稳定字符串；RTS 逐单位每步重复解析命名身份并设置状态。
  优先验证数值句柄、批量投影、只更新变化参数的收益。
- 出生事务复制全体 Crowd/场；推挤循环存在全体代理两两检查路径。
  大量生产建筑每 tick 重试可能放大成本；需空间查询、批处理、重试调度与局部事务，
  优化仍必须保持失败原子性。
- `CrowdField::isBuilt` 不表达地图修改后的 revision 新鲜度，不能用它证明方向场与地形一致。

## 6. 分阶段完成定义

### P0：可测量的正确基础

补基准与诊断、身份热路径、批量同步、导航 revision、交互规则拆分和完整恢复契约。
先保留现有串行算法作参考。验收：拒绝输入不修改状态；出生/恢复事务失败无部分发布；
敌军可接敌但不穿体；坚守、死亡、载具容纳与销毁两种顺序均无残留投影。

### P1：万级战斗移动

并行速度求解、空间搜索/接触预算、共享场缓存和稀疏区域预测降频；
RTS 补齐行进编队、AttackMove 组、队列与目的地容量，战斗域接入攻击槽和批量索敌。
验收：完整大规模 battle 场景持续运行并给出仿真、AI、同步和渲染分项，而非只画移动圆点。

### P2：大世界与表现协作

分块/分层导航与按需场更新；图形/动画域批量角色表现与近远切换；网络域重要性复制。
验收包含表现切换、多个目标/单位类别、动态封路和保存恢复。
万级角色画面用引擎拥有的截图证明；截图只证明画面，不代替计时和战斗正确性证据。

## 7. 基准与验收场景

数量层级：1k、5k、10k、30k、100k；前三级为主验收，后两级用于识别伸缩曲线和容量边界。
这些是拟定测试规模，不是当前性能结果。先选定 CPU/GPU/平台、地图大小、密度、半径/速度分布、
线程数、仿真 tick、邻居预算、接触迭代上限、可见角色数和动画质量，再制定帧时间阈值。

每个配置预热后固定种子运行至少 60 秒，记录 p50/p95/p99 与峰值内存、分配次数、
网格重建/导航构建/搜索/避让/接触/出生/RTS 投影耗时；同时记录吞吐、到达比例、
最大穿入、堵塞时间、最长等待和截断/预算耗尽次数。
60 FPS 的总帧预算是 16.67 ms；Crowd 所占预算须实测分配，不能把总预算全分给 Crowd。

必测场景：

- 同向行军、对向交叉、四路交叉、单/双向窄门与桥梁。
- 两军接敌、后排排队包围、远程阵地坚守、巨型与普通单位混行。
- 单点和区域集合、多组不同目标、不可达目标、动态建筑封路与拆除。
- 高速小单位穿过慢速密集群，检验全场子步放大与分级边界。
- 连续生产、出口被堵、死亡释放、登载/下车、组成员加入退出、订单反转。
- 远近仿真切换、暂停/恢复、保存加载、同种子重放。

性能跑三条独立路径：无渲染移动解算、带 RTS/战斗的完整仿真、带动画渲染的完整游戏。
必须分别报告；不能拿稀疏/纯 Boids 数据代表密集近战，也不能拿 GPU 实例数代表活跃战斗数。

## 8. 架构约束落地

本次建议遵循六份顶层架构规范；没有新增例外。未来实现按以下契约验收：

- Crowd 持有连续运动权威；RTS 在阶段边界投影 ECS，非链接单位另走原有路径。
  外部传送/击退须经显式命令进入 Crowd，再统一同步；动画/物理不能每帧反向覆盖。
- 新 ECS 系统先记录类型闭包、View、读写组件、结构变化、事件、服务与 phase。
  不新建通用兵种根；用现有短根、正交组件和强类型 Link。
- 运行句柄有代次失效检测；持久 ID 与 runtime handle 分离，跨帧不保存 slot 或临时指针。
  覆盖 provider 先销毁、consumer 先销毁、热重载和恢复重绑定。
- 低层到高层只经 capability/注入接口；按基数、频率、观察者可知性、原子性选机制。
  热路径预绑定数值句柄与服务，不做字符串查找或 `getModInst`。
- 创建、批量变更、场构建、推进、恢复使用结构化且不可丢弃的 Result。
  昂贵场构建/事务用 builder/token 和相对成本说明；廉价读取保持直接便利。
- 外部注入 dt、tick 和命名 RNG；并行归并次序与浮点容差写入共享契约。
  同一仿真线程提交，worker 仅操作规定阶段数据，锁内不调用未知脚本/回调。
- 持久数据写 schema/version/未知字段策略与迁移；派生缓存不成为第二份权威状态。
- 每个新增/改变模块在模块拥有的 architecture contract shard 声明六面边界。
  测试 provider present/absent、串行/并行共有契约、失败注入和 RTS 组合。
  裁剪证据按 L1/L2/L3/L4 标明，使用 trimmed build 与符号证据。
- 实现 PR 运行 `make check`、Windows 链接与相关 domain 测试；全平台完整测试交 CI。

本次源码阅读与在线资料核对均已完成；没有运行插件、性能基准或渲染场景，
因此只确定功能范围与验证路线，不宣称当前实现已达到所列规模。

本次检查：`git diff --check` 无错误；新增文档以 UTF-8 读取核对。
`make check` 在沙箱外启动后因 PATH 缺少 clang-format 提前退出，完整源质量门禁未通过。
本次没有运行时代码变更，未执行编译和 domain 测试。
