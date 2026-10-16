# 道路截面、桥隧区间、连续设施与自定义接入口

这一轮按 EasyRoads 外层包的 shape、bridge/tunnel marker 区间、side-object shape 和
custom connection port 约束补齐道路核心。作者数据仍由 RoadNetwork 唯一拥有；不引入新的
道路管理器、场景对象根类或运行时字符串查找。Unity 渲染对照延后，不影响 CPU 功能开发。

## 任意截面

RoadStyle.crossSection 是局部 (side, up) 点列，每一点的 material 指定到下一点的段材质。
空点列保留原来的标准道路截面；crossSectionClosed 连接最后点到第一点。
支持非对称、拱形、凹形以及带结构底面的闭合截面。闭合截面向外定向扫掠，并用耳切算法生成
凹端盖；开放截面不定义截面内部，因此不自动生成端盖。

最多 128 点；拒绝非有限数、零长段、自交、闭合零面积，以及沥青段无法连续覆盖所有车道的截面。
控制点 widthScale 缩放沥青横向范围，肩部宽度、截面高度与设施尺寸继续使用米。
车道和标线采样实际沥青高度；使用超出车道总宽度的自定义沥青截面时，导航车道仍由 laneWidth 决定。

标准程序路口只接收与标准肩部几何兼容的截面或平面双点沥青截面。
其它截面必须配有完整截面匹配的自定义接入口；不自动生成错误的平面补片。

## 桥隧区间

RoadStyle.structures 按归一化弧长 [0,1] 描述 start/end、Bridge/Tunnel、tunnelHeight 和
wallThickness。区间必须有正长度、排序且不重叠，最多 256 个。边界插入到采样折线中，
避免把相邻整段误归为桥或隧道。拆分按有界弧长采样分割并重新映射区间；反转按 1-end/1-start 映射。

存在显式区间时，区间外为地面道路；Bridge 生成桥面底部及可选支柱，Tunnel 生成有厚度的
墙顶 U 形壳体和开放洞口的壳体端盖，拒绝顶板侵入道路截面。
没有区间时，automaticStructures=true 保留原有自动桥面/支柱行为；设为 false 可明确关闭。
拆分显式区间会把两半的 automaticStructures 置为 false，避免没有桥区间的半边恢复成自动桥。

道路表面组分别为 asphalt、asphalt.bridge、asphalt.tunnel。地形写回仅写地面表面：
显式桥隧覆盖保持原高度，邻近地面道路的混合也不越入这些覆盖区域；同一网格格点存在地面
道路覆盖时，地面目标优先。高度 receipt 保留跳过的桥隧格点计数，并继续原子发布和 stale-safe 撤销。
道路碰撞与语义覆盖消费三个沥青组；结构组不当作行车表面。

这实现的是高度地形保护和结构网格，不包含 terrain holes、树木、细节或 splatmap 写回。

## 连续设施

RoadStyle.facilities 为每种设施持有稳定的 edge-local id、归一化 start/end、横向/高度偏移、
闭合截面、端盖开关及 uvMeters。最多 64 个，id 在一条边内唯一。截面沿道路 frame 连续扫掠，
尺寸保持米制，可制作护栏、墙、梁和线槽；坡度、横坡和宽度变化沿现有道路采样传播。

跨越拆分点的设施分别映射到两条边，并取消人为切口处的端盖。
对于双臂接入口，触达端点且 id、截面、物理侧别、偏移及 UV 尺度兼容的设施，自动生成沿端口
切线连接的连续扫掠，取消两侧 socket 的重复端盖。反转用同一截面反射机制保持物理侧别。
多臂节点可通过 connection.facilityRoutes 明确指定一对一衔接；空表不自行选择分叉路线。
UV 相位仍是逐边定义；不声明跨路口纹理相位一致。

本轮加入 prefab 变形、start/middle/end、显式中间段重复和兼容顶点焊接，以及多臂设施路线、
原生 TerrainWorldWorkspace 多通道写回与撤销。完整契约见
[2026-10-14 扩展说明](2026-10-14-road-prefabs-facility-routes-terrain.md)。
自动设施分类规则、源资产平滑组重建、独立设施碰撞输出等仍不等于完成整个 EasyRoads Side Object 系统。

## 自定义接入口

RoadConnection 拥有 assetId、yawDegrees、局部顶点/法线/UV、带材质的三角形和端口。
调用 setNodeConnection(nodeId, connection) 原子附着或用空值恢复程序路口。
端口持有 id、edgeId、局部位置、向外 forward、正交 up、sectionClosed 和完整截面。闭合截面
要求闭合端口，验证完整边界环，包括最后到第一点的段。旧端口字段缺失时默认开放。
edgeId=0 保留未使用的
资产端口，每条已连接道路必须恰好绑定一个端口。

资产验证拒绝非法索引、退化/反向三角形、非流形边、无效正交 frame、重复端口和 stale edge。
截面点必须落在已引用的网格边界顶点上，连续段必须对应开放边界；边界 UV 至少一个坐标接近 0/1。
拼接以 0.01 米位置容差和 0.9999 方向点积验证道路真实采样 frame 与完整截面，包含宽度和横坡。
源网格不做自动缩放或弯曲；不相容接入口、过短道路和超出工作/输出预算的资产明确失败。

实际 socket 距离来自端口局部半径，要求道路在该弧距确实到达端口。网格、裁切端点、车道转向
和连续设施使用同一 socket 契约。自定义网格按节点位置和 yaw 输出；它不再只是 placement 锚点。

绑定是网络内部的值关系，双方都属于 RoadNetwork，不是跨域 runtime Link。
反转保持 edge id；拆分把原 to 节点的绑定迁到 continuation edge id。
销毁绑定 edge 前须明确清空连接；节点必须隔离后才能删除。重连/节点移动/合并等操作不能丢弃
资产或留下 stale binding，不能保持绑定时返回失败。clear/完整快照替换同时销毁所有值，不保留外部地址。

## 编辑、持久化和成本

截面、桥隧和设施使用既有 road.edge.set-style.v1；连接使用新的 road.node.connection.set.v1，
payload 为 id 加 connection。它们通过现有 detached candidate / authority preflight / commit / inverse
进入编辑流程。道路配置保存在原来的 edge.style，连接保存在 node.connection，避免平行配置文件。

schema 仍为 eve.procgen.roadNetwork version 1，增加可选字段；旧快照缺少这些字段时使用标准截面、
自动结构、空设施和程序路口。未知字段延续忽略策略，未知 schema/version 拒绝。
老消费者无法无损保存新增字段，升级前不能拿老版本覆盖含新作者数据的快照。
解析到私有 owning candidate，完成索引、绑定与几何验证后再替换 live owner；错误不改变 revision。

所有操作同步且限 owner 线程，无回调或持锁脚本调用，没有新 ECS System、时间或 RNG。
生成仍通过显式昂贵 bake 调用；编辑是两阶段事务。返回节点/边快照会复制作者数据，不能用于逐帧遍历。
预算覆盖截面点数、设施/区间数、资产 mesh、端口验证工作、桥墩生成及最终 mesh 元素数。
重建采用确定性有界采样，浮点几何采用容差契约，不宣称跨编译器位级一致。

## 验证入口

- test/integration/gameplay-composition/procgen_road_authoring.cpp：拱面网格/车道/标线、凹闭合端盖方向、桥隧地形保护与恢复、设施反转、区间拆分、保存迁移、损坏数据与事务撤销。
- test/integration/gameplay-composition/procgen_road_connections.cpp：实际 mesh/socket/turn 对齐、损坏资产、stale binding、完整快照、连接撤销、拆分绑定迁移、设施跨自定义接入口无空洞。
- scripts/road_parity/native：STATIC/SHARED 源闭包和 CTest；不会启动 Unity。
- src/modules/procgen/road/CoreSources.cmake：将道路 CPU 文件加入正式 procgen-core-only 编译边界检查。

遵守 R-FACE 的单一作者数据与工具协议声明、R-MECH 的领域内直接组合、R-COST 的显式 bake 与
事务成本；Result、持久化原子性、确定性、线程生命周期、模块分层与失败注入均适用，无架构例外。

本轮之前的验证记录（2026-10-14；新增能力和最新验证见扩展说明）：

- STATIC / SHARED 的 17 个功能用例各通过 3,234 项断言，CTest 会把断言警告判为失败。
- 正式 procgen-core-only / OBJECT / 严格警告模式的 EVProcgenCoreCheck 编译通过，包含 18 个道路 CPU 文件。这是 L1 证据；独立 CPU SHARED harness 另提供其源闭包的 L2 链接证据，不宣称完整运行时 L3/L4 裁剪。
- make check CI_BASE=HEAD 通过 264 个脚本测试；最后的闭合端口变更另通过架构与格式检查。默认 make check 仍在既有 IStateProvider 的三个 bool 返回值违规处失败，不新增 baseline 或 allowlist。
- 扩展道路/编辑/地形测试记录为 42 个通过、4 个既有警告、0 个失败；警告涉及 NoOp 构造、位置投影和撤销，不计作通过。新增连接命令的注册/注销计数契约已经更新。
- 五个高级场景真实帧、作者快照、上传网格和 SHA-256 位于 ignored build/road-parity/functional-scenes/；重新生成后上传字节与捕获时完全相同。
- 渲染宿主为既有 EVEngine 可执行文件，当前道路核心供应全部几何；没有宣称完整引擎重编译或 Unity 对照通过。
