# 程序化生成模块

**脚本入口：** `eve.Procgen()`

按算法名和 Params 生成网格、地图层、图像、法线图或 GPU 纹理。

## 基本用法

```squirrel
local gen = eve.Procgen();
local p = gen.newParams();
p.setSeed(42); p.setSize(64, 40);
local grid = gen.generate("dungeon.bsp", p);
```

## 参数 schema 与动态编辑 UI

每个内置 Grid 生成器在注册执行函数时同时注册 UI 无关的参数 schema。项目不需要在
编辑器脚本里重复维护字段类型、默认值、范围或 choice 列表；开发者工具、游戏内建造器
和自动化都枚举同一份元数据，再选择自己的呈现方式：

```squirrel
local algorithm = "cave.cellular";
local params = gen.newParams();
gen.applyAlgorithmDefaults(algorithm, params);

for (local i = 0; i < gen.getAlgorithmParamCount(algorithm); ++i) {
    local key = gen.getAlgorithmParamKey(algorithm, i);
    local label = gen.getAlgorithmParamLabel(algorithm, i);
    local kind = gen.getAlgorithmParamKind(algorithm, i); // int|float|bool|string|choice
    local defaultText = gen.getAlgorithmParamDefault(algorithm, i);
    local advanced = gen.isAlgorithmParamAdvanced(algorithm, i);
    if (gen.algorithmParamHasMinimum(algorithm, i)) {
        local minValue = gen.getAlgorithmParamMinimum(algorithm, i);
        local maxValue = gen.getAlgorithmParamMaximum(algorithm, i);
        local step = gen.getAlgorithmParamStep(algorithm, i);
        // 用项目自己的 MVVM/UI 组件生成 slider 或 number field。
    }
    for (local c = 0; c < gen.getAlgorithmParamChoiceCount(algorithm, i); ++c)
        print(gen.getAlgorithmParamChoice(algorithm, i, c) + "\n");
}
```

算法级信息由 `getAlgorithmDisplayName`、`getAlgorithmCategory`、
`getAlgorithmCount`、`getAlgorithmId` 和 `hasAlgorithm` 提供；字段还可读取
`getAlgorithmParamLabel`、`getAlgorithmParamDescription`、
`getAlgorithmParamCategory`、`algorithmParamHasMaximum`。`Params.setInt` /
`getInt` 也统一识别 `seed`、`width`、`height`，所以反射生成的控件不需要为这三个
公共字段编写旁路逻辑。`examples/composable-editor` 在项目脚本中把 schema 映射为
普通 `ui.slider` / `ui.checkbox` / `ui.combo`，C++ 没有固定 Procgen 面板。

`getAlgorithmSchema` 返回通用 `ProcgenRecipeSchema`。同一个对象模型也由
`getTextureRecipeSchema`、`getPbrRecipeSchema` 和 `getMeshRecipeSchema` 返回，因此项目只需要一个字段组件：
`getId`、`getDisplayName`、`getCategory`、`getParamCount`、`getParamKey`、
`getParamLabel`、`getParamDescription`、`getParamCategory`、`getParamKind`、
`getParamDefault`、`paramHasMinimum`、`paramHasMaximum`、`getParamMinimum`、
`getParamMaximum`、`getParamStep`、`isParamAdvanced`、`getParamChoiceCount` 和
`getParamChoice`。`applyTextureRecipeDefaults` / `applyPbrRecipeDefaults` /
`applyMeshRecipeDefaults` 把缺失值写入
`Params`，已有的项目覆盖值保持不变。

```squirrel
local recipe = "pbr.rock";
local values = gen.newParams();
values.setSize(128, 128);
gen.applyPbrRecipeDefaults(recipe, values);
local schema = gen.getPbrRecipeSchema(recipe);
for (local i = 0; i < schema.getParamCount(); ++i)
    buildProjectField(schema, values, i);
local maps = gen.generatePbrMaterial(recipe, values);
local albedo = maps.getAlbedo();
local normal = maps.getNormal();
local roughness = maps.getRoughness();
local metallic = maps.getMetallic();
local height = maps.getHeight();
local ao = maps.getAo();
maps.destroy();
```

当前 `Material` 可直接使用 albedo、normal、height 纹理以及 scalar roughness / metallic。
roughness、metallic、AO 图仍可导出或交给自定义 shader；默认材质还没有对应纹理槽。

## 对象关系与调用时机

`Params` 描述 seed、尺寸和算法参数；`Grid2D` 是结果；`OutputSpec` 决定写入 TileLayer、Image 或 Texture；`Procgen` 按注册算法名执行。

## 纯脚本 PointSet 管线

程序化世界编排使用普通 Squirrel 函数，不要求节点图。`PointSet` 是带位置、法线、
旋转、缩放、密度、独立 seed 和自定义属性的 3D 采样集合。`sampleGrid`、
`filterHeight`、`filterDensity`、`excludeRadius`、`jitterPoints` 和 `selfPrune`
均返回新的集合，不修改输入，因此中间结果可以命名、检查、复用或分支：

```squirrel
local rootSeed = 42;
local candidates = procgen.sampleGrid(32, 20, 2.0,
                                      procgen.deriveSeed(rootSeed, "trees"), 0.75);
local outsideRoad = procgen.excludeRadius(candidates, 20.0, 12.0, 4.0);
local trees = procgen.selfPrune(outsideRoad, 1.8);
```

空间约束也可继续用纯函数串接。`filterBox` / `excludeBox` 接受世界空间 AABB，
`projectToHeightmap` 按 origin、cellSize 和 heightScale 把点投射到已有高度图并写入
地表法线，随后可用 `filterSlope` 按角度筛选：

```squirrel
local region = procgen.filterBox(candidates, 0, -100, 0, 512, 100, 512);
local ground = procgen.projectToHeightmap(region, terrain, 0, 0, 2.0, 80.0);
local buildable = procgen.filterSlope(ground, 0.0, 28.0);
local outsideTown = procgen.excludeBox(buildable, 120, -100, 120, 240, 100, 240);
```

任意多边形和折线样条同样用 `PointSet` 表示控制点，不需要额外节点类型。
`filterPolygon` / `excludePolygon` 处理 XZ 平面的凹多边形；
`filterSplineDistance` 以到最近线段的距离选择道路、河流或隔离带。
`sampleSpline` 按跨线段连续间距采样，并写入切线 yaw、稳定 point seed，可直接放置
路灯、护栏等资产：

```squirrel
local road = procgen.newPointSet();
road.add(0, 0, 0); road.add(80, 0, 30); road.add(140, 0, 120);
local reserved = procgen.filterSplineDistance(candidates, road, 0.0, 8.0);
local outsideRoad = procgen.filterSplineDistance(candidates, road, 8.0, 100000.0);
local lamps = procgen.sampleSpline(road, 12.0,
                                   procgen.deriveSeed(rootSeed, "lamps"), 0.0);

local town = procgen.newPointSet();
town.add(20, 0, 20); town.add(160, 0, 35); town.add(130, 0, 150); town.add(35, 0, 120);
local townCandidates = procgen.filterPolygon(candidates, town);
local wilderness = procgen.excludePolygon(candidates, town);
```

不要让不同内容共享一个可变随机流。用 `deriveSeed(root, "trees")`、
`deriveSeed(root, "rocks")` 为分支派生稳定 seed；修改岩石管线不会扰动树木结果。

### 事务式 hot reload

命名 `ProcgenContext` 是一次完整重建的 staging 区。`publish` 会复制输出，
`commitSystem` 成功后才原子替换该系统的上次快照；`fail`、`abortSystem`、脚本异常或
未提交的 context 都不会破坏旧结果：

```squirrel
function rebuildForest(seed) {
    local ctx = procgen.beginSystem("forest", seed);
    try {
        local points = procgen.sampleGrid(32, 20, 2.0, ctx.seedFor("trees"), 0.8);
        local trees = procgen.selfPrune(points, 1.8);
        ctx.trace("self prune", points.getCount(), trees.getCount(), 0.0);
        if (!ctx.publish("trees", trees)) throw ctx.getError();
        if (!procgen.commitSystem(ctx)) throw procgen.lastError();
    } catch (error) {
        ctx.fail(error.tostring());
        procgen.commitSystem(ctx); // 失败并关闭 staging；旧快照仍然有效
    }
}

eve_reload <- function() { rebuildForest(42); };
```

`getSystemOutput` 返回已提交输出的副本；`getSystemRevision` 可判断是否成功换代；
`getSystemDebugReport` 输出 seed、revision、命名阶段点数/耗时和最终输出点数。

### 按输入复用已提交结果

对昂贵管线可用 `beginCachedSystem(name, seed, buildKey)`。已有快照的 seed 和
buildKey 都相同时，context 的 `isCacheHit()` 为 true 且无需执行或提交；key 变化
时则是普通 active staging，成功提交后才更新缓存身份：

```squirrel
local key = "forest-layout-v2:size=" + worldSize;
local ctx = procgen.beginCachedSystem("forest", seed, key);
if (ctx == null) throw procgen.lastError();
if (ctx.isCacheHit()) {
    local cachedKey = ctx.getBuildKey();
    return procgen.getSystemOutput("forest", "trees");
}
// 构建并 publish，然后 commitSystem(ctx)
local committedKey = procgen.getSystemBuildKey("forest");
```

buildKey 应包含所有影响输出的参数和一段显式 recipe 版本；修改生成代码时同步提升
该版本。缓存命中不会增加 revision。普通 `beginSystem` 始终强制重建。

### 阶段级增量重建

完整系统需要重建但部分昂贵阶段输入未变化时，用 `reuseStage` / `cacheStage` 做细粒度
缓存。阶段缓存属于系统快照的一部分：只有 `commitSystem` 成功才更新，失败重建仍能
在下次尝试中读取上次成功版本。

```squirrel
local lotsKey = "lots-v3:terrain=" + terrainRevision + ":density=" + density;
local lots = ctx.reuseStage("lots", lotsKey);
if (lots == null) {
    lots = buildLots();
    if (!ctx.cacheStage("lots", lotsKey, lots)) throw ctx.getError();
}
local hits = ctx.getStageCacheHitCount();
local misses = ctx.getStageCacheMissCount();
```

每个 key 应覆盖该阶段的直接输入和实现版本。下游阶段把上游 key 纳入自己的 key，
即可用普通脚本明确表达依赖传播，而不需要隐藏的节点图执行器。

### 场景内检查中间结果

`ctx.captureDebug(name, points)` 会把命名 `PointSet` 复制进本次事务。它和正式
输出一起原子提交，因此失败的重建不会让调试视图与场景结果错位。脚本可用
`getSystemDebugStageCount/Name` 枚举阶段，或用 `getSystemDebugStage` 取得副本后
通过 `gfx` 自行选择颜色、大小和投影视图：

```nut
ctx.captureDebug("candidates", candidates);
ctx.captureDebug("after road exclusion", outsideRoad);
local stagedCount = ctx.getDebugStageCount();
local stagedName = ctx.getDebugStageName(0);
local stagedPoints = ctx.getDebugStage(stagedName);
// commitSystem(ctx) 成功后：
local preview = procgen.getSystemDebugStage("forest", "candidates");
local count = procgen.getSystemDebugStageCount("forest");
local firstName = procgen.getSystemDebugStageName("forest", 0);
```

这种方式保留纯代码编排，同时让每个命名中间值都能在游戏场景中检查。调试数据
不直接依赖 graphics 模块，裁剪构建和无头测试仍可使用同一套生成脚本。
可运行示例见 [`examples/procgen-script-pipeline`](../../../examples/procgen-script-pipeline/README.md)。

### 自动记录阶段耗时

用 `beginTrace(name, inputCount)` 和 `endTrace(outputCount)` 包住普通脚本调用，
引擎会使用单调时钟记录耗时并写入系统调试报告，不需要脚本自行读取计时器：

```nut
if (!ctx.beginTrace("self prune", candidates.getCount())) throw ctx.getError();
local trees = procgen.selfPrune(candidates, 32.0);
if (!ctx.endTrace(trees.getCount())) throw ctx.getError();
```

计时器采用后进先出顺序，因此可以嵌套。`getOpenTraceCount()` 可用于脚本断言；
存在未结束的计时器时，`commitSystem` 会拒绝提交并保留上一个快照。仍可使用
`trace(name, inputCount, outputCount, milliseconds)` 导入外部测得的阶段数据。

### 对比热重载前后的结果

每次成功提交会额外保留上一版已提交快照。用
`getPreviousSystemDebugStage(system, stage)` 取得上一版点集，与
`getSystemDebugStage` 的当前点集叠加绘制；`getPreviousSystemRevision(system)` 返回
对应 revision。`getSystemDebugDiffReport(system)` 会列出每个调试阶段的当前点数和
相对上一版的增减，新增和移除的阶段也会明确显示。失败事务不会覆盖这组对比基线。

```nut
local before = procgen.getPreviousSystemDebugStage("forest", "trees");
local after = procgen.getSystemDebugStage("forest", "trees");
print(procgen.getSystemDebugDiffReport("forest") + "\n");
```

## 目标导向指南

### 生成可玩的地牢层

创建 Params，设置 seed 和尺寸，按需添加算法参数；用 `generate("dungeon.bsp", p)` 先检查 Grid，也可配置 Output 将结果直接写入 TileLayer。保存 seed 可复现关卡。也可用 `generate("wfc.simple", p)`（`preset`=`dungeon`|`cave`|`terrain`）做约束驱动铺贴。

### 生成等值面网格（Marching Cubes）

```squirrel
local p = gen.newParams();
p.setSeed(1);
p.setInt("resolution", 32);
p.setString("field", "sphere"); // sphere | torus | noise | terrain
local cpu = gen.buildMesh("mesh.marchingcubes", p);
local gpu = gen.generateMesh("mesh.marchingcubes", p, gfx);
```

### 生成六边形网格星球

`mesh.hexplanet` 生成细分二十面体的对偶网格。星球表面以六边形单元为主，
并包含球面拓扑必需的 12 个五边形单元。所有顶点都位于指定半径的球面上，
可直接交给 3D 渲染系统使用。

```squirrel
local gen = eve.Procgen();
local p = gen.newParams();
p.setFloat("radius", 1.0);
p.setInt("subdivisions", 3);
p.setFloat("tileInset", 0.12);

local cpu = gen.buildMesh("mesh.hexplanet", p);
local gpu = gen.generateMesh("mesh.hexplanet", p, gfx);
```

参数：

- `radius`：星球半径，必须大于 `0`，默认 `1.0`。
- `subdivisions`：二十面体细分次数，范围 `[0, 7]`，默认 `2`。单元总数为
  `10 * 4^subdivisions + 2`；每增加一级，网格规模约增至四倍。
- `tileInset`：每个单元向自身中心收缩的比例，范围 `[0, 0.5)`，默认
  `0.06`。设为 `0` 时单元无缝相接；增大该值可形成清晰的网格间隙。

生成结果的 metadata 包含 `algorithm`、`cells`、`pentagons`、`hexagons`
和 `subdivisions`。网格始终含 12 个五边形，其余单元均为六边形；这些五边形
是用多边形铺满球面时无法消除的拓扑要求。

建议在创建关卡或切换星球时生成一次并缓存 GPU Mesh，不要逐帧重新生成。
### 生成可玩地牢（`level.roguelike`，Roguelike 风格）

`level.roguelike` 是种子驱动的房间-走廊地牢生成器，在墙/地板网格之上
再叠一层**细节**（`Grid2D.detail`）与**对象**，适合快速搭建 2D / 2.5D 关卡：

- 墙格 `getDetail` = 8 位邻接掩码（哪些方向是可行走地板），即瓦片方向，
  可驱动方向感知的自动拼墙。
- 地板格 `getDetail` = 地板图案变体（`1..N`）；`>= 100` 表示随机散落的装饰瓦片。
- `getObjectType` 提供 `spawn` / `stairs` 以及 `pillar` / `chest` 道具。
- `getMeta` 记录 `seed` / `rooms` / `floorPattern` / `decorTiles` / `corridorStyle`，
  便于复现或存档关卡。

```squirrel
local p = procgen.newParams();
p.setSeed(42);
p.setSize(48, 32);
p.setInt("roomCount", 12);
p.setString("corridorStyle", "l");   // l | straight | diagonal
p.setString("floorPattern", "brick");// brick | checker | plank | cobble | plain
p.setFloat("decorDensity", 0.06);
p.setString("decorSet", "mixed");    // none | pillars | treasure | mixed
local grid = procgen.generate("level.roguelike", p);
```

常用规则：`roomCount` / `roomMin` / `roomMax`（房间预算与尺寸）、
`corridorWidth`（走廊宽）、`padding`（外框墙厚）、`spacing`（房间间距）、
`floorVariants`（地板变体数）、`autotile`（是否写入墙方向掩码）。

配套工具：

- `procgen.autotileGrid(grid)`：对**任意**已生成网格的墙格补写 8 位方向掩码，
  为其它算法生成的关卡也加上“瓦片方向”细节。
- `procgen.randomSeed()`：产生一个非 0 的随机种子，用于再掷一局。

可运行脚本与快捷键见 [`examples/roguelike-generator`](../../../examples/roguelike-generator/README.md)。

### 生成城区布局（`urban.parcels` / `mesh.urban`）

`urban.parcels` 与 `mesh.urban` 是基于 Eurographics 2024 论文
*Hierarchical Co-generation of Parcels and Streets in Urban Modeling*
（Chen/Song/Ortner，CGF 43(2)）的引擎移植：从输入地块多边形层级化二分生成
**地块（parcels）与街道（streets）** 协同的城区布局。核心流程：

1. 每层对每个可分割地块计算 ~20 条流线候选（交叉场 + 超流线追踪，必要时回退
   直线弦），用论文式 2 质量分 `Q = λ1·Qsize + λ2·Qregu + λ3·Qacce` 选最优分割线；
2. 消除地块网格中的短边；
3. 对不可达地块分组，生成 I 形/L 形街道入口，并用转角感知 Dijkstra 接入既有
   街道网络，保证每个地块可达、网络连通；
4. 全局几何优化（规则角、边/街平滑、交叉口直角、贴近初始），带“变差回滚”保护。

```squirrel
local p = gen.newParams();
p.setSeed(20260823);
p.setString("land", "rect");          // rect | triangle | ellipse | l | hexagon
p.setFloat("landWidth", 100);
p.setFloat("landHeight", 60);
p.setFloat("minParcelArea", 4.0);
p.setInt("targetParcels", 120);
p.setString("streetPattern", "default"); // default | loop | culdesac | tree
p.setInt("optimize", 1);

// 1) 语义地图：路 = Semantic::Road(11)，地块 = Floor(2)，detail = 地块 id(1..N)
local grid = gen.generate("urban.parcels", p);

// 2) 城区网格：地块块 + 街道带（extrude>0 时挤压成体块）
p.setFloat("extrude", 6.0);
local mesh = gen.generateMesh("mesh.urban", p, gfx);
```

常用参数：`land` 也支持显式多边形（`"0,0;100,0;100,60;0,60"`）；
`lambdaSize/lambdaRegu/lambdaAcce` 控制地块形状偏好（论文式 2 权重）；
`orientation` 设为 `east-west` / `north-south` 可控制地块长边朝向；
`boundaryStreet` 设为 `none` / `random` 可关闭或随机化地块边界街道；
`cellSize`（栅格分辨率）、`extrude`（网格块高）。网格与地图的 metadata 记录
`parcels` / `streets` / `junctions` / `streetLength` / `avgIrregularity`。
交互示例与完整参数见 [`examples/urban-generator`](../../../examples/urban-generator/README.md)。

### 生成随机树木网格

`mesh.tree` 生成可复现的树干、分枝和叶片网格。`branchAlgorithm` 的两个值互斥：

- `weberPenn`（默认）：按层级、枝序和分枝角快速生成稳定骨架。
- `spaceColonization`：让枝梢向树冠吸引点迭代生长，适合更不规则的冠形。

```squirrel
local p = gen.newParams();
p.setSeed(31415);
p.setString("style", "lowpoly");           // lowpoly | realistic
p.setString("branchAlgorithm", "weberPenn");
p.setString("leafMode", "canopy");        // cards | canopy | none
p.setFloat("leafDensity", 0.75);
p.setFloat("height", 6.0);
p.setFloat("crownRadius", 2.0);

local tree = gen.generateMesh("mesh.tree", p, gfx);
```

两种算法共享主干曲率、向性、下垂、随高度变化的枝长/枝径，以及上下层叶片覆盖参数。常用调整：

- 外形与精度：`height`、`trunkRadius`、`crownRadius`、`radialSegments`、`curveSegments`。
- 通用枝形：`trunkCurve`、`branchCurve`、`curveBack`、`tropism`、`droop`。
- 层级生长：`branchLengthFalloff`、`branchRadiusFalloff`；默认表现为下层枝更粗更长。
- 叶片：`leafSize`、`leafDensity`、`foliageStart`、`lowerLeafCoverage`、`upperLeafCoverage`。
- Weber–Penn：`branchLevels`、`branchCount`、`branchAngle`、`branchAngleVariation`、`phyllotaxis`、`apicalDominance`。
- 空间殖民：`attractorCount`、`colonizationIterations`、`influenceRadius`、`killRadius`、`growthStep`、`branchInertia`、`maxTurnAngle`、`maxCumulativeAngle`、`maxChildren`。

完整参数范围、交互快捷键和可运行脚本见 [`examples/tree-generator`](../../../examples/tree-generator/README.md)。生成网格有一定成本，应在加载、换 seed 或修改参数时重建，不要每帧调用。

### 生成线性可拼接结构（栅栏 / 石墙 / 桥 / 长城 / 树篱 / 拒马）

将单个可拼接单元段沿 X 轴重复 N 次生成连续结构，接缝处纹理连续平铺。

```squirrel
local p = gen.newParams();
p.setInt("segments", 8);      // 重复单元数
p.setFloat("segLength", 2.0); // 单元段长度
p.setFloat("height", 1.5);    // 高度覆盖
p.setFloat("uvRepeat", 2.0);  // 每世界单位的纹理重复次数
local mesh = gen.generateMesh("mesh.stonewall", p, gfx); // 或 mesh.fence / mesh.bridge
                                                         // mesh.greatwall / mesh.hedge / mesh.chevaldefrise
```

共享参数：`segments`、`segLength`、`height`、`depth`、`thickness`、`scale`、`uvRepeat`。

### 生成无缝材质

设置 texture recipe、尺寸、octaves、pixelSize 和 seamless，调用纹理或法线图生成接口；开发期切换 seed 预览，发布时缓存 Texture，不能每帧重新生成。

### 生成多层城堡

`mesh.castle` 生成可复现的完整城堡网格：同心多层城墙、转角与区间塔楼、带真实门洞的城门楼、墙顶步道、中央多层主堡，以及通往每圈墙顶和每层主堡的实体楼梯。

```squirrel
local p = gen.newParams();
p.setSeed(20260826);
p.setFloat("width", 48.0);
p.setFloat("depth", 40.0);
p.setInt("rings", 2);          // 1..4 层同心城墙
p.setInt("keepFloors", 4);     // 1..8 层主堡
p.setInt("detail", 2);         // 0 主体，1 垛口，2 区间塔楼与庭院建筑
p.setFloat("towerSpacing", 16.0);
p.setFloat("stairWidth", 2.0);
local castle = gen.generateMesh("mesh.castle", p, gfx);
```

主要参数还包括 `wallHeight`、`wallThickness`、`ringInset`、`ringHeightStep`、`towerRadius`、`towerHeight`、`towerHeightStep`、`towerSides`、`gateWidth`、`keepWidth`、`keepDepth`、`floorHeight`、`courtyardBuildings`、`stepHeight`、`merlonWidth`、`uvRepeat` 和 `scale`。CPU `buildMesh()` 的元数据提供 `rings`、`wallSections`、`towerCount`、`stairFlights`、`keepFloors`、`courtyardBuildings`、`detail` 与 `seed`，可用于生成图调试、预算检查和自动化验证。

城堡还输出 `walls`、`battlements`、`towers`、`gatehouses`、`stairs`、`keep`、`courtyard` 命名三角形组。可用 `copyGroup()` 提取组件、`appendTransformed()` 组合多个生成结果，再用 `uploadMesh()` 上传；因此墙体、楼梯和塔楼可以使用不同材质、碰撞或 LOD 策略，而无需重新实现 recipe。

`getMeshRecipeSchema()` 返回统一的 `RecipeDescriptor` 输入 schema，`applyMeshRecipeDefaults()` 可填充缺省参数。编辑器或可视化生成图可据此自动创建输入 pin、滑杆、默认值和帮助文本，不必硬编码 `Params` 字符串键。

## 常见问题

- 未保存 seed，无法复现玩家问题。
- Output palette 缺少算法输出的 tile key。
- 在每帧 update 生成大地图或纹理。
- 在每帧重新生成树木网格；应缓存 `Mesh`，仅在 seed 或参数变化时重建。

## L-system 文法生成

通用随机括号 L-system 引擎(`procgen.newLSystem()`)。给定 axiom 与产生式(可带权重随机),迭代若干次后用 3D 海龟解释:绘制 `/` 折返、`[ ]` 入/弹栈产生分支,枝条粗细随深度衰减。固定 seed 结果完全可复现。除 `mesh.lsystem` 网格配方外,`trace()` 可把枝段作为样条控制点输出(道路、二维布局)。

```squirrel
local ls = procgen.newLSystem();
ls.setAxiom("F");
ls.addRule('F', "F[+F]F[-F]F");          // 确定性产生式
// ls.addRules('A', ["F[+A]A", "FA"], [2.0, 1.0]);  // 加权随机产生式
ls.setIterations(4); ls.setAngle(26.0); ls.setSeed(42);
local road = procgen.newPointSet(); ls.trace(road);   // 枝段 → 控制点
```

`mesh.lsystem` 配方内置 `tree` / `fern` / `plant` / `weed` 预置,输出锥形枝干与叶片卡。

## 蓝噪声撒点

`procgen.poissonDisk(width, depth, radius, seed, maxPoints)` 在 XZ 平面做 Bridson 蓝色噪声撒点(任意两点间距 ≥ radius,确定性),适合均匀散布草丛、石头等。

```squirrel
local scatter = procgen.poissonDisk(100, 100, 2.5, 99, 500);  // 最多 500 点
```

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `abort()`、`abortSystem()`、`add()`、`addObject()`、`addObjectAt()`、`addRule()`、`addRules()`、`applyToLayer()`、`autotileGrid()`、`beginSystem()`、`buildMesh()`、`clear()`、`clearObjects()`、`clearRules()`、`commitSystem()`、`deriveSeed()`、`empty()`、`excludeRadius()`、`fail()`、`fill()`、`filterDensity()`、`filterHeight()`、`generate()`、`generateImage()`、`generateMesh()`、`generateNormalImage()`
- `generateTexture()`、`generateTo()`、`getAlgorithmCount()`、`getAlgorithmId()`、`getCell()`、`getDetail()`、`getFloat()`、`getHeight()`、`getInt()`
- `getIterations()`、`getLayer()`、`getMeshRecipeCount()`、`getMeshRecipeId()`、`getMeshRecipeSchema()`、`getMeta()`、`getName()`、`getObjectCount()`、`getObjectGid()`、`getObjectHeight()`、`getObjectName()`、`getObjectType()`
- `getObjectWidth()`、`getObjectX()`、`getObjectY()`、`getPalette()`、`getPaletteGid()`、`getPath()`、`getSeed()`、`getString()`
- `getTarget()`、`getTextureRecipeCount()`、`getTextureRecipeId()`、`getWidth()`、`gridToJson()`、`has()`、`hasAlgorithm()`、`hasMeshRecipe()`、`hasTextureRecipe()`、`applyMeshRecipeDefaults()`
- `getDensity()`、`getError()`、`getFloatAttribute()`、`getNormalX()`、`getNormalY()`、`getNormalZ()`、`getOutput()`、`getOutputCount()`、`getOutputName()`、`getPointSeed()`、`getScaleX()`、`getScaleY()`、`getScaleZ()`、`getStringAttribute()`、`getSystemDebugReport()`、`getSystemOutput()`、`getSystemOutputCount()`、`getSystemOutputName()`、`getSystemRevision()`、`getSystemSeed()`、`getTraceCount()`、`getTraceInputCount()`、`getTraceMilliseconds()`、`getTraceName()`、`getTraceOutputCount()`、`getX()`、`getY()`、`getYaw()`、`getZ()`、`hasFailed()`、`hasFloatAttribute()`、`hasOutput()`、`hasStringAttribute()`、`hasSystem()`、`isActive()`、`jitterPoints()`、`lastError()`、`newGrid()`、`newLSystem()`、`newOutput()`、`newParams()`、`newPointSet()`、`poissonDisk()`、`publish()`、`randomSeed()`、`removeSystem()`、`resize()`、`sampleGrid()`、`seedFor()`、`selfPrune()`、`setAngle()`、`setAxiom()`、`setBranchRadius()`、`setBranchRadiusFalloff()`、`setCell()`、`setDensity()`、`setDetail()`、`setFloat()`、`setFloatAttribute()`、`setInitialHeading()`、`setInt()`
- `setIterations()`、`setLayer()`、`setLeafSize()`、`setLeafSymbols()`、`setMeta()`、`setNormal()`、`setPalette()`、`setPaletteGid()`、`setPath()`、`setPointSeed()`、`setPosition()`、`setScale()`、`setSeed()`、`setSize()`、`setStep()`、`setString()`、`setStringAttribute()`、`setTropism()`、`setYaw()`、`trace()`
- `setTarget()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/procgen/`](../../../src/modules/procgen/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `procgen`；跨模块 hex 关卡见 [`test/hex_level_simulation.cpp`](../../../test/hex_level_simulation.cpp)、[`test/hex_level_data.cpp`](../../../test/hex_level_data.cpp)、[`examples/hex-levels/`](../../../examples/hex-levels/)。
