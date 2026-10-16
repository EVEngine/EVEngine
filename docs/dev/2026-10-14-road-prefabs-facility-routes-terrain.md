# 道路 prefab、设施路线和地形多通道事务

2026-10-14。参考 outer EasyRoads 包的 side_objects.html 和 terrain_control.html；不采用 Beta 包行为。
Unity 许可证未解决，Unity 渲染对照继续延后。本文记录当前核心行为和可复现验证边界。

## Prefab 沿路变形

RoadContinuousFacility.prefab 拥有源顶点、法线、UV、材质三角形和 assetId，不保留 Unity 对象、
资源注册表指针或 GPU 地址。空 mesh 延续 shape 截面扫掠。非空 mesh 沿局部 Z 轴变形；
源 Z 限制在 [0,length]，X/Y 是设施 frame 中的米制横向/高度，不随道路宽度缩放。
设施偏移仍随道路驾驶面宽度和横坡变化。

可选 startMesh/endMesh、middleCopies（1..4096）构成 start / repeated middle / end 序列。
各段源长度确定相对长度，整个序列拟合设施作者区间，socket 裁切不会重新拉伸剩余源段。
这是显式重复次数与整体拟合，尚不包含 EasyRoads 按世界间距自动计算段数的编辑器策略。
三角形在源序列边界和道路采样帧处裁切并重新三角化，保留插值源 UV；不会在裁切处自动生成
新的 prefab 端盖。形状端盖设置只适用于 shape 模式，prefab 端部由资产自身定义。

法线使用变形的逆转置 Jacobian。没有长度的相邻样条段跳过；真实折叠/奇异变形明确失败。
源网格非法索引、非有限值、非单位法线、退化或反向三角形及超预算数据全部拒绝。
weldCompatibleVertices 仅在位置、法线和 UV 都在 1e-5 精度上相同时共用索引；不覆盖硬边法线、
不合并 UV 接缝，也不声明重建平滑组。中间段重复不会改写源 UV。

sourceStart/sourceEnd 是整个源序列上的归一化窗口。拆分按原道路弧长比例裁切窗口；两半共享
作者资产值，分别拥有其副本。反转反射 X/Z 与法线、交换 start/end、映射源窗口并保持物理侧别。
来自同一源序列且窗口相邻的设施，在新增节点里只补两个 socket 之间缺失的源窗口，不重新生成
start/end 或完整重复序列。测试确认拆分后端件的世界位置仍只在原来的两端。
字段随既有 road.edge.set-style.v1、快照和事务 inverse 保存。旧快照缺少 prefab 时迁移为 shape。
反转命令的 inverse 拥有原始 style 快照，不以第二次浮点反射代替精确作者数据恢复；旧 inverse payload 仍可读取。

## 多臂设施路线

RoadConnection.facilityRoutes 是节点拥有的无向一对一配对，记录两侧 edge/facility 的稳定身份。
有显式路线时只遵从该表，不另外自动配对；空表继续支持已有双臂自动衔接。
多个独立路线可以穿过同一个多臂节点；同一端点只能参与一条路线，不能把实体护栏一分为多。
shape 路线要求行进坐标下的截面、材质、物理侧别、垂直偏移和 UV 尺度兼容；可以连接不同的设施 id。
prefab 路线要求两侧同时具有 prefab，且行进坐标下的实际三段源几何、法线、UV、三角形和材质
在 1e-4 容差内相容；相同 assetId 不代表相同 mesh 快照。使用源 prefab 填充连接曲线。

所有路线端点必须属于该节点的 incident edge，且设施区间必须触达该端点。
失效身份、重复端点、未触达 socket 和不兼容形状在私有候选里拒绝。烘焙使用实际 socket frame、
切线和共同节点平面，抑制相连 shape 的 socket 端盖；未配对设施保持终止。
UV 仍由源段/边定义，不声明整个网络的统一纹理相位；作者仍须选择不会彼此相交的设施路线。

道路反转保留身份，拆分迁移原 to 节点的端口和路线到 continuation edge。
撤销拆分在 detached candidate 内暂存完整连接值、恢复原道路、迁回端口/路线，再整体验证发布。
节点/边恢复按稳定 id 排列，车道连接和禁止转向按稳定连接键排序，避免一次撤销改变快照或后续生成顺序。
删除绑定边、合并带路线节点前须先清除路线；操作失败不丢弃绑定。
clear 或完整快照替换同时销毁这些领域内值，不存在跨域 Link 或两种对象销毁顺序。

## 地形写回、保存和撤销

RoadTerrainWorkspace 是道路生成地形值的唯一可变 owner。initialize 导入 owning tile 数据；
copyTile 返回独立副本用于外部地形/渲染器发布，不提供另一个可写视图。它与 TerrainWorldWorkspace
是独立地形工作流；同一 tile 不同时绑定两个可变 owner。
现有 TerrainWorldWorkspace.conformRoad 接入同一核心：只在私有事务里构造 owning projection，
完成所有通道后回写原生 splat/detail/tree/holes，再记录一个完整世界历史帧并一次发布。
投影随后销毁，不形成另一个 live owner。保留树的原生 PointSet 稳定身份、所有属性和 Data metadata，
并保持 objects/probes 等未选通道不变；hole mask 也加入现有世界历史和 copyHoles 输出。
完整道路 terrain schema 的保存/恢复属于 RoadTerrainWorkspace；没有宣称新增整个原生世界、
其所有对象和探针的跨进程保存格式。Unity Terrain adapter 与 Unity 对照仍待后续。

每个 tile 拥有稳定 id、世界原点/大小/高度比例，以及高度、二值 hole mask、归一化 splat layers、
按 resourceId 分开的整数 detail density 和按 id/resourceId 标识的完整树实例。
高度图要求正方形世界单元，其他通道各有独立分辨率并按同一世界矩形采样。
纹理和细节允许原生宿主已有的 1x1 配置，该唯一样本取世界矩形中心。
样本和历史受 16M 存储单元、4096 历史帧、256 tile 及各资源拓扑预算限制。

apply 对所有 tile 求值后才交换 owner：

- 高度通过既有道路 conformance 核心处理，桥/隧保持原地形，垂直阈值继续生效。
- splatLayer=-1 明确保留纹理；选择某层时按世界距离覆盖/混合权重，把其他层同比降低，保持和为 1。
- clearDetails/clearTrees 仅清除普通道路覆盖和 blend 区；不清桥下或未切孔隧道的顶层植被。
- cutTunnelHoles 默认 false。显式启用时切除输入网格中 asphalt.tunnel 的整个投影足迹，并清对应植被。
  该模式不是 EasyRoads 的自动 portal-only 开孔；要只切入口须提供经选择的 tunnel 足迹。
- 普通道路与结构重叠时普通道路覆盖优先；桥隧保护区不接受邻近普通道路的 blend 扩张。
- 任意 tile 目标层不存在、网格损坏、工作/历史预算不足等错误不发布任何通道，也不截断 redo。

高度 raster/blend 与各通道 raster/blend 共用 maximumWork；不按每个 tile 重新获得预算。
undo/redo 选择 owner 内的完整历史帧，不能出现某些通道已经撤销而其他通道未撤销的状态。
capture/restore 使用 canonical Value，schema=procgen.road.terrain、version=1，保存完整历史与 cursor。
未知对象字段忽略；未知版本拒绝。所有历史帧的拓扑、身份、有限值、二值洞口和权重和均验证后
一次 swap。尚无更早的道路多通道格式，不做猜测迁移。调用方可用 Value.toJson/fromJson 写入其保存边界。

## 同日后续进度

上述段落保留初次扩展的实现记录；后续已增加平均/精确自动间距、可跨 UV 接缝的平滑组，
以及由作者隧道区间生成的首尾洞口，cutTunnelHoles 启用时默认只切 portal。
当前规范、迁移及测试见 [间距、平滑组和入口洞口](2026-10-14-road-spacing-smoothing-portals.md)。
完整隧道足迹仍可通过 tunnelHolesAtPortalsOnly=false 明确选择。

## 架构和验证

适用 R-FACE（六面声明）、R-MECH（同一 procgen 域内直接组合）、R-COST（导入后显式烘焙/写回）、
Result/nodiscard、单一 owner、持久化原子性、线程/重入、确定性和裁剪证据规则。
没有新增 ECS System、全局时间、随机源、回调、可选 provider 或跨模块向上 include；无架构例外。
调用同步且 owner-thread affine。浮点几何是同构建容差契约，不承诺跨编译器位级一致。

test/procgen_road_extensions.cpp 覆盖变形法线/UV、源裁切、分段重复/焊接/反转、保存、
多臂路线的拆分身份迁移与事务撤销、地形跨分辨率通道、桥隧保护、JSON 历史往返、
undo/redo、后一个 tile 失败时回滚和 redo 保留。
组合用例带真实车道连接和禁止转向，验证拆分撤销后的导航网格 positions/indices 与原值逐项一致；
另一用例拒绝相同 prefab 标签但实际源网格不同的接续。

scripts/road_parity/native/FunctionalFixtures.cpp 追加 prefab-deformation、multi-arm-facilities 和
terrain-multichannel 场景。地形场景实际执行 apply / undo / JSON capture / restore / redo，
从恢复后的唯一 owner 上传高度与纹理权重派生的网格；显示地形额外下移 0.04 米防止共面闪烁。
场景拥有 authoring.json、mesh_data.nut 和 terrain_state.json，不通过 surface-only parity adapter。
真实 EVEngine readback 只证明当前核心派生几何的渲染，不是 Unity 视觉对齐证据。

验证记录（2026-10-14）：STATIC / SHARED 各通过 24 个用例、5,951 项断言，无警告或失败；
正式 procgen-core-only / OBJECT 的 EVProcgenCoreCheck 编译通过（L1）。独立 CPU harness 提供相关
源闭包的 STATIC / SHARED 链接证据（L2），不宣称完整引擎或运行时 L3/L4 裁剪。
三个新场景的 frame、snapshot、上传网格和 SHA-256 位于 ignored build/road-parity/extensions-scenes/；
最终生成器复生成后，作者数据、上传网格、主脚本和地形历史字节均与捕获输入完全相同。
默认 make check 仍被既有 IStateProvider 的 captureState/restoreState/resetToDefaults bool API 违规阻断；
没有修改 baseline 或添加 allowlist。本次增量 make check CI_BASE=HEAD 通过 264 个脚本测试，
结果写入 build/road-parity/extensions-quality-head.log。最后的顺序、prefab 兼容性及 owning 洞口查询修复
另复跑 STATIC/SHARED、正式 OBJECT 核心编译、架构契约与 changed-lines 格式门禁。
扩展 CPU 道路 harness 为 49 个通过、4 个既有警告、0 个失败；警告仍为两项 NoOp 状态构造、
一项位置投影容差和一项已有节点连接撤销，不计作通过。没有启动完整引擎的数千项本地 suite。
