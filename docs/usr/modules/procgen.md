# 程序化生成模块

**脚本入口：** `eve.Procgen()`

按算法名和 Params 生成网格、地图层、图像、法线图或 GPU 纹理。

## Result 投影约定

Procgen 的创建、生成、输出和事务提交 API 都返回统一的 Squirrel Result 表：
`{ ok, code, hasValue, status, diagnostics, value }`。调用方必须先读取 `ok`；只有
`ok == true` 时才读取 `value`。失败信息使用 `status.summary` 或结构化
`diagnostics`，不再通过空值和全局错误字符串拼接错误协议。Graphics 上传属于
C++ render bridge 的 borrowed 边界，不是 Squirrel 的第二套生成入口。

这里的 `newParams`、`generate`、`buildMesh` 等是当前 canonical Squirrel 方法名；
Procgen 不提供 `lastError()`、`*Owned` 或 `*Checked` 的兼容命名。Result 的 `value`
可以是由 generation handle 支持的 owned proxy，释放和 stale 检查遵循该 proxy 的
公共方法。
## UE PCG 对标范围

本模块对标的是 UE PCG 的核心工作流，而不是复制 UE 类型或资产格式：统一 Spatial Data、
带属性 Point Data、可缓存 Point Graph/子图、分区与 Hierarchical Generation、多 Generation
Source、视锥与时间预算、Biome Rules、Shape Grammar，以及到 Scene 的可撤销实例批次均有
对应实现。编辑器侧使用通用 `GraphDocument` 的 `procgen.point` domain，编译为版本化运行时
PointGraph 定义。

当前执行器是确定性的 CPU 实现；没有照搬 UE 的 GPU PCG Compute Graph。Scene sink 发布
稳定节点与资产 tag，具体模型加载/渲染仍由项目的 scene/graphics 资产系统消费。PointGraph
保持无环数据流，迭代算法应封装为一个 operation 或子图，而不是创建反馈边。

## 基本用法

```squirrel
local gen = eve.Procgen();
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(42); p.setSize(64, 40);
local gridResult = gen.generate("dungeon.bsp", p);
if (!gridResult.ok) throw gridResult.status.summary;
local grid = gridResult.value;
```

`Grid2D` 的资产对象接口用于把生成布局与任意项目资产包解耦：
`addAssetObject(name, role, asset, x, y, width, height, rotation, flags)` 添加带语义角色、
资产标识、占地、旋转和标志位的对象；读取时使用 `getObjectAsset(index)`、
`getObjectRotation(index)` 与 `getObjectFlags(index)`。资产标识只是调用方配置的字符串，
具体 prefab、模型或精灵由渲染适配器解析。

### 受检 artifact API

跨存档、跨进程或需要发布到可选后端时，使用 `buildArtifact()` 与
`publishArtifact()`。两个方法都返回公共 Result 投影表；脚本必须检查
`result.ok`，或用 `eve.result.ignore(result, "明确原因")` 记录有意忽略。artifact
身份必须是非 nil 的规范 UUID 文本，发布选项依次为 scene、graphics、physics、map：

```squirrel
local artifactResult = gen.buildArtifact("mesh.castle", p,
                                         "11111111-1111-4111-8111-111111111111");
if (!artifactResult.ok) throw artifactResult.status.summary;
local artifact = artifactResult.value;
local receiptResult = gen.publishArtifact("mesh.castle", p,
    "11111111-1111-4111-8111-111111111111", false, true, false, false);
if (!receiptResult.ok) throw receiptResult.status.summary;
local receipt = receiptResult.value;
```

所有可能失败的 Procgen 创建、生成、上传、输出和提交操作都返回同一套 Result
投影；成功后才读取 `value`，失败时使用 `status.summary` 或 `diagnostics`。
不要用空值与另一个错误字符串拼接成错误协议。

## 参数 schema 与动态编辑 UI

每个内置 Grid 生成器在注册执行函数时同时注册 UI 无关的参数 schema。项目不需要在
编辑器脚本里重复维护字段类型、默认值、范围或 choice 列表；开发者工具、游戏内建造器
和自动化都枚举同一份元数据，再选择自己的呈现方式：

```squirrel
local algorithm = "cave.cellular";
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local params = paramsResult.value;
local defaultsResult = gen.applyAlgorithmDefaults(algorithm, params);
if (!defaultsResult.ok) throw defaultsResult.status.summary;

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

`generateTexture(recipeId, params, graphics)` 直接把纹理配方生成的临时
`ImageData` 上传到传入的 `Graphics`，返回统一 Result。成功时 `value` 是由该
`Graphics` 资源系统拥有的 borrowed `Texture`；调用方不得销毁它，也不得跨
Graphics 关闭、资源重建或后端切换保存引用。参数、Graphics 或生成结果无效时，
调用方必须检查失败 Result。

## 内置原型建造套件（纯程序化）

模型分类和视觉语言参考 [RGSDev Free 3D Modular Low Poly Assets](https://rgsdev.itch.io/free-3d-modular-low-poly-assets-for-prototyping-by-rgsdev)，纹理方向参考 [Kenney Prototype Textures](https://kenney-assets.itch.io/prototype-textures)。实现只生成新的顶点、索引和 RGBA8 像素数据，不复制、打包或运行时加载参考资源中的模型与图片文件。

`prototype.*` 提供 75 个基础 3D 原型模块，覆盖方块、锥体、圆柱、门窗、墙角、
楼梯、坡道、围栏、栏杆、柱子、梯子、地面、机关和标记物。它们不是内置 FBX，
也不会从项目目录读取模型；每次生成都由 CPU 几何函数直接写入 `MeshBuild`。
所有网格使用 Y-up、XZ 居中占地、Y=0 落地的统一原点，避免导入资源中常见的
偏移和旋转修正。

```squirrel
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
local defaultsResult = gen.applyMeshRecipeDefaults("prototype.stairs-corner", p);
if (!defaultsResult.ok) throw defaultsResult.status.summary;
p.setFloat("width", 4.0);
p.setFloat("height", 2.0);
p.setFloat("depth", 4.0);
p.setFloat("thickness", 0.16);
p.setInt("steps", 8);
local meshResult = gen.buildMesh("prototype.stairs-corner", p);
if (!meshResult.ok) throw meshResult.status.summary;
local mesh = meshResult.value;
```

通用参数为 `scale`、`width`、`height`、`depth`、`thickness`、`detail`、
`steps` 和 `uvScale`（每世界单位的纹理重复次数）。`detail` 控制圆柱、球体和圆环等的
径向细分，`steps` 控制楼梯和梯级数量；
每个 recipe 的尺寸默认值来自同一份 `RecipeDescriptor`。C++ 可用
`prototypePieceDescriptors()` 枚举，或调用 `generatePrototypePiece()` 获得带结构化
诊断的 owning `MeshBuild`。

`tex.prototype.*` 提供 13 种原型图案：标注/象限/细分/面板网格、两种斜线网格、
两种棋盘格、弱网格、楼梯/门洞/窗洞尺寸引导和十字定位点。每种图案通过
`palette` 参数选择 `dark`、`light`、`purple`、`orange`、`green`、`red`，因此同一套
13 个函数可产生 78 个标准组合；`custom` 还允许自定义背景与线色。纹理像素由
CPU 直接绘制，不嵌入 PNG/SVG。

```squirrel
local textureParamsResult = gen.newParams();
if (!textureParamsResult.ok) throw textureParamsResult.status.summary;
local tp = textureParamsResult.value;
tp.setSize(1024, 1024);
tp.setString("palette", "orange");
tp.setInt("cellSize", 128);
tp.setInt("lineWidth", 2);
tp.setFloat("minorAlpha", 0.10);
tp.setFloat("majorAlpha", 0.45);
local textureResult = gen.generateTexture("tex.prototype.diagonal-grid", tp, gfx);
if (!textureResult.ok) throw textureResult.status.summary;
local texture = textureResult.value;
```

纹理参数还包括 `guideSteps`、`backgroundR/G/B` 与 `lineR/G/B`。C++ 可用
`prototypeTextureDescriptors()` 枚举，并以 `generatePrototypeTexture()` 生成 owning
RGBA8 `ImageData`。相同参数逐字节确定；生成结果应按参数 build key 缓存，不能每帧重建。

### Params 的类型与尺寸语义

`Params` 的算法私有值由 owning 的 `Value::Object` 保存。`setInt`、`setFloat`、
`setBool`、`setString` 分别保留整数、浮点数、布尔值和字符串类型；getter 不会把
字符串解析成数字/布尔，也不会把数字 stringify 成字符串。`getInt` 支持范围内的
整数、有限整数浮点数和 `bool -> 0/1`；`getFloat` 支持可表示的整数/浮点数和
`bool -> 0/1`；`getBool` 只接受布尔值或精确数值 `0/1`；不匹配时返回默认值。

`seed`、`width`、`height` 的 `setSeed`/`setSize` 和 `setInt` 路径属于独立的生成
维度域。浮点、布尔或字符串 setter 使用同名 key 时属于算法私有域，不会修改
`getSeed`/`getWidth`/`getHeight`。`canonicalString()` 稳定排序并带类型标签，
因此同一组参数在不同插入顺序下相同，而 `1`、`1.0`、`true`、`"1"` 不会碰撞。

```squirrel
local recipe = "pbr.rock";
local valuesResult = gen.newParams();
if (!valuesResult.ok) throw valuesResult.status.summary;
local values = valuesResult.value;
values.setSize(128, 128);
local defaultsResult = gen.applyPbrRecipeDefaults(recipe, values);
if (!defaultsResult.ok) throw defaultsResult.status.summary;
local schemaResult = gen.getPbrRecipeSchema(recipe);
if (!schemaResult.ok) throw schemaResult.status.summary;
local schema = schemaResult.value;
for (local i = 0; i < schema.getParamCount(); ++i)
    buildProjectField(schema, values, i);
local mapsResult = gen.generatePbrMaterial(recipe, values);
if (!mapsResult.ok) throw mapsResult.status.summary;
local maps = mapsResult.value;
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

## PointSet 管线

空间运算、分区、发布、图资产、GPU compute、hot reload 与增量重建见 [PointSet 管线](procgen/pointset-pipeline.md)。

## 目标导向指南

### 生成可玩的地牢层

创建 Params，设置 seed 和尺寸，按需添加算法参数；用 `generate("dungeon.bsp", p)` 先检查 Grid，也可配置 Output 将结果直接写入 TileLayer。保存 seed 可复现关卡。也可用 `generate("wfc.simple", p)`（`preset`=`dungeon`|`cave`|`terrain`）做约束驱动铺贴。

### 生成等值面网格（Marching Cubes）

```squirrel
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(1);
p.setInt("resolution", 32);
p.setString("field", "sphere"); // sphere | torus | noise | terrain
local cpuResult = gen.buildMesh("mesh.marchingcubes", p);
if (!cpuResult.ok) throw cpuResult.status.summary;
local cpu = cpuResult.value;
// Graphics 上传由 C++ render bridge 的 generateMeshBorrowed 完成；
// 当前 Squirrel facade 只返回 handle-backed CPU MeshBuild。
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
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setFloat("radius", 1.0);
p.setInt("subdivisions", 3);
p.setFloat("tileInset", 0.12);

local cpuResult = gen.buildMesh("mesh.hexplanet", p);
if (!cpuResult.ok) throw cpuResult.status.summary;
local cpu = cpuResult.value;
// Graphics 上传由 C++ render bridge 的 generateMeshBorrowed 完成。
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
local paramsResult = procgen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(42);
p.setSize(48, 32);
p.setInt("roomCount", 12);
p.setString("layoutStyle", "clustered");       // grid | clustered
p.setString("connectionStyle", "nearest");    // sequential | nearest
p.setString("corridorStyle", "l");   // l | straight | diagonal
p.setString("floorPattern", "brick");// brick | checker | plank | cobble | plain
p.setFloat("decorDensity", 0.06);
p.setString("decorSet", "mixed");    // none | pillars | treasure | mixed
p.setFloat("propDensity", 0.16);     // themed room-edge prop dressing
p.setFloat("corridorLightDensity", 0.035); // semantic wall lights on corridors
// Optional, asset-pack-neutral pools (model/prefab ids, comma separated):
p.setString("assetPack", "my-dungeon-pack");
p.setString("assets.container", "crate,barrel,chest");
p.setString("assets.light", "torch,candle");
local gridResult = procgen.generate("level.roguelike", p);
if (!gridResult.ok) throw gridResult.status.summary;
local grid = gridResult.value;
```

常用规则：`roomCount` / `roomMin` / `roomMax`（房间预算与尺寸）、
`corridorWidth`（走廊宽）、`padding`（外框墙厚）、`spacing`（房间间距）、
`floorVariants`（地板变体数）、`autotile`（是否写入墙方向掩码）。`clustered`
布局从中心房间向四周生长，配合 `nearest` 连接可得到短走廊和分叉拓扑；默认值仍为
`grid` / `sequential`，以保持既有调用结果。

配套工具：

- `procgen.autotileGrid(grid)`：对**任意**已生成网格的墙格补写 8 位方向掩码，
  为其它算法生成的关卡也加上“瓦片方向”细节。
- `procgen.randomSeed()`：产生一个非 0 的随机种子，用于再掷一局。
- 自动布景会给房间选择储藏、寝室、餐厅、军械、宝库、祭坛或酒馆主题。
  每个房间还会输出带主题资产名与矩形范围的 `room` 区域对象；楼梯对象带朝向与
  边界开口标记，3D 渲染器可据此替换对应墙段并生成向外下行的入口。
  对象通过 `getObjectType/Asset/Rotation/Flags` 暴露语义角色、可替换资产、朝向和
  放置属性；`assets.<role>` 池可映射任意 3D 资产包，无需修改生成器。

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
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(20260823);
p.setString("land", "rect");          // rect | triangle | ellipse | l | hexagon
p.setFloat("landWidth", 100);
p.setFloat("landHeight", 60);
p.setFloat("minParcelArea", 4.0);
p.setInt("targetParcels", 120);
p.setString("streetPattern", "default"); // default | loop | culdesac | tree
p.setInt("optimize", 1);

// 1) 语义地图：路 = Semantic::Road(11)，地块 = Floor(2)，detail = 地块 id(1..N)
local gridResult = gen.generate("urban.parcels", p);
if (!gridResult.ok) throw gridResult.status.summary;
local grid = gridResult.value;

// 2) 城区网格的 CPU 表示：地块块 + 街道带（extrude>0 时挤压成体块）
p.setFloat("extrude", 6.0);
local meshResult = gen.buildMesh("mesh.urban", p);
if (!meshResult.ok) throw meshResult.status.summary;
local mesh = meshResult.value;
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
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(31415);
p.setString("style", "lowpoly");           // lowpoly | realistic
p.setString("branchAlgorithm", "weberPenn");
p.setString("leafMode", "canopy");        // cards | canopy | none
p.setFloat("leafDensity", 0.75);
p.setFloat("height", 6.0);
p.setFloat("crownRadius", 2.0);

local treeResult = gen.buildMesh("mesh.tree", p);
if (!treeResult.ok) throw treeResult.status.summary;
local tree = treeResult.value;
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
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setInt("segments", 8);      // 重复单元数
p.setFloat("segLength", 2.0); // 单元段长度
p.setFloat("height", 1.5);    // 高度覆盖
p.setFloat("uvRepeat", 2.0);  // 每世界单位的纹理重复次数
local meshResult = gen.buildMesh("mesh.stonewall", p); // 或 mesh.fence / mesh.bridge
if (!meshResult.ok) throw meshResult.status.summary;
local mesh = meshResult.value; // mesh.greatwall / mesh.hedge / mesh.chevaldefrise
```

共享参数：`segments`、`segLength`、`height`、`depth`、`thickness`、`scale`、`uvRepeat`。

### 生成无缝材质

设置 texture recipe、尺寸、octaves、pixelSize 和 seamless，调用纹理或法线图生成接口；开发期切换 seed 预览，发布时缓存 Texture，不能每帧重新生成。

### 生成多层城堡

`mesh.castle` 生成可复现的完整城堡网格：同心多层城墙、转角与区间塔楼、带真实门洞的城门楼、墙顶步道、中央多层主堡，以及通往每圈墙顶和每层主堡的实体楼梯。

```squirrel
local paramsResult = gen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(20260826);
p.setFloat("width", 48.0);
p.setFloat("depth", 40.0);
p.setInt("rings", 2);          // 1..4 层同心城墙
p.setInt("keepFloors", 4);     // 1..8 层主堡
p.setInt("detail", 2);         // 0 主体，1 垛口，2 区间塔楼与庭院建筑
p.setFloat("towerSpacing", 16.0);
p.setFloat("stairWidth", 2.0);
local castleResult = gen.buildMesh("mesh.castle", p);
if (!castleResult.ok) throw castleResult.status.summary;
local castle = castleResult.value;
```

主要参数还包括 `wallHeight`、`wallThickness`、`ringInset`、`ringHeightStep`、`towerRadius`、`towerHeight`、`towerHeightStep`、`towerSides`、`gateWidth`、`keepWidth`、`keepDepth`、`floorHeight`、`courtyardBuildings`、`stepHeight`、`merlonWidth`、`uvRepeat` 和 `scale`。CPU `buildMesh()` 的元数据提供 `rings`、`wallSections`、`towerCount`、`stairFlights`、`keepFloors`、`courtyardBuildings`、`detail` 与 `seed`，可用于生成图调试、预算检查和自动化验证。

城堡还输出 `walls`、`battlements`、`towers`、`gatehouses`、`stairs`、`keep`、`courtyard` 命名三角形组。可用 `copyGroup()` 提取组件、`appendTransformed()` 组合多个生成结果；需要上传时由 C++ render bridge 调用 `uploadMeshBorrowed()`，因此墙体、楼梯和塔楼可以使用不同材质、碰撞或 LOD 策略，而无需重新实现 recipe。

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

- `abort()`、`abortSystem()`、`add()`、`addObject()`、`addObjectAt()`、`analyzeTerrain()`、`analyzeTerrainScaled()`、`appendTransformed()`、`applyToLayer()`、`autotileGrid()`、`bakeTerrainAsset()`、`beginSystem()`、`buildMesh()`、`buildTerrainChunk()`、`clear()`、`clearObjects()`、`commitSystem()`、`copyGroup()`、`createTerrainMaterialShader()`、`createTerrainWaterShader()`、`deriveSeed()`、`empty()`、`erodeTerrainFluvial()`、`erodeTerrainFluvialAdvanced()`、`erodeTerrainFluvialDetailed()`、`erodeTerrainFluvialScaled()`、`erodeTerrainHydraulic()`、`erodeTerrainThermal()`、`excludeRadius()`、`fail()`、`fill()`、`filterDensity()`、`filterHeight()`、`generate()`、`generateHeightmap()`、`generateImage()`、`generateNormalImage()`
- `generatePbrMaterial()`、`generateTerrainAlbedoMap()`、`generateTerrainChunkMesh()`、`generateTerrainDepositionMap()`、`generateTerrainErosionMap()`、`generateTerrainLakeMesh()`、`generateTerrainRiverMesh()`、`generateTerrainRiverMeshAdvanced()`、`generateTerrainSplatMap()`、`generateTerrainWearMap()`、`generateTexture()`、`generateTo()`、`getAlgorithmCount()`、`getAlgorithmId()`、`getCell()`、`getDetail()`、`getFloat()`、`getHeight()`、`getInt()`
- 地形层与网格查询：`getBaseVertexCount()`、`getFlowDirection()`、`getFlowVectorX()`、`getFlowVectorY()`、`getGeometricError()`、`getLodStep()`、`getOriginX()`、`getOriginY()`、`getSplatHeight()`、`getSplatWidth()`、`getStreamOrder()`、`selectTerrainLod()`。
- `getGroupCount()`、`getGroupName()`、`getLayer()`、`getMeshRecipeCount()`、`getMeshRecipeId()`、`getMeshRecipeSchema()`、`getMeta()`、`getName()`、`getObjectCount()`、`getObjectGid()`、`getObjectHeight()`、`getObjectName()`、`getObjectType()`
- `getObjectWidth()`、`getObjectX()`、`getObjectY()`、`getPalette()`、`getPaletteGid()`、`getPath()`、`getSeed()`、`getString()`
- `getTarget()`、`getTextureRecipeCount()`、`getTextureRecipeId()`、`getWidth()`、`gridToJson()`、`has()`、`hasAlgorithm()`、`hasMeshRecipe()`、`hasTextureRecipe()`、`applyMeshRecipeDefaults()`
- `getDensity()`、`getError()`、`getFloatAttribute()`、`getNormalX()`、`getNormalY()`、`getNormalZ()`、`getOutput()`、`getOutputCount()`、`getOutputName()`、`getPointSeed()`、`getScaleX()`、`getScaleY()`、`getScaleZ()`、`getStringAttribute()`、`getSystemDebugReport()`、`getSystemOutput()`、`getSystemOutputCount()`、`getSystemOutputName()`、`getSystemRevision()`、`getSystemSeed()`、`getTraceCount()`、`getTraceInputCount()`、`getTraceMilliseconds()`、`getTraceName()`、`getTraceOutputCount()`、`getTriangleGroup()`、`getX()`、`getY()`、`getYaw()`、`getZ()`、`hasFailed()`、`hasFloatAttribute()`、`hasOutput()`、`hasStringAttribute()`、`hasSystem()`、`isActive()`、`jitterPoints()`、`newGrid()`、`newOutput()`、`newParams()`、`newPointSet()`、`publish()`、`randomSeed()`、`removeSystem()`、`resize()`、`sampleGrid()`、`seedFor()`、`selfPrune()`、`setCell()`、`setDensity()`、`setDetail()`、`setFloat()`、`setFloatAttribute()`、`setInt()`
- `setLayer()`、`setMeta()`、`setNormal()`、`setPalette()`、`setPaletteGid()`、`setPath()`、`setPointSeed()`、`setPosition()`、`setScale()`、`setSeed()`、`setSize()`、`setString()`、`setStringAttribute()`、`setYaw()`、`trace()`、`setActiveGroup()`
- L-system 引擎(`ProcgenLSystem`)：`addRule()`、`addRules()`、`clearRules()`、`derive()`、`getIterations()`、`getSeed()`、`setAngle()`、`setAxiom()`、`setBranchRadius()`、`setBranchRadiusFalloff()`、`setInitialHeading()`、`setIterations()`、`setLeafSize()`、`setLeafSymbols()`、`setStep()`、`setTropism()`；蓝噪声撒点：`poissonDisk()`。这些创建与转换入口返回统一 Result，其 `value` 是带所有权的代理。
- `setTarget()`
- Handle 生命周期：`ownership()`、`ownerEpoch()`、`isStale()`、`release()`；这些接口用于检查资源所属模块、拒绝过期引用并显式释放模块持有对象。
- UE PCG 扩展：`clearCache()`、`clearGenerationSources()`、`clearParameterOverride()`、`disconnect()`、`exposeParameter()`、`getActiveCellCount()`、`getAssetCount()`、`getAssetName()`、`getCacheHitCount()`、`getCellRevision()`、`getComputeBufferReuseCount()`、`getComputeDispatchCount()`、`getComputeFallbackReason()`、`getComputeMinimumPoints()`、`getComputePeakBufferBytes()`、`getComputePolicy()`、`getComputeReadbackCount()`、`getComputeUploadCount()`、`getDirectionWeight()`、`getExclusionCount()`、`getExecutionCount()`、`getExecutionNodeBudget()`、`getFailedCellCount()`、`getFrameTimeBudget()`、`getFrustumBehindRadius()`、`getFrustumHalfAngle()`、`getGeneratingCount()`、`getGenerationSourceCount()`、`getGenerationSourceId()`、`getInputNode()`、`getLastFusedTransformCount()`、`getLayerCount()`、`getLayerDensity()`、`getLayerName()`、`getLayerPriority()`、`getLevelCellSize()`、`getLevelCleanupRadius()`、`getLevelCount()`、`getLevelGenerationRadius()`、`getMaxActiveCells()`、`getMaxGenerating()`、`getMaxGenerationRetries()`、`getMaxY()`、`getMetricBackend()`、`getMetricCount()`、`getMetricMilliseconds()`、`getMetricNodeId()`、`getMetricOutputCount()`、`getMinY()`、`getModuleCount()`、`getModuleSymbol()`、`getNodeCount()`、`getNodeId()`、`getNodeOperation()`、`getOperationInputCount()`、`getOperationParamCount()`、`getOperationParamDefault()`、`getOperationParamKey()`、`getOperationParamKind()`、`getParameterCount()`、`getParameterFloat()`、`getParameterInt()`、`getParameterKind()`、`getParameterName()`、`getParameterString()`、`getPendingCleanupCount()`、`getPendingGenerateCount()`、`getRevision()`、`getTicket()`、`getVariantAsset()`、`getVariantCount()`、`getVariantLength()`、`hasCell()`、`hasLayer()`、`hasModule()`、`hasNode()`、`hasParameterOverride()`、`intersectSpatial()`、`isFrustumCullingEnabled()`、`isMetricCacheHit()`、`pointData()`、`refreshGenerationSources()`、`removeLayer()`、`removeModule()`、`removeNode()`、`requestCancel()`、`resetCancellation()`、`retryFailedCells()`、`setComputeMinimumPoints()`、`setComputePolicy()`、`setExecutionNodeBudget()`、`setMaxActiveCells()`、`setMaxGenerationRetries()`、`setNodeString()`、`setParameterFloat()`、`setParameterInt()`、`setParameterString()`、`wasCancelled()`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/procgen/`](../../../src/modules/procgen/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `procgen`；跨模块 hex 关卡见 [`test/hex_level_simulation.cpp`](../../../test/hex_level_simulation.cpp)、[`test/hex_level_data.cpp`](../../../test/hex_level_data.cpp)、[`examples/hex-levels/`](../../../examples/hex-levels/)。
