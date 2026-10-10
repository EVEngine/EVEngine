# 程序化生成模块

**脚本入口：** `eve.Procgen()`

按算法名和 Params 生成网格、地图层、图像、法线图或 GPU 纹理。

## Result 投影约定

PointGraph 的 `executeResult`、`validateResult` 以及 RuntimeGeneration 的
`nextGenerationJob`、`completeGenerationJob`、`failGenerationJob`、`nextCleanupRequest`、
`completeCleanupRequest` 已使用结构化 Result。领取成功但暂无任务时 `value == null`，不代表失败。
旧图执行和调度方法只作为兼容投影保留；具体错误码、所有权和迁移窗口见
[PointSet 管线](procgen/pointset-pipeline.md)。

Procgen 的创建、生成、输出和事务提交 API 都返回统一的 Squirrel Result 表：
`{ ok, code, hasValue, status, diagnostics, value }`。调用方必须先读取 `ok`；只有
`ok == true` 时才读取 `value`。失败信息使用 `status.summary` 或结构化
`diagnostics`，不再通过空值和全局错误字符串拼接错误协议。Graphics 上传属于
C++ render bridge 的 borrowed 边界，不是 Squirrel 的第二套生成入口。

这里的 `newParams`、`generate`、`buildMesh` 等是当前 canonical Squirrel 方法名；
Procgen 不提供 `lastError()`、`*Owned` 或 `*Checked` 的兼容命名。Result 的 `value`
可以是由 generation handle 支持的 owned proxy，释放和 stale 检查遵循该 proxy 的
公共方法。

## 脚本生成器宿主

项目脚本可以把参数 schema 与 `generate(params, ctx)` 封装成生成器 table，再交给
`runScriptGenerator(generator, params, systemName, seed)` 同步执行。宿主在当前 Squirrel
VM 的 owning thread 上创建临时事务 context；成功后原子提交并返回 system、seed、revision
及 output 名称，脚本抛错、标记 context 失败或遗留未闭合 trace 时返回结构化失败并保留上一
次成功快照。同一 system 不允许递归重入。宿主不会保存 generator closure、VM 栈或 Params；
context proxy 只在本次调用中有效，返回后其 handle 会变为 stale。

```squirrel
local forest = {
    generate = function(params, ctx) {
        local points = procgen.sampleGrid(16, 12, 8.0, ctx.seedFor("trees"), 0.2).value;
        if (!ctx.publish("trees", points)) throw ctx.getError();
    }
};
local run = procgen.runScriptGenerator(forest, params, "forest", params.getSeed());
if (!run.ok) throw run.status.summary;
```

生成器文件仍由项目通过 `dofile`/模块加载器载入；宿主负责的是校验 `generate` 入口、事务、
结构化错误和提交生命周期，不把任意 Squirrel closure 注册为后台线程或 PointGraph operation。
## UE PCG 对标范围

本模块对标的是 UE PCG 的核心工作流，而不是复制 UE 类型或资产格式：统一 Spatial Data、
带属性 Point Data、可缓存 Point Graph/子图、分区与 Hierarchical Generation、多 Generation
Source、视锥与时间预算、Biome Rules、Shape Grammar，以及到 Scene 的可撤销实例批次均有
对应实现。编辑器侧使用通用 `GraphDocument` 的 `procgen.point` domain，编译为版本化运行时
PointGraph 定义。

PointGraph 一等节点已覆盖 UE 基础节点库中的 Mesh/Spline Sampler、规则网格与 Poisson
蓝噪声采样（`grid.sample` / `poisson.sample`）、Landscape/Texture Sampler
（`landscape.sample` / `texture.sample`）、点集布尔
（Union/Intersection/Difference）、Bounds Modifier、Normal→Density、Static Mesh 权重
Spawner，以及 Disable/Inspect。Landscape/Spline/Actor 源仍通过外部绑定注入；详见
[PointSet 管线](procgen/pointset-pipeline.md)。

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

Hexmap 地形不走 `generate` / `Grid2D`。`hex.terrain` 与 `hex.sphere` 只提供 Params
schema（给 `applyAlgorithmDefaults` 用）；真正的出口是 `generateHexTerrain` /
`generateHexSphere`，返回 hexmap 可 `applyTerrain` / `applySphereTerrain` 的 bake
对象。`generate("hex.terrain", …)` 会失败并指向这两个入口。`width`/`height` 必须是
hexmap 的 5×5 分块整数倍。

```squirrel
local p = procgen.newParams().value;
p.setSeed(st.seed);
p.setSize(hexmap.cellCountX(), hexmap.cellCountZ());
procgen.applyAlgorithmDefaults("hex.terrain", p);
p.setInt("landPercentage", 50);
local baked = procgen.generateHexTerrain(p);
if (!baked.ok) throw baked.status.summary;
local applied = hexmap.applyTerrain(gfx, baked.value);
if (!applied.ok) throw applied.status.summary;
```

球面拓扑由 `hexmap.newSphere` 先建好，bake 必须匹配同一 `subdivision`：

```squirrel
hexmap.newSphere(gfx, subdivision, radius, seed);
local p = procgen.newParams().value;
p.setSeed(seed);
procgen.applyAlgorithmDefaults("hex.sphere", p);
p.setInt("subdivision", subdivision);
p.setFloat("radius", radius);
local baked = procgen.generateHexSphere(p);
hexmap.applySphereTerrain(gfx, baked.value);
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

## 程序化地板纹理（木地板 / 瓷砖）

`tex.floor.wood` 与 `tex.floor.tile` 用参数排列组合生成可平铺地板 albedo，无需外部图片。
对应的完整 PBR 配方为 `pbr.floor.wood` / `pbr.floor.tile`，一次烘焙产出
albedo / normal / roughness / metallic / height / AO（位移场与 albedo 像素对齐）。
示例见 `examples/procedural-textures`（Material 绑定 albedo+normal+height，scalar roughness/metallic）。

木地板 `layout` 支持 `planks` / `staggered` / `herringbone` / `chevron` / `parquet` /
`basket` / `diagonal` / `ladder` / `finger` / `versailles`；`tone` 支持 `oak` / `walnut` /
`pine` / `cherry` / `ebony` / `ash` / `maple` / `teak`。
其余常用旋钮：`rows`、`cols`、`gap`、`grain`、`warp`、`wear`、`stain`、`bevel`。
PBR 额外旋钮：`roughnessLow`、`roughnessHigh`、`metallic`、`normalStrength`、`aoStrength`、
`heightStrength`（沟槽偏粗糙，板面偏光滑）。

瓷砖 `pattern` 支持 `square` / `checker` / `diamond` / `hex` / `subway` / `brick` /
`stack` / `mosaic` / `basket` / `herringbone` / `octagon` / `fishscale` / `scallop` /
`pinwheel` / `windmill` / `star` / `moroccan` / `cobble` / `arabesque` / `terrazzo`；
`palette` 支持 `ceramic` / `terracotta` / `slate` / `porcelain` / `marble` / `black` /
`mosaic` / `subway` / `encaustic` / `jade` / `cobalt`。其余常用旋钮：`tilesX`、
`tilesY`、`grout`、`bevel`、`glaze`、`wear`、`speckles`、`motif`。釉面越高，板面粗糙度越低。

```squirrel
local textureParamsResult = gen.newParams();
if (!textureParamsResult.ok) throw textureParamsResult.status.summary;
local tp = textureParamsResult.value;
tp.setSize(256, 256);
tp.setString("layout", "herringbone");
tp.setString("tone", "walnut");
tp.setInt("rows", 8);
tp.setInt("cols", 8);
tp.setFloat("gap", 0.035);
local textureResult = gen.generateTexture("tex.floor.wood", tp, gfx);
if (!textureResult.ok) throw textureResult.status.summary;

local pbrResult = gen.generatePbrMaterial("pbr.floor.wood", tp);
if (!pbrResult.ok) throw pbrResult.status.summary;
local maps = pbrResult.value;
local albedo = maps.getAlbedo();
local normal = maps.getNormal();
local height = maps.getHeight();
local roughness = maps.getRoughness();
local metallic = maps.getMetallic();
local ao = maps.getAo();
maps.destroy();
```

C++ 可用 `generateWoodFloorTexture()` / `generateTileFloorTexture()` 获得 albedo，以及
`generateWoodFloorPbr()` / `generateTileFloorPbr()` 获得完整 `PbrTextureSet`；注册入口为
`registerFloorTextureRecipes()` 与 `registerFloorPbrRecipes()`。

## 程序化钢缆 / 铁链 / 麻绳

`mesh.cable` / `mesh.chain` / `mesh.rope` 沿 +X 生成可拼接线性构件；配套纹理与完整
PBR 配方为 `tex.cable.steel` / `pbr.cable.steel`、`tex.chain.iron` / `pbr.chain.iron`、
`tex.rope.hemp` / `pbr.rope.hemp`。示例见 `examples/cable-chain-rope`。

| 网格 | 材质 | 形态 |
|------|------|------|
| `mesh.cable` | 钢缆编织 albedo + PBR | 多股螺旋管（默认 6 股） |
| `mesh.chain` | 铸铁/锈蚀金属 | 交替椭圆环互扣 |
| `mesh.rope`  | 麻纤维编织 | 三股螺旋（股径更大、略鼓） |

网格共享参数：`segments`、`segLength`、`radius`、`thickness`、`strands`、`twists`、
`lengthSegs`、`radialSegs`、`majorSegs`、`minorSegs`、`scale`、`uvRepeat`。
`twists` 取整数时，每股螺旋在单元接缝处相位闭合，可无缝拼接。

纹理常用旋钮：`strands`、`twist`、`gap`、`wear`、`contrast`；钢缆另有 `polish`，
铁链有 `rust`，麻绳有 `fiber`。PBR 额外旋钮与地板配方相同（`roughnessLow` /
`roughnessHigh` / `metallic` / `normalStrength` / `aoStrength` / `heightStrength`）。

```squirrel
local pResult = gen.newParams();
if (!pResult.ok) throw pResult.status.summary;
local p = pResult.value;
p.setInt("segments", 8);
p.setFloat("segLength", 1.0);
p.setFloat("radius", 0.08);
p.setFloat("thickness", 0.022);
p.setInt("strands", 6);
p.setInt("twists", 1);
local meshResult = gen.buildMesh("mesh.cable", p);
if (!meshResult.ok) throw meshResult.status.summary;

local tpResult = gen.newParams();
if (!tpResult.ok) throw tpResult.status.summary;
local tp = tpResult.value;
tp.setSize(256, 256);
tp.setInt("strands", 6);
tp.setFloat("twist", 3.0);
local pbrResult = gen.generatePbrMaterial("pbr.cable.steel", tp);
if (!pbrResult.ok) throw pbrResult.status.summary;
```

C++ 入口：`generateCableChainRope()` / `registerCableChainRopeRecipes()`，以及
`generateSteelCableTexture()` / `generateIronChainTexture()` / `generateHempRopeTexture()`
与对应 `*Pbr` / `registerCableTextureRecipes()` / `registerCablePbrRecipes()`。

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

空间运算、分区、发布、图资产、GPU compute、hot reload、增量重建与 cell 同步 API 见
[PointSet 管线](procgen/pointset-pipeline.md)。

## 目标导向指南

按目标组织的生成配方（地牢、地形、六边形星球、城区、溶洞、树木、线性结构等）见
[目标导向指南](procgen/guides.md)。

Geometry Stroke、Mesh Modifier Graph 与融合执行见
[网格修改与融合](procgen/mesh-modifiers.md)。

## 常见问题

- 未保存 seed，无法复现玩家问题。
- Output palette 缺少算法输出的 tile key。
- 在每帧 update 生成大地图或纹理。
- 在每帧重新生成树木网格；应缓存 `Mesh`，仅在 seed 或参数变化时重建。

## 运行时质量与延迟任务

`eve.PcgFrameRateManager()` 提供可回放的帧率采样和地形质量档策略。调用
`configure(targetFrameRate, checkInterval, minQuality, maxQuality, currentQuality)` 原子配置状态机；
调用方每帧注入 `dt` 与 `timeScale` 给 `update()`，因此它不依赖 OS 墙钟。随后可读取
`getFps()`、`getQuality()`、`getQualityChanged()` 和 `getPreset()`。手动选择使用
`selectManualQuality(level)`，自动模式使用 `setAutomatic(enabled)` / `getAutomatic()`。
返回的 `PcgTerrainQualityPreset` 提供 `treeDistance`、`treeBillboardDistance`、
`treeCrossFadeLength`、`treeMaximumFullLodCount`、`detailObjectDistance`、
`detailObjectDensity`、`heightmapPixelError`、`heightmapMaximumLod` 和 `basemapDistance`。

`eve.PcgTaskQueue()` 提供 callback-free 的延迟任务状态机。`add(waitSeconds)` 加入任务并返回
稳定 ID；`tick(deltaSeconds)` 只把到期任务发布为 Ready，调用方通过 `getReadyTaskId()` 取得
ID、在队列外执行任务，再调用 `resolveReady(finished)`。未完成任务留待下一轮，完成任务被移除；
`cancelAll()` 清空队列，`getQueueSize()` 与 `getStatus()` 提供状态。队列不保存回调或脚本对象，
显式 dt 使调度可回放，非法时间不会改变队列。

这两个 PCG 策略类型由 `procgen` 模块绑定，不属于 `os`。

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
- UE PCG 扩展：`clearCache()`、`clearGenerationSources()`、`clearParameterOverride()`、`continueGenerationRefresh()`、`disconnect()`、`exposeParameter()`、`getActiveCellCount()`、`getAssetCount()`、`getAssetName()`、`getCacheHitCount()`、`getCellRevision()`、`getCommittedRefreshRevision()`、`getCompiledSegmentCount()`、`getComputeBufferReuseCount()`、`getComputeDispatchCount()`、`getComputeFallbackReason()`、`getComputeMinimumPoints()`、`getComputePeakBufferBytes()`、`getComputePolicy()`、`getComputeReadbackCount()`、`getComputeUploadCount()`、`getDirectionWeight()`、`getExclusionCount()`、`getExecutionCount()`、`getExecutionNodeBudget()`、`getExecutionPlanBuildCount()`、`getFailedCellCount()`、`getFrameTimeBudget()`、`getFrustumBehindRadius()`、`getFrustumHalfAngle()`、`getGeneratingCount()`、`getGenerationSourceCount()`、`getGenerationSourceId()`、`getInputNode()`、`getLastFusedTransformCount()`、`getLayerCount()`、`getLayerDensity()`、`getLayerName()`、`getLayerPriority()`、`getLevelCellSize()`、`getLevelCleanupRadius()`、`getLevelCount()`、`getLevelGenerationRadius()`、`getMaxActiveCells()`、`getMaxGenerating()`、`getMaxGenerationRetries()`、`getMaxY()`、`getMetricBackend()`、`getMetricCount()`、`getMetricMilliseconds()`、`getMetricNodeId()`、`getMetricOutputCount()`、`getMinY()`、`getModuleCount()`、`getModuleSymbol()`、`getNodeCount()`、`getNodeId()`、`getNodeOperation()`、`getOperationInputCount()`、`getOperationParamCount()`、`getOperationParamDefault()`、`getOperationParamKey()`、`getOperationParamKind()`、`getParameterCount()`、`getParameterFloat()`、`getParameterInt()`、`getParameterKind()`、`getParameterName()`、`getParameterString()`、`getPendingCleanupCount()`、`getPendingGenerateCount()`、`getRefreshWorkBudget()`、`getRevision()`、`getTicket()`、`getVariantAsset()`、`getVariantCount()`、`getVariantLength()`、`hasCell()`、`hasLayer()`、`hasModule()`、`hasNode()`、`hasParameterOverride()`、`intersectSpatial()`、`isFrustumCullingEnabled()`、`isMetricCacheHit()`、`isRefreshPending()`、`pointData()`、`refreshGenerationSources()`、`removeLayer()`、`removeModule()`、`removeNode()`、`requestCancel()`、`resetCancellation()`、`retryFailedCells()`、`setComputeMinimumPoints()`、`setComputePolicy()`、`setExecutionNodeBudget()`、`setMaxActiveCells()`、`setMaxGenerationRetries()`、`setNodeString()`、`setParameterFloat()`、`setParameterInt()`、`setParameterString()`、`setRefreshWorkBudget()`、`wasCancelled()`。

## 使用要点与地形

高度图印章、侵蚀、蒙版、生成会话、草地/树木、多地形事务，以及 GTS 切片与 PCG 运行时辅助见
[地形](procgen/terrain.md)。

## GridGraph、PointGraph 与 MeshGraph 混合编排

`GridGraph` 是面向语义格子的确定性数据流图。内建掩码生成节点覆盖填充、随机噪声、棋盘、点阵、
形状、元胞自动机、随机游走、迷宫和 Poisson 撒点；`generate.registry` 则直接复用现有
`GeneratorRegistry`，以 `algorithm` 字符串选择 `dungeon.bsp`、`level.roguelike`、WFC 等成熟
生成器，并把节点上的整数、浮点和字符串参数投影为 `Params`。地牢算法只保留注册表中的一份实现。
修改与选择节点覆盖布尔组合、反转、膨胀、收缩、平滑、随机/边缘/邻居/规则/岛屿/岛心/
细节范围/语义选择、寻路与八邻域 autotile。固定 seed 的生成结果可复现。

`convert.grid_to_points` 是显式的类型边界：它将匹配语义的格子中心转换为 `PointSet`，并写入
`cell_x`、`cell_y`、`semantic` 和 `detail` 属性。`point.subgraph` 随后可把这组点送入独立的
`PointGraph`，用于筛选、变换、采样和组合。

`MeshGraph` 接受 `Grid2D`、`PointSet` 与 `MeshBuild` 三种强类型输入。`mesh.grid_tiles` 从格子
构造顶面和暴露侧墙，并按
`group/semantic/tile-kind/rN/(normal|mx)/reference-configuration` 建立命名三角形组。
其中标准网格 mask 会映射到 TileWorldCreator 4.3.5 的 3×3 配置表，直接携带瓦片类别、
0/90/180/270 度朝向和 X 镜像语义；原版未归类的配置 350 保持为 `none`。

`eve.ProcgenObjectBuildLayer()` 是 Objects Build Layer 的原生对应物。它接收 PointGraph 产生的
`PointSet`，按稳定点身份和层 seed 确定性选择加权 `asset`，并应用层偏移、位置散布、Euler
旋转、统一或非统一缩放。`setOrientation` 可按北、东、南、西优先级朝向另一点层；
`setPlaceOnTop` 读取输入点的 `surface_highest_y` 或 `surface_lowest_y`。`addChild` 生成带
`object_role=child` 与 `parent_index` 的子对象点。结果仍是普通 PointSet，可继续送入 PointGraph、
`mesh.instance_points` 或实例发布接口。

对象层 API 速查：`clearAssets` 清空加权资产；`setLayerOffset`、`setLayerScale` 设置层级变换；
`setPositionRadius` 设置平面散布半径；`setRandomRotation`、`setRandomScale` 设置确定性随机范围；
`disableOrientation` 关闭朝向层；`clearChildRules` 清空子对象规则；`buildOriented` 使用显式朝向点集
执行对象层。GridGraph 与 MeshGraph 的运行时输入分别通过 `setNodeGrid` 绑定；GridGraph 使用
`setNodePointSubgraph` 绑定拥有明确输入/输出节点的 PointGraph 子图。

`eve.ProcgenBuildLayerStack()` 将 Tiles 与 Objects 定义放进同一个有序执行器。`addTileLayer` 和
`addObjectLayer` 保存值副本，`setEnabled` 控制参与执行的层；`execute` 只在全部启用层成功后返回
`ProcgenBuildLayerExecution`，再通过 `getType/getMesh/getPoints` 获取具名 artifact。配置使用
`EVPCG_BUILD_LAYERS 1`，对象层使用嵌入式 `EVPCG_OBJECT_LAYER 1`；反序列化拒绝未知记录和尾随字段，
并在完整校验成功后原子替换旧配置。

层栈可用 `getLayerId`、`getLayerType` 和 `isLayerEnabled` 查询定义；`executeOriented` 为需要朝向
点集的对象层执行完整栈。增量执行器的 `getCachedClusterCount` 返回当前成功提交的活动簇数量，
失败构建不会改变该计数。

`eve.ProcgenIncrementalBuildExecutor()` 为 BuildLayerStack 增加跨次构建缓存。`update` 接收簇边长（格子数）
和格子世界尺寸，返回 `ProcgenIncrementalBuildDelta`；每项通过 `getClusterX/getClusterZ/isRemoved` 标识
场景侧应更新或删除的簇，非删除项用 `getArtifacts` 取得该簇的完整层结果。格子哈希带一格 halo，
因此边界 autotile 改动会同时使相邻簇失效；点按半开世界坐标范围归属一个簇。任一脏簇构建失败时，
内部旧缓存不变。带朝向层时使用 `updateOriented`；`getCachedArtifacts` 可按簇坐标读取当前快照。
场景侧应以 `clusterX:clusterZ` 作为派生缓存键：普通 delta 替换该簇的渲染/碰撞 artifact，removed
delta 先从场景隐藏或移除旧对象再删除缓存项。空 PointSet 是合法簇结果，不应尝试上传空实例网格。
示例中的物理消费者按同一键维护静态三角网格刚体：先成功创建替换体再销毁旧体，避免失败时留下
无碰撞窗口；裁剪掉 `physics` 模块时明确退化为纯渲染路径，不影响生成结果与缓存权威状态。
`mesh.instance_points` 把任意 `MeshBuild` 实例化到点集；`mesh.merge` 与 `mesh.transform` 完成
组合和变换。输出仍是现有 `MeshBuild`，可继续交给统一发布、碰撞、材质和场景生命周期。

三个图各自拥有缓存，不共享跨帧可变裸指针。连接时检查端口类型，Grid 输出不能直接连到
Point 或 Mesh 输入，必须经过显式转换或输入绑定。图拓扑、参数或输入变化会从修改点向下游
失效缓存，相同输入重复执行会复用结果。

脚本通过 `procgen.newGridGraph()` 与 `procgen.newMeshGraph()` 获取模块拥有的句柄；所有失败均为
统一 Result 投影。两种图定义都支持版本化 `serializeDefinition()` / `deserializeDefinition()`；
运行时输入值刻意不写入定义，加载后由调用方重新绑定，避免资产与场景实例形成第二份权威状态。

TileWorldCreator 4 运行时核心能力的逐项对应和边界见
[`TileWorldCreator4核心移植.md`](../../dev/TileWorldCreator4核心移植.md)。

## 模块体积与连接约束

`eve.ProcgenModuleAssemblyPlan()` 是 VM-owned 同步规划对象，不依赖渲染模块。
`configure(cellSize,floorHeight,minX,maxX,minZ,maxZ,maxLevels,bounded,requireConnection)`、
`setAllowedQuarterTurns(turn0,turn90,turn180,turn270)` 和 `setMinimumSupportRatio(ratio)`
返回 Result，必须在注册定义或放置前调用。
`registerModule(id,widthCells,depthCells)` 注册单层体积；
`registerVolumeModule(id,widthCells,depthCells,heightLevels)` 注册多层体积。
`addConnector(id,x,z,facing,tag,accepts)`、
`addVolumeConnector(id,x,z,level,facing,tag,accepts)` 设置带类型的六向连接面；
facing 0–5 对应 north/east/south/west/up/down。定义已使用后禁止修改连接器。
这些写操作均返回 Result，失败不提交。

`applyConfigJson(json)` 只接受空计划，原子加载
`eve.procgen.module-assembly` schema version 1，unknownFields 必须为 reject。
根字段为 schema/version/unknownFields/constraints/modules；constraints 包含
cellSize、floorHeight、bounds、maxLevels、allowedQuarterTurns、requireConnection、
minimumSupportRatio。modules 定义 id、widthCells、depthCells、heightLevels、connectors；
connector 字段为 cellX/cellZ/level/facing/tag/accepts。未知版本/字段及非法值拒绝。
v1 无前驱迁移，不恢复 placement；调用成本随 JSON/连接器大小增长，限制输入 1 MiB。

`place(id,x,z,level,quarterTurn)` 返回 Result：整数体素占地不得重叠或越界，
所有接触面必须双方 tag/accepts 互认，受旋转白名单、连通性和垂直支撑比例约束。
拒绝后计划不变；quarterTurn 为 90 度整数倍。规划成本随已有体积和连接面增长，
应在地图构建阶段执行，而非每帧调用。
`getPlacementCount()`、`getPlacementModule(index)`、`getPlacementX(index)`、
`getPlacementY(index)`、`getPlacementZ(index)`、`getPlacementYawDegrees(index)`
读取已提交的布局及米制变换。所有方法由调用者线程独占使用，不执行回调、不持锁。

# House generation grid and points

房屋生成已归入 procgen，`eve.HouseGen()` 保留为兼容构造器。它提供
`loadComponentsFromJson`、`loadComponentsFromFile`、`clearComponents`、
`getComponentCount`、`newRequest` 与 `newLayout`；请求可通过 `setPlot`、
`setFloors`、`setModuleSize`、`setFloorHeight`、`setStyle`、`setFootprint`、
`setRoof`、`setEntrance`、`setRequiredRooms` 与 `setPerimeter` 配置。
布局提供 `toJson`、`fromJson`、`getInstanceCount`、`getInstanceComponentId`、
`getInstanceX`、`getInstanceY`、`getInstanceZ`、`getInstanceRotationDeg`、
`getFloorHeight`、`getFootprintStyle`、`getRoofStyle`、`getRoomCount`、
`getDiagnosticCount`、`writeFootprintGrid` 与 `writeComponentPoints`。

房屋不定义专用图类型。`writeFootprintGrid` 输出 `Grid2D`，供现有 `GridGraph.grid.input` 使用；
`writeComponentPoints` 输出 `PointSet`，供现有 `PointGraph.input` 使用。后续筛选、变换、合并、
缓存与序列化全部由既有 graph 系统处理。
