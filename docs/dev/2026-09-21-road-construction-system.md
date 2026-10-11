# EVEngine 道路构建系统设计

日期：2026-09-21  
状态：架构设计 + 第一纵向切片

## 参考包结论

`EasyRoads3D Pro Add On - HD Roads v1.2.7` 已重新完整审计：外层共 692 个资产，包括 350 个 Prefab、112 个材质、
88 张 TGA、27 个 FBX、5 个 Shader 和 14 个 SRP 子包；外层及全部子包均没有 `.cs`、`.dll` 或 `.asmdef`。
手册也明确要求另装 EasyRoads3D Pro v3.1，因此包内不存在可直接复制的道路生成算法源码。

但 Prefab 并非只有美术数据：170 个资产保留同一外部 EasyRoads3D MonoBehaviour GUID 的完整序列化状态，能够验证
连接口、X/T/Y/三四向环岛、路口裁切/三角化、地形变形、左右车道和 AI traffic、起终点贴花优先级、side-object
自动激活、起终点 offset、沿线随机位置/旋转、两侧人行道及端盖等功能。五页官方手册进一步确认三层 PBR 路面混合、
packed metallic/AO/height/smoothness、UV1/UV3 切换、路口绿色顶点色遮罩、随机/手选过渡贴花和沿线 side-object 工作流。
EVEngine 按这些可验证契约移植功能与数据分层，不复制 Unity 对象模型或依赖缺失的外部脚本实现。

## 简化原则与目标模型

继续由现有 `RoadNetwork` 唯一拥有节点、边和车道连接，不创建 `RoadNetworkDocument`、lane-port 对象层、
road manager 或新的 ECS 实体体系。方向只是 `RoadLaneConnection` 上的一个枚举；可从 edge 和 direction 直接推导的
端口、宽度和连接位置不另存状态。

优先复用现有能力：曲线使用 `SplinePath`，几何使用 `MeshBuild`，输出使用 `GeneratedArtifact`，编辑撤销复用现有
Editor operation/transaction。只有第二个真实调用方出现后才抽取新公共接口；只有性能测量证明需要时才增加缓存、
dirty chunk 或异步 prepare 类型。

渲染 mesh、碰撞、导航、地形印章、植被遮罩和贴花都是同一 `RoadNetwork::revision()` 的派生结果，不保存第二份
可编辑道路状态。模块继续留在 procgen core；core 不依赖 graphics、physics、scene 或 editor。跨模块发布复用已有
artifact/capability 机制，不为道路再造事件总线、registry 或生命周期框架。

## 构建流水线

实现保持三步，不把每一步变成类：

1. `RoadNetwork`：稳定 ID、spline 控制点、截面 style、方向和合法 turn；mutation 后立即维持基本不变量。
2. `validate()`：集中检查悬空引用、端点、lane 范围和重复连接；持久化或外部输入以后也复用同一校验。
3. `bakeRoadNetwork()`：复用 spline/mesh 算法一次生成道路、路口、标线和导航 overlay；碰撞、地形和渲染按需从结果投影。

## 连接正确性契约

- node 是端点位置的唯一真源；写入 edge 时首尾控制点规范化到 node。
- lane 方向显式为 Forward/Backward；反向车道在 edge.from 入站、在 edge.to 出站。
- turn 两端必须共享同一 node，lane index 必须属于对应方向；重复 turn 和同物理边即时 U-turn 被拒绝。
- 自动连接必须幂等，按稳定 edge/lane 顺序生成；不依赖 unordered 容器遍历。
- bake 前执行完整 topology validation；需要跨线程发布时再在现有 artifact metadata 中记录 revision/build key。
- junction mesh 的每个端口边界必须与 edge trim ring 共享同一位置/法线/UV seam；测试覆盖不同宽度、锐角、坡度和高度层。

## 交付阶段

本切片已完成双向 lane port、反向导航/转向烘焙、端点规范化、重复连接拒绝、幂等自动连接与 bake 前校验。

后续只按实际缺口增量扩展：

1. [已完成] 在 `RoadBake.cpp` 内增加通用 junction polygon 纯函数；T/Y/斜角路口使用道路裁切口凸包，轴对齐十字路口继续保留带人行道圆角的高质量特例。
2. [已完成] 复用现有 `RoadMaterial`/mesh group 作为材质槽，不建立第二套材质命名；`RoadStyle::uvMeters`
   明确定义为每次纹理重复的世界米数，纵向 UV 使用完整样条距离，裁切与细分不会改变纹理相位。
3. [已完成] 通过现有 composite artifact 发布 mesh、过滤标线/导航辅助面的 collider、带世界范围元数据的
   road-footprint topology，以及逐 lane/turn 的 PointSet；不新增道路专用 adapter、provider 或 registry。
4. [已完成] `RoadNetworkEditTarget` 借用唯一 live network，只在 transaction staging 中临时复制；node move、
   edge control-point/style replacement 与 node/edge 增删均使用稳定 ID，并复用 `LocalWorldAuthority`。
   edge 删除的 inverse 只携带该 edge 与受影响 lane links；node 仅可在隔离后删除，不引入整网快照。
   同一 target 直接输出已有 `GizmoSnapshot`：稳定 ID 的节点手柄、边控制线和内部控制点受统一 primitive budget
   约束，编辑后以 network revision 刷新；没有增加道路专用 gizmo builder 或 viewport 协议。
   未创建 RoadNetworkDocument；持久化仍推迟到确有存档需求时再引入 schema/migration。
5. [已完成] CPU 场景矩阵覆盖直线、曲线、不同宽度、T/Y/X、环岛、合流、坡道、桥隧和异层交叉；
   它们全部复用同一 graph/bake 模型，没有场景专用生产类型。真实 Vulkan 帧检查覆盖直线、十字路口和复杂立交，
   并据此修复了两类静态测试未暴露的问题：反向道路臂斑马线背面剔除，以及 seed wobble 无符号下溢导致
   立交样条、桥墩和顶点数爆炸。跨 seed 回归现在约束坐标与 mesh 规模上界。
6. [已完成] 按参考 Prefab/手册补齐沿道路派生物：同一次 bake 产出带稳定 ID、edge ID、角色、左右侧、
   沿线距离、朝向和独立 seed 的 `PointSet`。`transition.start/end` 对应起终点过渡贴花，`side.left/right`
   对应护栏、路缘尘土、破损贴花、线标和人行道等 side-object。调用方复用 `filterPointStringAttribute` 与
   `ObjectBuildLayer` 完成单资源手选、加权随机资源和确定性随机变换；不新增道路装饰管理器。该 PointSet 同时作为
   composite artifact 的 `placements` part 发布，并有硬数量预算防止极端长度/间距产生无界输出。
7. [已完成] 按参考 Prefab 的 `aiTraffic`、`priorityRoads` 与 lane 数据补齐交通语义：`RoadStyle` 直接携带
   米/秒限速和 0..255 路权，不建立第二套 traffic-road 对象。lane overlay 标记 edge/lane/direction，turn overlay
   标记入边、出边、入/出 lane、方向、入口路权和两段中的较低限速；发布为导航 PointSet 时写入 @Data 元数据，
   使寻路/车辆系统无需解析 child 名称或反查 bake graph。编辑 style 的既有事务同时覆盖交通参数和撤销。
8. [已完成] 补齐手动车道连接的编辑闭环：作者可对精确的入边/入 lane/方向到出边/出 lane/方向执行可撤销增删，
   用于修正自动连接在特殊路口上的结果。连接仍是 `RoadNetwork` 唯一拥有的 `RoadLaneConnection` 值，不增加 connector
   对象、路口模板管理器或第二份编辑状态；新增与删除都先在事务候选网络上复用同一拓扑校验，失败不改变 live network。
9. [已完成] 同一个 placements PointSet 现在也发布路口资产锚点：根据节点实际连接臂的方向确定
   `junction.t`、`junction.y`、`junction.x` 或 `junction.multi`，并携带稳定 point/node id、臂数、朝向和 seed。
   资产层可继续使用字符串属性过滤和 ObjectBuildLayer 选择 EasyRoads 风格的 X/T/Y prefab；分类不写回网络，避免产生
   第二份拓扑真源。路口锚点与沿线锚点共享同一硬预算，极端图不会绕过输出上限。
10. [已完成] 环岛继续使用普通 node/edge 图表示；bake 只识别由度数为 3 的节点组成、近水平、半径一致且每条环边
    都有曲线控制点的三/四节点小闭环，发布一个 `junction.roundabout.center` 和每条外接支路的
    `junction.roundabout.entry` 锚点。入口朝向外接道路，
    中心与入口共享稳定 `road_roundabout_id`。检测结果仍是派生 PointSet 数据，没有新增 Roundabout 类，也不会把普通
    立交的大型/非规则回路误当成需要 prefab 的小环岛。
11. [已完成] 增加无状态 `conformHeightmapToRoad`：直接复用 bake mesh 的 asphalt 三角形，把落在路面及指定过渡宽度
    内的 Heightmap 样本原子地贴合到路面高度。调用方显式提供世界原点、cell size、height scale、过渡距离和最大允许
    高差；超过高差的桥面/高架自动跳过，因此不会把桥下地形抬到桥面。三角形栅格化和过渡扩散共用硬工作量预算，
    失败保持目标 Heightmap 不变；没有新增道路地形对象、brush 类型或长期缓存。
12. [已完成] road artifact 已有的 `topology` Grid2D 直接作为树木、草地、岩石和普通 PCG 散布点的排除蒙版。
    通用 `excludePointsByGridMask` 保留未覆盖点的稳定 ID 和全部属性，支持世界原点/cell size 映射、指定 semantic、
    世界空间 clearance 和最大检查预算；EveScript 通过现有 owned PointSet/Grid handle 调用 `excludeGridMask`。
    该能力不依赖 RoadNetwork，建筑、水体等网格蒙版也能复用，因此没有增加道路专用 vegetation manager。
13. [已完成] 高清路面材质复用现有通用纹理/PBR 配方系统，增加确定性的 `tex.asphalt` / `pbr.asphalt`，由同一
    高度场派生 albedo、normal、roughness、height 和 AO。道路示例仅为 asphalt material group 上传所需贴图，继续
    使用 Mesh3D 已有的 cell bombing 与轻量 parallax；不增加道路 shader、材质类或 procgen 到 graphics 的模块依赖。
    EasyRoads 的三层 shader、UV3 与绿色顶点遮罩没有伪装成已移植能力：当前单 UV/单 albedo 材质无法等价表达，待
    第二个真实需求证明值得扩展通用 Mesh3D 材质契约后再处理。真实 AMD Vulkan 帧已确认纹理细节覆盖直线、弯道、
    匝道和路口组，标线/路缘仍保持独立 material group。
14. [已完成] `RoadNetworkEditTarget` 直接提供 `eve.procgen.roadNetwork` v1 快照读写，覆盖稳定 node/edge ID、完整
    style、控制点和手动车道连接；没有增加 RoadNetworkDocument。加载先在临时 RoadNetwork 中按 node、edge、link
    顺序恢复并执行完整 validate，只有全部成功才替换 live network，因此损坏引用、重复 ID 或非法拓扑不会留下部分
    修改。格式要求 schema/version，忽略未知 object 字段以支持增量元数据，并对节点、边、控制点和连接数量设置硬预算。
15. [已完成] 道路编辑不再停留在测试专用构造：既有 `procgen_editor` 自动化 target factory 支持
    `road-network`，复用同一个 `RoadNetworkEditTarget` 的自有/借用两种生命周期；没有新增 editor module 类。
    同一 target 通过通用 `IEditingSnapshotProvider` 暴露持久化快照，并向现有命令 registry 注册 node add/move/remove、
    edge add/remove 与 lane-link add/remove 七个可自动化命令。所有命令只负责规划既有 DomainOperation，提交、冲突检测、
    撤销与历史仍由统一编辑 authority/session 处理，因此没有第二条道路修改路径。
16. [已完成] 修复不同车道数道路的自动连接偏斜：旧逻辑把所有超出目标数量的入口 lane 压到最后一条目标 lane，
    例如 4→2 得到 `[0,1,1,1]`。现在按入口 lane 的归一化横向排名映射到完整目标宽度，4→2 为 `[0,0,1,1]`、
    2→4 为 `[0,3]`，不依赖尚未定义的左/右行交通规则。若作者已为同一入口 lane 与目标道路方向指定连接，自动生成
    将其视为覆盖并不再添加平行默认 turn；重复调用仍保持幂等。该修复只改 `connectAllTurns` 的纯映射策略，没有增加
    lane-port 对象或连接管理器。
17. [已完成] 补齐道路创建后的 lane-count 编辑。`setEdgeLaneCounts` 先用既有 profile 校验新数量，在候选网络中
    原子更新并移除所有因此越界的入站/出站 lane link，返回实际移除数；非法数量不改变 revision 或链接。编辑 target
    的 `road.edge.set-lanes.v1` 操作把被移除链接写入 inverse payload，因此撤销会先恢复旧车道数再恢复精确连接，
    不依赖重新自动生成。相同能力注册为 `road.edge.lanes.set.v1` 自动化命令，仍走统一事务和冲突检查。
18. [已完成] 将节点自动转向生成接入编辑器：`makeConnectAllTurns` 在候选网络调用同一个核心算法，只收集本次新增
    的精确 lane link，并编码成单个批量 DomainOperation。应用和撤销都先在临时网络逐条验证，任一重复、悬空或越界
    连接都会使整批失败且 live network 不变；inverse 精确删除本批新增项，不影响作者原有手动连接。编辑器注册
    `road.node.connect-all-turns.v1`，重复规划在无缺失连接时返回明确 NoOp，而不是写入空事务。
19. [已完成] 支持在既有道路的内部控制点原子分割边，使支路可连接到道路中段。原 edge id 留给起始半段，终点侧
    的 lane link 自动迁移到 continuation edge，并为两个半段生成同 lane 的双向连续连接；非法端点分割、ID 冲突或
    拓扑校验失败都不改变 live network。`road.edge.split.v1` 复用现有编辑事务，inverse 恢复原 edge、控制点和精确
    外部连接，重做使用事务预留的 node/edge id，不因 ID 不回收而漂移。实现只增加一个返回 ID 的值类型和一个显式
    稳定 ID 重放重载，没有新增道路工具类、分割管理器或第二份拓扑状态。
20. [已完成] 中段接入不再要求作者预先放置控制点：`splitEdgeAtPosition` 在完整 XYZ 空间把世界坐标投影到最近的
    authored centerline segment，只有在显式 snap distance 内才插入精确投影点并复用同一原子分割路径。3D 距离可防止
    地面道路误吸附到其上方的桥面；落在道路端点、超出距离或无效数值时不改变网络。编辑器命令
    `road.edge.split-at-position.v1` 使用同一 unsplit inverse 和稳定 ID 重放，支持撤销/重做；实现仍在 RoadNetwork 与
    既有 edit target 内完成，没有增加空间索引或专用吸附对象。当前线性扫描成本已在 API 标明，待真实大图性能证据
    出现后再考虑复用通用空间索引。
21. [已完成] 补齐相邻端点的真实拓扑连接：`mergeNodes` 在显式 3D snap distance 内保留目标 node id，将被合并节点
    的所有入/出 edge 端点及首尾控制点原子重锚到目标节点，既有 lane link 因 edge id 不变而继续有效，随后可直接调用
    `connectAllTurns`。若节点超距、缺失、相同，或两节点间已有直连边而会产生 self-loop，操作整体拒绝且 revision 不变。
    `road.node.merge.v1` 复用现有编辑事务；inverse 只保存被移除节点、受影响 edge 和引用它们的 lane link，撤销精确恢复，
    不使用整图快照覆盖无关对象。没有新增连接管理器、吸附组件或重复拓扑所有者。
22. [已完成] 将“从已有节点接入道路中段”收敛为单个 `road.node.connect-at-position.v1` 原子作者操作。候选网络先
    复用 3D 投影分割，再以预留稳定 id 创建直线支路，并在新节点调用既有 `connectAllTurns`，因此一次提交同时得到
    几何连接和可导航 lane turn，不会暴露“主路已切开但支路尚未创建”的中间状态。inverse 删除支路与分割产物、恢复
    原 edge 及其精确外部 lane links；重做恢复相同 node/edge id。失败的吸附、style、lane 数量或拓扑校验均只发生在
    候选副本。该工作流只增加两个 operation type 和编解码值，没有新增 branch builder 或道路文档类。
23. [已完成] 增加 `road.edge.connect-intersection.v1`，把两条道路唯一的内部 XZ 交点转换为真正的平交路口。
    算法逐 authored segment 求交并插值两条道路各自高度；只有垂直差不超过显式容差时，才原子分割两边、把共享节点
    调整到平均高度、合并节点并生成全部 lane turns。高差超限的桥面/地面交叉保持 grade-separated；没有交点、端点
    接触、同一对道路多次相交或同一 edge 输入都会明确拒绝，避免静默选择错误交点。inverse 恢复两条原 edge 和其精确
    lane links，重做保持四个分割产物的稳定 id。求交是编辑时的有界二次扫描，没有引入道路空间索引或常驻交叉对象。
24. [已完成] 道路编辑适配器随功能增长达到 1683 行后，按既有职责纯拆分为 codec/base（733 行）、operation planning
    （437 行）、operation apply（291 行）和 command registration（242 行）四个编译单元；共享内容仅放入无状态的
    internal 函数声明，没有新增类、所有者或运行时层。公开 `RoadNetworkEditTarget` API、operation type、序列化 payload
    和命令注册结果均保持不变，并通过重新配置后的真实链接和完整回归验证，避免后续连接功能继续堆进单个大文件。
25. [已完成] 自环道路被收紧为底层拓扑不变量：`addEdge`、撤销/导入使用的 `restoreEdge` 和全图
    `validate` 都要求两个端点不同。直接创建、稳定 ID 恢复或损坏快照导入中的 `from == to` 都会返回结构化
    错误且不改变 revision/live network，不再只依赖 `mergeNodes` 这一个上层操作做防御。没有增加新类或第二份验证状态。
26. [已完成] 沿线 side-object 支持每条 edge 独立设置起点/终点留空距离和左右侧启用状态，参数直接存入
    既有 `RoadStyle`，并随 style 编辑事务、撤销和 v1 快照往返。旧 v1 快照缺少这四个增量字段时使用原有
    全区间/双侧默认，因此不需要分叉迁移器。placement 预算与实际生成共用同一可用区间和启用侧数，仍受原有
    硬上限约束。资产权重、随机选择/旋转继续交给通用 `ObjectBuildLayer`，没有新建道路 side-object 管理器。
27. [已完成] 零长度中心线被收紧为底层拓扑不变量：新增、稳定 ID 恢复、控制点替换和全图验证都要求
    至少存在一个有效长度段。`setNodePosition` 改为先在候选网络重锚全部关联边并执行完整 `validate`，确认不会压塌
    直边后才一次替换 live network。失败时节点坐标、边端点和 revision 全部保持原值，避免将错误延迟到样条、网格或
    导航烘焙阶段。该约束使用一个无状态内部函数，没有新增类或几何管理器。
28. [已完成] `connectAllTurns` 从逐条直接写入 live network 改为在候选 `RoadNetwork` 中生成全部车道连接、
    执行完整拓扑验证后一次提交。一个节点无论新增多少 lane link 都只增加一次 revision；幂等重复调用返回 0
    且不改 revision。任何单条连接错误或最终验证失败都不会留下部分转向，编辑器的 connect-all、支路接入和平交路口
    继续复用这一核心路径。没有新增事务类或 lane-link 管理器。
29. [已完成] 节点 `junctionRadius` 从“创建后只读”补齐为可撤销的作者参数。核心 `setNodeJunctionRadius`
    验证有限正值、幂等设置不增 revision；`RoadNetworkEditTarget` 的 `road.node.set-radius.v1` 操作和
    `road.node.radius.set.v1` 自动化命令复用原有 authority/preflight/commit/compensate 路径，因此路口裁切范围可直接调整而不必
    删除重建节点。快照原本已持久化该字段，没有格式版本变更、新类或新状态所有者。
30. [已完成] 将既有边样式事务正式暴露为 `road.edge.style.set.v1` 自动化命令。命令直接复用快照和
    DomainOperation 已有的完整 style codec，一次替换 lane/curb/sidewalk/deck/pier/marking/UV/traffic 和
    side-object 参数，并继续走候选验证、revision 冲突检查和撤销路径。该命令要求完整 style payload，避免在
    command 层发明另一套部分 patch 语义或默认值真源；没有新增类或编解码器。
31. [已完成] 将既有控制点替换事务暴露为 `road.edge.control-points.set.v1` 命令，创建后的道路可经由通用
    command registry 从直线改为多段曲线。命令直接复用快照/DomainOperation 的 `decodeEdge`，因此共享控制点
    数量上限、有限数验证、节点拥有的端点重锚、非退化几何约束和撤销 payload。没有新增 spline document、道路曲线类或
    第二套控制点真源。
32. [已完成] 增加原子 `reverseEdge` 和 `road.edge.reverse.v1` 命令：在保留 edge id 的同时交换 from/to、
    反转控制点、交换正反向车道数、side-object 起终 offset 和左右启用状态，并翻转所有引用该 edge 的
    lane-link direction。操作在候选网络完整验证后一次提交，自身即为 inverse，执行两次恢复原始几何、车道和连接。
    为完整支持单向道路反转，车道约束同步收敛为“两个方向各允许 0..8，但总数至少为 1”，并在 profile、快照与命令
    codec 统一执行。因此作者不需要删除重建反向创建的道路，也不会遗留方向错位的导航连接。没有新增类或第二份边状态。
33. [已完成] 增加 `road.node.merge-and-connect.v1` 原子作者操作，解决近邻端点合并后还必须另发一次
    connect-all、期间几何与导航状态短暂不一致的问题。操作先在事务候选网络复用 `mergeNodes`，再对保留节点调用
    `connectAllTurns` 和完整校验，成功后一次提交；inverse 继续复用既有 unmerge 编码，通过删除并精确恢复受影响边和
    原始 lane link 去除自动生成的转向。普通 `road.node.merge.v1` 仍保留给只想焊接几何、手工布置连接的作者。
    实现只增加一个私有规划辅助函数和 operation type，没有新增类、连接管理器或第二份拓扑状态。
34. [已完成] 增加 `road.edge.add-and-connect.v1`，把创建道路与两端车道接通收敛成一个可撤销操作。规划阶段
    复用既有 edge restore payload 和 `connectAllTurns`，但只把引用新 edge 的精确 lane link 写入 forward payload，
    因此不会顺带补齐或覆盖端点上旧道路之间的作者连接。提交通过既有 restore-edge 原子候选路径，撤销继续由
    remove-edge 同时清除新道路及其链接，重做保留稳定 edge id。没有增加 operation type、类或第二套连接算法。
35. [已完成] 修复弯道路口“渲染相交但拓扑检测不到”的中心线不一致：平交搜索现在对与 bake 相同的
    Catmull-Rom 中心线做确定性有界采样，而不是检查作者控制折线；交点以归一化样条参数写入 operation payload，
    `splitEdgeAtSplineParameter` 在对应曲线上求值、必要时插入一个控制点并原子复用既有 split/link 迁移路径。
    两条分割边先共同锚定到同一交点再合并，避免采样弦误差造成近邻节点残留。直路、多交点拒绝、高度分层、撤销和
    稳定 ID 语义保持不变；搜索最多使用每边 4096 个区间，没有增加空间索引、曲线类或第二套中心线实现。
36. [已完成] 将世界位置吸附和 `road.node.connect-at-position.v1` 同步升级到实际 Catmull-Rom 中心线。
    `splitEdgeAtPosition` 不再投影作者控制折线，而是在最多 4096 个确定性样条区间上寻找最近 3D 投影参数，再复用
    `splitEdgeAtSplineParameter` 完成稳定 ID 分割、链接迁移和事务重放。因此支路可以接入视觉弯道外鼓部分，同时完整
    保留 3D 距离阈值、端点拒绝、桥面隔离、撤销和失败不修改网络的契约。共享的无状态 spline 构造函数也消除了核心
    两条分割路径的重复代码，没有增加类、缓存或常驻空间结构。
37. [已完成] 增加可持久化的精确禁转关系。禁转直接复用 `RoadLaneConnection` 值类型，由 `RoadNetwork`
    与正常 lane link 共同拥有；`connectAllTurns` 跳过命中的禁转，作者可用 `road.lane-link.block.v1` 和
    `road.lane-link.unblock.v1` 撤销式编辑。快照以可选增量字段保持 v1 向后兼容，并拒绝同一连接同时允许与禁止的
    歧义输入。反向道路、曲线分割、节点合并、平交连接、车道缩减和边删除都会迁移或清理禁转，其 inverse 精确恢复；
    因此重新自动连接不会复活作者禁止的转向，也没有增加 turn manager、规则对象或第二份路口拓扑。
38. [已完成] 增加道路端点重接能力，补齐已有道路无法直接改接现有节点的作者缺口。核心
    `reconnectEdgeEndpoint` 保留 edge 稳定 id，将选定首/尾控制点重锚到目标 node，并在候选网络中只清理仍留在旧路口的
    active/blocked lane connection；目标为另一端时明确拒绝 self-loop，失败不改变网络。编辑操作和
    `road.edge.reconnect-endpoint.v1` 命令使用同一 payload/inverse，撤销会恢复旧节点及被清理的允许/禁转关系，重做保持
    同一 edge id。该能力直接复用既有端点、连接和值编解码，没有引入 connector、端口对象或第二份几何状态。
39. [已完成] 增加 `road.edge.reconnect-endpoint-and-connect.v1` 原子工作流，避免端点改接后几何已经连通、导航仍未接通的
    中间状态。规划在候选网络中依次复用 `reconnectEdgeEndpoint` 和 `connectAllTurns`，但 forward payload 只携带引用被移动
    edge 的新增 lane link，不会顺带补齐新路口上其他既有道路之间的缺失关系；inverse 继续恢复旧端点的允许/禁转集合。
    实现复用同一个 reconnect operation type、codec 和 apply 分支，仅增加规划入口与命令，没有第二套连接算法。
40. [已完成] 增加 `road.edge.snap-endpoint.v1` 邻近吸附入口。它从当前端点出发，在显式有限 3D 距离内排除自身节点和
    道路另一端后选择最近节点；等距时以稳定 node id 决胜，超距返回 NotFound 且不修改网络。命令可选择是否同时生成
    新转向，默认复用原子 reconnect-and-connect，撤销语义完全继承已有 operation。该能力只进行编辑时线性节点扫描，
    没有增加空间索引、吸附状态或新 operation 类型，并通过真实命令规划/执行覆盖确定性等距选择。
41. [已完成] 修复短道路与锐角折返连接的导航转向曲线过冲。旧 cubic handle 至少为 2 米且只由 junction radius 决定，
    当两条 lane 裁切端点非常接近时会把 turn ribbon 拉到路口范围之外；现在 handle 同时受端点 chord 一半约束，普通大路口
    保持原曲率，短/锐角连接保持局部。连续采样点重合时不再提交退化 ribbon quad，零 chord 也不生成错误方向箭头。
    新回归覆盖大 radius、短 edge、近折返角，逐点验证有限坐标和局部空间上界；实现仍留在既有纯 bake 函数中。
42. [已完成] 收紧公开 road bake 输入预算：每边路径采样和转向采样统一限制在 `[2,4096]`，避免外部工具输入极大整数
    触发无界顶点/索引分配；启用 navigation 时，ribbon half-width 与 arrow spacing 必须为有限正数，NaN、无穷和零值在
    写入 mesh 前返回结构化 InvalidArgument。关闭 navigation 时这些无关字段不会阻止普通道路烘焙。约束直接位于
    `bakeRoadNetwork` 入口且补入公开契约，没有增加配置类、预处理对象或隐藏回退。
43. [已完成] 为完整 road bake 增加 `maximumMeshElements` 总输出预算，统一计算 vertex + index 元素数，默认上限为
    8M。每条 edge、每个 junction 和每条 navigation turn 生成后立即检查，超限返回结构化 PreconditionViolation；由于
    `RoadBakeResult` 只在成功时发布，失败不会暴露半成品。该预算与已有 `maximumPlacements` 分别约束 mesh 和 point 输出，
    没有修改通用 `MeshBuild`、增加道路缓存或建立第二条低成本烘焙路径。
44. [已完成] 增加对称的端点拆开能力与 `road.edge.detach-endpoint.v1`。核心操作在候选网络中按原 junction 的位置和
    radius 创建新稳定节点，把指定 edge 端点重接到新节点并清理留在旧路口的允许/禁转关系，整个过程只增加一次
    revision。编辑 forward 使用预留 node id，inverse 重接原节点、恢复精确连接集合并删除已隔离的新节点，重做保持相同
    node/edge id。实现复用 reconnect、RoadNode 和 lane-link codec，仅增加 detach/reattach operation 名称，没有端口类、
    junction manager 或第二份连接状态。
45. [已完成] 收紧 detach 的语义边界：只有端点节点同时连接至少另一条 edge 时才允许拆开。独立道路端点现在返回
    PreconditionViolation，不再创建无意义的重合节点并遗留孤立旧节点；失败时 node/edge 数量、端点 id 与 revision 全部
    保持不变。约束放在核心 `RoadNetwork`，因此直接 API、编辑命令和未来脚本入口共享同一行为，没有命令层特判。
46. [已完成] 在既有 `procedural-road` 示例增加 `tight-turn` 调试场景，复用 `mesh.roadNetwork` 和同一条
    navigation bake 路径，不增加新示例、渲染管理器或专用几何实现。该场景将 CPU 回归中的短边近折返拓扑放大到
    可视尺度，并只为它打开青色 turn ribbon。真实 Vulkan 帧暴露了中点箭头沿端点 chord 而非曲线切线定向的
    交叉伪影；现已改为 cubic Bézier 中点导数，仅在导数退化时回退到 chord，最终帧中箭头与 ribbon 切向一致。
47. [已完成] `tight-turn` 同时打开通用 junction polygon，真实帧发现了一个跨投影的连接错误：edge strip 会把大路口半径限制在
    半条短边内，但 junction mouth、斑马线和 navigation turn 仍使用未限幅的作者半径，因此同一节点的路面与导航会落在不同端点。
    现在四条路径共用一个无状态 trim 函数，保证每条短边至少保留一半可用长度；新回归检查大半径下的 turn 两端与平台裁切一致。
    最终 Vulkan 帧中发卡弧连续并落在通用凸包路口内；没有增加二臂路口类型或场景专用 mesh 分支。
48. [已完成] 四向路口的高质量圆角特例现在还要求四个 arm mouth 实际位于作者 `junctionRadius` 上。当最短允许跨度触发半边
    trim 限幅时，不再用未限幅半径生成特例平台，而是回落到同一个通用 arm hull。回归直接过滤路口高度的 asphalt 顶点，
    验证平台不超出限幅 mouth；普通长臂十字路口仍保留原圆角路缘效果，没有添加配置开关或另一套路口算法。
49. [已完成] 通用 T/Y/锐角路口不再只生成 asphalt 凸包。烘焙器直接复用 arm 的 `RoadStyle` 尺寸，为凸包计算一组共享 miter
    外扩 ring，只在暴露边生成路缘顶/立面和人行道；被识别为同一 arm 两个 asphalt corner 的 mouth 边保持敞开，与既有 edge profile 衔接。
    真实 Vulkan 发卡弯帧已检查连续边界；T 路口回归比较同一网络开/关 junction 后的 curb/sidewalk 顶点数，确保通用平台不会退化回裸沥青。
    整个改动仍在 `bakeJunction` 纯烘焙逻辑内，没有新材质管理器、路口对象或持久状态。
50. [已完成] 通用路口 mouth 不再被强制压平到 `node.y`。每个凸包端点直接保留与 edge trim 共用的样条 frame 高度，路缘和
    人行道各层也以端点局部基准高度外扩，因此上坡、下坡或不同高程的 T/Y arm 在裁切口不会产生竖向裂缝。回归用三条不同坡度的
    arm 定位同一 mouth 的 edge/junction 重合顶点，并把高度差约束在 2 厘米内；真实 Vulkan 立交帧同时验证完整复杂场景仍可渲染。
    修复只改变派生 mesh 顶点，不增加坡度字段、路口状态或新的 bake 分支。
51. [已完成] 混合 `RoadStyle` 路口不再用全路口最大 curb/sidewalk 尺寸覆盖每条 arm。凸包顶点从其真实 mouth corner 继承
    curb/sidewalk 的宽度和高度，暴露边在相邻顶点间自然过渡；mouth 端点则直接沿 arm side 对齐 edge profile，而不参与会把端点
    沿道路方向推出的 miter 计算。窄路接入宽路的回归分别验证 curb 与 sidewalk 外缘在开关 junction 前后的共点增量，真实 Vulkan
    发卡帧确认原先突出的尖角已收回到 edge 截面。实现只扩充既有局部 `ArmTip` 值和向量计算，没有新增类型或持久化字段。
52. [已完成] edge 只在“节点至少连接两条 edge 且本次 bake 启用 junction”时按 `junctionRadius` 裁切。度数为 1 的道路终点现在
    延伸到作者节点并生成完整 profile 端盖；显式关闭 junction 时，相连 edge 也都延伸到共享节点，不再留下没有平台填充的空洞，lane 与
    turn overlay 使用同一个有效裁切判定。回归验证 20 米独立道路覆盖完整 `[-10,10]`、关闭 junction 的两段道路在 hub 处拥有端点顶点，
    世界米 UV 相应覆盖完整 5 次重复；真实 Vulkan 直路帧确认两端路面和路肩闭合。实现仅增加一个无状态度数检查，没有端点对象或缓存。
53. [已完成] 平滑分段节点不再误判为路口。bake 开始时一次构建节点 incident-edge 表：三臂以上节点始终是路口；二度节点仅在两边
    截面不同或中心线转角超过 30° 时生成 junction，因而 `splitEdge` 产生的同截面平滑 continuation 不再被裁开、加平台或斑马线，真正的
    发卡转角和宽度过渡仍保留路口。terminal、active-junction 集合都只在单次 bake 内派生，不写回网络。`includeMarkings=false` 也同步覆盖
    junction crosswalk，不再只关闭 edge 标线。回归检查平滑 split 没有 junction 高度顶点，并检查普通路段和十字路口都不残留 marking。
54. [已完成] 虚线不再以 path sample 为最小绘制单位。`addLaneMarkings` 直接使用 frame 的全边归一化距离和完整样条长度，在每个采样段内
    精确切分世界米制 dash/gap 边界；改变 `pathSegmentsPerEdge` 不再改变虚线长度、数量或相位，路口裁切也不会让纹理式标线从零重新开始。
    `dashLength=0` 明确表示不生成虚线，`dashGap=0` 表示连续线，避免零周期。回归比较 40 米道路在 4/40 段细分下的黄色标线面积，均为
    `22m × 0.22m`；真实 Vulkan 直路帧确认虚线长度与间距均匀。实现仍是既有标线 lambda 内的局部距离切分，没有新生成器或状态字段。
55. [已完成] 米制虚线细分不再依靠固定循环 guard 静默停止。每个实际标线 quad 在写入前共享 `maximumMeshElements` 总预算，预算不足或
    浮点周期小到无法前进时返回结构化 `PreconditionViolation`，不会发布半成品 `RoadBakeResult`。回归使用 `0.1mm/0.1mm` 的极端 dash/gap
    和 1000 元素预算，验证长道路被明确拒绝；正常 4/40 段米制面积与完整 73 项道路测试保持通过。实现复用同一 mesh 预算，没有新增标线预算、
    fallback 或第二套低成本生成路径。
56. [已完成] 沿线 side-object placement 与道路几何复用同一 junction socket 裁切距离。启用路口烘焙时，护栏、路灯和贴花候选区间会从
    每条 edge 的实际裁切口外开始并在另一端裁切口前结束，再叠加作者设置的起终 offset；placement 预算预估和实际生成使用完全相同的有效区间。
    因此侧向对象不会生成在 T/Y/X 路口铺面或车道汇入口内，关闭 junction 时仍保持原全边行为。实现只扩充单次 bake 的私有 `EdgeSource` 值，
    没有增加道路装饰管理器、缓存或持久状态。
57. [已完成] 将 Heightmap 三角形栅格化、过渡扩散和原子写回从过大的 `RoadBake.cpp` 纯移动到 `RoadTerrain.cpp`，作为后续道路挖填、
    隧道口和地形恢复工作的实现边界。`conformHeightmapToRoad` 的公开签名、诊断、工作预算和失败不修改语义保持不变；拆分没有增加类、状态、
    模块依赖或第二条地形处理路径。新增编译单元经 source-list 重配、真实链接和完整道路回归验证。
58. [已完成] 路口交通控制成为 `RoadNode` 的单一权威字段，支持 `Uncontrolled/Yield/Stop/Signal`，并通过既有编辑事务、撤销、命令与
    `eve.procgen.roadNetwork` v1 快照往返。该字段是 v1 可选增量字段，旧快照缺失时迁移为 `Uncontrolled`，未知枚举在候选网络发布前
    整体拒绝。烘焙在实际 junction socket 的行车方向右侧生成稳定 `junction.control.*` 锚点；`Yield` 在存在不同路权时只约束较低
    `trafficPriority` 的入口，同权时所有入口让行，`Stop/Signal` 约束所有入口。锚点携带 node/edge/direction/priority/distance，可由交通灯、
    标志、停止线或 AI 消费；实现拆入 `RoadTraffic.cpp`，没有新增 manager、长期类或第二套路网状态。
59. [已完成] 道路组合产物新增 `terrain/surface-weight` 连续权重网格，复用现有 `Grid2D.detail` 作为 `0..255` 权重通道：路面覆盖区为
    255，外侧按 `terrainBlendDistance` 确定性衰减至 0。它与保留二值语义的 `topology` 植被排除网格并存，分别服务材质混合与拓扑清除，
    都从同一次 bake mesh 栅格化且依赖同一 mesh 子产物。两遍 chamfer 距离变换有固定遍历序、无缓存和随机源；配方范围、直接调用验证、
    完整零值/过渡值/满值回归均已覆盖，没有引入道路材质管理器或第二套产物发布机制。
60. [已完成] 轴对齐十字路口的高质量圆角特例不再把中心铺面、路缘和人行道硬压到 `node.y`。它与通用 T/Y 路口复用
    同一个最小二乘 `JunctionPlane`，所有中心扇面、封缝条、圆角顶面和立面都从平面高度及平面法线构造；斑马线法线也改用
    实际样条 frame 的 up。新增四臂双向坡度十字回归，确认中心 10 米范围存在连续高差、没有退化沥青三角形且标线法线有限向上；
    完整道路测试提升为 81 项。实现只增加局部坐标转换 lambda，没有新增类型或另一套路口生成器。
61. [已完成] 路口圆角不再使用与尺度无关的固定采样数。`RoadBakeOptions::junctionChordError` 以世界米约束圆弧和二次
    Bezier curb-return 的最大弦误差，轴对齐圆角按半径/夹角求段数，通用路口按二阶差分的误差上界求段数，统一限制在
    4..64；非法、非有限或超过 1 米的误差在 bake 前结构化拒绝。配方公开同一参数，回归证明 1m 与 1cm 误差会产生不同
    几何密度且两者均无退化沥青三角形，没有增加 tessellator 类或缓存。
62. [已完成] 地形 conform 增加精确可恢复 receipt。`conformHeightmapToRoadWithReceipt` 仍复用同一原子栅格化/混合实现，
    只记录实际变化样本的 row-major index、before 和 after；旧 `conformHeightmapToRoad` 是向内调用该实现的兼容入口。
    `restoreHeightmapFromRoad` 在候选 Heightmap 上恢复前验证尺寸、排序、有限值以及每个当前样本仍等于 after，任一陈旧编辑
    都返回 Conflict 且不改地形，避免撤销道路时覆盖后续地形修改。回归覆盖完整恢复与陈旧拒绝，没有长期 terrain manager。
63. [已完成] 环岛和匝道立交不再只是测试拼图或名不副实的演示。`makeRoundabout` 用八条共享解析端点切线的普通曲线环边、四条双向接入边和
    既有 `connectAllTurns` 生成正式可配方场景，仍由环形拓扑派生中心/入口锚点；`makeInterchange` 在保持中央 XZ 同位但
    拓扑断开的地面/高架节点基础上，增加四条外围双向爬坡匝道，在北/西与西北高架汇入口、南/东与东南高架汇入口生成
    实际 lane link。立交保持 8 节点、扩展为 10 边，测试确认匝道边参与导航连接、跨层中心仍不误连且 mesh/桥墩预算有界。
    两者均只是 `RoadNetwork` 工厂，没有新增 Roundabout、Ramp 或 Interchange 运行时类型。
64. [已完成] 环岛的八段曲线不再靠近似圆周控制点碰接：相邻边在每个节点共享解析圆切线，二度平滑节点也进入既有口沿多边形
    求解但不生成斑马线，入口节点只恢复由严格反向切线识别出的连续圆环口沿。真实 Vulkan 渲染验证八个分段点均无三角破洞；
    二度点使用 0.25 米局部接缝半径，避免为消缝引入大面积材质相位块。仍未新增环岛专用 mesh 类型。
65. [已完成] bake 前增加通用路口可解性预检：三臂以上几乎同向的重复支路、四臂以上无法共享有界构造平面的多层口沿、以及
    两端裁切只剩不足一米的六车道以上超宽短边会返回结构化 `PreconditionViolation`。阈值按冲突性质收窄，既有短急弯、三臂坡口
    和匝道合流继续通过；新增三类失败回归且 83 个道路测试全绿。
66. [已完成] 道路组合产物 schema v2 从同一 bake 结果派生结构碰撞体、仅 asphalt 的行驶碰撞体，以及按世界坐标划分的
    `render/chunk/<x>/<z>/lod/<n>` 渲染块。LOD 继续复用同一 baker，只降低曲线/路口采样密度且关闭非渲染派生物；每个块的
    BuildKey 由实际 mesh 内容计算，因此无关参数变化不会使未变化块失效。这里的“增量”明确是产物缓存、上传与下游重建粒度，
    不维护第二份可编辑路网或隐式 CPU bake 缓存；完整 bake 仍是确定性的权威生成路径。
67. [已完成] side-object 锚点仍沿实际样条 transported frame 生成，但接受前会计算到其它道路中心线的三维最近点，并同时检查
    已接受锚点间距；落入其它道路半宽加 clearance 的候选或相互过近的候选被确定性跳过。路口裁切区、作者起终 offset 和左右侧
    开关继续共同生效，避免护栏、路基石或装饰物堵住汇入口；实现只增加一个 bake 选项和局部值集合。
68. [已完成] 环岛四个入口复用权威 `Yield` 控制和 edge `trafficPriority`，每个入口都发布低优先级让行锚点。普通 T/Y 三臂路口
    额外发布稳定的 `junction.channelizing.island` 资产锚点，位置由节点半径与支路方向计算，并携带 node id、方向、距离和臂数；
    交通灯、标志和导流岛继续由同一 PointSet/资产层消费，没有新增交通设施管理器。
69. [已完成] 道路标线按车流语义生成：路缘侧为白色连续边线，同向车道之间为使用世界米相位的白色虚线，双向车流分界为
    双黄实线；前向与反向多车道各自完整覆盖。标线仍使用既有 `marking`/`markingYellow` mesh group，路口斑马线、全局关闭和
    mesh 总预算保持原契约。回归同时验证不同细分下虚线面积不变、反向车道分隔存在以及中央双黄线成对生成。
70. [已完成] 地形回执显式统计因垂直高差而保持不变的桥下和隧道顶样本；只有没有其它合法贴地道路覆盖的 cell 才计入跳过数。
    自动测试分别把道路放在地形上方和下方，证明桥下净空与隧道覆土不会被 conform 改写。该诊断复用可恢复 receipt，不增加
    terrain/tunnel manager，旧的 changed-count 兼容入口保持不变。

类型预算：在通用路口完成前，除必要的值类型/枚举外不增加长期类；任何新抽象都必须同时减少重复代码、拥有至少
两个真实调用方，并能删除等量或更多的专用逻辑。

确定性为相同输入/seed 下 CPU topology 与 mesh 索引 bit-exact；不同图形后端的最终像素采用容差比较。
热路径只使用现有稳定 ID/revision，不执行字符串 capability lookup。无经批准的架构例外。
