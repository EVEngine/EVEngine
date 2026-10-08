# EasyRoads3D Pro v3.2.4f5 道路与路口算法分析

## 1. 范围与结论

本文分析对象是用户提供的：

`EasyRoads3D Pro v3 v3.2.4f5.unitypackage`

Unity 包已还原到：

`reference/EasyRoads3D-Pro-v3.2.4f5-unpacked/`

三个核心托管程序集已用 ILSpy 11.0 反编译到：

- `reference/EasyRoads3D-Pro-v3.2.4f5-decompiled/runtime/`
- `reference/EasyRoads3D-Pro-v3.2.4f5-decompiled/editor/`
- `reference/EasyRoads3D-Pro-v3.2.4f5-decompiled/delaunay/`

包内只有两个明文辅助脚本。道路、路口和编辑器算法分别位于
`EasyRoads3Dv3.dll`、`EasyRoads3Dv3Editor.dll` 和 `DelaunayER.dll`。反编译中可见大量混淆名称，
但主要数据结构、几何过程和大量方法仍可阅读；控制流缺失的原因应单独核验。

最重要的结论是：保留下来的 Flex Connector 辅助算法不只是“所有入口点连接到一个中心点”。可确认的机制包括：

1. 道路端点受连接器 socket 的位置与方向约束；
2. 接入道路按极角排序并建立左右邻居；
3. 相邻道路边缘之间生成可调半径、曲率和分段数的 rounding curves；
4. 完整道路横截面沿 rounding curves 扫掠；
5. 根据优先级和分区混合使用条带网格与二维 Delaunay，部分剖分包装使用重心过滤；
6. 主次道路具有不同分支；两路变宽的明确实现位于 `ERIConnector`；
7. 部分路口计算使用局部 XZ 与横截面高度，接入道路还会逐顶点对齐连接平面并平滑过渡。

这些机制提供了分析不等宽 Y 型、坡道路口和曲线上坡汇合的方向，但不构成对 EVEngine 每项缺陷的已验证根因。
本次为静态源码审阅，未运行 Unity，也未验证所有辅助分支的可达性。第 17～19 节为对照与建议，不是参考系统的质量保证。

## 2. 证据可靠性

本文使用三个可靠性等级：

- **确认**：可从反编译方法体和字段的数据流直接验证。
- **高可信推断**：方法名被混淆，但调用点、字段写入和几何计算一致。
- **待验证**：完整调用关系尚未证实，或需要核对原始 IL、程序集版本及 Unity 运行状态。

`Unknown result type` 提示类型解析不足或 IL 问题；缺少 Unity 引用是可能原因，不能仅凭提示断言 IL 损坏。
`ERCrossings.OOCDDDDDCC()` 调用状态准备函数后会调用 `QDDDQODDQDQDQDD.OOOQDOQCDC()`，后者在当前 C# 输出中只有 `return false`。
`HandleSidewalks()` 也为空。现有证据不能确定这是原程序集占位、版本差异还是反编译问题，不能直接归因于混淆。
本文按可读方法体说明局部算法，不声称已恢复完整 Flex 调度或证明所有分支都能运行；未核对原始 IL 的入口不标为“IL 已确认”。
下文行号以此次本地反编译快照为准。

## 3. 核心数据模型

### 3.1 道路

`ERModularRoad` 持有：

- 用户 marker；
- 采样后的 `splinePoints`；
- 道路横截面 `roadShape`；
- 起点与终点连接器引用；
- 对应的 connection segment；
- 车道、路肩、地形变形和 side-object 数据。

道路横截面不是只有左右两点。`ERRoadShape` 与道路类型对象保存多个二维节点，每个节点至少包含横向位置 `x`
与局部高度 `y`，还具有材质段、UV、车道和硬边语义。生成网格时，系统沿样条的每个采样帧复制完整横截面。

### 3.2 路口入口

`ERConnectionSibling` 是 Flex Connector 的核心入口记录。重要字段包括：

- `angle`、`angleControlPoint`、`dir`、`forward`；
- `roadType`、`roadWidth`、`roadShape`；
- `leftSibling`、`rightSibling`；
- `leftRoundingPoints`、`rightRoundingPoints`；
- `radius`、`cornerSegments`；
- `leftCornerAngle`、`rightCornerAngle`；
- `buildPriority`、`primaryPriorityConnection`、`secondaryPriorityConnection`；
- `leftRoadIndent`、`rightRoadIndent`；
- 左右 sidewalk、crosswalk 和 terrain surrounding；
- `roadVecs`、`priorityPointsMain`、`normalIndexes`；
- 车道连接数据 `laneData`。

它相当于一个包含几何、横截面、材质和导航语义的 connection socket，而不是一个普通图节点。

### 3.3 路口

`ERCrossingPrefabs` 保存路口实例状态、入口数组、连接道路、局部 mesh、surface mesh 和 sidewalk 对象。
`ERCrossings` 负责更新入口方向、刷新道路类型、发起 Flex Connector 重建和车道连接更新。

核心几何主要集中在经过混淆的 `QDDDQODDQDQDQDD` 中。虽然类名无意义，但其中仍保留了若干有意义的方法名：

- `GetOQDCQQCOOC`：生成圆角采样点；
- `MatchLeftRights`：匹配左右边界点数量和起点；
- `ForkTriangulationDelaunay`：分叉区域 Delaunay；
- `MatchInnerOQDCQQCOOC`：内部轮廓匹配；
- `SetPriorityConnection`：入口优先级；
- `GetAdjacentMainRoadAngle`：相邻主路夹角。

## 4. 普通道路算法

### 4.1 marker 到样条

主更新在 `ERModularRoad.cs:3344` 调用本类 `OODOOOCCQC()`（定义于 10351 行）。
`ODODOQDDOQ.OODOOOCCQC()` 是分段辅助实现；下面具体控制类型来自该辅助方法，不能将两者视为同一入口。

已确认的流程：

1. 收集当前构建区间的 marker 位置与 `splineStrength`；
2. 在区间两端补充前后控制点；
3. 如果道路连接到 prefab/Flex Connector，则用连接器提供的控制点替换普通端部控制点；
4. 对普通曲线 marker，调用 `ERModularRoad.OCQQQOOCDD(p0,p1,p2,p3,t,tension)` 采样；
5. 先用约 `0.2 / segmentLength` 的参数步长走曲线，再按 `faceDistance` 筛选实际顶点；
6. 记录累计三维距离、marker 到 spline index 的映射以及方向；
7. 若启用 terrain contour，再对采样点高度进行地形调整。

`ERModularRoad.OCQQQOOCDD()`（14137 行）的实际公式为：

```text
m1 = tension * (P2 - P0)
m2 = tension * (P3 - P1)
C(t) = (2t³ - 3t² + 1)P1 + (t³ - 2t² + t)m1
     + (-2t³ + 3t²)P2 + (t³ - t²)m2
```

这是三次 Hermite 形式的 Cardinal 样条；`tension = 0.5` 时为均匀 Catmull-Rom。
`splineStrength` 调整切向量尺度，不是直接指定曲率。`faceDistance` 控制采样密度，主采样器还有角度及端部条件。
细步进加距离筛选不是严格等弧长求解，也没有由这些代码直接给出的最大弦误差保证。

### 4.2 marker 控制类型

从 `ODODOQDDOQ.OODOOOCCQC()` 可恢复至少四类控制方式：

- `controlType == 0`：完整三次样条；
- `controlType == 1`：直线采样后，用三次样条覆盖 Y；
- `controlType == 2`：保留直线采样结果，不执行上述高度覆盖；
- `controlType == 3`：bend 辅助函数生成平面走向，再用三次样条覆盖 Y；
- closed track：按控制类型补充首尾点，不据此保证任意混合类型的曲率连续性。

这意味着系统明确区分“平面路径形状”和“纵向高度曲线”，并非所有维度始终使用同一个插值策略。

### 4.3 连接器对道路端部的约束

当道路端点连接到路口时，系统不会让普通 Catmull-Rom 自由结束。它调用
`OODCOOCDQO.OQCOCQQCQO()` 将 prefab socket 的控制点、方向和道路端部 marker 写入样条临时节点。

结果是道路接近路口时满足：

- 端点位置等于 connection center；
- 末端方向由 connection control point 决定；
- 连接器可要求 rotation priority；
- 道路重建会反向更新 Flex Connector 的入口角度。

这构成了道路与路口之间的双向约束，而不是两个网格最后才尝试焊接。

具体地，`OODCOOCDQO.cs:1393` 会用连接边界对相邻 marker 做投影/反射，构造端外虚拟控制点并插入临时节点首尾。
虚拟点通过上述 `m1/m2` 改变端部切线，并非只复制固定控制点。端点位置对齐不等于整排接缝顶点无缝或曲率连续。

### 4.4 普通道路的横截面成网格

`ODODOQDDOQ.cs:448` 起的分段网格辅助实现，在每个中心线采样点放置一排横截面顶点：

```text
vertex(i,j) = spline[i] + side[i] * shape[j].x + up * shape[j].y
```

这是无旋转、无连接修正时的概括。内部点用前后差分估计方向，端点用单侧差分；横向向量由道路方向的 XZ 分量构造，
不能假定每个分支都在投影后重新归一化。非零旋转走独立变换分支，连接附近还有第 11 节的高度修正。

N 个纵向采样、M 个横截面点形成 N×M 规则顶点排列，相邻行列的每个单元生成两个三角形。
`roadShapeMaterialInts` 决定横截面区间材质。横向 U 使用横截面累计距离，纵向 V 使用道路累计距离除以 UV 尺度。
普通道路主体不需要 Delaunay。见 `ERModularRoad.cs:3211` 与 `ODODOQDDOQ.cs:974` 起的顶点、UV 和索引生成。

## 5. Flex Connector 输入规范化

### 5.1 入口方向

`ERConnectionSibling.GetAngleControlPoint(cp,p0,p1,p2)` 使用道路端部的前三个 marker 和路口中心，计算稳定的
方向控制点。`ERCrossings.OOQDQQQCCD()` 比较新旧控制点；变化超过约 `0.0025` 时标记连接器需要重建。

因此弯道接入时采用道路真实趋近方向，不使用“路口中心到道路端点”的径向向量。

### 5.2 极角排序和邻接环

`ERCrossingPrefabs.SetLeftRightSiblings()`：

1. 按 `angle` 升序排序所有 sibling；
2. `leftSibling` 指向下一个入口；
3. `rightSibling` 指向上一个入口；
4. 首尾循环连接。

之后所有圆角、sidewalk 和主次道路算法都在这个有序环上工作。

### 5.3 尺寸可行性检查

`QDDDQODDQDQDQDD.VerifyFlexUpdate()` 在修改半径或宽度前计算连接器所需长度，并验证：

- 不得超过整条连接道路长度；
- 不得碰到下一道路 marker；
- 修改半径后仍有足够过渡距离。

这是长度可行性检查，不是完整的自交、曲率、坡度或网格质量证明。不能据此声称参考系统已覆盖所有无效连接。

## 6. 主路、次路和连接优先级

EasyRoads3D 不把所有入口视为对称参与者。

`buildPriority == 0` 使用主路参数：

- `cornerRadiusMainRoad`；
- `cornerSegmentsMainRoad`。

次路使用：

- `cornerRadiusSecondaryRoad`；
- `cornerSegmentsSecondaryRoad`；
- `cornerRadiusSecondaryCurvature`。

同一路型的相对入口可以组成贯穿路口的 primary road。次路的圆角和内部横截面会向主路边界收敛，而不是让所有入口共同扭曲一个中心点。

主路/次路还影响：

- 圆角半径；
- 中心 mesh 的分区和材质；
- normal 平滑；
- 车道 connector 的 stop 状态；
- sidewalk 绕角方式；
- terrain indent；
- 贴花和主路接缝。

对于不等宽 T/Y 路口，这是保持主路轮廓连续的关键。

## 7. 相邻入口圆角算法

`GetOQDCQQCOOC()` 是可恢复的核心圆角过程。

输入包括：

- 路口中心或控制点 `cp`；
- 左右边界起点；
- 半径；
- `cornerSegments`；
- 是否拆为左右半段；
- 当前 sibling。

其几何过程可整理为：

```text
leftDir  = normalize(cp - leftPoint)
spanDir  = normalize(rightPoint - leftPoint)
rightDir = normalize(cp - rightPoint)

for i = 0..segments:
    t = i / segments
    lineA = line(leftPoint, lerp(leftDir, spanDir, t))
    lineB = line(rightPoint, lerp(-spanDir, rightDir, t))
    corner[i] = intersectionOfInfiniteLines(lineA, lineB)
```

若启用半径裁剪，左右起点先投影到以 `cp` 为中心的指定半径。特殊共线情况直接按等距线性采样。

该算法不是严格圆弧，也不是标准 Bézier，而是一组插值方向线的交点。它的优点是：

- 构造使用相邻边缘端点与方向，切向和曲率连续程度仍需检验；
- 支持不对称入口；
- 可以通过 `cornerSegments` 控制拓扑密度；
- 生成的点列可被道路、路缘、人行道和地形缩进共同复用。

`MatchLeftRights()` 随后负责左右 rounding point 的数量和朝向一致，使横截面扫掠可以建立规则索引。
这里描述的是数据依赖，不代表已确认空入口之后的执行顺序。交点函数使用 `flag:false` 的无限直线分支，
不是只接受正向射线交点；近乎平行、重合和零方向等退化情况仍需检查。

## 8. 横截面沿圆角扫掠

`QDDDQODDQDQDQDD.OQDDQQDQQO()` 接收左右 rounding points 和完整 `roadShape`。

对每个 rounding sample：

1. 首点使用入口左右边界决定横向方向；
2. 中间点使用前后 rounding point 的差分估算切线；
3. 将切线在 XZ 平面旋转 90 度得到横截面方向；
4. 对 `roadShape` 的每个节点按横向偏移复制顶点；
5. 固定边界之外的节点通过直线交点和 Lerp 向中心收敛；
6. 对次路数据进一步依据主路边界修整。

该方法主要组织 `roadVecs` 顶点列；三角索引、UV、材质和 normal 索引涉及后续网格函数，不能全部归到此方法。

中间采样、左侧固定横截面节点的核心形式是（首尾和右侧方向另有分支）：

```text
tangent(i) = normalize(round[i+1] - round[i-1])
side(i)    = normalize(perpendicularXZ(tangent(i)))
vertex(i,j)= round[i] + side(i) * (shape[j].x - shapeEdge.x)
vertex.y   = shape[j].y  // 此局部路口分支是赋值，不是叠加 round[i].y
```

注意：沥青、路肩、路缘石等不是各自独立偏移出来的闭合环，而是同一个 `roadShape` 中的不同横截面节点。
共享横截面有利于减少边界重复计算，但系统也有独立 sidewalk 数据，不能推广为所有人行道都属于同一横截面。
这种结构并非所有场景中接缝稳定性的证明。

## 9. 路口的条带网格与局部三角剖分

### 9.1 Delaunay 实现

`DelaunayER.dll` 使用经典 Bowyer-Watson：

1. 构造覆盖所有点的 super triangle；
2. 顺序插入每个点；
3. 删除外接圆包含新点的三角形；
4. 收集空腔边界并删除重复边；
5. 用新点连接空腔边界；
6. 删除与 super triangle 共享顶点的三角形。

三角剖分只使用 XZ：`PointER(x,z)`。Y 保留在原始三维顶点数组中，索引通过 XZ 坐标映射回来。

### 9.2 多边形过滤

原始 Delaunay 会覆盖点集的凸包，无法直接处理凹路口。所读的剖分包装对候选三角形计算重心：

```text
centroid = (a + b + c) / 3
keep triangle iff centroid is inside boundary polygon
```

这种过滤不足以保留凹边界：重心在内部的三角形仍可能跨过凹缺口或约束边。它不是约束 Delaunay，
也不能保证孔洞、边界覆盖或最小内角，不能写成“保证凹边界正确”。

`delaunayER.FindVertice()` 按 XZ 精确相等查找，未匹配时返回 0，会隐藏映射失败，也不能区分 XZ 相同而 Y 不同的点。
移植应保留稳定顶点索引并显式报告失败，不应复用这种静默回退。

### 9.3 多种 triangulation path

代码中同时存在：

- 普通中心 Delaunay；
- `ForkTriangulationDelaunay`；
- priority-road 专用连接；
- single-section 与 multi-section；
- IConnector 专用三角剖分。

已确认的分派位于 `QDDDQODDQDQDQDD.cs:5090` 起：`buildPriority == 0` 走 `OOCOCOCCQD()`，
次级分支调用 `ForkTriangulationDelaunay()`。这不是先统一扫掠再对一个剩余中心洞统一 Delaunay。
其余字段组合与顶层可达路径仍需验证，方法存在不等于所有 T/Y/X 已受支持。

## 10. 不等宽道路和 IConnector

`ERIConnector` 专门处理两条道路之间的横截面变化，包括：

- 不同道路宽度；
- 不同横截面节点；
- 横向 offset；
- 材质/纹理过渡；
- 左、中、右对齐；
- Linear、Exponential、Smooth 三种 transition curve。

对长度归一化参数 `u`：

```text
Linear:      w = u
Exponential: w = u * u
Smooth:      w = smoothstep(0, 1, u)

x(u,j) = lerp(shape[j].x, shape[j].x * stretchRatio, w)
x      -= roadOffset * w
```

完整横截面沿 connector spline 扫掠，材质 UV 也可按两段连接器长度做过渡。
上述为 `ERIConnector.cs:2393` 的核心概括；实际分支还有两侧方向、组合长度和采样数量相关修正。
名为 Exponential 的模式实际是平方，不是数学上的指数函数。

可供 EVEngine 评估的一种分层方案是：

1. 在路口外，用有限长度 IConnector 将道路横截面渐变到路口 socket；
2. 在路口内，使用已规范化的 socket 构造 rounding curves 和中心铺面。

这不是已证实的 EasyRoads3D 不等宽 Y 型流程：两路变宽机制不能证明每个多路路口都会先插入 IConnector。
应根据现有端口和横截面模型决定是否需要该阶段，避免无条件增加连接器对象。

## 11. 坡度和三维策略

不能把“部分计算置零 Y”推广为“所有路口顶点都在一个刚性平面上”。已确认的是：

1. 部分转角拓扑在局部 XZ 计算，横截面分支直接赋 `vertex.y = roadShape[j].y`；
2. 局部点经过连接对象变换，局部 Y 为零不等于世界高度为零；
3. 道路端部通过连接口和虚拟控制点约束位置与方向；
4. `ODODOQDDOQ.cs:979` 起对横截面顶点逐一求连接平面高度，近端采用该高度，过渡区使用 SmoothStep 混合；
5. 无旋转分支之后还叠加横截面高度，旋转分支采用不同处理，不能统一为“只改中心线 Y”。

`OQOCDDDQQD.OOQOOOCQDQ()`（194 行）使用三点平面在 XZ 投影中的重心坐标：

```text
(x,z) = α * p1.xz + β * p2.xz + γ * p3.xz
α + β + γ = 1
yPlane = α * p1.y + β * p2.y + γ * p3.y
```

过渡段将 `yPlane` 与原道路高度混合。该求高函数自身未检查投影面积分母接近零，退化输入防护不能省略。
这些实现也不能证明所有接缝都满足一阶或二阶连续性，仍需数值验收。

这对 EVEngine 有直接启示：坡道路口不应把三个互不共面的截面直接连接到一个中心点。应先确定路口平面，
然后在足够长的 approach transition 内把中心线高度、切线坡度和横坡逐渐对齐到 socket。

建议 EVEngine 的路口平面按以下优先级确定：

1. 显式 authored plane；
2. 主路入口的加权平均平面；
3. 所有入口位置与切线的加权最小二乘平面。

之后检查每个入口达到该平面所需的纵向过渡长度。如果超过可用道路长度，返回结构化失败，而不是生成扭曲路口。
上述平面选择与求解方式是设计建议，不是从参考代码中发现的最小二乘或完整约束求解器。

## 12. 地形 indent 和 surrounding

道路和路口的地形处理不直接复用路面外轮廓的固定宽度偏移。

对相邻入口：

1. 读取左右 `roadIndent` 和 `roadSurrounding`；
2. 与相邻道路对应值取中间目标；
3. 沿 rounding points 使用 `Mathf.SmoothStep` 渐变；
4. 从曲线差分估算局部外法向；
5. 生成 indent 与 surrounding 两条轮廓；
6. 用方向测试和交点裁剪避免轮廓穿越。

因此 terrain footprint 只是同一参数化边界的另一个 offset channel，而不是新的几何权威。

另一个独立方向是“道路跟随地形”：`OODCOOCDQO.cs:3747` 采样地表，根据高度误差、坡度方向变化、距离及端部余量
选择高度控制点，再通过样条覆盖道路采样点的 Y，保持 XZ 不变。不能与“修改地形去贴合道路”混为一谈。
本次未完整核验高度图写入、恢复和各地形后端，不声称已还原完整地形变形内核。
`indent/surrounding` 的 SmoothStep 是地形影响范围过渡；车道变宽应引用第 10 节。

## 13. Sidewalk 与 crosswalk

Sidewalk 使用独立横截面，但仍围绕 connection sibling 的 rounding points 构建。

算法包含：

- 相邻入口人行道宽度与夹角检查；
- 小角度路口为 sidewalk 增加额外转弯空间；
- 左右 sidewalk 独立启用；
- 主路与次路采用不同绕角策略；
- inner pavement 与 outer pavement 分别生成；
- 必要时使用 Delaunay 填充 sidewalk 中央区域；
- crosswalk handle 基于 sidewalk width、depth 和 connection direction；
- diagonal crosswalk 是独立后处理。

一个很实用的细节是：当入口夹角小于 90 度时，预留空间系数从约 `2.3` 平滑下降到 `1.2`；
夹角接近 150 度以上时则不再做同样的 sidewalk 扩张。这是预留空间的启发式规则，不保证所有锐角 Y 型都不侵入车道。
本节描述保留的辅助分支；`HandleSidewalks()` 入口为空，完整执行顺序和最终几何仍待验证。

## 14. UV、材质和法线

系统并非生成完位置后统一套一张纹理：

- 道路横截面节点保存 U；
- V 来自沿 rounding curve 或 connector spline 的累计距离；
- 主路中心区域可使用单独 connection material；
- IConnector 可以 BlendTextures 或 TextureTransition；
- 多道路类型产生多个 submesh；
- 特定接缝的 normal index 会成对平均，所读分支还要求相同道路类型且没有 `primaryPriorityConnection`；
- 最终统一 `RecalculateNormals()`、局部 normal 修正、`RecalculateTangents()`。

材质与法线应独立验收；仅凭静态代码不能断言 EVEngine 必然达不到参考效果，硬边也不应无条件平滑。

## 15. 车道连接

保留的车道辅助代码使用 `ERLaneConnector`，可整理出以下数据依赖（不代表已确认顶层执行顺序）：

1. 从道路横截面的 lane position 计算入口 lane socket；
2. 遍历其他入口的目标车道；
3. 根据 `TurnOptions`、方向夹角和道路优先级筛选；
4. 建立 start/end lane index；
5. 主路通常不停，次路根据 priority 设置 stop；
6. connector 的几何锚点复用左右 rounding points。

因此导航拓扑依赖最终几何 socket，而不是另行猜测路口中心路径。

移植时优先复用 EVEngine 既有 lane/turn 发布机制；接入新的 socket 后还需验证方向、权限、优先级和几何一致性，
不能将工作量概括为“只改端点和控制点”。

## 16. 增量更新模型

EasyRoads3D 的更新不是每帧盲目重建：

- connection angle 变化超过阈值才触发；
- `hasChanged` 标记单个 sibling；
- `updateQueue` 防止同一更新批次重复执行；
- road type timestamp 决定参数是否需要刷新；
- 修改 connection 后会回建连接道路；
- sidewalk、lane data、terrain surface 和 side objects 分阶段更新。

EVEngine 不需要复制这套 Unity 编辑器状态机。以下是建议的产物依赖顺序，不是已恢复的 Unity 完整调用图：

```text
network validation
  -> socket normalization
  -> approach transition
  -> corner boundaries
  -> road/junction mesh
  -> sidewalk/curb
  -> terrain footprint
  -> lane turns
  -> collider/artifact publication
```

## 17. 与 EVEngine 实现的对照与待验证问题

初稿将 `RoadBake.cpp::bakeJunction()` 概括为以下问题清单。它不是持续更新的仓库状态报告；
本次只读检查仍可见 hub 三角扇分支，但没有逐项验证所有特殊分支、已有约束和最新测试。
以下其余项目应作为核查项，而非“当前完全没有”的结论：

- 入口是否只提供左右 asphalt 点，缺少完整横截面契约；
- hub 三角扇是否适合所处理的轮廓，是否会跨出凹边界；
- curb/sidewalk 的二维 miter offset 是否跨越汇入口；
- 不同入口 `up` 与中心高度模型是否一致；
- 主路/次路策略是否覆盖相关场景；
- approach transition 是否充分；
- 圆角曲线是否覆盖全部相邻入口；
- 中心填充是否保留约束边（不要求一定使用 Delaunay）；
- 三角形质量约束是否完整。

针对用户报告的现象，应分别取证：路缘石挡车道检查各横截面条带的终止和边界归属；破损三角片检查退化、绕序和投影；
曲线上坡错位检查位置、切线、横坡与整排接缝顶点。不能仅凭参考系统结构就把这些归为同一个已确认根因。

## 18. 建议移植到 EVEngine 的最小算法

遵循“不增加大量类”的约束，应先复用既有类型与几何工具。以下三个结构仅示意需要携带的数据，
不是新增类型清单，也不是可直接编译的补丁；实施前需检查现有类型能否承担这些职责：

```cpp
struct JunctionSocket {
    V3 center, tangent, side, normal;
    RoadProfile profile;
    float approachLength;
    int priority;
};

struct JunctionCorner {
    int rightArm, leftArm;
    std::vector<V3> asphaltBoundary;
    std::vector<V3> curbBoundary;
    std::vector<V3> sidewalkBoundary;
};

struct JunctionPatch {
    JunctionPlane plane;
    std::vector<JunctionSocket> sockets;
    std::vector<JunctionCorner> corners;
};
```

如确需新增，可使用私有/局部数据结构；不需要新增公开类、模块或第二套道路权威。
现有 `RoadBake.cpp` 已超过约 1000 行，按仓库规则不能把所有新算法继续堆入该文件；应沿职责拆分实现 TU，
拆分文件不等于增加类。任何公开接口或边界变更仍须遵守仓库架构规范与门禁。

推荐实现阶段：

### 阶段 A：socket 与路口平面

- 从真实样条计算入口位置和趋近切线；
- 按角度排序；
- 选择主路；
- 计算统一路口平面；
- 求每条道路所需 approach transition 长度；
- 长度不足则返回 `Result` 诊断。

### 阶段 B：approach transition

- 中心线位置使用三次 Hermite；
- 高度和纵坡使用带端部导数约束的 Hermite；SmoothStep 只用于合适的混合权重，不能直接替代任意非零纵坡约束；
- 横坡逐渐对齐路口平面；
- 横截面宽度使用 SmoothStep；
- 道路 mesh 与 junction socket 共用最后一排顶点。

### 阶段 C：rounding curves

- 计算相邻入口 asphalt edge ray 的交点；
- 根据半径和入口夹角确定切点；
- 生成固定最大弦误差的圆角采样；
- 锐角、近共线和反向入口必须有显式退化处理。

方向线交点算法可作为候选，但必须通过退化和弦误差验收；若评估 biarc，它通常提供切向连续，
两段弧连接处不自动保证曲率连续，不能将更换曲线类型当成正确性证明。

### 阶段 D：横截面扫掠和受边界约束的填充

- rounding curve 扫掠 asphalt/curb/sidewalk 共享横截面；
- 收集中心边界点；
- 投影到 junction plane 2D；
- 优先复用能够保留边界和孔洞的多边形剖分，或使用真正的约束 Delaunay；
- 不可只删除普通 Delaunay 中越界的三角形，那可能留下孔洞且不会自动恢复约束边；
- 按选定的路口基面和横截面高度重建三维顶点，不得把路缘/横坡全部压平；
- 使用共享接缝位置/拓扑标识；材质、UV 和硬法线缝可保留独立渲染顶点。

### 阶段 E：派生产物

- collider 使用最终路面拓扑；
- terrain footprint 复用 rounding samples；
- lane/turn 使用 socket 和 corner curve；
- curb、sidewalk、marking 不再独立猜测边界。

## 19. 必须增加的数值验收

下一版不能只看截图。至少应验证：

### 拓扑

- 在明确的单层路面 patch 拓扑上，外边界及孔洞边界每条边使用一次；
- 内部 edge 使用两次，组合道路和路口后原接入口不再算外边界；
- 没有非流形边；
- 没有重复或零面积三角形；
- curb/sidewalk 不跨越 road socket。

边计数应使用几何拓扑标识，不直接拿因 UV、材质或硬法线拆分的渲染索引判定破洞；桥梁上下层也不能仅按 XZ 焊接。

### 接缝

- 道路最后一排顶点与 socket 顶点逐点相等；
- 接口的横截面节点有明确一一对应；不同节点数必须有显式重采样/映射，不能直接按下标拼接；
- 位置误差、切线夹角和法线夹角分别受限；
- 同一接缝 UV 连续或被显式标记为 material seam。

### 三角形质量

- 最小内角达到可行阈值；输入锐角本身不满足时需细分、调整或返回明确失败，不声称任意输入均可达标；
- 最大长宽比受限；
- 三角形 winding 一致；
- 投影到路口平面后没有边相交；
- 每个三角形位于目标区域且不越过外边界/孔洞边界；重心检查仅作辅助；
- 检查缺口、重叠与目标区域覆盖，面积相等也不能单独排除重叠抵消缺口。

### 坡度

- 按选定高度模型检查基面及横截面偏移残差；仅在明确采用平面路面时要求路面点共面；
- approach 端点位置、坡度和横坡与 socket 一致；
- 最大纵坡和纵坡变化率受限；
- transition 长度不足时明确失败。

### 场景矩阵

- 等宽/不等宽 T；
- 对称/不对称 Y；
- 十字路口；
- 锐角与钝角；
- 弯道接入；
- 上坡、下坡和曲线上坡；
- 不同 sidewalk 组合；
- 主路宽于次路、次路宽于主路；
- 极短 approach 的预期失败。

数值检查还应覆盖非有限值、重复控制点、近共线/近反向入口、近平行转角线与退化求高平面。
容差需注明单位与尺度；数值验收和实际渲染回归互补，不能只靠截图或只靠三角形计数。

## 20. 不建议照搬的部分

- 混淆后的巨型静态类和全局静态状态；
- Unity GameObject/MonoBehaviour 生命周期；
- 以 prefab 组件作为数据权威；
- 大量布尔标记组合；
- `updateQueue` 式编辑器批次状态；
- 精确浮点相等查找顶点；
- 仅以三角形重心判断复杂约束边界；
- 未暴露结构化错误的静默 return。

EVEngine 应移植几何机制和数据约束，不应复制参考工程的对象模型或混淆后的实现结构。

## 21. 主要源码索引

- 道路主采样 / 样条公式：`runtime/EasyRoads3Dv3/ERModularRoad.cs:10351 / 14137`
- 分段采样 / 普通道路网格：`runtime/EasyRoads3Dv3/ODODOQDDOQ.cs:10 / 448`
- 样条端部和连接器对齐：`runtime/EasyRoads3Dv3/OODCOOCDQO.cs`
- 连接入口数据：`runtime/EasyRoads3Dv3/ERConnectionSibling.cs`
- Flex Connector 状态：`runtime/EasyRoads3Dv3/ERCrossingPrefabs.cs`
- Flex Connector 更新入口：`runtime/EasyRoads3Dv3/ERCrossings.cs`
- Flex Connector 几何：`runtime/EasyRoads3Dv3/QDDDQODDQDQDQDD.cs`
- 两路宽度过渡：`runtime/EasyRoads3Dv3/ERIConnector.cs`
- 人行道与人行横道：`runtime/EasyRoads3Dv3/ERSideWalkVecs.cs`
- 中心主路铺面：`runtime/EasyRoads3Dv3/ERCrossingMainRoad.cs`
- Delaunay：`delaunay/delaunayER.cs`

本次重点核对的位置（相对 `reference/EasyRoads3D-Pro-v3.2.4f5-decompiled/`）：

- `runtime/EasyRoads3Dv3/ERCrossings.cs:559`：Flex 更新调用点。
- `runtime/EasyRoads3Dv3/QDDDQODDQDQDQDD.cs:261 / 335`：固定返回/空入口，不能推定原因。
- 同文件 `2435 / 3408`：方向线交点转角 / 横截面顶点列。
- 同文件 `5090 / 5249 / 7241`：网格分派 / 条件法线平滑 / Delaunay 包装。
- `runtime/EasyRoads3Dv3/ERCrossingPrefabs.cs:2566`：按角度建立左右邻接。
- `runtime/EasyRoads3Dv3/OODCOOCDQO.cs:1393 / 3747`：端部虚拟控制点 / 跟随地形高度。
- `runtime/EasyRoads3Dv3/OQOCDDDQQD.cs:194`：重心坐标求平面高度。
- `runtime/EasyRoads3Dv3/ERIConnector.cs:2393`：横断面缩放与偏移渐变。
- `delaunay/delaunayER.cs:47 / 61`：精确 XZ 映射及失败回退 / Bowyer-Watson。

## 22. 最终判断

静态源码支持借鉴以下几何机制组合，但这不是已验证可运行的完整调度链：

```text
道路真实趋近方向
  -> 规范化 socket
  -> 主路/次路选择
  -> 相邻边缘 rounding curves
  -> 完整横截面扫掠
  -> 条带网格与局部剖分
  -> sidewalk/terrain/lane 复用同一边界
```

优先评估 socket、端部切线约束、完整横截面接缝、逐顶点高度过渡及主次道路边界处理，
保持 `RoadNetwork` 为唯一权威并复用现有工具。不能承诺“只需很少代码就达到参考质量”，
也不能将普通 Delaunay 加重心过滤视为正确性方案。最终结论取决于第 19 节数值验收和渲染回归。

## 23. 本次复核修订与剩余边界

- 修正空 Flex 入口的归因，明确未核对原始 IL、未运行 Unity。
- 补充准确样条公式、普通道路扫掠/索引/UV，区分主采样器与分段辅助实现。
- 修正 `controlType 1/2` 的高度处理差异，以及局部横截面 Y 的赋值语义。
- 补充虚拟控制点、逐顶点平面求高、SmoothStep 过渡及投影退化风险。
- 修正“统一中心 Delaunay”“重心过滤保留凹边界”的过度结论。
- 区分两路宽度过渡、地形 indent 渐变及未证实的多路路口调度。
- 将 EVEngine 比较与设计建议和参考事实分开，补足约束边覆盖、属性缝和非平面横截面的验收条件。
- 尚未完成：Flex 空入口的原始 IL/版本核验、Unity 运行验证、完整地形写入恢复、所有人行道与车道分支可达性验证。

本次仅更新分析文档；没有修改引擎代码、公共 API、ECS、持久化格式或模块边界，因此未触发重构架构门禁。
未运行引擎测试，不将文档核对表述为运行正确性证明。
