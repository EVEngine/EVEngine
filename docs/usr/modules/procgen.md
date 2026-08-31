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

### 脚本基础 API 索引

`PointSet` 的生产语义包括局部包围盒 `getBoundsMinX`、`getBoundsMinY`、
`getBoundsMinZ`、`getBoundsMaxX`、`getBoundsMaxY`、`getBoundsMaxZ`，线性色彩
`getColorR`、`getColorG`、`getColorB`、`getColorA`，以及归一化坡度
`getSteepness`。

每个新采样点都有稳定的非零身份。脚本使用 `getPointId(index)` 读取十进制字符串，
避免 64 位身份在 Squirrel 数值转换中丢失精度。旧缓存或手工构造的点可能返回 `"0"`；
可调用 `assignPointIds(namespace)`，其中 `namespace` 是非零十进制字符串，为缺失身份的点
确定性补齐 ID。已有重复 ID 时操作会失败且不会部分修改集合。过滤和变换保留身份，
`copyPoints` 为每个 source/target 组合派生新的稳定身份。

点元数据支持 `setIntAttribute`、`getIntAttribute`、`hasIntAttribute`，
`setBoolAttribute`、`getBoolAttribute`、`hasBoolAttribute`，以及
`setVectorAttribute`、`getVectorAttributeX`、`getVectorAttributeY`、
`getVectorAttributeZ`、`hasVectorAttribute`；`getAttributeType` 返回
`float`、`int`、`bool`、`vector`、`string` 或空字符串。

点集合组合使用 `unionPoints`、`intersectPoints`、`differencePoints`；
`copyPoints` 按 target-major 顺序复制源点，`transformPoints3D` 应用完整
pitch/yaw/roll、平移和非均匀缩放。`remapDensity` 重映射密度，
`mathFloatAttribute` 对浮点元数据执行受检的标量运算。

运行时 cell 热重载使用 `applyCellUpdate(level, x, z, revision, points)`；`revision`
以非零十进制字符串传入，返回的 Result `value` 也是字符串。提交成功后可通过
`getCellDelta()` 获取 `ProcgenPointDelta`，并用 `getAdded()`、`getUpdated()`、
`getAddedCount()`、`getUpdatedCount()`、`getRemovedCount()`、`getRemovedId()`、
`getTargetCount()`、`getTargetId()`、`getBaseFingerprint()` 和
`getTargetFingerprint()` 驱动局部场景更新。旧 cell 可先调用
`migrateCellPointIds()`；过期 revision、重复/缺失 ID、schema 冲突或点预算超限均返回
失败 Result，且不会修改 cell 快照或递增 revision。

首次把 cell 发布到 Scene 时调用
`publishCellInstances(prefix, request, points, assetAttribute, defaultAsset)`；Scene batch 从 revision 1
开始。后续 `applyCellUpdate` 成功并取得 `getCellDelta()` 后，调用
`publishCellInstanceDelta(prefix, request, delta, targetRevision, assetAttribute, defaultAsset)`，其中
`targetRevision` 使用 `applyCellUpdate` 返回的十进制字符串。该调用按 PointId 原子处理新增、更新、
删除和精确顺序；即使 `instanceId` 属性发生改名，也仍以 PointId 找到原实例。Scene revision
过期、PointId 缺失或重复、目标顺序不完整、实例 ID 冲突时返回失败 Result，原 Scene batch
保持不变。增量路径要求所有参与点具有非零稳定 ID；旧数据应先完成 `migrateCellPointIds()`。

Scene 漏掉一个或多个中间 revision 后，不应继续重放不完整的最后一个 delta。改用
`publishCellSnapshot(prefix, request, points, targetRevision, assetAttribute, defaultAsset)` 将
`getCellOutput()` 返回的完整有序快照原子发布到明确的 RuntimeGeneration revision。该入口允许
从较旧 Scene revision 直接前进到较新的 cell revision，同时拒绝相同或更旧 revision；因此可用于
provider 暂时失败后的追赶、存档恢复和 Scene 重建，而不会通过反复整批发布猜测 revision。

常规 streaming 循环优先调用
`synchronizeCellInstances(prefix, runtime, request, assetAttribute, defaultAsset)`。它把
`RuntimeGeneration` 视为 authoritative owner：Scene revision 已相同时幂等成功，恰好落后一个且存在
最新 delta 时走增量，首次发布、漏掉多个 revision、迁移后没有 delta 时自动走完整快照；如果 Scene
revision 反而更高则返回冲突，不会用旧 RuntimeGeneration 状态覆盖新场景。返回的成功值是已同步的
十进制 revision，可用于日志和监控，不再要求脚本复制 revision 分支策略。

空间数据构造器包括 `polygonVolume`、`textureMaskData` 和 `meshSurfaceData`。
它们可继续传给统一的 spatial union/intersection/difference、采样、过滤和投射 API。
`projectToWorld` 通过可选 `IProcgenWorldQuery` capability 做垂直世界表面查询；
provider 缺失或执行失败返回失败 Result，正常未命中则遵循 `keepUnmatched`。

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

### 生成三维随机溶洞（`mesh.cave`）

`mesh.cave` 在实体体积中刻蚀一条保证连通的主通道，再加入分支与椭球洞室，最后
通过 Marching Cubes 提取朝向洞内的岩壁。全部随机性只来自显式 seed；相同参数在
CPU 后端得到 bit-exact 网格，适合存档复现、离线烘焙碰撞或按命名组 `caveWalls`
分配材质。

```squirrel
local paramsResult = procgen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(20260830);
p.setString("style", "mixed"); // cavern | tunnels | vertical | labyrinth | mixed
p.setString("genesis", "mixed"); // epigene | hypogene | mixed
p.setInt("resolution", 56);
p.setFloat("width", 32.0);
p.setFloat("height", 13.0);
p.setFloat("depth", 25.0);
p.setInt("chambers", 8);
p.setInt("branches", 5);
p.setFloat("tunnelRadius", 0.16);
p.setFloat("chamberScale", 1.05);
p.setFloat("roughness", 0.14);
p.setFloat("multiscaleRoughness", 0.88); // 带限的宏观/中观/细观洞壁起伏谱
p.setFloat("roughnessFlowCoupling", 0.72); // 凸起暴露增强、凹窝遮蔽抑制局部溶蚀传质
p.setFloat("surfaceSlopeReactivity", 0.74); // 由最终壁面法线离散度增强棱边与角点反应率
p.setFloat("reactivePatchiness", 0.68); // 用多尺度空间相关反应斑替代逐体素白噪声
p.setFloat("erosion", 0.72);             // 总溶蚀强度
p.setFloat("bedding", 0.68);             // 层理面凹槽与壁龛
p.setFloat("fractureDissolution", 0.62); // 节理裂隙导流溶蚀
p.setFloat("fractureApertureVariability", 0.76); // 空间相关的裂隙孔径非均质
p.setFloat("fractureStressControl", 0.82); // 应力诱发的溶蚀前缘分裂与分支通道
p.setFloat("fractureFlowFeedback", 0.74); // 孔径立方导流与反应物更新驱动的优势通道
p.setFloat("vadoseIncision", 0.48);      // 排水后溪流向下切割
p.setFloat("waterTableCorrosion", 0.72); // 地下水位附近的侧向腐蚀带
p.setFloat("waterTableLevel", 0.26);     // 最高历史水位（归一化高度）
p.setInt("waterTableStages", 3);         // 基准面下降留下的水位级数
p.setFloat("waterTableDrop", 0.19);      // 相邻历史水位的垂直间距
p.setFloat("waterTableFluctuation", 0.42); // 每级水位带厚度/波动范围
p.setFloat("scallopErosion", 0.72);      // 湍流边界层形成贝壳状溶蚀窝
p.setFloat("scallopScale", 0.11);        // 沿通道流向的无量纲波长
p.setFloat("scallopHydraulicScaling", 1.0); // 让高速段形成更短、更浅的 scallop
p.setFloat("scallopMaturity", 0.68);        // 细胞合并、宽凹窝与窄尖脊的成熟度
p.setFloat("scallopScaleVariability", 0.72); // 空间相关的近似对数正态波长分布
p.setFloat("scallopFlowSeparation", 0.78);   // 上游陡面与下游缓面的非对称行进波
p.setFloat("scallopFlowHistory", 0.68);      // 分区保存的基流/洪水多期 scallop 与局部流向反转
p.setFloat("bendUndercut", 0.72);          // 弯道外侧流动分离与反应物聚焦淘蚀
p.setFloat("fragmentDetachment", 1.0);     // 清除已完全脱离母岩的侵蚀残片
p.setFloat("curvatureDissolution", 0.72);  // 优先退蚀暴露凸部并圆化人工棱角
p.setFloat("reactiveSurfaceCoupling", 0.88); // 岩性可达表面积与水力暴露共同调制退蚀
p.setFloat("hydraulicErosion", 0.86);   // 流量—溶蚀反馈强度；0 保持旧行为
p.setFloat("mixingCorrosion", 0.76);    // 支洞—主洞汇流处的混合腐蚀扩腔
p.setFloat("lithologicHeterogeneity", 0.82); // 异质层组与缝合线选择性溶蚀
p.setFloat("floodAbrasion", 0.78);      // 含砂洪水在近床区形成冲槽和涡蚀坑
p.setFloat("sedimentLoad", 0.48);       // 搬运中的磨蚀工具量；过高时覆盖会保护床面
p.setFloat("floodPlucking", 0.66);      // 强流从裂隙预制区拔出块体
p.setFloat("pluckingBlockScale", 0.12); // 归一化块体尺度
p.setFloat("hydraulicGradient", 0.52);  // 水力坡降代理量
p.setFloat("recharge", 0.78);           // 补给/输运能力
p.setFloat("flowFocusing", 0.82);       // 优势通道溶蚀集中度
p.setFloat("damkohler", 0.0035);        // 有效 Da：均匀溶蚀/通道化/虫孔化分区
p.setFloat("transportG", 1.6);          // 横向扩散限制；高值保留更多细长通道
p.setFloat("microstructure", 0.78);     // 多尺度岩性非均质强度；0 保持旧行为
p.setFloat("microporosityAccess", 0.42); // 可接触微孔表面积；高值偏分布式退缩
p.setFloat("permeabilityContrast", 0.72); // 孔渗对比；高值强化局部优势通道
p.setFloat("condensationCorrosion", 0.72); // 潮湿、低流速顶板的凝结腐蚀浅蚀坑
p.setFloat("condensationFaceting", 0.62); // 局部对流凝结/蒸发形成的平面腐蚀面
p.setFloat("differentialVeinErosion", 0.58); // 抗蚀矿脉周围母岩退缩形成 boxwork 凸脊
p.setFloat("breakdown", 0.68); // 溶蚀削弱后的顶板剥落与配对崩积块
p.setInt("breakdownEvents", 5); // 确定性剥落事件数量
p.setFloat("sedimentDeposition", 0.64); // 流向控制的洞底沉积坝与叠瓦砾石
p.setFloat("paragenesis", 0.70); // 沉积充填约束下的向上顶板溶蚀槽
p.setInt("sedimentBars", 5); // 低起伏纵向沉积坝数量
p.setFloat("biogenicCorrosion", 0.68); // 有鸟蝠粪、湿壁和气流时的硝化生物腐蚀
p.setFloat("mineralArmoring", 0.52); // 次生矿物覆盖抑制化学退缩，强水流可部分清除
p.setInt("fractureCount", 7);            // 高级参数：节理面数量
p.setInt("cupolas", 7);                  // 上升流顶板穹穴
p.setInt("feeders", 4);                  // 深部补给管及相连顶板半管
p.setInt("dripstones", 16);              // 滴水点对数
p.setFloat("dripstoneScale", 0.75);      // 沉积体长度尺度
p.setString("stalagmiteShape", "mixed"); // conical | columnar | flatTop | mixed
p.setFloat("normalSmoothing", 0.86);     // 0 保留三角面，1 完全共享顶点法线
p.setString("surfaceNormalMode", "densityGradient"); // 以连续密度场梯度重建法线
p.setInt("wetnessRefinement", 1);        // 沿连续排水湿润场零线细分材质边界
p.setFloat("boundaryClosure", 1.0);      // 用起伏宿岩包络封闭有限采样域
p.setInt("isosurfaceSampling", 2);       // 以两倍采样重建连续等值面轮廓
p.setInt("surfaceRefinement", 2);        // 曲率误差驱动的共形自适应细分
p.setFloat("refinementThreshold", 0.0015); // 越小细分越密
p.setInt("flowstones", 9);               // 贴壁流石薄层
p.setInt("curtains", 7);                 // 波状洞帘
p.setFloat("flowstoneScale", 0.85);      // 二次沉积整体尺度
local caveResult = procgen.buildMesh("mesh.cave", p);
if (!caveResult.ok) throw caveResult.status.summary;
local cave = caveResult.value;
```

`resolution` 是三个轴的默认采样精度，也可用高级参数 `nx/ny/nz` 分别覆盖；每轴
限制为 `8..128`。世界尺寸只缩放最终网格，不改变洞穴拓扑。`cavern` 强调宽阔洞室，
`tunnels` 强调狭长通道，`vertical` 增加竖井高差，`labyrinth` 增加水平曲折，
`mixed` 适合作为通用默认值。

`multiscaleRoughness>0` 把旧的单频洞壁噪声渐变为三个带限空间频段：大尺度起伏控制
整体壁面轮廓，中尺度起伏表达成片溶蚀差异，细尺度只保留当前体素分辨率能够稳定重建的
部分。频段幅度随频率衰减，并降低垂直频率以反映层状碳酸盐岩的各向异性；它不会把
晶体微米地形直接放大为米级尖刺。Zhou 与 Fischer（2025，DOI
`10.1021/acsearthspacechem.5c00161`）对 80 帧连续方解石表面地形的 PSD 分析说明，局部
微地形及空间频率高度非均质，而总体溶蚀仍保持有界；Racine 等（2025，DOI
`10.5194/essd-17-4671-2025`）发布的 16 个洞穴 LiDAR 数据则同时提供 2 mm 与 5 cm 点云、
网格及洞底/顶板粗糙度栅格，强调真实洞道几何的跨尺度性质。当前实现只采用这些统计约束，
不把三个频段解释成特定绝对岩石粒径或年代。默认值为 `0`，保持旧单频密度场逐顶点一致；
`wallRoughnessSpectrum`、`minimumWallRelief` 和 `maximumWallRelief` 公开实际生成状态。

`roughnessFlowCoupling>0` 进一步把上述有符号壁面起伏反馈到化学退缩：凸出的脊面更直接
接触持续补给的欠饱和水，局部传质率提高；凹入窝穴代表回流、低速或停滞区，传质率降低，
强水力暴露只会部分冲洗这种遮蔽。乘数严格限制在 `0.60..1.45`，只作用于化学溶蚀，不会
放大洪水磨蚀、拔蚀或石块冲刷。2026 年粗糙溶蚀裂隙研究（DOI
`10.1016/j.rineng.2026.110345`）报告粗糙壁面中的回流、低速区和通道化；2026 年碳酸盐岩
物理非均质模拟（DOI `10.3390/min16010110`）则显式分析粗糙度对局部 Darcy 速度、反应率和
有效溶蚀率的控制。这里采用的是有界亚网格代理，不是 CFD、边界层或浓度场求解。默认值为
`0`，保持旧密度场逐体素一致；元数据公开实际状态、影响体素数、最小/最大传质乘数以及最大
凸脊暴露和凹窝遮蔽。

`surfaceSlopeReactivity>0` 在初始洞腔和化学退缩完成后，从最终密度场重建局部表面法线，
用相邻法线的旋转不变离散度识别实际存在的棱边、角点和粗糙斜坡。离散度高且反应流体
可达的部位发生额外退缩；平整表面不受影响。每轮退缩后重新计算法线，因此几何变化会反馈
到下一轮反应，而不是只读取生成前的噪声标签。InterPore 2026 的粗糙度反应率参数化研究
提出用局部点云协方差的最小特征值构造旋转不变 `Rq`，并在二维粗糙通道、三维大理石表面
和方解石晶体上区分高反应性棱角与低反应性平面。当前实现采用适合体素 SDF 的法线离散度
近似，不等同于论文的协方差特征值，也不解析微米级晶体缺陷。默认值为 `0`；影响体素数、
最大退缩、总退缩和最大法线离散度均写入元数据。

`reactivePatchiness>0` 为最终壁面引入确定性的双频空间相关反应率场，使相邻表面点共享
连续的溶蚀前缘，而不是各体素独立闪烁。反应场复用最近洞道水文帧的归一化切向量：沿流向
压缩频谱坐标，使主要蚀坑和残留台面的相关长度大于横向，并在弯曲洞道与支路处随局部流向
旋转。较小尺度只调制边缘；两轮退缩均读取当前零等值面，因此优先斑块会形成受控的扩宽
正反馈。Zhou 与
Fischer（2025，DOI `10.1021/acsearthspacechem.5c00161`）对 80 期方解石表面地形的功率谱
密度分析表明，反应输运参数化既需要局部反应率范围，也需要表面空间频率的统计分布。
Ma 等（2026，DOI `10.1016/j.advwatres.2025.105202`）的时序微 CT 与孔隙尺度模拟进一步
观察到孔隙结构和流体可达性控制溶蚀图样，并形成通道扩宽的正反馈。这里用两个带限值噪声
频带近似这种统计相关性，不是时变浓度、两相流或晶体台阶动力学求解；其频率代表洞穴网格
可解析的上尺度形态，不能解释为微米尺度实测波长。默认 `0` 保持旧几何逐体素一致；元数据
公开斑块速率范围、沿流/横向相干度、通道各向异性、影响体素数和退缩量。Hyman 等（2026，
DOI `10.1029/2025JB033004`）的三维裂隙网络模型进一步给出均匀、通道化和虫洞化三种溶蚀
状态，并显示主通道会在低传质限制下继续扩宽；本实现只借用其方向性和正反馈约束，不求解
Reynolds 流、粒子轨迹或突破曲线。

`waterTableCorrosion>0` 在 `epigene` 与 `mixed` 成因中生成地下水位附近的侧壁浅层腐蚀带。
每一级以 `waterTableLevel - stage * waterTableDrop` 排列，较老的低位带逐级减弱；沿洞道的
长波小幅摆动避免把整座洞穴切成数学平面，环向侧壁遮罩则保护洞顶与洞底。Gabrovšek 等
（2025，DOI `10.5194/hess-29-6685-2025`）的分布式补给离散裂隙网络模型显示，最高溶蚀率
形成于地下水位附近；随着水位下降，该高溶蚀带向下迁移并提高经过区域的水力传导率。
Acqua Fitusa 洞穴研究记录的不同高度平顶硫酸腐蚀 notch，则把多级侧向腐蚀与历史基准面
下降直接联系起来。实现只表达这种水文—形态关系，不模拟地下水面求解、真实化学浓度、
硫化氢氧化或地质年代。纯 `hypogene` 成因下状态为 `inactive-hypogene`，密度场完全不变。
该机制与 `paragenesis` 分开拥有事实来源：前者由水位附近侵蚀控制，后者必须由实际沉积坝
遮蔽洞底后才会发生。默认值为 `0`；影响体素数和最大退缩通过元数据公开。

`genesis` 控制水文成因，并与外观 `style` 正交：`epigene` 保留地表补给、潜水通道和
包气带溪流下切；`hypogene` 关闭溪流下切，从深部 `feeders` 经上升壁槽/顶板半管连接
到 `cupolas`，形成受隔层控制的上升流形态套系；`mixed` 表示多期叠加，同时保留两类
特征。该套系依据 Roded 等（2024）的 confined-cooling-flow 案例与模型（DOI
`10.5038/1827-806X.53.2.2505`）：富 CO2 热液上升并冷却后增强碳酸盐溶蚀，能够形成
远离补给口的大洞室和复杂迷宫。`epigene` 仍是默认值，因此既有调用不会改变拓扑。

`surfaceRefinement=1` 将每个 Marching Cubes 三角形统一细分为四个；模式 `2` 比较
线性边中点和投影后等值面中点的距离，只拆分超过 `refinementThreshold` 的高曲率边。
相邻三角形通过量化共享边键复用同一决定，避免不同细分层级产生裂缝。两种模式都用
原始密度场的三线性采样与 Newton 投影把新增顶点拉回零等值面，不重新计算更高分辨率
体素场；模式 `2` 对三条边的八种拆分组合使用显式共形模板，避免 fan 三角化在非共面
边界产生长对角折痕。因此密度内存保持不变。默认值为 `0`，以保持既有调用的网格规模
和拓扑。

`isosurfaceSampling=2` 在侵蚀、沉积和碎块脱落全部结束后，把最终密度场按每轴
`(n-1)*2+1` 三线性重采样，再执行 Marching Cubes。所有原始体素样本都会在偶数格点原样
保留，新样本近似同一连续三线性场，因此比仅拆分已有三角形更能减少零等值面轮廓锯齿；
后续 Newton 投影也改用该高采样场。该方法会让体素内存最多增至约 8 倍、三角形数量通常
增至约 4 倍，所以只提供 `1|2`，默认 `1` 保持原性能和 bit-exact 输出。设计参考 Wang 等
（2025，arXiv `2506.09579`）以网格—连续 SDF 偏差驱动增采样的自适应提取思路，以及 Stahl
与 Grosso（2025，DOI `10.5220/0013309800003912`）对三线性插值面、单元歧义和拓扑正确性
的分析。当前实现提高几何采样精度，但仍使用经典查表 Marching Cubes，不宣称解决论文中的
全部鞍点和奇异拓扑案例。

侵蚀模型采用适合实时程序生成的现象学近似：只在连通洞腔表面附近扩大岩体，
`bedding` 形成受层理控制的水平溶蚀带，`fractureDissolution` 沿随机节理面形成高窄
槽和分叉，细尺度正向噪声形成溶孔；`scallopErosion` 沿主通道流向生成交错的圆弧凹窝，
并用非对称相位近似湍流剪切峰值向上游偏移；`scallopScale` 控制基准波长。
`scallopHydraulicScaling>0` 将相邻水力段先转换成节点强度并沿通道连续插值，再按
`1/sqrt(flowWeight)` 缩放局部波长和凹窝深度：高流量区形成密集、较浅的小 scallop，
低流量支洞形成更长、更深的凹窝，且不会在汇流段产生频率硬接缝。关闭水力侵蚀时全部
权重为 1，因此该参数不会改变网格；其默认值为 `0`，保持既有输出。
`scallopMaturity>0` 表示持续法向退蚀后的形态年龄：细尺度胞元逐渐减弱，带固定相位关系的
较大胞元接管壁面，凹窝展宽并合并，而余弦胞元边界的幂指数降低，使分隔凹窝的脊线变窄。
这近似 Chaigne 等（2023，DOI `10.1073/pnas.2310206120`）实验与几何模型观察到的胞元
粗化、连通尖脊网络及高波数 `k^-4` 特征；Fowler（2025）则为成熟尖脊所需的非线性
流动分离提供了水动力解释。它不是时间积分器，参数只表示归一化成熟阶段；默认值为 `0`，
此时严格使用原单尺度波形。
`scallopScaleVariability>0` 不再令整条洞道使用单一波长，而以洞道距离和环向角生成低频、
空间相关的指数尺度场；其几何标准差由 `scallopGeometricStdDev` 公开。这个分布是对
Springer 与 Hall（2020，DOI `10.5038/1827-806X.49.1.2292`）100 个实测 scallop 呈
对数正态分布结论的形态代理，不用于反演精确流量。`scallopFlowSeparation>0` 对流向相位
施加非线性偏斜，使上游侧更陡、下游侧更缓，并随成熟度强化尖脊；它对应 Fowler（2025，
DOI `10.1098/rspa.2025.0033`）模型中湍流边界层剪切峰值上游偏移、坡度依赖传质与流动
分离产生行进波及 cusp 的机制。两项默认均为 `0`，显式设为 `0` 与省略参数逐顶点一致；
它们是受约束的统计/形态近似，不宣称求解瞬态湍流。
`scallopFlowHistory>0` 在旧 scallop 之上加入第二期、空间受限的流水退缩。低频洞段窗口和
环向水位带共同决定后期形态保存位置；较高水力对应更短的第二期波长，只有连续洞段尺度的
掩码才允许相位/不对称方向反转，用于表达排水捕获或回水洪泛。第二期只继续移除岩石，绝不
把第一期凹窝“长回”，因此壁面保留可读的交切层序。2025 National Cave and Karst Management
Symposium 的 Black Canyon 现场研究在同一洞段测得高流与基流、上下层位的多个 scallop
速度群，并将其与层位滞水和排水方向迁移联系；该资料目前是会议研究，证据等级不同于正式
期刊论文。Beus 等（2025，Scientific Reports，DOI `10.1038/s41598-025-17472-6`）以超过
10 km、厘米级洞穴 LiDAR 证明跨洞段的节理组和层理倾向持续组织地下水路径，为采用连续
洞段而非逐体素反转提供结构约束。更早的 Agen Allwedd 研究则直接以 scallop 记录确认冰期
洪泛、蓄水和流向反转的现场先例。当前参数不表示洪水次数、年代或真实流量过程；默认 `0`
逐顶点保持旧输出，后期影响体素、覆盖率、反转掩码和尺度比均通过元数据公开。
`fractureApertureVariability>0` 把原本沿整张节理面恒定的孔径替换为空间相关的指数场，
让开放斑块在米级尺度连续扩缩；`fractureApertureGeometricStdDev` 公开其几何离散程度。
`fractureStressControl>0` 进一步把均匀溶蚀前缘限制在随纵向推进而分裂、弯曲并重新汇合的
开放窗口内，使节理槽从整齐直切面变为多条竞争性虫孔/分支通道。该约束来自 Jiang 等
（2025，DOI `10.1029/2024JB029901`）的场尺度耦合水力—力学—化学模型：应力重分布会
加剧溶蚀前缘不稳定性，使均匀溶蚀转向前缘分裂、持久分支和虫孔化；孔径与局部刚度的
演化决定接触斑块何时转为开放斑块。当前实现是这种拓扑趋势的确定性形态代理，不求解
真实应力张量、接触力学、反应输运或年代。两项默认均为 `0`，省略参数与显式零值逐顶点
一致，并分别通过 `fractureApertureDistribution` 和 `fractureDissolutionFront` 元数据
显式区分。
`fractureFlowFeedback>0` 把上述孔径非均质从被动纹理提升为导流—溶蚀正反馈。实现采用
平行板裂隙的孔径立方导流近似，只计算相对于均匀孔径的额外流量集中；优势孔径沿既有
水力场获得更强退缩，两组开放裂隙重叠处因多一个对流补给面形成有界交汇溶蚀窗。同时
复用 `CaveHydrologyWeights::reactantPenetration`，让较高有效 Damköhler 数下的反应物供应
随入口距离衰减，避免整张裂隙面等强扩宽。Xu 等（2025，DOI
`10.3389/feart.2025.1701477`）的动态石灰岩裂隙实验观察到“孔径扩大—流量上升—溶蚀增强”
的非线性反馈；Aliouache 与 Jourde（2025，DOI `10.1016/j.jhydrol.2024.131684`）的离散
裂隙反应运移模型进一步表明连通性、交汇类型、各向异性和水力边界共同决定初生岩溶
优势通道。当前实现只复现这些因果关系的介观形态，不求解瞬态压力、应力闭合或 Ca²⁺
输运。该参数默认 `0`；没有孔径对比时状态为 `inactive-no-aperture-contrast`，保持密度场
不变，并通过影响体素、最大退缩、最大流量集中、交汇增幅和最小反应物可达性元数据公开。
`fragmentDetachment>0` 在化学侵蚀和碳酸盐沉积完成后，从体素域六个外边界出发，用
26 邻域标记仍与母岩相连的固体。只有完全失去母岩连接的组件才继续退蚀；与顶板、地板
或洞壁保持哪怕斜向连接的石柱、钟乳石和残丘都会保留。强度 `1` 完全移除悬浮残片，
较小值只让其继续收缩。该步骤近似溶蚀暴露颗粒后由水流剪切触发的机械脱落，不把一般
凸面一概删除。依据 real-rock microfluidic 实验（2025）观察到的流动控制化学溶蚀—颗粒
脱落转变，以及 Noiriel 等（2023，DOI `10.3389/frwa.2023.1185608`）4D X-ray 结果：
凸角/高曲率区退蚀更快，非反应矿物暴露后逐渐脱落并使粗糙度趋稳。默认值为 `0`，保持
既有网格 bit-exact。
`curvatureDissolution>0` 在洞腔主体形成后、钟乳石等二次沉积出现前，对零等值面附近计算
各向异性体素间距下的梯度、Hessian 与平均曲率速度。它只沿法向退蚀面向洞腔的暴露凸部，
让钻孔状尖角、方块状突出和高反应曲率区逐步圆化；受遮蔽的凹部不进行反向平滑，因此不会
抹掉已经由定向水流形成的 scallop 尖脊网络。该近似参考 Rodrigues 等（2024，DOI
`10.1016/j.gca.2024.05.028`）观察到矿物孔壁在饱和度梯度下由表面台阶传播控制、初始尖角
迅速接近圆形的结果，以及 Briolet 等（2025，DOI `10.1016/j.gca.2025.03.019`）证明岩石
微结构决定局部化或分布式碳酸盐溶蚀的对比实验。演化采用两次 Jacobi 更新并记录受影响
体素数和最大单步退缩；默认值为 `0`，所以原有配方仍保持 bit-exact。
`reactiveSurfaceCoupling>0` 复用同一微结构样本，把可接触反应表面积、局部渗透率和通道
水力强度组合成 `0.25..2.5` 的有界速率乘子，再调制曲率法向退缩。它不会新增无方向噪声：
同一水文网络中，高可达且持续得到新鲜欠饱和水的表面退缩更快，低渗或输运受阻区保留更久。
范围有意远小于真实纳米尺度局部速率的 2–3 个数量级，避免把不可解析的晶体台阶错误放大成
米级尖刺。该设计参考 Zhou 与 Fischer（2025，DOI
`10.1021/acsearthspacechem.5c00161`）对 80 帧方解石表面地形的 PSD/速率谱分析，以及 Ma 等
（2026，DOI `10.1016/j.advwatres.2025.105202`）关于孔隙结构、流体分布和反应表面可达性
共同控制碳酸盐溶蚀的时序 micro-CT 研究。默认值为 `0`，保持均匀曲率速率。
`condensationCorrosion>0` 是独立于主水流 scallop 的晚期微气候改造阶段。它从密度梯度
识别朝上的顶板/上壁，仅在零等值面浅壳层内作用，并用局部水力暴露抑制持续冲刷的通道；
带 seed 的低频湿度斑块和高频蚀坑载波形成不均匀、浅而有界的凹蚀。该设计依据 Šebela 等
（2024，DOI `10.1007/s12665-024-11449-w`）对冷岩面凝结、相对湿度、壁面温差与富 CO2
欠饱和水侵蚀性的总结，以及 Domínguez-Villar 等（2021，DOI
`10.1016/j.ringeo.2021.100008`）观察到的表面微蚀坑和约 50 微米影响层。实现将长期累计的
微米过程放大为当前网格可解析的形态代理，最大单次归一化退缩限制为 `0.032`；它不是逐微米
水膜、热传导或 CO2 扩散模拟。默认值为 `0`，因此既有配方 bit-exact 不变，并通过
`condensationAffectedVoxels`、`maximumCondensationRetreat` 等元数据显式记录影响。
`condensationFaceting>0` 在通道局部坐标中构造 5--7 个平面支撑包络，再以沿程缓变的
湿度/对流斑块仅混合到部分侧壁和顶板浅壳层，使圆滑断面出现成组的真实平面腐蚀面，
而不是把整条洞道硬切成规则多边形。Audra 等在 Geomorphology 492（2026，article
110054）的凝结腐蚀综述将这类地貌明确归为 corrosion planes / facets，并报告一处壁面
约 15 cm 的退缩；实现只把该观察作为定性尺度依据，单次归一化退缩有界于 `0.055`，
不求解洞内热对流、相变或瞬态 CFD。默认值为 `0`，影响体素、最大退缩与平面数量通过
`facetAffectedVoxels`、`maximumFacetRetreat`、`condensationFacetCount` 元数据可观测。
`differentialVeinErosion>0` 生成两组轻微弯曲、相交的抗蚀矿脉，只退缩其周围的母岩，
使窄脉芯相对凸起为断续 boxwork，而不是向表面额外粘贴装饰几何。低频地质斑块限制它
只出现在局部壁面；单次母岩归一化退缩上限为 `0.04`。Audra 等（2026，DOI
`10.1016/j.geomorph.2025.110054`）在 Morgana Cave 记录了含 Mn/Fe 氧化物的深色矿脉，
其周围大理岩差异侵蚀后凸出数厘米，并同时观察到小裂隙优先溶解的 grooved surfaces。
本参数表示已经存在抗蚀脉体时的长期形态结果，不模拟矿物沉淀、氧化反应或岩石力学；
默认值为 `0`。`differentialVeinAffectedVoxels`、`maximumDifferentialVeinRetreat` 与
`maximumVeinProtection` 分别记录影响范围、最大母岩退缩和最大脉芯保护权重。
`breakdown>0` 把化学退缩后的结构失稳表示为成对事件：每次事件在选定洞室顶板切出一个
浅薄、带方位的椭圆剥落疤痕，并由同一尺寸来源在正下方洞底生成 2--4 个板状或近块状
崩积体。块体与洞底轻微咬合，所以既不会悬空，也不会被 `fragmentDetachment` 的宿岩
连通性清理误删；它们进入独立 `breakdown` 三角组。Konsolaki 等（2026，DOI
`10.1016/j.geomorph.2026.110280`）将高分辨率洞穴裂隙测绘与 3DEC 稳定性分析结合，说明
结构裂隙是潜在失稳机制的必要输入；溶蚀诱发剥落的 DDA 研究（2026，DOI
`10.3390/app16125900`）则显示水岩化学退化会降低断裂韧度和强度，推动裂纹萌生、贯通及
最终块体脱离。实现据此保证疤痕—落块因果配对，但不宣称求解应力、瞬态裂纹或真实落体
动力学。默认 `0`；事件数、块数及近似剥落/堆积体积通过元数据显式记录。
`sedimentDeposition>0` 将侵蚀产物的水力搬运结果并回洞穴形态：低起伏细料坝沿现有
主干水流切向延伸，坝面扁平砾石的长轴横跨流向，并以约 9--28 度向上游倾斜形成叠瓦。
Sevil-Aguareles 等（2025，DOI `10.1016/j.geomorph.2024.109576`）用 TLS、洞图与沉积
观测联合重建古流向，并证明壁面 notch 与河流砾石叠瓦给出一致方向；Miklavc 等（2025）
则把低起伏纵向坝、叠瓦和正粒序归入高能洞穴通道沉积相。2026 年砾石床表面研究（DOI
`10.1080/00221686.2025.2606942`）进一步量化了砾石长轴主要垂直于流向、粗化表面具有
更大倾角和粗糙度。实现只表达这些形态约束，不求解颗粒碰撞、瞬态洪水或粒径输运方程；
默认 `0`。独立 `sediment` 三角组及坝数、砾石数、体积、平均倾角元数据可用于材质和 QA。
`paragenesis>0` 只在上述沉积坝实际存在时产生反馈：沉积充填遮蔽洞底后，通道只能向上
扩展，于沉积坝上方沿同一古流向切出浅长的反重力顶板槽。Holzer 等（2025，DOI
`10.1002/dep2.70028`）在 Dachstein 洞穴沉积层序研究中将“底部沉积迫使通道剖面仅向上
扩展”明确归为 paragenesis；Sevil-Aguareles 等（2025，DOI
`10.1016/j.geomorph.2024.109576`）也把 antigravitative ceiling channels 用作古水流
方向指标。实现因此复用沉积坝的位置与流向，而不创建第二套随机方向场；关闭
`sedimentDeposition` 时状态为 `inactive-no-sediment`，密度场完全不变。该效果是层序约束的
形态代理。槽体使用带圆滑渐缩端部的连续半管，并在古流向上加入低幅度摆动，而不是单个
椭圆蚀坑。宽度以原通道半径作为流量代理，并遵循 Cooper 与 Covington（2020，DOI
`10.1002/esp.4915`）所得“约随流量平方根增宽、随沉积供给略微收窄”的平衡尺度关系；
沉积供给增加时，受保护洞底上方的抬升量则增加。实现不求解沉积充填历史、地下水位变化
或反应输运；默认 `0`。槽数、状态、平均宽度与最大顶板抬升量通过元数据可观测。
为了保留沉积充填—搬空的层序证据，实现还从当前残余坝推导历史最高填充面：供给较高时
古填充面更接近顶板，并在该固定高度切出窄而横向连续的侧壁 alluvial notch；槽与半管
之间未溶解的宿岩自然保留为 pendant，而不是额外添加石柱。Farrant 与 Smart（2011，DOI
`10.1016/j.geomorph.2011.06.006`）区分了沉积覆盖下包气带水流造成的侧向腐蚀/notch 与
潜水带向上溶蚀；Sevil-Aguareles 等（2025）则使用壁面 notch、顶板通道和砾石叠瓦的
组合证据重建古流向。2026 年 Sa Gleda 洞穴腐蚀速率研究（DOI
`10.3390/jmse14050469`）记录了固定高度、宽约 0.3--1 m、深约 0.5--3 m 且可沿洞道连续
超过 150 m 的腐蚀 notch，支持用低纵向变化、强横向连续性表达长期稳定界面。生成器只
采用这种形态关系；`meanPalaeofillRatio`、`maximumAlluvialNotchRetreat` 与
`meanAlluvialNotchThickness` 公开推导尺度，
不把归一化参数解释为真实年代或绝对侵蚀速率。
`biogenicCorrosion>0` 模拟含鸟蝠粪的暖湿洞穴中，氨气沿洞道输运、在潮湿洞壁微生物膜
中硝化并生成侵蚀性含氮酸的后成洞改造。它会有方向地削弱较老的小型流水 scallop，叠加
尺度更大的 `megascallop`、顺气流槽纹和局部蜂窝状退蚀；高水力暴露区通过水膜保护项保留
更多原始 scallop，因为快速水膜会带走氨和壁面微生物群落。实现参考 Farrant 等（2025，
DOI `10.1016/j.geomorph.2025.109822`）在 Mulu 洞穴记录的 1--2 米 megascallop、气流槽纹、
残脊/pendant、蜂窝状壁面，以及高位老通道中至少约 10 mm 的二次退蚀足以抹除原有流水
scallop 的观察。该参数只表示已满足粪源、暖湿微气候和通风条件后的归一化长期强度，不
模拟单只动物、真实氨浓度或瞬态洞内 CFD；没有这些生态条件时应保持默认 `0`。输出通过
`biogenicAffectedVoxels`、`minimumFluvialScallopRetention` 和侵蚀强度统计显式可观测。
`mineralArmoring>0` 在主要化学退缩阶段加入次生矿物沉淀形成的负反馈。覆盖斑块沿洞道
连续，而不是逐体素白噪声；`genesis` 只提供有界的成矿供给权重，低水力暴露区保留较厚
覆盖，快速水流则剥离大部分保护层。致密覆盖的溶蚀速率下限约为未覆盖面的十分之一，
对应 Zhang 等（2025，DOI `10.1016/j.bgtech.2025.100186`）石灰岩微流控实验中高硫酸盐
触发沉淀主导状态、致密石膏层完全遮蔽反应表面并使 CaCO3 平均溶蚀率降低一个数量级的
结果。Adedipe 等（2026，DOI `10.1029/2025WR042362`）结合流动实验、微 CT 和孔隙尺度
模拟进一步表明，多矿物空间排列及其与快速流道的距离会改变有效反应率，强流异质性可
增加传质限制。实现据此只衰减碳酸盐化学退缩，不保护床载磨蚀、拔蚀、跌坎或崩块冲刷；
它不把归一化强度解释为真实硫酸盐浓度，也不求解成核、晶体生长或瞬态水化学。默认 `0`
保持旧网格逐顶点一致；没有化学退缩时状态为 `inactive-no-chemical-retreat`，覆盖率、水力
保留率和最小剩余溶蚀率均通过元数据公开。
`bendUndercut>0` 从通道中心线相邻切向量计算连续曲率向量，并在曲率指向的反侧施加宽缓
余弦形退蚀增益，形成弯道外侧淘蚀、局部回流壁龛和不对称断面；直道及中心线端点的增益
自然归零。它与 scallop 的尖脊/凹窝尺度相乘，而不会额外叠加无方向噪声。该近似参考
Fowler（2025，DOI `10.1098/rspa.2025.0033`）对洞穴 scallop 非线性阶段中坡面相关流动
分离和尖脊形成的模型，以及 Hyman 等（2026）关于初始结构异质性持续控制反应输运
聚焦的结果。它是用于形态生成的曲率代理，不求解弯管二次流或瞬态 CFD；默认值为 `0`。
`vadoseIncision` 在潜水期圆形通道底部
叠加一条保持连通的包气带溪流峡谷。`erosion=0` 可关闭前三种表面溶蚀；溪流切割
由 `vadoseIncision` 独立控制。该模型复现可见形态与结构控制，不宣称替代完整的
水流—溶质输运—化学反应模拟。

`hydraulicErosion>0` 会进一步建立沿主通道的确定性相对流量场：局部速度按
Manning–Strickler 的 `R^(2/3) * sqrt(S)` 关系由水力半径 `R` 与坡降 `S` 估算，
再叠加沿程汇流。每条支洞都记录它在主洞的汇入口：远端分散补给沿支洞向汇入口累积，
再加入主洞汇入口下游的流量；因此主干、支洞和交汇段共用同一归一化网络，而低流量
支洞远端会保留更窄的断面。`flowFocusing` 控制溶蚀是否集中到高流量段，最终同时调制
溪流下切宽度和岩壁退缩。`hydraulicGradient` 与 `recharge` 提高整体输运能力，较高值
更容易产生扩大的优势通道。`damkohler` 是有效反应/对流比：低于 `8e-4` 时将低流量
通道抬升为较均匀的壁面退缩，`8e-4..8e-3` 形成多条竞争性优势通道，更高值强化为
入口控制的虫孔化；反应物穿透长度按 `Da^-1` 缩放。`transportG` 表示反应相对横向
扩散的限制，高值会增加穿透距离并削弱单一路径聚焦，留下更多细长通道。阈值采用
Hyman 等（2026）在三维裂隙网络中使用的 `2e-4 / 2e-3 / 2e-2` 三组数量级，并结合
Szawełło 等（2024）对低 Da 均匀溶蚀、中 Da 通道化和高 Da 虫孔化的空间聚焦测量；
这些量仍是用于形态生成的无量纲代理，而不是完整地球化学求解。默认
`hydraulicErosion=0`，保证旧参数输出 bit-exact 不变。此反馈关系参考 Aliouache 与
Jourde（2024）的裂隙网络反应输运结果（DOI `10.1016/j.jhydrol.2024.131684`），以及
Xu 等（2025）关于流量、裂隙宽度和石灰岩动态溶蚀速率耦合的实验模型（DOI
`10.3389/feart.2025.1701477`）、Szawełło 等（2024，DOI `10.1029/2024GL109940`），
以及 Hyman 等（2026，DOI `10.1029/2025JB033004`）的图反应输运模型：不同溶蚀
机制下，初始裂隙非均质性持续控制流路重组和突破行为（DOI
`10.1029/2025JB033004`）。

`mixingCorrosion>0` 复用每条 `CaveHydrologyBranch` 的 `trunkAnchor`，只在真实支洞—主洞
交汇点附近扩大既有洞壁。位点长轴沿主洞切向，尺度来自主支通道半径；主支流截面积比例
形成有界混合权重，seed 只决定各水体之间的化学差异，不改变汇流位置。Ma 等（2025，
DOI `10.1016/j.marpetgeo.2025.107465`）的水热化学模型表明，不同 CO2 含量与温度的地下水
混合会通过 mixing corrosion 和 retrograde solubility 改变优势路径及洞隙网络架构；
2024 年实测/化学模型也指出不同 pCO2 下分别达到方解石饱和的水体混合后可重新欠饱和。
实现有意只生成局部汇流壁龛和扩大段：Gabrovšek 与 Dreybrodt 的汇流裂隙模型表明，混合
腐蚀本身不足以生成大型主洞，因此它不会新增独立巨型洞室，也不宣称求解 PHREEQC、温度
传输或真实 CaCO3 平衡。没有支洞时状态为 `inactive-no-confluence` 且密度场不变；默认值
为 `0`，位点数、影响体素和最大退缩通过元数据公开。

`lithologicHeterogeneity>0` 将旧的等强度周期性层理扩展为连续但不均一的岩性层组。每个
层组拥有 seed 稳定的相对抗蚀性；少数层间接触面形成毫米至厘米级缝合线簇的介观代理，
只有具备横向连续性且能被当前通道水流接触的部分才会扩大为薄凹槽。该项仍受 `bedding`
总强度约束：`bedding=0` 时状态为 `disabled` 且密度场完全不变。它不会用三维白噪声替代
地层，也不会把每条层界都刻成等宽环带；受影响体素、缝合线体素、最小层组抗力和最大
退缩均写入元数据。默认 `0` 保持原网格 bit-exact。

现场依据来自 Dutra 等对巴西 Salitre 组异质碳酸盐洞穴的 LiDAR、地层、强度和孔隙率
联合研究（2023，DOI `10.1016/j.marpetgeo.2022.106029`）：高达约 20% 孔隙率的层理平行
缝合线及其厚度、间距控制了选择性溶蚀和洞穴几何。Kanavas 等汇总 29 组碳酸盐三维
实验与反应运移模拟（2025，DOI `10.1029/2024GL114369`），进一步表明初始流动非均质
会限制可接触反应表面并决定反应热点；因此实现把层组信号与权威水力暴露场相乘，而不是
只增加视觉条纹。700 余天野外浸泡试验（2025，DOI
`10.16030/j.cnki.issn.1000-3665.202405005`）观察到石灰岩偏向细裂纹、白云岩偏向明显
选择性孔蚀，支持保留层组抗性差异，但当前参数仍是介观形态代理，不求解具体矿物组成、
水化学或地质时间。

`floodAbrasion>0` 增加独立于碳酸盐化学反应的含砂洪水机械磨蚀。它复用主、支洞的水力
强度和局部弯曲方向，把退缩限制在底床及近床侧壁；沿程连续的低频涡胞决定壶穴/冲槽
候选区，较细的顺流波形形成磨蚀沟纹。`sedimentLoad` 同时表示可移动磨蚀工具和覆盖层：
从零升高时磨蚀迅速增强，但高含砂量会因床面覆盖而削弱增幅，避免“泥沙越多、无限切深”
的错误单调关系。纯 `hypogene` 洞穴不具备地表含砂洪水，状态为 `inactive-hypogene`；
`sedimentLoad=0` 时状态为 `inactive-no-tools` 且密度场不变。默认 `floodAbrasion=0`。

2024 年 Höllental 石灰岩峡谷单次超浓流前后 LiDAR 研究（DOI
`10.1038/s43247-024-01353-3`）量化到全段平均 3.41 mm、局部 10 m 段最高约 43 mm
的侧蚀，并总结近床磨蚀随高度快速减弱、中等活动床载受“工具—覆盖”平衡控制；实现
据此采用近床掩码和非单调泥沙效率，而没有把整条湿周同强度削薄。Gabel 等（2024，
DOI `10.1002/esp.5957`）把移动泥沙磨蚀、岩块拔蚀、颗粒磨圆及床载输运统一到临界
砾石河床模型中，支持将水力输运与磨蚀工具量共同控制退缩。Wang（2026，DOI
`10.1038/s41598-026-46196-4`）的开放水槽 CFD 与现场对照显示，壶穴内部的水平旋转和
垂向次级流共同搬运颗粒并分配底部/侧壁剪应力；当前涡胞是这一机制的介观形态代理，
不宣称求解自由表面 CFD、真实颗粒轨迹或事件持续时间。

`floodPlucking>0` 在连续磨蚀之外增加阈值化块体拔蚀。生成器在同一体素采样中保留最强和
次强裂隙暴露，单一强节理可释放薄板，两组节理交汇会提高块体预制程度；只有局部水力
强度越过阈值、且 seed 选中的有限块体单元与洞壁相交时才退缩。块体采用不同纵横尺度的
圆角长方体包络，因此留下角状台阶和断口，而不是把全部裂隙加宽成光滑沟槽。没有至少
两条裂隙时状态为 `inactive-no-fracture-network`；纯潜成洞穴状态为 `inactive-hypogene`；
默认 `0`。`pluckingAffectedVoxels`、最大退缩和最大裂隙预制程度通过元数据公开。

Fournereau 等（2025/2026，DOI `10.5194/egusphere-2025-1541`）用可控三维打印裂隙网络、
水与活动颗粒的侵蚀磨实验表明：完整基岩以磨蚀为主，裂隙基岩同时发生磨蚀、宏观撞蚀和
拔蚀；拔蚀更多改变侵蚀位置和暴露面积，不一定提高平均侵蚀率，且与裂隙间距和倾角呈
非线性关系。Chilton 等的直接水槽实验（2025，DOI `10.1130/G53413.1`）进一步显示水平
层面最易拔蚀、顺倾次之、逆倾最难。2025 年水力—断裂力学模型（Geomorphology，PII
`S0169555X25001175`）则表明数米每秒流动压力可扩展既有裂纹并形成非平面块体。当前
实现据此把拔蚀限制为裂隙预制且越过流动阈值的稀疏事件，但现有 `CaveFracture` 是近竖直
节理代理，因此不声称求解完整倾角、断裂韧度、压力脉动、块体轨迹或后续碰撞。

`constrictionScour>0` 将通道尺度的随机变化转为具有因果位置关系的“收缩—深潭—再展宽”
序列。位点只来自主洞和支洞半径的显著局部极小值；跌流冲刷中心沿水力路径移到收缩下游并
向床底偏移，出口段的横向掩码再扩大两侧壁。收缩比和权威水力权重共同控制强度，seed 不会
凭空放置深潭。纯 `hypogene` 洞穴状态为 `inactive-hypogene`，没有合格局部收缩时为
`inactive-no-constriction`；默认 `0` 保持旧网格 bit-exact。位点数、影响体素、最大退缩和
最大收缩比均写入元数据。

Kusack 等（2024，DOI `10.1029/2024JF007808`）的基岩水槽实验显示，上游回水使水和颗粒
越过强制收缩后向床底俯冲，形成顺流拉长的深潭；出口沉积改变颗粒方向并增强侧向侵蚀，
随后还能向下游传播较弱的 CPW 序列。Ross 等（2026，DOI `10.5194/esurf-14-553-2026`）
综合重复测深指出，天然基岩峡谷同样在收缩内及下游形成深潭，并在展宽段变浅、储存连贯
覆盖斑块。当前实现只生成一次静态介观形态，不求解自由表面、速度反转、瞬变流量、颗粒
轨迹或沉积覆盖随洪水变化。

`knickpointErosion>0` 从主洞溪流的真实纵剖面提取跌坎：只有相邻下游段同时满足显著陡化、
绝对坡度和相对通道半径落差阈值时，才在坎脚生成顺流拉长且向下偏移的冲潭，并对下部坎壁
施加较弱欠切。沉积物不是单调增益旋钮：低供给缺乏磨蚀工具，高供给则形成保护性覆盖；
权威水力权重再决定合格位点的侵蚀势。支洞的数组方向与实际汇流方向相反，因而本模型刻意
只读主干，避免颠倒上下游。纯 `hypogene`、无移动沉积物或无合格坡折时分别报告
`inactive-hypogene`、`inactive-no-tools`、`inactive-no-slope-break`；默认 `0` 保持旧网格
bit-exact。位点数、影响体素、最大退缩、最大坡折和最大落差均写入元数据。

Davy 等（2026，DOI `10.5194/egusphere-2026-420`）的基岩切蚀—沉积动力学模型显示，低沉积
浓度下磨蚀可让跌坎在保持形状时向上游迁移，狭窄峡谷先随跌坎传播、通过后才展宽，而过高
沉积负荷会抬高并覆盖坎脚。Hiramatsu 等（2024，DOI `10.2208/jscejj.23-16049`）的缓坡水槽
实验直接观察到瀑缘上游的阶梯侵蚀与迁移。Scheingross 等（2017，DOI
`10.1029/2016GL071730`）的均质岩实验及其机制模型（DOI `10.1002/2017JF004195`）进一步表明，
冲潭早期垂向钻蚀可明显强于横向扩宽，随后深潭中的沉积覆盖限制继续下切。当前实现把这些
结果转换为静态洞穴形态代理，不求解自由表面、瞬态颗粒轨迹、真实年代或瀑布迁移时间序列。

`streamBedKarren>0` 在活动溪床而不是整圈洞壁上扩大现有节理。采样器用通道局部坐标的
重力方向限制洞床暴露，用权威水力强度激活溶蚀，并组合当前点最强的两组 `CaveFracture`
掩码：单组节理形成连续溶沟，交叉处形成更深的局部溶坑。因此纹理方向来自同一份岩体结构，
而不是额外的随机线条。少于两组裂隙时报告 `inactive-no-crossing-fractures`，纯 `hypogene`
时报告 `inactive-hypogene`；默认 `0` 保持旧网格 bit-exact。影响体素、最大退缩、最大裂隙
导向强度和交叉溶坑强度均写入元数据。

Racine 等发布的 2025 年 KarstConduitCatalogue（DOI `10.5194/essd-17-4671-2025`）提供了
16 个水文一致洞段的厘米级地面/顶板栅格、点云、网格和中心线。其 Markov Spodmol 实测案例
明确记录：平滑倾斜层面下游约 20 m 出现沿两组基岩裂隙发展的 karren，随后才过渡到崩石段。
实现采用这一“暴露溪床 + 两组结构方向”的空间关系。Guérin 等的流膜溶蚀实验（2020，DOI
`10.1103/PhysRevLett.125.194502`）显示，倾斜可溶表面会自发产生沿主流方向的近似平行沟槽；
实现用水力门限保留这种流动选择，但没有把盐/石膏实验的毫米尺度直接冒充为灰岩洞穴尺度，
也不求解薄膜厚度、化学饱和度或随时间增长的沟槽波长。

`eddyPotholes>0` 进一步把溪床弱区发展为旋涡壶穴。候选点沿权威主干细分采样，但只有两条
现有 `CaveFracture` 同时张开的交汇区、具备活动水力且 `sedimentLoad` 仍有移动磨蚀工具时
才会保留；相邻候选还按通道半径去重。因此 seed 只决定已有岩体结构，不会直接撒布圆坑。
`potholeGravelSize` 控制磨蚀分区：细砾易悬移，在床底和内壁产生较宽、偏下游的磨蚀；粗砾
在相对壶穴直径更小的范围内旋转，形成偏上游的局部切蚀。侵蚀势足够高时，主坑底部还会
出现较小的复合次级坑。默认 `eddyPotholes=0` 保持旧网格 bit-exact；纯 `hypogene`、无工具、
不足两组裂隙或没有合格交汇分别公开明确的 inactive 状态。位点数、影响体素、最大退缩、
最大次级侵蚀和最大裂隙交汇强度写入元数据。

Sumner 与 Inoue 的 2026 年水力模型实验（DOI `10.2208/jscejj.25-16064`）直接比较了砂砾
尺度：较小颗粒悬移后广泛侵蚀内壁和床底、下游尤强；较大颗粒局限旋转并强化上游切蚀，
实验还观察到坑底次级壶穴。2026 年三维流场研究（DOI `10.1038/s41598-026-46196-4`）指出
壶穴初始凹陷常位于直立节理交汇、强化风化区或旧侵蚀沟，并描述了外侧下沉、底部向中心
旋转、中心轴上升的次级流。实现保留这些可识别的位点与非对称形态约束，但不声称求解
自由表面、颗粒离散轨迹、真实旋涡速度场或壶穴年代。

`breakdownScour>0` 让已经落地的 `CaveBreakdownBlock` 成为水力障碍物，而不再只是最后叠加的
静态崩石。每块石头先投影到最近的权威主干段；只有位于活动通道足迹内、具有可移动磨蚀工具
且突出高度足够时才建立位点。迎水端生成包绕石基两侧的较深双叶马蹄冲刷，下游则生成更长、
更浅的尾流槽。冲刷尺度来自石块尺寸和通道半径，方向来自主干流向；`multiscaleRoughness`
增大时会降低相干马蹄涡保留率。默认 `0` 保持旧网格 bit-exact；纯 `hypogene`、无沉积工具、
无崩块或崩块不在溪流足迹内分别公开 inactive 状态。位点数、影响体素、最大总退缩、最大
马蹄冲刷、最大尾流冲刷和最小粗糙度保留率均写入元数据。

2026 年山地河流巨砾沙洲水槽实验（Water 18, 1720）观察到迎水高压下洗形成马蹄涡，并把
最深冲刷定位在巨砾迎水端；尾流再循环控制更长的下游侵蚀—输移区。2026 年 CFD–DEM 多巨砾
研究（DOI `10.1038/s41598-026-38978-7`）进一步区分了孤立尾流、尾流干涉和密集掠流状态。
2025 年高频 PIV 实验（PII `S1001627925000691`）显示床面粗糙度会破坏马蹄涡系统、降低主涡
旋转强度并增强停滞。当前洞穴实现采用这些相对形态与抑制趋势，但没有把可动砂床平衡冲刷
深度冒充为灰岩切蚀速率，也不求解水深、Froude 数、洪水历时、崩石倾倒或尾流沉积脊。

2026 年大涡模拟（Samarasinghe 等，arXiv `2607.16908`）进一步约束了非线性响应：低流量
下约 35% 收缩产生最强俯冲流，高流量下最优值移向约 50%；继续收窄并不会让局部床面
剪应力无限单调增加，峰值主要位于深潭入口。实现因此使用随权威水力强度从 0.35 移到
0.50 的宽峰效率曲线，并把床面退缩偏向潭口；`maximumPlungingEfficiency` 公开实际
命中的效率。它仍是静态形态代理，不把 LES 的瞬时脉动伪装成已求解的湍流时间序列。

`microstructure>0` 在水力网络之上叠加双尺度岩性场：低频、空间连续的场表示相互连通的
粒间大孔及渗透率，高频场表示可被反应液接触、但未必形成贯通导管的粒内微孔表面积。
`microporosityAccess` 越高，溶蚀越均匀地分配到洞壁；较低值配合较高
`permeabilityContrast`，会让新鲜反应液聚焦到少量连续高渗区，形成分叉和虫孔式壁龛。
该场同时调制局部反应面积与水力强度，因此孔隙扩大后能形成正反馈，而不是在洞壁贴一层
无关流向的噪声。默认 `microstructure=0`，新增两个岩性参数不会改变既有网格。

这一设计直接采用 Briolet 等（2025，DOI `10.1016/j.gca.2025.03.019`）的受控实验结论：
在相同流体条件下，两种近纯方解石灰岩仍分别产生全宽分布式溶蚀和局部虫孔，传统 Pe–Da
图无法区分，必须加入粒内微孔可接触性及比表面积。孔渗正反馈还参考了 2025 年三维
Darcy 尺度实验—模拟研究（DOI `10.1016/j.ijggc.2025.104452`）：仅使用图像派生的
渗透率非均质即可复现实验溶蚀形态，而较强孔隙率—渗透率指数是 CO2 虫孔形成的关键。
2026 年天然多矿物岩石微连续体研究（DOI `10.1016/j.ces.2025.122507`）进一步观察到
均匀、锥形虫孔和分叉虫孔三类形态，以及高反应矿物比例只在中等 Pe 下推动锥形虫孔的
阈值效应；当前生成器保留其空间异质性和竞争通道机制，但不模拟具体矿物化学。

形态依据包括：Aliouache 与 Jourde（2024）关于裂隙网络各向异性、连通性和边界条件
控制早期岩溶通道的反应输运研究（DOI `10.1016/j.jhydrol.2024.131684`）；Jiang 等
（2025）关于应力、非均质裂隙与流量集中产生分叉/虫洞状溶蚀的耦合模型（DOI
`10.1029/2024JB029901`）；以及 Gabrovšek 等（2025）对随机裂隙网络中水位面、流动、
输运和裂隙孔径迭代演化的模型（DOI `10.5194/hess-29-6685-2025`）。实现选择了这些
研究共同支持的介观结构特征，而非照搬其面向地学时间尺度的昂贵求解过程。
贝壳状侵蚀依据 Fowler（2025）的非线性边界层模型（DOI
`10.1098/rspa.2025.0033`）：流动表面的传质相位偏移使波动失稳，随后形成圆弧凹窝、
斜率突变的尖脊和沿流向传播的形态；实现保留这些可辨识特征，但不声称执行完整 CFD。
尺度—流速关系还采用 2025 年 Mulu 洞穴形态研究对经典规律的现场总结：scallop 尺寸
与古流速成反比，陡侧位于上游（Geomorphology 483, 109822），并遵循 Springer 与
Hall 对洞穴 scallop 长度统计的提醒：尺度只作为定性水力代理，不把单一生成值解释为
精确流量测量。

二次碳酸盐沉积会在洞室内部生成细长钟乳石与更粗壮的石笋。`stalagmiteShape` 的
三类轮廓来自 2025 年理想石笋解析模型所描述的不同反应—输运状态：`conical` 为
尖锥，`columnar` 保持近似柱宽，`flatTop` 形成宽顶；`mixed` 按 seed 混合，并让少量
上下沉积体接合成石柱。所有沉积面归入独立的 `speleothems` 三角形组，洞壁仍属于
`caveWalls`，便于分配湿润方解石材质。形态模型标识记录为
`damkohler-thin-film-ripple-v2`，参考 DOI `10.1073/pnas.2513263122`。

`flowstones` 在洞室壁面叠加贴壁椭圆薄层，并让沉积面沿重力方向出现周期脊；
`curtains` 从洞顶边缘生成带横向波折的薄片。其依据是实测洞壁/活跃沉积物薄水膜和
波纹流石的水动力成因（DOI `10.5038/1827-806X.ijs2568`，以及 Bossea Cave 波纹
流石研究）。几何厚度会提升到当前体素分辨率可解析的尺度，因此表达的是长期累积
沉积层，而不是微米级瞬时水膜厚度。

主排水通道下半部会根据距中心水流线的距离和低频湿度变化归入 `wetWalls`，其余基岩
仍属于 `caveWalls`。这不是第二份几何，而是确定性的三角形材质分区，模型标识为
`drainage-proximity-v1`。默认的 `surfaceNormalMode="faceAverage"` 让
`normalSmoothing` 在空间重合顶点间平均面法线，保持原有输出。高质量模式
`surfaceNormalMode="densityGradient"` 则在最终连续三线性密度场上求中心差分梯度，以
负梯度作为朝向洞腔的表面法线，再用 `normalSmoothing` 控制从面法线到场法线的混合量。
这与 2026 年 *Contouring Signed Distance Fields by Approximating Gradients* 将距离场梯度
视为表面接触方向的思路一致；这里只借用法线重建原则，不把洞穴密度值宣称为严格 SDF。
两种模式都不会改变顶点位置、索引、碰撞形状或洞穴拓扑。湿润和干燥基岩保持连续；
面平均模式下，只有作为独立沉积物的方解石保留自己的平滑岛。

默认 `wetnessRefinement=0` 继续按三角形中心将主排水通道下方表面分入 `wetWalls`。
设为 `1` 后，生成器在最终自适应网格上分别计算三个顶点的连续湿润场；该场取排水通道
邻近度、重力流线以下的相对高度和低频表面可达性的最小值。跨越零线的三角形会被裁成
湿、干两个共面多边形再三角化，因此只提高材质边界分辨率，不改变洞壁形状或制造有厚度
的水层。2025 年洞穴现场测量显示洞壁水膜通常只有 25--70 微米，活动沉积物水平面约
200--300 微米；在米级洞穴网格中将其表达为表面状态而非宏观几何壳层更符合尺度。
高质量模型标识为 `gravity-drainage-contour-v2`，并记录被切分的源三角形数和新增三角形数。

洞穴通道可能穿过有限密度网格边缘。默认 `boundaryClosure=0` 保持开放域，适合需要显式
入口、出口或后续拼接洞块的场景；此时元数据会报告边界上的空气样本数。设为正值后，
`rough-host-envelope-v1` 在侵蚀和碎块脱落完成后、等值面重建之前，使用随 seed 轻微起伏
的宿岩包络把六个采样边界推回正密度。`1` 对应约 0.1 个归一化单位的最大封闭深度，
能够消除直视背景的有限域泄漏，同时保留内部通道。该选项解决的是域边界条件，不是
Marching Cubes 单元内的拓扑歧义；当前实现仍不宣称达到 2025 年 MCPro 对三线性等值面
的拓扑正确保证。

可运行示例与换 seed/形态快捷键见
[`examples/cave-generator`](../../../examples/cave-generator/README.md)。生成成本随三轴
分辨率乘积增长，应在加载或参数变化时重建并缓存，而不是逐帧生成。

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
