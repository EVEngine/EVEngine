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

## 对象关系与调用时机

`Params` 描述 seed、尺寸和算法参数；`Grid2D` 是结果；`OutputSpec` 决定写入 TileLayer、Image 或 Texture；`Procgen` 按注册算法名执行。

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

### 生成、侵蚀并分析 3D 地形

高度场生成后可串联热力侵蚀、水力侵蚀、D8 水文分析与生态群落分类。所有阶段均为
确定性 CPU 算法，适合在关卡烘焙、编辑器修改或世界创建时运行；不要逐帧重新侵蚀。

```squirrel
local p = gen.newParams();
p.setSeed(20260826);
p.setSize(512, 512);
p.setFloat("frequency", 1.0 / 96.0);
p.setInt("octaves", 6);

local hm = gen.generateHeightmap(p);
gen.erodeTerrainThermal(hm, 20, 0.018, 0.32);
gen.erodeTerrainHydraulic(hm, 60, 0.012, 0.08, 2.0, 0.18, 0.12);
// Priority-Flood 排水 + 沟头起蚀 + stream-power 下切；闭合盆地会在允许的
// maxDepth 内切穿溢流坎，随后反复汇流俘获并扩宽为 V 形河谷。
gen.erodeTerrainFluvial(hm, 8, 0.006, 0.0075, 0.12, 1.8);

// Production terrain should normally separate ordinary river-bed incision
// from the much more destructive act of cutting through a watershed sill.
// The last value is maxBreachDepth; a connected lake deeper than this remains
// endorheic even when maxDepth allows deeper channel beds elsewhere.
gen.erodeTerrainFluvialAdvanced(hm, 12, 0.008, 0.02, 0.14, 4.0, 0.025);

// For a heightfield sampled at twice the reference resolution, scale routing
// coordinates and per-cell grades while keeping thresholds area-relative.
gen.erodeTerrainFluvialScaled(hm2x, 12, 0.008, 0.02, 0.14, 8.0, 0.025, 2.0);
local layers2x = gen.analyzeTerrainScaled(hm2x, 400.0, 0.22, 0.42, 2.0);

// Detailed mode performs the same scaled erosion but also returns persistent
// process diagnostics, analogous to erosion tools' Wear / Deposit outputs.
// Values use heightmap units; deposition - wear always equals heightDelta.
local erosion = gen.erodeTerrainFluvialDetailed(
    hm, 12, 0.008, 0.02, 0.14, 4.0, 0.025, 1.0);
local removed = erosion.getWear(100, 80);
local deposited = erosion.getDeposition(100, 80);
local netChange = erosion.getHeightDelta(100, 80);
// exposure <= 0 selects a robust 99th-percentile auto exposure. Combined maps
// encode wear as orange and deposition as cyan; separate maps are also exposed.
local processMap = gen.generateTerrainErosionMap(erosion, 0.0);
local wearMap = gen.generateTerrainWearMap(erosion, 0.0);
local depositMap = gen.generateTerrainDepositionMap(erosion, 0.0);

// River surfaces can be split by world-space longitudinal grade. This allows
// calm water and cascades to use different shaders/tints while sharing the
// same drainage graph and hydraulic-geometry width calculation.
local calm = gen.generateTerrainRiverMeshAdvanced(
    hm, layers, gfx, ox, oy, 64, 64, cellSize, heightScale,
    0.04, 0.24, 0.035, 0.0, 0.12);
local cascades = gen.generateTerrainRiverMeshAdvanced(
    hm, layers, gfx, ox, oy, 64, 64, cellSize, heightScale,
    0.035, 0.20, 0.030, 0.12, 1.6);

// riverThreshold <= 1 时表示占地图格数的比例；> 1 时表示汇流格数阈值。
local layers = gen.analyzeTerrain(hm, 0.025, 0.25, 0.65);
local biome = layers.getBiomeName(100, 80);
local river = layers.isRiver(100, 80);
local lakeDepth = layers.getLakeDepth(100, 80);
local moisture = layers.getMoisture(100, 80);

// 每块 64x64，返回可写入文件或直接交给 VoxelWorld 的 ByteData。
local terrainAsset = gen.bakeTerrainAsset(hm, layers, 64);

// 构建 64x64 单元的 LOD1 渲染块；边裙深度 2 世界单位。
local chunk = gen.buildTerrainChunk(hm, layers, 0, 0, 64, 64, 1, 1.0, 24.0, 2.0);
local gpuMesh = gen.generateTerrainChunkMesh(chunk, gfx);
local splat = gen.generateTerrainSplatMap(chunk); // RGBA = 沙地、植被、岩石、积雪
local splatTexture = gfx.newTexture(splat, false, false);
local terrainShader = gen.createTerrainMaterialShader(gfx);
// entity.setTexture(splatTexture); entity.setShader(terrainShader);
// splat 的 Alpha 是积雪权重，不能直接作为普通透明材质显示。
local albedo = gen.generateTerrainAlbedoMap(chunk); // 不透明生态诊断色，河流/海洋单独着色
local albedoTexture = gfx.newTexture(albedo, false, false);
// 独立水面：宽度随汇流量增长，沿 D8 下游接收格连续连接。
local riverMesh = gen.generateTerrainRiverMesh(hm, layers, gfx, 0, 0, 64, 64,
                                                1.0, 24.0, 0.15, 0.8, 0.04);
// Priority-Flood 保留的深洼地生成独立湖面；岸线按深度等值线在格内插值。
local lakeMesh = gen.generateTerrainLakeMesh(hm, layers, gfx, 0, 0, 64, 64,
                                              1.0, 24.0, 0.004, 0.04);
local waterShader = gen.createTerrainWaterShader(gfx); // 微法线、Fresnel 与太阳高光
```

`ProcgenTerrainLayers` 提供 `getFlowAccumulation`、`isRiver`、`getLakeDepth`、`isLake`、`getTemperature`、
`getMoisture`、`getBiome` 和 `getBiomeName`。群落名称包括 `ocean`、`beach`、
`desert`、`grassland`、`forest`、`rainforest`、`tundra`、`taiga`、`alpine`、
`river`、`lake`、`wetland`。湖岸和低坡河岸会依据淡水邻域、湿度与坡度形成
湿地过渡。可运行的交互流程见 [`examples/terrain-editor`](../../../examples/terrain-editor/)。

河流显示阈值和沟头起蚀阈值彼此独立：低阶细沟可以先以较弱速率侵蚀并竞争径流，
但只有达到 `riverThreshold` 的通道才进入最终河流层。`flowAccumulation` 采用
Freeman 多流向权重分配以削弱 D8 栅格偏置，同时保存一条主接收器保证阈值河网
可追踪；连续流向向量用于平滑河面中心线。
当溢流坎所需切深超过 `maxDepth` 时，系统保留闭合盆地，而不会无界削平山脊。

C++ 烘焙工具还提供版本化 `EVTR` v5 分块资产：每块独立压缩和校验，高度使用
UNORM16，汇流/湖深/温湿度使用 UNORM8，连续流向使用两个 SNORM8 分量，D8
流向、Strahler 河序、河流和群落保留精确字节。河序用于稳定地区分沟头、支谷和
冲积主谷。旧版 v1/v2/v3/v4 仍可读取，并从旧 D8 方向重建兼容流向。
加载后的块无需重新分析高度场即可追踪下游、重建河面或执行水文查询。`TerrainAsset`
只解析目录并按块解码；`TerrainStreamingCache` 支持观察点半径、每帧加载预算、
淘汰、整数层采样和跨块双线性高度采样。运行时水文不再止于块内查询：
`getReceiver()` 使用全局格坐标跨越 EVTR 块边界解析 D8 下游格，`traceFlow()` 可沿
已驻留块连续追踪整条河道；`buildWindow()` 将任意矩形及其一格 halo 拼成完整的
高度、水文和气候窗口。河面网格、岸线和法线构建应读取该 halo，从而让汇流量、
连续流向与 Strahler 河序在块接缝两侧保持一致。若相邻块尚未驻留，这些接口明确
返回失败，调用方可遵守加载预算延后构网，而不会把缺失邻块误判为河流出口。

`ProcgenTerrainMeshChunk` 的基础网格按 `2^lod` 采样，高度法线始终从原始全分辨率
高度场求导，因此同边相邻块不会出现光照法线断层。块四周生成可配置深度的边裙，
用于遮盖不同 LOD 之间的 T 型接缝。每个顶点另有四个归一化材质权重，可通过
`getMaterialWeight(vertex, channel)` 读取；通道依次为沙地、植被、岩石、积雪，
由群落、坡度、高程和湿度共同计算。`generateTerrainSplatMap()` 会生成标准 RGBA8
splat 纹理，每个像素四通道之和严格为 255，适合跨 Vulkan/WebGPU 上传及离线保存。
`generateTerrainAlbedoMap()` 则将四种权重混合为不透明预览颜色，并用块内保存的
群落 ID 区分海洋、海滩和河流；它适合编辑器诊断，不能替代运行时多层 PBR shader。
`createTerrainMaterialShader()` 创建运行时四层地形 shader：直接采样 splat，混合各层
Albedo 与粗糙度，并用世界坐标平滑噪声增加宏观/细节变化。shader 接入 Mesh3D
三级级联阴影，并用连续噪声梯度产生轻微微表面法线。它与普通 Mesh3D ABI兼容，
不需要给地形顶点增加私有属性。
`generateTerrainRiverMesh()` 为河道格生成从当前格到下游接收格的水面带，最小/最大
宽度按对数汇流量插值。水面按地形块分别构建，可使用低粗糙度蓝色材质独立渲染。
标准 Mesh 仍上传几何、法线和 UV，避免改变通用网格 ABI。

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
p.setString("decorSet", "mixed");    // none | pillars | treasure | nature | mixed
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

## 常见问题

- 未保存 seed，无法复现玩家问题。
- Output palette 缺少算法输出的 tile key。
- 在每帧 update 生成大地图或纹理。
- 在每帧重新生成树木网格；应缓存 `Mesh`，仅在 seed 或参数变化时重建。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `addObject()`、`addObjectAt()`、`analyzeTerrain()`、`analyzeTerrainScaled()`、`applyToLayer()`、`autotileGrid()`、`bakeTerrainAsset()`、`buildMesh()`、`buildTerrainChunk()`、`clearObjects()`、`createTerrainMaterialShader()`、`erodeTerrainFluvial()`、`erodeTerrainFluvialAdvanced()`、`erodeTerrainFluvialScaled()`、`erodeTerrainHydraulic()`、`erodeTerrainThermal()`、`fill()`、`generate()`、`generateImage()`、`generateMesh()`、`generateNormalImage()`、`generateTerrainAlbedoMap()`、`generateTerrainChunkMesh()`、`generateTerrainRiverMesh()`、`generateTerrainRiverMeshAdvanced()`、`generateTerrainSplatMap()`
- `generateTexture()`、`generateTo()`、`getAlgorithmCount()`、`getAlgorithmId()`、`getCell()`、`getDetail()`、`getFloat()`、`getHeight()`、`getInt()`
- `getLayer()`、`getMeshRecipeCount()`、`getMeshRecipeId()`、`getMeta()`、`getName()`、`getObjectCount()`、`getObjectGid()`、`getObjectHeight()`、`getObjectName()`、`getObjectType()`
- `getObjectWidth()`、`getObjectX()`、`getObjectY()`、`getPalette()`、`getPaletteGid()`、`getPath()`、`getSeed()`、`getString()`
- `getTarget()`、`getTextureRecipeCount()`、`getTextureRecipeId()`、`getWidth()`、`gridToJson()`、`has()`、`hasAlgorithm()`、`hasMeshRecipe()`、`hasTextureRecipe()`
- `lastError()`、`newGrid()`、`newOutput()`、`newParams()`、`randomSeed()`、`resize()`、`setCell()`、`setDetail()`、`setFloat()`、`setInt()`
- `setLayer()`、`setMeta()`、`setPalette()`、`setPaletteGid()`、`setPath()`、`setSeed()`、`setSize()`、`setString()`
- `setTarget()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/procgen/`](../../../src/modules/procgen/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `procgen`；跨模块 hex 关卡见 [`test/hex_level_simulation.cpp`](../../../test/hex_level_simulation.cpp)、[`test/hex_level_data.cpp`](../../../test/hex_level_data.cpp)、[`examples/hex-levels/`](../../../examples/hex-levels/)。
