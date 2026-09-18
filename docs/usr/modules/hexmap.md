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
```

`surface` 取 `0=Terrain, 1=Water, 2=River, 3=Road, 4=Fog, 5=Wall, 6=Feature`。
`x, z` 是偏移坐标（列、行）——`pickCell` 的返回值同样是偏移坐标，与 `elevation` /
`editElevation` / `cellPositionX` 等所有格子接口同一坐标系，因此拾取结果可以直接回喂
给它们（`findPath` 与 `unitSample` 返回的格子也已经是偏移坐标）。`unitSample` 复用
`advance(dt=0)`，因此返回的是含高程与扰动的插值位姿，不是地格中心。`generateMap` 按
**当前网格尺寸**重新生成，会丢弃全部单位与迷雾状态，调用方必须在成功后重新放置单位
并重建全部块。

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
