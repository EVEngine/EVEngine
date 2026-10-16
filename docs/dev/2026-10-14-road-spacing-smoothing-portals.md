# 道路 prefab 间距、平滑组与隧道入口洞口

日期：2026-10-14。承接 prefab、多臂设施与多通道地形写回；Unity 许可证问题未解决，继续延后双端渲染。

## 包内证据与实现范围

同一 EasyRoads 包的 side_objects.html（E15）说明 Distance between objects、默认启用的
Average by Road Length、关闭平均后使用精确距离；Mesh Editor 的 Smooth Normals 用于分段之间；
Cut Holes 在隧道首尾开孔，手册仍标为 Preview 且依赖 terrain heightmap scale。
这些是行为依据，不声称复刻封闭 DLL 的内部算法。

## 自动间距

RoadFacilityPrefab.automaticSpacing 默认 false，保留已有 middleCopies 显式重复。
自动模式以完整设施弧长扣除非空 startMesh/endMesh 的源段长度，源 middle length 为米制重复距离。
averageSpacing 默认 true：取最接近的正整数重复数，仅调整 middle 段到剩余空间，起终段保持源长度。
关闭平均时取向上整数重复数，middle 保持精确源长，最后不足一个段的部分裁切源三角面，不拉伸或超出终点。
不生成缺少源几何的端面，不改变源 UV。没有随机源或随机间距。

重复数上限 4096；端部占满弧长、超预算、折叠或坍缩均返回结构化失败。
昂贵工作仍在显式 bake 阶段；已有 maximumMeshElements 同时限制预估细分工作与输出。
repeatSourceLength=0 从路径推导；拆分时由唯一 RoadNetwork owner 保存原始全段弧长，
两个片段只裁切 sourceStart/sourceEnd，不能各自重置重复节奏。保留该跨度的片段长度编辑会形变源窗口。
自动模式反转时 reverseRepeatSequence 保存方向；exact 模式按此保留余段方向，余段改在开头裁切镜像源末端；
反转撤销继续使用原始 style 的 owning 逆操作，保证作者数据逐项恢复。
人工拼接独立设施时连接段独立计算重复数；同一来源拆分出的节点段使用保留跨度填补源窗口。

## 平滑组

每个 RoadPrefabTriangle.smoothingGroup 范围 0..65535；0 保留导入法线，正整数为显式平滑组。
变形后以世界坐标量化 1e-5 米与组号聚合面积加权面法线。不同组保留硬边；
同组可跨源顶点重复、重复段接缝和 UV 接缝平滑，UV 本身保持独立。
焊接键额外包含组号，不会让不同硬边组共享可变法线。相反面导致平滑向量抵消时失败，
不能发布无效法线。分组索引与数据都由源 prefab 值拥有，无外部引用或额外缓存 owner。
这是同次设施 bake 内的组语义；独立道路/路口 bake 批次间的全局平滑仍是差异。

## 仅入口开孔

RoadStructureInterval.portalStartLength/portalEndLength 默认各 2 米，0 禁用对应入口；非负有限且各不超过 10000 米。
结构烘焙把区间首尾足迹标为 asphalt.tunnel.portal，中间保持 asphalt.tunnel；
只分割驾驶面标记，不在这些内部边界增加隧道壳端盖。
孔口深度按弧长定义；两端范围相交时其并集覆盖短隧道。
反转交换两端深度；拆分减去离原端点的弧长，所以不会在人工拆分节点新建入口。
洞口网格标记是作者区间的派生数据，不能作为第二套可编辑事实。

cutTunnelHoles 仍默认 false。启用后 tunnelHolesAtPortalsOnly 默认 true，仅 portal 足迹写 holes；
屋顶地形、detail、tree 留在未开孔区，普通道路覆盖仍优先。显式 false 保留旧全隧道投影切孔模式。
仅有旧 asphalt.tunnel 的外部网格没有可推导入口，portal-only 模式保持无孔；调用方应重新烘焙或明确选全足迹。
栅格边缘采用 2 毫米世界容差，避免弧长采样产生的毫米误差改变恰好落边界的格点。
地形输出仍受各通道分辨率限制，不承诺跨路径重参数化与不同网格精度的位级一致。

RoadTerrainWorkspace 和已有 TerrainWorldWorkspace 都在一个候选中发布全部通道。
undo/redo、失败保留 redo、capture/restore 历史沿用原子契约；不添加第二个持久化 World owner。

## 保存与迁移

eve.procgen.roadNetwork schemaVersion=1 的增量可选字段保存上述间距策略、源跨度、反转余段方向、
与 triangle 数量一致的 smoothingGroups 数组及两端洞口深度。缺失字段分别迁移为显式重复、
平均偏好、自动推导跨度、正向、保留源法线和两端 2 米。未知对象字段忽略，未知版本拒绝；
字段类型、数量或范围错误在私有候选中拒绝，不能部分替换网络。
Terrain 的 procgen.road.terrain version=1 保存写回后的完整通道与 undo/redo 历史；不重复保存可派生 bake 标记。

## 验证与剩余范围

test/procgen_road_spacing_portals.cpp 覆盖平均/精确重复、余段反转与拆分相位、保存及事务撤销，
非法平滑组原子拒绝和旧字段迁移，UV 接缝平滑与不同组硬边，首尾开孔与覆土/植被保护，
拆分后的洞口栅格、地形历史恢复、真实 native World 写回与失败后 redo 保留。
FunctionalFixtures.cpp 增加 prefab-auto-spacing、tunnel-portals，完整作者场景共 10 个；
入口夹具保存并恢复历史，从实际 holes 跳过地形面。渲染证据仅是 EVEngine，Unity 对照继续等待账户。

适用规则为 Result/nodiscard、单一 owner、持久化迁移与原子性、公开成本、线程/重入及六面模块契约。
继续同 procgen 域直接组合，无新 ECS System、Link、时间、RNG、可选 provider 或向上 include，无架构例外。
STATIC/SHARED CPU 闭包与正式 procgen-core-only OBJECT 编译提供 L1/L2 证据，不宣称完整引擎 L3/L4 裁剪。

随机摆放、复杂设施自动分类、命名预处理、全局跨路口平滑、入口 offset 与地形吸附仍未对齐；
详见更新后的 [EasyRoads 剩余差异](2026-10-13-easyroads-core-comparison.md)。


验证记录（2026-10-14）：STATIC/SHARED 各 28 项、6,069 个断言，无失败或警告；正式
procgen-core-only/OBJECT 的 EVProcgenCoreCheck 编译通过。测试日志位于 ignored
build/road-parity/spacing-tests-static.log 与 spacing-tests-shared.log。
两个真实 engine-owned readback 帧及 authoring/mesh/history 哈希在
build/road-parity/spacing-scenes/{prefab-auto-spacing,tunnel-portals}/evidence.json，最终生成器
复生成数据与捕获时上传数据逐字节一致。场景使用已有 renderer 上传本工作树 C++ 核心网格；
材质为 unlit，平滑法线以数值测试验证，不声称已完成光照或 Unity 视觉对齐。
默认 make check 对 origin/dev 报告分支既有 IStateProvider.h 三项 bool API，以及
RenderSystem.h 两项原始指针生命周期注释违规；这两个文件本次均未修改。
没有新建 baseline、allowlist 或关闭规则，原始结果保留在 spacing-quality-default.log。

扩大 CPU 道路测试集合为 53 个通过、4 个既有断言警告、0 个失败（8,070 项通过断言）。
四项警告仍对应 NoOp 状态构造两例、投影容差和已有节点连接撤销，不计为全套绿色。
结果在 spacing-all-road-tests.log；本轮没有启动完整引擎的数千项本地 CTest。

本次增量 make check CI_BASE=HEAD 完整通过（264 项脚本测试，271.502 秒），日志为
spacing-quality-head.log；最终另外复核 architecture contracts、changed-lines 格式与 diff 空白。
该增量结果不替代上述默认 origin/dev 门禁失败，也未改动默认基准。
