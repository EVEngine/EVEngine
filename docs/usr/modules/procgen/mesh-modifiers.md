# Procgen 网格修改与融合

[返回模块概览](../procgen.md)

Geometry Stroke、Mesh Modifier Graph，以及静态合并 / 动态融合接触带。

### Geometry Stroke 几何绘制

`eve.ProcgenGeometryStroke()` 创建一个 UI 无关、调用方持有的几何绘制会话。输入层负责鼠标、触摸、
VR 控制器或物理射线，只把命中的世界点传给 `addPoint()`；会话不保存窗口、相机、碰撞体或回调指针。
`setShape()` 选择官方三种截面 `quad`、`triangularPrism` 或 `cube`，`setInputSpace()` 选择保留三维坐标的
`spatial` 或约束到指定 Y 平面的 `planar`。`setSize()` 配置宽度和深度，`setMinimumSpacing()` 在高频输入时
确定性过滤过近采样点。配置失败不会改变点序列或 revision。

`undo()` 撤销最近一个已接受点，`clear()` 清空笔迹；`buildMeshResult()` 以 owning `MeshBuild` 返回连续共享
截面环的三角网格，原会话仍可继续编辑。`getShape()`、`getInputSpace()`、`getPointCount()` 和
`getRevision()` 可供运行时或编辑器面板查询。脚本可将返回网格交给 `procgen.uploadMesh()`，也可送入
Mesh Modifier Graph 的 `mesh.input`，继续 Bend、Noise、Cut、Weld 等融合处理。

### Mesh Modifier Graph 与融合执行

`newMeshModifierGraph()` 创建独立于 PointGraph 的网格修改图。它复用相同的稳定节点、
Result 诊断、计划统计和缓存约定，但 mesh pin 只传递 owning `MeshBuild`，不会把点生成语义
混入网格拓扑。首版节点包括 `mesh.input` / `mesh.output`、`deform.transform`、
`deform.bend`、`deform.twist`、`deform.noise`、`deform.radial`、`deform.smooth`、
`deform.angularBend`、`deform.spherify`、`deform.ffd`、`deform.morph`、
`deform.spline`、`deform.splinePath`、`mesh.splineTube`、`mesh.splineRibbon`、`mesh.splineExtrude`、`mesh.subdivide`、`mesh.cutPlane`、
`mesh.append`、`mesh.boolean`、`mesh.projectUv` 和 `mesh.weld`。

`mesh.boolean` 接收两个闭合三角网格，提供 `union`、`difference` 与 `intersection` BSP 实体运算，
交点处插值法线和 UV，并把切割体产生的面放入 `cutter.*` 分组。`mesh.projectUv` 为动态或程序化网格
生成 `planar`、`box` 或 `spherical` UV；两者都是拓扑边界节点，不参与逐顶点融合。

所有可融合的逐顶点节点都反射 `maskX/Y/Z`、`maskRadius`、`maskFalloff` 和 `maskInvert`
参数；半径为零时关闭遮罩，否则仅在球形选择区域内按幂次衰减混合结果。`deform.spline`
使用四个三次 Bezier 控制偏移沿选定轴弯曲网格。`mesh.cutPlane` 的 `cap=1` 会追踪闭合
截面轮廓、生成独立 `__cut_cap` 三角形组并保持原网格组与 metadata。
`deform.noise` 使用仅由位置和 seed 决定的三轴位移；硬边处共享位置但使用不同法线的重复
顶点仍会得到相同位移，因此不会撕开 UV seam 或平滑组边界。
`deform.soundReact` 消费音频适配器提供的归一化 `level`，在超过 `threshold` 后按 `strength`
绕指定轴径向位移；`frequency`、`phase` 与 `axis` 控制网格上的空间波形。相同位置的硬边/UV seam
副本得到相同位移，不会因法线分裂而撕开。节点不持有 Audio、FFT
或播放设备，游戏可按帧把 RMS、频段能量或包络写入 `level`，离线图执行仍保持确定性，并可与
其他逐顶点 modifier 融合及使用通用空间 mask。
`deform.effector` 提供一至四个独立球形权重节点。每个节点拥有中心、位移方向、半径和权重，
全局 `density` 控制径向衰减，`multiplier` 控制整体强度；多个节点的影响确定性叠加。它是可融合的
逐顶点节点，可与 Bend、Twist、Noise 等在一次顶点遍历内执行，并支持通用空间 mask。
`deform.meshFit` 接收源网格和目标表面网格两个输入，沿指定 direction 对每个源顶点执行有界
射线-三角形求交；`maxDistance` 限制搜索距离，`bidirectional` 可启用双向贴合，`surfaceOffset`
沿命中面法线保留间隙。节点只消费 owning 网格快照，因此既可由 Physics collider 适配器提供目标，
也可完全离线运行，不引入 procgen 到 physics 的反向依赖。

### 静态合并与动态融合（接触带）

`MeshMergePlan` / `eve.mergeStaticMeshes(plan)` 是提交式静态合并入口：用 `appendSource` /
`clear` / `getSourceCount` 组织源网格后一次合并。**接触带边缘/材质融合默认关闭**；
`setEnableContactBlend(false|true)` 开关，`getEnableContactBlend` 查询。开启后用
`setContactBlend(...)` 设置半径与强度。可选 `setWeldTolerance`（正值在**接触融合之后**
硬焊，便于 soft-snap 后把贴合顶点并成连续壳）、`setSimplifyQuality`（(0,1) 启用 GTS 简化）、
`setPivotMode`（0 = 首源枢轴，1 = 世界原点）。流水线顺序：拼接 →（可选）接触融合 →
（可选）weld →（可选）simplify → 枢轴。融合权重写入顶点色 alpha（`contactBlend`），不改 UV。
`softSnapPositions=true` 时按变形后的三角几何重算法线（不再向命中面法线混），再做一轮
1-ring 法线平均，减轻 soft-snap 颈处的光照折痕。纯拼接仍可用
`combinePcgStaticMeshes`；canonical 作者路径是 `mergeStaticMeshes`。

共享 CPU 原语为 `meshContactBlendResult`（多 sourceId）与
`meshContactBlendAgainstSurfaceResult`（A 贴 B）。`deform.meshAdhere` 图节点与
`MeshAdhereLive` 会话走后者：`activate` 后用 `setParams` / `setSource` / `setSurface`
随时改参数或源，`isDirty` / `evaluate` / `getRevision` 驱动实时重算；
`derivedMeshResult()` 返回 owning 派生快照（会话保持活跃，便于上传显示），
源网格保持权威，`bakeToMesh` 可选冻结，`removeSetup` 结束会话，`isActive` 查询状态。
完整并排示例见 [`examples/mesh-contact-fusion/`](../../../examples/mesh-contact-fusion/)。
设计说明见 `docs/dev/superpowers/specs/2026-10-08-mesh-merge-adhere-plan.md`。

连续、单消费者的逐顶点 deform 会编译成一个 CPU segment，一次遍历完成；smooth、append、
weld 等需要邻接或拓扑处理的节点是明确的融合边界。参数或连线变更递增 revision 并使缓存失效，
失败执行不会发布部分修改的网格。
`getFusedOperationCount()` 可用于确认计划中被合并的逐顶点操作数量。

正式的多段 3D 路径由 `newSplinePath()` 创建，支持 `linear`、`catmullRom`、`quadraticBezier` 与
三次 `bezier`，以及
开放/闭合路径。`addPoint()` / `setPoint()` 接收锚点、入切柄偏移和出切柄偏移共九个浮点数；
`removePoint()` 与 `clear()` 修改 owning 控制点集合。`evaluateResult()` 按归一化段参数采样，
`evaluateDistanceResult()` 与 `lengthResult()` 使用有上限的弧长表按世界距离采样，
`closestPointResult()` 返回近似最近点。返回的 `ProcgenSplineSample` 通过 `getX()`、`getY()`、
`getZ()`、`getTangentX()`、`getTangentY()`、`getTangentZ()` 和 `getNormalizedDistance()` 查询纯值，
不会暴露路径内部指针。`setKind()`、`setClosed()`、`getKind()`、`isClosed()`、
`getPointCount()`、`getSegmentCount()`、`getChunkCount()` 和 `getRevision()` 用于编辑器与调试面板。
`setPointChunkBreak(index, true)` 在指定点前断开路径，弧长、最近点、frame 和生成器都不会跨越空隙；
`setPointChunkBreak(index, false)` 重新连接。采样值的 `getChunkIndex()` 明确指出其所属 chunk。

每个控制点还携带 `pitchDegrees`、`yawDegrees`、`rollDegrees`、`scaleX` 与 `scaleY`。脚本用
`setPointRotation()` 和 `setPointProfile()` 原子更新；`evaluateResult()` 返回值可通过
`getPitchDegrees()`、`getYawDegrees()`、`getRollDegrees()`、`getScaleX()`、`getScaleY()` 读取段间插值结果。
非法或非正缩放不会改变路径 revision。Tube 使用双轴缩放生成椭圆截面并计算逆缩放法线，Ribbon
使用横向缩放控制宽度、纵向缩放控制厚度。版本一旧快照缺少这些字段时按 0/1/1 迁移读取。

挤出工具使用 `sampleFramesResult()` 取得按段参数或弧长均匀分布的 owning frame 数组。frame 通过
最小旋转的平行输运连续传播 side/up，并在闭合路径上均摊 holonomy 误差，使末端 frame 精确回到
起点；恒定 `rollDegrees` 在输运完成后绕切线施加。相同路径与采样参数在 CPU 浮点容差内确定。

`travelResult(distance, wrapMode, samplesPerSegment)` 提供显式距离驱动的 `clamp`、`loop`、
`pingPong` 行为，不读取 wall clock；调用方可注入固定步长或回放时间。`distributeResult()` 返回 owning
`ProcgenSplineDistribution`，用 `getCount()` 与 `getSampleResult()` 读取按弧长均匀分布且不持有路径引用
的实例位置；闭合路径不会重复首尾点。`applyShapePreset()` 原子生成 `line`、`circle`、`arc`、
`spiral` 或 `wave`，参数失败时保留原路径和 revision。
需要放置完整朝向时，`travelFrameResult()` 与 distribution 的 `getFrameResult()` 返回 owning
`ProcgenSplineFrameSample`；除位置、切线、roll 和 scale 外，还可读取正交的 `getSideX()`、
`getSideY()`、`getSideZ()`、`getUpX()`、`getUpY()`、`getUpZ()`、`getForwardX()`、
`getForwardY()` 与 `getForwardZ()`，因此实例不需要再次猜测
世界 up，也不会在近垂直路径上翻转。
运行时线框显示使用 UI/graphics 无关的 `polylineResult()` 生成 owning `ProcgenSplinePolyline`。
脚本通过 `getCount()`、`getPointResult()`、`getChunkCount()`、`getChunkPointCount()`、
`getChunkPointResult()` 与 `isClosed()` 读取后，可直接交给 graphics 的
`newPrimitivePolyline3D()`，或交给其他渲染后端；闭合路径不会在数组中复制首点，闭合边由适配器表达。

`deform.splinePath` 通过 `setNodeSplinePath()` 复制一份路径快照，沿 `axis` 归一化源网格并将
截面搬运到路径的切线坐标系；`roll`、`scale` 与 `weight` 控制扭转、截面缩放和混合。
图不持有脚本路径的借用引用，因此路径后续编辑不会在执行中产生悬垂引用；需要更新时再次绑定，
revision 与缓存会一起失效。旧的 `deform.spline` 保留为单段控制偏移兼容节点。

`mesh.splineTube` 是无需 `mesh.input` 的纯生成节点。它沿绑定路径按弧长均匀生成带 UV 和解析法线的圆形
截面，`radius`、`pathSegments`、`radialSegments` 与 `roll` 控制采样；开放路径可用 `cap=1`
生成独立 `caps` 三角形组，闭合路径会强制复用首环位置与法线作为末环，避免几何和光照接缝。
节点限制路径段数、径向段数及一百万顶点预算。编辑器 preview 的 `inputNode` 可留空，因此同一
GraphDocument 可以直接预览生成器到 `mesh.output` 的链路。

`mesh.splineRibbon` 使用相同的弧长 frame 生成平面道路或轨道；`width` 控制横向宽度，
`thickness=0` 输出单独的 `surface`，正厚度会额外生成独立法线和 UV 的 `bottom`、`sides` 与
开放路径 `caps` 分组。闭合路径自动连接最后一段并省略端盖。断开的每个 chunk 独立生成端面和索引，
不会产生跨越空隙的桥接三角形。它同样是无需 `mesh.input` 的纯生成节点。

`mesh.splineExtrude` 接受由 `setNodeSplineProfile(node, [x0,y0,...], closed)` 复制的 owning 二维截面。
开放截面可生成 U 型槽、滑道或带状轮廓，且不会隐式封口；闭合的简单多边形会沿相同 parallel-transport
frame 挤出，并在开放路径且 `cap=1` 时用独立顶点和端面法线进行耳切封盖。逐控制点 roll 与双轴 scale
同样作用于自定义截面；非法、退化或超过 256 点的截面会在修改 revision 前以结构化诊断拒绝。

编辑器 authoring 使用 UI 无关的 `SplinePathDocument`。控制点由稳定 ID 和显式 order 标识，移动
锚点或切柄生成 `spline.point.set.v1` 可逆操作，删除和路径设置同样携带完整 inverse payload，
可直接进入编辑器事务栈。持久化格式为 schema `eve.procgen.splinePath` version 1；未知字段忽略，
已知字段先在隔离 candidate 中完整校验，失败不会污染当前文档。`deform.splinePath` 与
`mesh.splineTube`、`mesh.splineRibbon` 与 `mesh.splineExtrude` 的 GraphDocument 节点将该快照保存在 `splinePath` 属性中；
自定义挤出还将 owning `splineProfile` 对象保存在节点属性里。编译和 preview 时生成 owning runtime path，因此工具预览、
撤销/重做和游戏运行共享同一份路径语义，但不共享可变存储。

`SplinePathDocument` 还提供一条操作对应一次撤销的 `makeSnapAll()`、`makeAppendChunk()`、
`makeDeleteChunk()`、`makeSetChunkBreak()`、`makeSetPointRotation()`、`makeResetPointRotation()`、
`makeCenterPoint()` 与 `makeMirrorAxis()`，对应工具箱的 Snap All、Add/Remove Chunk、Split/Connect、
Reset Rot、Center Control Point 与 Flip X/Y/Z，而不是让具体 UI 拼接多次状态修改。

`SplinePathGizmoBuilder` 将文档投影成 renderer-neutral `GizmoSnapshot`：每个 chunk 的采样曲线由稳定
`spline.chunk.*.segment.*` 线段组成，锚点和 Bezier 入/出切柄分别使用 `spline.anchor.*`、
`spline.in.*`、`spline.out.*` 拾取 ID。只有一个点的未完成路径仍显示可编辑锚点，并以诊断
提示需要第二个点，而不是让工具消失。`SplinePathDragSession` 使用摄像机朝向的拖动平面；
pointer move 只更新 detached draft 和临时 gizmo，`finishDrag()` 才返回单个可逆 point-set
操作。拖动期间若文档 revision 改变，会返回 Conflict 并丢弃草稿，避免覆盖其他编辑来源。
`SplineGizmoStyle` 提供 node/handle size、line/text color 与 point label 开关，供编辑器实现
Always Draw Gizmos、Node Size 和颜色面板而无需改变 spline 文档本身。

`SplinePathBinder` 用稳定 point ID、scene host ID 和 object ID 保存跨域链接，不持有文档、场景节点或
查询服务的指针。每次 refresh 会先把所有来源解析成纯值并在隔离文档副本中应用，只有全部成功才整体
提交；任一对象或控制点失效时原文档不变，链接仍保留以便场景重载后重建。绑定快照使用
`eve.procgen.splineBinder` version 1，并在装载时原子拒绝错误 schema、目标不匹配或重复链接。
`SceneQuerySplineBindingSource` 是可选 scene provider 的适配器：裁剪掉 scene 模块时显式返回 Unsupported，
而不是静默冻结控制点。

```squirrel
local graphResult = procgen.newMeshModifierGraph();
if (!graphResult.ok) throw graphResult.status.summary;
local graph = graphResult.value;
graph.addNode("source", "mesh.input");
graph.addNode("move", "deform.transform");
graph.addNode("twist", "deform.twist");
graph.addNode("out", "mesh.output");
graph.connect("source", "move", 0);
graph.connect("move", "twist", 0);
graph.connect("twist", "out", 0);
graph.setNodeFloat("move", "x", 2.0);
graph.setNodeFloat("twist", "angle", 0.35);
graph.setNodeMesh("source", sourceMesh);
local output = graph.executeResult("out");
if (!output.ok) throw output.status.summary;
local modifiedMesh = output.value;
```

编辑器侧使用 schema 版本 1、domain `procgen.meshModifier` 的通用 `GraphDocument`；
`MeshModifierGraphDomain` 负责类型连接规则、断连输入/重复输入/环路校验及隔离预览。
GraphDocument 是 authoring state 的唯一所有者，运行时图只作为编译产物存在。

`procgenEditor.createMeshModifier(targetId)` 返回统一的 `MeshModifierEditor` controller；
`configureWorkspace()` 一次安装 Modifier Graph、Mesh Preview、Inspector、Spline、Sculpt & Damage 和 UV Paint
六个语义面板，`activateTool()` 在 graph/spline/sculpt/uvPaint 之间切换焦点。
controller 只记录稳定 target id 与各文档 revision，不复制 GraphDocument、SplinePathDocument、
MeshDeformationSession 或 UV Paint 的可变状态；`observeRevision()` 会拒绝倒退的陈旧 revision。

需要鼠标、触摸或碰撞驱动的实时塑形时，使用 `newMeshDeformationSession()`。Session
分别持有 original/current 和最多 32 个 undo 快照，提供 `inflate`、`dent`、`flatten`、
`smooth` 与 directional brush；`bake()` 显式更新恢复基线，`restore()` 回到最近一次基线。
每次获取网格都通过 `currentMeshResult()` 返回 owning 快照，脚本不会持有内部顶点指针。
先以 `initialize()` 设置基准网格，再用 `applyBrush()` 或 `applyDirectionalBrush()` 原子地
提交笔刷；`getUndoCount()` 暴露当前撤销深度，`isInitialized()` 可在交互工具接收输入前
检查会话是否已经就绪。

运行时碰撞形变使用 `applyImpact()`：调用方传入已经转换到网格空间的接触点、冲量向量、
半径、plasticity、hardness 和相对最近一次 bake 基线的最大位移。多个命中会累积塑性形变，
但每个顶点都会被 `maxDisplacement` 限制；每次成功命中仍是一个可撤销的原子提交。
Procgen 不保存物理世界或 contact 指针，Physics、武器和脚本只负责把各自事件转换成这份
纯值命令，因此 provider 缺失时手动雕刻与离线图执行不受影响。
高密度重复碰撞可先调用 `prepareImpactVertexBlocks(divisionsPerAxis)` 建立有界均匀空间块；
随后 `applyImpact()` 只扫描与影响球相交的块，并保持与全量扫描逐顶点一致的结果。
`hasImpactVertexBlocks()` 与 `getImpactVertexBlockCount()` 公开加速状态。普通笔刷、Surface、Slime、
顶点移动、恢复或撤销会显式使索引失效，避免查询陈旧分区；连续的有界 Impact 可复用同一索引。
`applyBrushGpu()` 与 `applyImpactGpu()` 使用可选 GPGPU capability 执行真实 compute shader 顶点位移，
支持 inflate、dent、flatten、directional、smooth 和有界 plastic impact。provider 缺失或设备不可用时
明确返回 Unsupported，不会静默回退 CPU；CPU Session 仍是唯一权威状态，只在 GPU 结果尺寸与有限值
校验通过后重建法线并原子提交 undo。Vertex Blocks 仍是 CPU `applyImpact()` 的独立批量碰撞加速路径。

动态纹理绘制使用 `newDynamicMeshUvPaintSession()`。它保存当前 mesh 快照，把 triangle hit 的三维点
按重心坐标映射到 UV，再委托既有 `UvPaintSession` 完成像素事务和 undo，因此不会复制 Ink Arena/UV Paint
的 raster 实现。`updateMesh()` 可在每次变形后替换网格而保留已绘制纹理；无 UV 的网格可先通过
`mesh.projectUv` 自动投射。`paintSurfacePoint()` 提交一次表面命中绘制，`currentImageResult()` 返回 owning
像素快照；`getMeshRevision()` 与 `getPaintRevision()` 分别报告几何和像素事务 revision。
Interactive Surface 使用 `applySurfaceContact()` 接收纯值接触点、法线、切向速度、半径、压入深度、
拖拽、衰减和塑性比例；物理、鼠标、触摸与 VR 适配器负责坐标转换，不会被 Session 持有。
`recoverSurface(dt, recoveryRate)` 使用调用方注入的 dt 做指数恢复，不读取 wall clock，也不会每帧污染
撤销历史。塑性部分写入独立 equilibrium，弹性部分回弹；`undo()`、`restore()` 与 `bake()` 同时维护
网格和 equilibrium，因此两者不会分叉。
Mesh Slime 在相同 Session 中用 `applySlimeImpulse()` 向局部顶点速度场注入冲量，再由
`stepSlime(dt, stiffness, damping, maxSpeed)` 执行有界弹簧-阻尼积分。dt 由调用方注入，速度受
`maxSpeed` 限制，相同初始状态与输入序列产生相同顶点结果；Undo 同时恢复网格、equilibrium 与速度场。
动态碰撞代理通过 `configureColliderRefresh(mode, interval, offsetX, offsetY, offsetZ)` 配置 `once`、
`everyFrame`、`interval` 或 `manual` 调度。`updateColliderRefresh(dt)` 只返回本帧是否应发布，
`colliderMeshResult()` 返回带位置偏移的 owning 三角网格快照；Physics 适配层据此安全地替换 shape，
避免在求解回调里直接重建。调度失败不会发布不完整 collider，manual 请求由
`requestColliderRefresh()` 显式触发。
运行时顶点编辑复用同一 Session：`selectVerticesSphere()` 用于鼠标、触摸或 VR 半径选择，
`selectVerticesBox()` 接收屏幕框选适配器转换后的空间包围盒，二者支持替换或追加选择。
`moveSelectedVertices()` 接收 Axis Gizmo 的局部位移；`manipulateSelectedVertices()` 提供 `pull`、
`push` 与 `grab`，每次几何提交形成一个 Undo 快照。选择本身是瞬态工具状态，可用
`clearVertexSelection()` 清除并由 `getSelectedVertexCount()` 查询，不会污染网格历史。
工具侧可用 `selectedVertexCenterResult()` 取得不暴露内部选择数组的纯值中心，再由
`MeshVertexAxisGizmoBuilder` 生成带稳定 `mesh.axis.x/y/z` 拾取 ID 的三色箭头快照。
`pickAxisResult()` 把世界射线映射为轴名，最终位移仍通过 Session 的 `moveSelectedVerticesResult()`
提交，因此渲染、拾取与可撤销变形之间没有第二份权威状态。
完整 Box3D 集成见 [`examples/mesh-impact-lab/`](../../../examples/mesh-impact-lab/)：示例显式开启
Shape3D hit events，按目标位于稳定 A/B 侧修正法线方向，过滤小冲量，并在 `Subdivide -> Weld`
准备出的共享邻接网格上提交 Impact。渲染网格在命中后立即替换；静态碰撞代理采用 deferred
策略，不在求解回调或同一物理 step 内重建。

完整运行示例见 [`examples/mesh-modifier-lab/`](../../../examples/mesh-modifier-lab/)：它在同一场景中
渲染原网格、融合 Bend/Twist、Noise/Spherify、FFD、封口 Cut、Spline 和可撤销
Sculpt 七种结果。

