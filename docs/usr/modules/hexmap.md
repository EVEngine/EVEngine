# 六边形立体地图（Hex Map）

**脚本入口：** `hexmap`（`eve.HexMap`，模块槽位 `hexmap`）

`hexmap` 是引擎的**可编辑 3D 六边形地图**层：持有尖顶（pointy-top）六边形格网、
按 5×5 分块、按面（terrain / water / river / road）生成 CPU 网格并上传 GPU，同时
提供世界坐标拾取、邻居查询与笔刷编辑。参考实现是 Catlike Coding 的
[Hex Map](https://catlikecoding.com/unity/hex-map/) 工程；坐标、高程台地、悬崖、
水面与河道几何均与之一致。

## 布局与常量

| 项 | 值 | 说明 |
|----|----|------|
| 朝向 | 尖顶 | XZ 平面，+X 向东，+Z 向北，Y 为上 |
| `outerRadius` | 10 | 六边形外接圆半径 |
| `innerRadius` | 8.66025404 | `outerRadius * √3/2` |
| `elevationStep` | 3 | 每级高程的世界高度 |
| `solidFactor` / `waterFactor` | 0.8 / 0.6 | 实心区与水面内缩因子 |
| `terracesPerSlope` | 2（`terraceSteps` = 5） | 斜坡台地级数 |
| `kChunkSizeX/Z` | 5 / 5 | 分块尺寸；地图尺寸必须是它的整数倍 |
| 地面扰动 | XZ ±4，Y ±1.5 | `HexNoise`（种子决定，无需贴图资源） |

偏移坐标到轴坐标：`axial = offset - (z/2, 0)`（`z/2` 向下取整）。世界坐标：
`x = (axialX + 0.5·axialZ) · innerDiameter`，`z = axialZ · outerRadius · 1.5`。
`HexCoordinates::fromWorldPosition` 是其精确逆变换（含立方体取整修正）。

## C++ API 快查

### `HexMetrics`（[HexMetrics.h](../../../src/modules/hexmap/HexMetrics.h)）

常量与角点/边采样助手：`firstCorner` / `firstSolidCorner` / `firstWaterCorner` /
`bridge` / `waterBridge` / `solidEdgeMiddle` / `terraceLerp` / `wallLerp`，
以及 `elevationY` / `streamBedY` / `waterSurfaceY`。`EdgeVertices` 是边上 5 个
采样点（v1…v5）。

### `HexCoordinates`（[HexCoordinates.h](../../../src/modules/hexmap/HexCoordinates.h)）

轴坐标 `(x, z)`，`y = -x - z`。`step(direction, distance)`、`distanceTo`、
`fromOffset` / `offsetX` / `offsetZ`、`chunkColumn` / `chunkRow`、
`toWorldPosition` / `fromWorldPosition`。

### `HexMap`（[HexMap.h](../../../src/modules/hexmap/HexMap.h)）

值类型格网，`reset(cellCountX, cellCountZ, seed)` 建立地图（尺寸必须是 5 的倍数）。

- 拓扑：`contains` / `indexOf` / `coordinatesAt` / `tryGetNeighbor` / `edgeTypeTo`
- 分块：`chunkIndexOf` / `chunkColumnOf` / `chunkRowOf` / `chunkCenter` /
  `takeDirtyChunk` / `markAllChunksDirty` / `dirtyChunkCount`
- 拾取：`pickCell(rayOrigin, rayDirection)` —— 无碰撞体，改为在候选格高程平面上
  迭代求交并二分收敛
- 查询：`elevation` / `waterLevel` / `terrainType` / `urbanLevel` / `farmLevel` /
  `plantLevel` / `specialIndex` / `isUnderwater` / `hasRiver` / `hasRoad` / `isWalled`
- 编辑：`setElevation` / `setWaterLevel` / `setTerrainType` / `setUrbanLevel` /
  `setFarmLevel` / `setPlantLevel` / `setSpecialIndex` / `setWalled` /
  `setOutgoingRiver` / `removeRiver` / `addRoad` / `removeRoads`
- 笔刷：`editElevation` / `editWaterLevel` / `editTerrainType` / `editFeatureLevel`
  （六边形圆盘，`collectBrush` 可单独枚举）

跨格约束与参考工程一致：河流只能顺坡或从湖面流出；道路不能跨两级以上高程、
不能穿过河流中段、城市/特殊地物会移除道路；特殊地物在有河时被忽略。

### 网格生成（[HexMapMesh.h](../../../src/modules/hexmap/HexMapMesh.h)）

`buildChunkSurfaceMesh(map, chunkIndex, surface, out)`，`surface` 取
`HexSurface::{Terrain, Water, River, Road}`。生成物是 `HexMeshData`
（position / normal / uv / index，三角形不共享顶点，`finalize()` 写平面法线）。

分块所有权与参考工程一致：地面扇形按本块 25 格生成，**只有 NE / E / SE**
方向的混合带、台地与悬崖归属本块，其余由邻块生成，因此块与块之间不会重复或漏缝。

顶点只有两个纹理坐标，地形三层信息按
`u = layerA + layerB·8 + layerC·64`、`v = weightB + weightC·16` 打包；这是当前
顶点布局的**兼容桥**，不是地形数据格式。

`HexSurface::Fog`（值 4）是唯一不能由 `buildChunkSurfaceMesh` 生成的流，因为它
除了格子还需要可见性计数器，由 `buildFogMesh(map, visibility, chunkIndex, out)`
生成：每个"当前不可见"的格子输出一根六棱柱（顶面 + 侧裙），裙脚垂到最低可编辑
高程之下，因此迷雾边界上的悬崖立面也会被覆盖；第一纹理坐标 `u` 承载明暗
（`0` = 曾经探索过但此刻无人看见，`1` = 从未探索）。迷雾流在可见性跨越 0/1 时
自动把所在块标脏。

另外两条流由 [HexFeatures.h](../../../src/modules/hexmap/HexFeatures.h) 生成，也不走
`buildChunkSurfaceMesh`：`HexSurface::Wall`（值 5，`buildWallMesh`）输出城墙、墙塔
与桥梁，`HexSurface::Feature`（值 6，`buildFeatureMesh`）输出 urban / farm / plant /
special 地物。两者共用一条片元着色器，因为第一纹理坐标已经标明了部件种类：

| `u` | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| 部件 | 城墙 | 墙塔 | 桥梁 | urban | farm | plant | special |

城墙的归属规则与地形一致：只有当 `d ∈ {NE, E, SE}` 或该方向已经在网格之外时，
才由本块生成该边的城墙——这样每条内部边恰好被一侧生成，而地图边界仍然闭合。
道路横穿城墙边界时会开出城门（只生成两侧外段 + 两个端帽）；高程差产生楔形挡墙。

### 地物与城墙（[HexFeatures.h](../../../src/modules/hexmap/HexFeatures.h)）

参考工程实例化 prefab 模型；本引擎不携带资源，因此 `HexFeatures.cpp` 用程序化几何
替代（墙＝两侧面 + 顶盖，塔/桥/建筑＝盒体，plant＝压扁八面体，special＝锥形柱）。
`HexHashGrid` 是参考工程 `hashGrid` 表的确定性替代：由 `(gridX, gridZ, seed,
channel)` 的整数哈希直接导出五个分量，因此不需要分配表、也没有全局状态，同一地图
种子永远得到同一套地物布局。`featureThreshold(level, index)` 返回参考工程的
三级阈值表 `{0,0,0.4} / {0,0.4,0.6} / {0.4,0.6,0.8}`，越界参数被钳制。

拾取规则与参考工程一致：urban / farm / plant 三个集合各自用格子哈希判定是否
命中，命中的里面哈希最小者胜出；`specialIndex` 非零时该格不生成普通地物。

### 地图生成（[HexMapGenerator.h](../../../src/modules/hexmap/HexMapGenerator.h)）

`generateHexMap(map, settings)` 把参考工程 `MapGenerator` 的流水线移植为纯 CPU 步骤：

1. 全部格子初始化到 `elevationMinimum`；
2. `regionCount` 个陆地区域从种子格生长（抖动、随机抬升/下沉，遵守地图与区域边界）；
3. 海岸线侵蚀直至陆地占比接近 `landPercentage`；
4. 顺坡开凿河流（含湖泊概率与已有的汇流判定）；
5. 40 轮下风向水汽传输（蒸发、降水、坡面径流、下渗），得到湿度与温度；
6. 按参考工程的生物群系表写地形类型与植被等级。

可复现性只依赖 `settings.seed` 与网格本身：不使用时间、`random_device` 或可变静态
状态。`elevationMaximum` 会被钳制到 `HexMetrics::kMaxElevation`；不合法或放不下的
区域参数在改动地图之前就以 `InvalidArgument` 拒绝。生成后的地图**全部格子未探索**，
因此调用方必须重新放置单位并重建全部块。

### 搜索与寻路（[HexSearch.h](../../../src/modules/hexmap/HexSearch.h)）

- `HexSearchContext`：可复用的搜索草稿（每格一条 `HexSearchData` + 按优先级分桶
  的前沿队列）。用**相位**（`beginPhase()`，每次 +2）代替"清空 visited"，因此一次
  搜索不付全图重置的代价。
- `isValidDestination(map, c, occupied)`：`explored && explorable && !underwater &&
  !occupied`；地形部分由 `HexCell.h` 的 `canHoldUnit()` 单点定义。
- `moveCost(map, from, to, direction, occupied)`：悬崖与墙界不可通行；起点有路 = 1，
  否则平地 5 / 斜坡 10，再加目标格的 urban+farm+plant 等级。返回**负数**表示阻断。
- `findPath(map, scratch, from, to, rules, occupied) -> Result<HexPath>`：A\*（启发式
  为六边形距离），代价按 `speed` 折算成"回合"，`HexPath::turns` 给出每个格子到达
  时的回合数。目标不可进入时返回 `NotFound`，端点越界或 scratch 尺寸不匹配返回
  `InvalidArgument`。
- `collectVisibleCells(map, scratch, from, range, out)`：视线可达格（距离 + 目标格
  `viewElevation` 与 `range + 起点 viewElevation` 比较，且不超过直接六边形距离）。

### 可见性与迷雾（[HexVisibility.h](../../../src/modules/hexmap/HexVisibility.h)）

`HexVisibility` 是**引用计数**而非布尔：多个观察者重叠时，只有最后一个离开才让格子
转回不可见；某格第一次被看见时同时把地图上的 `explored` **单向闩锁**置位，此后不再
回落。`increase` / `decrease` / `clear` / `resetVisibility`（按当前单位重算全图）。

### 单位（[HexUnits.h](../../../src/modules/hexmap/HexUnits.h)）

`HexUnitRegistry` 持有单位、寻路占用与视野贡献：

- `addUnit` 立即授予视野；`unitIdAt` / `isOccupied` 用于寻路占用谓词
  （`occupancyQuery()` 返回借用 `*this` 的可调用对象）。
- 单位 id 就是注册表下标，删除会让后面的 id 前移；调用方不要跨删除缓存 id。
- 移动沿二次贝塞尔曲线（Unity 参考工程的转角做法），`advance(map, visibility,
  scratch, id, dt)` 既推进动画又返回**当前位姿样本**（世界坐标已含高程与扰动），
  `dt <= 0` 表示只读不推进。**目的地自 `beginTravel` 起即被占用**，视野则跟着
  实际走过的格子移动（`visionIndex`）。
- `restore` 先清空再校验，被拒绝的载荷不会留下"半恢复"的注册表。

### 存档（[HexSerializer.h](../../../src/modules/hexmap/HexSerializer.h)）

`saveHexMap(map, units, out)` / `loadHexMap(bytes, map, units)`：小端、显式逐字节
写出的自描述载荷（magic `EVEHEX\0\0` + 版本 + 尺寸 + 种子 + 每格 `HexValues` /
`HexFlags` + 单位表）。版本不匹配会**拒绝**而不是猜测；载荷在触碰输出前整体校验
（长度、magic、版本、尺寸上限 512 且为 5 的倍数、单位格在界内、单位格可站立、
单位不重复），因此被拒绝的载荷不会留下部分写入的地图。修订号字段只作诊断。

### 球面后端（[HexSphereTopology.h](../../../src/modules/hexmap/HexSphereTopology.h)、[HexSphereMap.h](../../../src/modules/hexmap/HexSphereMap.h)、[HexSphereMesh.h](../../../src/modules/hexmap/HexSphereMesh.h)、[HexSphereGenerator.h](../../../src/modules/hexmap/HexSphereGenerator.h)）

前面所有小节都是**平面**地图：单元身份是 `HexCoordinates`，邻居是固定的整数方向偏移。
球面后端复用同一套单元记录（`HexCellData` / `HexValues` / `HexFlags`）、同一套几何常量
（`HexMetrics`）、同一个 `HexMeshData` 与同一份顶点编码，只替换这两件事——因为在闭合
球面上它们才真正不成立：

- **单元身份**是稠密的 `HexSphereCell`（`int32`），来自**二十面体（Goldberg）拓扑**：
  二十面体按 `f = 2^subdivision` 细分后的**顶点**就是单元，于是
  `cellCount = 10f² + 2`，其中恰好 12 个是五边形（恒为 `[0, 12)`，因为中点细分从不
  重编号原始顶点）；`edgeCount = 30f²`，`cornerCount = 20f²`，每个角点恰好被 3 格共享。
- **邻居关系**来自半边表。**方向不是算术**：六条边的方位逐格变化，而五边形只有五条边，
  因此不存在处处成立的 `opposite(direction)`——所有跨边查询走
  `directionOf(cell, other)`。方向 `d` 的边位于 `corner(d)` 与 `corner(d+1)` 之间，
  这两个角点由该边两侧的单元**以相反顺序**共享。

`HexSphereMap` 是 `HexMap` 的球面对应物：持有单元存储、`HexNoise`、**逐格**脏标记
（球面没有分块网格，拓扑已经给每格一个全局稳定 id）、拾取、距离、刷子与全套编辑接口。
高程是**径向**的：`surfaceRadius = sphereRadius + elevation · elevationStep`。
`elevationStep` 默认取半径的 `0.006`，而不是平面的 `HexMetrics::kElevationStep`
（3 世界单位）——后者在半径 100 的星球上会让 13 级高程跨越 39% 半径。`HexFlags` 的
道路/河流 6 个 bit 槽直接由方向索引寻址，五边形上槽 5 永不置位。

`buildSphereTerrainMesh` / `buildSphereWaterMesh` 沿用平面构建器的角点矩阵方案，顶点
编码（`HexTerrainVertexCode`）完全一致，因此地形着色器的读法不变。三处平面假设被替换：

1. **边数**不是 6（五边形是 5 条），没有任何循环可以写死 `kHexDirectionCount`；
2. **边归属**：平面版靠分块网格（`direction <= SE`）保证每条共享边只发一次，球面改用
   **较小的单元 id** 拥有该边——同一组边上的全序，同样保证恰好一次；
3. **"垂直"是径向**：坡度台地沿**弧**走水平步、沿**半径**走垂直步。

平面版的梯田带用弦 `v1 → v5` 跨越整条边，这在平面上安全**只因为**它的 5 个采样点共线；
球面上采样点在弧上，弦会在每条带坡度或崖壁的边留下 T 型接缝，因此球面版按扇形同样的
4 段拆分。地形顶点扰动也改为**切向**并把结果投影回原半径：切向偏移会以 `O(d²/r)` 改变
半径，而这里扰动量是单元尺寸的可观比例。

`generateSphereMap` 采样**三维**值噪声：`HexNoise` 是 XZ 平面场，用单元方向去采样会在
两极塌缩（方向的 XZ 投影趋零）、在经度回绕处接缝。海平面由噪声自身的分位数确定，因此
`landPercentage` 真正决定陆地占比，而不是听任噪声分布。输出是
`(seed, 拓扑, 半径)` 的纯函数。

## 脚本 API（`hexmap`）

```squirrel
hexmap.newGrid(gfx, cellCountX, cellCountZ, seed)   // -> Result
hexmap.hasGrid() / cellCountX() / cellCountZ() / chunkCountX() / chunkCountZ() / chunkCount() / seed()
hexmap.surfaceCount()                               // -> 7，见下面的 surface 编号表

hexmap.takeDirtyChunk()                             // -> 脏块索引，-1 表示没有
hexmap.rebuildChunk(gfx, chunkIndex)                // -> 生成出几何的面数
hexmap.rebuildDirtyChunks(gfx)                      // -> 重建全部脏块，返回面数合计
hexmap.chunkMeshAt(chunkIndex, surface)             // -> Mesh（借用），无几何时为 null
hexmap.releaseMeshes(gfx)                           // 释放本模块创建的全部网格

hexmap.elevation(x, z) / waterLevel(x, z) / terrainType(x, z) / urbanLevel(x, z) / ...
hexmap.isUnderwater(x, z) / hasRiver(x, z) / hasRoad(x, z) / isWalled(x, z)
hexmap.specialIndex(x, z) / farmLevel(x, z) / plantLevel(x, z)
hexmap.setElevation(x, z, v) / setWaterLevel(x, z, v) / setTerrainType(x, z, v) / ...
hexmap.setUrbanLevel(x, z, v) / setFarmLevel(x, z, v) / setPlantLevel(x, z, v) / setSpecialIndex(x, z, v)
hexmap.setWalled(x, z, v) / setOutgoingRiver(x, z, direction) / removeRiver(x, z)
hexmap.addRoad(x, z, direction) / removeRoads(x, z)
hexmap.editElevation(x, z, radius, delta) / editWaterLevel(x, z, radius, delta)
hexmap.editTerrainType(x, z, radius, terrainType) / editFeatureLevel(x, z, radius, feature, delta)

// 世界坐标（含高程与扰动）；同一坐标系的逐分量版本，供脚本直接喂给 renderable
hexmap.cellPositionX(x, z) / cellPositionY(x, z) / cellPositionZ(x, z)
hexmap.chunkCenterX(chunkIndex) / chunkCenterY(chunkIndex) / chunkCenterZ(chunkIndex)
hexmap.pickCell(ox, oy, oz, dx, dy, dz)             // -> {ok, value=[x, z]}

// 迷雾
hexmap.isExplored(x, z) / isExplorable(x, z) / setExplored(x, z, v) / setExplorable(x, z, v)
hexmap.isCellVisible(x, z) / visibleCellCount() / resetVisibility()

// 单位
hexmap.unitCount() / addUnit(x, z, orientation) / removeUnit(id) / removeAllUnits()
hexmap.unitIdAt(x, z)
hexmap.unitSample(id)   // -> {ok, value=[cellX, cellZ, worldX, worldY, worldZ, yaw, traveling]}
hexmap.travelUnit(id, path) / advanceUnits(dt)

// 寻路 / 存档 / 生成
hexmap.findPath(fx, fz, tx, tz)   // -> {ok, value=[[x, z, turn], ...]}
hexmap.saveMap()                  // -> {ok, value=<二进制安全字符串>}
hexmap.loadMap(gfx, blob)         // -> Result
hexmap.generateMap(gfx, seed, landPercentage, waterLevel, riverPercentage)   // -> Result

// 球面（与平面地图相互独立，可同时存在）
hexmap.newSphere(gfx, subdivision, radius, seed)     // -> Result；重建拓扑并释放上一颗星球的网格
hexmap.sphereReady() / sphereCellCount() / spherePentagonCount() / sphereSubdivision()
hexmap.sphereRadius() / sphereElevationStep() / sphereCellSpacing()
hexmap.sphereIsPentagon(cell) / sphereNeighborCount(cell) / sphereNeighbor(cell, direction)
hexmap.sphereDirection(cell) / sphereCornerDirection(cell, corner) / sphereCornerCount(cell)
hexmap.sphereElevation(cell) / sphereWaterLevel(cell) / sphereTerrainType(cell) / sphereIsUnderwater(cell)
hexmap.sphereCellAt(x, y, z) / sphereDistance(a, b)
hexmap.spherePickCell(ox, oy, oz, dx, dy, dz)        // -> {ok, value=<cell>}
hexmap.sphereSetElevation(cell, v) / sphereSetTerrainType(cell, v)
hexmap.sphereEditElevation(cell, radius, delta) / sphereEditTerrainType(cell, radius, terrainType)
hexmap.generateSphere(gfx, seed, landPercentage, waterLevel)   // -> Result；生成并重建两个网格
hexmap.rebuildSphere(gfx)                            // -> Result；批量编辑后重建
hexmap.sphereTerrainMesh() / sphereWaterMesh()       // -> Mesh（借用），无几何时为 null
hexmap.sphereReleaseMeshes(gfx)
```

`surface` 取 `0=Terrain, 1=Water, 2=River, 3=Road, 4=Fog, 5=Wall, 6=Feature`。
`x, z` 是偏移坐标（列、行）——`pickCell` 的返回值同样是偏移坐标，与 `elevation` /
`editElevation` / `cellPositionX` 等所有格子接口同一坐标系，因此拾取结果可以直接回喂
给它们（`findPath` 与 `unitSample` 返回的格子也已经是偏移坐标）。`unitSample` 复用
`advance(dt=0)`，因此返回的是含高程与扰动的插值位姿，不是地格中心。`generateMap` 按
**当前网格尺寸**重新生成，会丢弃全部单位与迷雾状态，调用方必须在成功后重新放置单位
并重建全部块。

球面接口用 `cell`（稠密 id，`0 .. sphereCellCount()-1`），与平面接口的偏移坐标 `(x, z)`
是**两套互不相通的地址空间**：不要拿平面坐标去喂球面接口，反之亦然。`sphereDirection`
与 `sphereCornerDirection` 返回 `[x, y, z]` 单位向量。`newSphere` 只重建拓扑，
`generateSphere` 才写入单元并重建网格；两者都会释放上一颗星球的网格，因此调用方必须在
成功后重新绑定 renderable。`rebuildSphere` 是批量编辑后的重建入口——球面是**整张一个
网格**（没有分块），每次重建都是全量，应当攒够一批编辑再调一次，而不是每改一格就调。

## 生命周期与所有权

- 模块**只持有一张活动地图**（参考工程的编辑器同样只编辑一张格子）；`newGrid`
  会释放上一张地图生成的全部 GPU 网格。
- 模块拥有 `HexMap` 与它创建的每个 `graphics::Mesh`；`chunkMeshAt` 返回**借用**
  指针，`Renderable3D` 只能在模块存活且未换图期间引用它。
- 某面在一张地图内可能从"有几何"变为"无几何"（例如最后一条道路被擦掉）：此时
  该面的 GPU 网格**延迟释放**（保留到下次 `newGrid`），`chunkMeshAt` 返回
  `null`，调用方据此隐藏 renderable，不会出现悬垂指针。
- `loadMap` 走 `adoptGrid`：先释放旧网格、接管已解码的地图、重建搜索草稿与迷雾
  计数器，再恢复单位。**调用方必须在成功后重建全部块并重新绑定 renderable**，
  因为所有块都是脏的且没有任何 renderable 指向新网格。
- 主/渲染线程亲和；不调用未知回调，不跨外部调用持锁。

## 示例

`examples/hex-terrain-3d` 是本模块的完整示例（与之无关的 `examples/hex-terrain` 是
`mesh.hexterrain` 配方，保持原样）：`WASD` 平移、`Q/E` 旋转、滚轮缩放，
`1`–`5` 切换高程/水面/地形/河流/道路模式，`[`/`]` 调整笔刷半径，`,`/`.` 切换地形
调色板，左右键升降或拖拽绘制河流与道路，`R` 换种子、`F` 换地图尺寸、`N` 重建。
第二轮补上战争迷雾、单位与寻路、存档：`tab` 切换选中单位，`g` 规划到光标格的路径，
`space` 出发，`escape` 取消预览，`t`/`y` 增删单位，`F5`/`F9` 存档与读档。第三轮补上
城墙/桥梁/地物与地图生成器：`m` 按当前尺寸生成一张程序化地图（重新随机种子）。

存档载荷经 `eve.Filesystem().writeText()` 落到保存目录，读回时先整体校验再接管
网格，因此损坏的文件不会破坏当前地图。

`examples/hex-planet` 是**球面后端**的示例：轨道相机绕星球旋转，`LMB` 拖拽改变
方位角/仰角，滚轮缩放，`space` 自动旋转，`R` 换种子，`[`/`]` 切换细分级别，`-`/`=`
调整陆地占比，`F5` 抓帧。默认姿态是静止的，所以两次运行抓到的画面几乎逐像素
一致（仅 ImGui 覆盖层有像素级差异）。它自带两个片元着色器：平面地形着色器把
`vWorldPos.y` 当海拔、`N.y` 当"上"、细节噪声投影到 XZ、远处用距离雾——这四件事在球面上
都不成立，因此球面版保留调色板与 BRDF、只替换这四处。另外宿主会在片元输出之后再套一层
Reinhard 风格的 tonemap（调试着色器输出常量 `1.0` 到达帧缓冲是 `224/255`，即
`y = x/(x+0.139)`），两个球面着色器都用 `hostTonemapInverse` 反解这条曲线，否则颜色会被
压缩两次且无法调参。

相机方面示例做了一个必要的修正：`eve.Camera3D()` 的默认裁剪范围是 `0.1..100`，
比一张地图的跨度还小，超过 100 世界单位的地形会被静默裁掉，远端的缩放档位因此只
能看到空屏。示例现在每帧按地图跨度推导远裁剪面，并让最远缩放距离随之放大
（`ZOOM_FAR_MARGIN`），所以任何地图尺寸都能完整入画。

## 已知边界（尚未实现）

瀑布与河口（estuary）几何尚未移植；河道是等宽中心线带，未做 7 分支交汇解算；迷雾按
"已探索 / 当前可见"两态渲染（不区分"当前可见"之外的中间态透明度），且迷雾柱顶取
自身与邻居的最高面，因此在高差极大的边界上会比参考工程略"鼓"；地物是程序化几何
而非参考工程的 prefab 比例。

水面与参考工程逐方向对齐：邻居是水就发一个中心三角形 + 桥接四边形（桥接只由
NE/E/SE 侧发，避免水/水边界重复），邻居是陆地就发四个细分楔形三角形 + 四段岸带；
岸带**平铺在水面高度**，只借用邻格的实体角点平面位置，不借用它的高程——倾斜到岸顶的
写法会在每条六边形边上留下一条亮带。`HexSurface::Water` 因此完全水平，测试
`hexmap.mesh.waterSurfaceIsFlatAtAShore` 会断言所有水面顶点同高、所有法线朝 +Y。

地图生成器的两处刻意偏离参考工程（均已在源码中注释）：侵蚀取共享搜索前沿中最低的
格子，而参考工程是均匀随机挑选可侵蚀格；温度抖动使用私有的值噪声，而参考工程采样
噪声贴图。生成器的 `HemisphereMode` 只实现参考工程的 `North` 映射。

球面后端的已知边界：球面**没有**河流、道路、城墙、地物、迷雾与单位的网格与查询接口
（`HexFlags` 的位可以设置，但没有对应的球面几何与寻路）；海洋是每格一个六边形盖面，
没有浅滩、折射与岸线浪花，且按模块自身的规则 `waterLevel == elevation` 不算淹没，
所以海平面上的格子会露出；崖壁是径向直墙，没有悬垂或侵蚀细节；球面生成器没有河流、
侵蚀与板块构造——平面的 `HexMapGenerator` 没有移植过来。球面地图也**没有存档格式**。
