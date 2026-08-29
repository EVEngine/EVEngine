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

## 纯脚本 PointSet 管线

程序化世界编排使用普通 Squirrel 函数，不要求节点图。`PointSet` 是带位置、法线、
旋转、缩放、密度、独立 seed 和自定义属性的 3D 采样集合。`sampleGrid`、
`filterHeight`、`filterDensity`、`excludeRadius`、`jitterPoints` 和 `selfPrune`
均返回新的集合，不修改输入，因此中间结果可以命名、检查、复用或分支：

```squirrel
local rootSeed = 42;
local candidatesResult = procgen.sampleGrid(32, 20, 2.0,
                                            procgen.deriveSeed(rootSeed, "trees"), 0.75);
if (!candidatesResult.ok) throw candidatesResult.status.summary;
local candidates = candidatesResult.value;
local outsideRoadResult = procgen.excludeRadius(candidates, 20.0, 12.0, 4.0);
if (!outsideRoadResult.ok) throw outsideRoadResult.status.summary;
local outsideRoad = outsideRoadResult.value;
local treesResult = procgen.selfPrune(outsideRoad, 1.8);
if (!treesResult.ok) throw treesResult.status.summary;
local trees = treesResult.value;
```

空间约束也可继续用纯函数串接。`filterBox` / `excludeBox` 接受世界空间 AABB，
`projectToHeightmap` 按 origin、cellSize 和 heightScale 把点投射到已有高度图并写入
地表法线，随后可用 `filterSlope` 按角度筛选：

```squirrel
local regionResult = procgen.filterBox(candidates, 0, -100, 0, 512, 100, 512);
if (!regionResult.ok) throw regionResult.status.summary;
local region = regionResult.value;
local groundResult = procgen.projectToHeightmap(region, terrain, 0, 0, 2.0, 80.0);
if (!groundResult.ok) throw groundResult.status.summary;
local ground = groundResult.value;
local buildableResult = procgen.filterSlope(ground, 0.0, 28.0);
if (!buildableResult.ok) throw buildableResult.status.summary;
local buildable = buildableResult.value;
local outsideTownResult = procgen.excludeBox(buildable, 120, -100, 120, 240, 100, 240);
if (!outsideTownResult.ok) throw outsideTownResult.status.summary;
local outsideTown = outsideTownResult.value;
```

任意多边形和折线样条同样用 `PointSet` 表示控制点，不需要额外节点类型。
`filterPolygon` / `excludePolygon` 处理 XZ 平面的凹多边形；
`filterSplineDistance` 以到最近线段的距离选择道路、河流或隔离带。
`sampleSpline` 按跨线段连续间距采样，并写入切线 yaw、稳定 point seed，可直接放置
路灯、护栏等资产：

```squirrel
local roadResult = procgen.newPointSet();
if (!roadResult.ok) throw roadResult.status.summary;
local road = roadResult.value;
road.add(0, 0, 0); road.add(80, 0, 30); road.add(140, 0, 120);
local reservedResult = procgen.filterSplineDistance(candidates, road, 0.0, 8.0);
if (!reservedResult.ok) throw reservedResult.status.summary;
local reserved = reservedResult.value;
local outsideRoadResult = procgen.filterSplineDistance(candidates, road, 8.0, 100000.0);
if (!outsideRoadResult.ok) throw outsideRoadResult.status.summary;
local outsideRoad = outsideRoadResult.value;
local lampsResult = procgen.sampleSpline(road, 12.0,
                                         procgen.deriveSeed(rootSeed, "lamps"), 0.0);
if (!lampsResult.ok) throw lampsResult.status.summary;
local lamps = lampsResult.value;

local townResult = procgen.newPointSet();
if (!townResult.ok) throw townResult.status.summary;
local town = townResult.value;
town.add(20, 0, 20); town.add(160, 0, 35); town.add(130, 0, 150); town.add(35, 0, 120);
local townCandidatesResult = procgen.filterPolygon(candidates, town);
if (!townCandidatesResult.ok) throw townCandidatesResult.status.summary;
local townCandidates = townCandidatesResult.value;
local wildernessResult = procgen.excludePolygon(candidates, town);
if (!wildernessResult.ok) throw wildernessResult.status.summary;
local wilderness = wildernessResult.value;
```

不要让不同内容共享一个可变随机流。用 `deriveSeed(root, "trees")`、
`deriveSeed(root, "rocks")` 为分支派生稳定 seed；修改岩石管线不会扰动树木结果。

### 统一空间数据与集合运算

`ProcgenSpatialData` 为点、体积和样条提供统一的世界空间查询。它不引用 scene 或
graphics，因此生成逻辑可以在无头测试、编辑器预览和运行时流送中复用。当前支持
Box、Sphere、Spline、Point Data，以及 Union、Intersection、Difference 三种组合；
组合对象会保存输入的不可变副本，调用方可以安全销毁原对象：

```nut
local world = procgen.boxVolume(0, -20, 0, 512, 200, 512);
local town = procgen.sphereVolume(256, 0, 256, 80);

local roadPoints = procgen.newPointSet();
roadPoints.add(20, 0, 30);
roadPoints.add(180, 0, 220);
roadPoints.add(480, 0, 420);
local road = procgen.splineData(roadPoints, 8.0);

local reserved = procgen.unionSpatial(town, road);
local wilderness = procgen.differenceSpatial(world, reserved);
local candidates = procgen.sampleSpatial(wilderness, 16.0,
    procgen.deriveSeed(42, "wilderness"), 0.7);
```

`sampleSpatial(spatial, spacing, seed, jitter)` 在空间包围盒内建立确定性三维格点，
然后使用真实空间查询剔除外部候选。相同输入会得到相同位置和 point seed。
`filterSpatial(points, spatial, false)` 保留内部点；最后一个参数为 `true` 时保留外部点。
可用 `getKind`、`contains`、`hasBounds`、`getMinX/Y/Z` 和 `getMaxX/Y/Z` 检查空间值。

高度图可包装为 `surface.heightfield`，成为与体积、样条相同的一等空间输入。
`sampleSpatial` 对高度面只在 XZ 平面产生候选并自动写入高度和地表法线；已有点集可用
`projectToSpatial` 投影：

```nut
local surface = procgen.heightfieldData(heightmap, 0, 0, 2.0, 80.0);
local ground = procgen.sampleSpatial(surface, 8.0, 42, 0.5);
local projected = procgen.projectToSpatial(importedPoints, surface);
```

常用 PCG 点处理以返回新值的纯函数提供：`mergePoints`、`transformPoints`、
`filterFloatAttribute`、`filterStringAttribute` 和 `densityCull`。属性过滤最后一个参数
是 invert；`densityCull(points, seed, multiplier)` 使用 point density 和稳定 seed，
不会因其它分支新增随机调用而改变结果。

### 分区、层级与运行时生成

`newRuntimeGeneration(worldSeed)` 创建与场景/渲染解耦的流送调度器。每个 level
声明 cell size、生成半径和清理半径倍率；大格适合道路、山体和乔木，小格适合草、
碎石等近景细节。生成与清理使用不同半径以避免边界抖动：

```nut
local runtime = procgen.newRuntimeGeneration(42);
runtime.addLevel(256.0, 900.0, 1.25); // large shared work
runtime.addLevel(64.0, 260.0, 1.5);   // nearby details
runtime.setMaxGenerating(4);
runtime.setMaxActiveCells(2048); // active + in-flight resident budget
runtime.setMaxPointsPerCell(200000); // protect against pathological graph output
runtime.setMaxResidentPoints(4000000); // hard PointSet cache budget
runtime.setMaxGenerationRetries(3);
runtime.setDirectionWeight(0.35);
runtime.setFrameTimeBudget(3.0);

function updateRuntime(playerX, playerZ, forwardX, forwardZ) {
    runtime.updateSource(playerX, playerZ, forwardX, forwardZ);
    runtime.beginFrame();
    for (local request = runtime.nextGenerate(); request != null;
         request = runtime.nextGenerate()) {
        try {
            local cell = buildCell(request.getLevel(), request.getMinX(), request.getMinZ(),
                                   request.getMaxX(), request.getMaxZ(), request.getSeed());
            if (!runtime.completeGeneration(request, cell)) throw "stale PCG cell request";
        } catch (error) {
            runtime.failGeneration(request); // 保留相同 cell seed，稍后重试
        }
    }
    for (local request = runtime.nextCleanup(); request != null;
         request = runtime.nextCleanup()) {
        removeCellInstances(request.getLevel(), request.getX(), request.getZ());
        runtime.completeCleanup(request);
    }
}
```

队列按距离与视线朝向排序，同优先级使用 level/z/x 稳定打破平局，保证相同输入跨运行产生
相同发放顺序。`setMaxGenerating` 限制同时发出的请求，`setMaxActiveCells` 为 active 与
in-flight Cell 设置常驻硬上限（0 表示无限），
`setMaxPointsPerCell` 和 `setMaxResidentPoints` 分别限制单 Cell 与全部 active Cell 的点数；
超预算提交不会发布，调用方可缩减输出后重交同一 ticket，或调用 `failGeneration` 进入有界
重试。`getMaxPointsPerCell` / `getMaxResidentPoints` 返回当前配置，`getResidentPointCount` 与
`getRejectedOutputCount` 提供内存压力遥测，0 表示无限制。全局预算拒绝提交时，调度器会按
当前 source 距离/朝向优先级确定性地将最远 active Cell 放入 cleanup 队列；也可调用
`trimToResidentPoints(target)` 主动收缩缓存。被 trim 的 Cell 在清理完成前不会因 source
刷新而复活；其 PointSet 在 `completeCleanup` 确认前仍计入常驻预算，实际释放后原异步提交
才可安全重交。
`setFrameTimeBudget` / `beginFrame` 限制一帧内继续发放工作的 CPU 时间。每格拥有由
world seed、level、x、z 派生的独立 seed、revision 和 PointSet 输出缓存。每次异步生成或
清理还携带唯一 `getTicket()`；Cell 离开、重新进入调度范围后，旧任务即使 seed 相同也会
被 `completeGeneration` / `completeCleanup` 拒绝，避免大世界流送中的 ABA 陈旧提交。
生成失败只按 `setMaxGenerationRetries` 有界重试，耗尽后进入 Failed 状态并从工作队列移除；
修复资产或外部依赖后用 `retryFailedCells()` 显式恢复，避免确定性错误形成 retry storm。
`getCellOutput` 返回缓存副本，`debugReport` 汇总 pending/generating/active/cleanup/failed。
需要跨会话或跨 World Partition 回访复用时，`serializeCell(level,x,z)` 输出版本化、属性键
稳定排序的完整 Cell 缓存；`deserializeCell(definition)` 校验 world seed、level、数据上限和
完整输入后原子恢复，同时使同 Cell 的旧异步 ticket 失效。实例覆写或图版本应由调用方纳入
缓存文件名/外部 build key，避免把旧内容恢复到新图定义。

调度器也支持多个命名 generation source。Cell 只生成一次，但只要仍在任意 source 的
cleanup radius 内就不会清理；`radiusScale` 可让任务目标使用比玩家更小的影响范围：

```nut
runtime.setGenerationSource("player", playerX, playerZ, forwardX, forwardZ, 1.0);
runtime.setGenerationSource("quest-target", questX, questZ, 0, 0, 0.5);
runtime.removeGenerationSource("quest-target");
```

`setFrustumCulling(true, halfAngleDegrees, behindRadius)` 只给视锥内远处 Cell 发放生成请求，
但 `behindRadius` 内的近处 Cell 无论朝向都会生成，避免玩家转身时看到空洞。已经 Active
的 Cell 不会仅因离开视锥立即消失，仍按所有 source 的 cleanup radius 使用迟滞清理。

### 发布到 Scene

当 scene 模块存在时，它通过 `IProcgenSceneSink` capability 接收实例批次；procgen
本身没有 scene include 或链接依赖。`publishInstances` 从 PointSet 的位置、yaw、scale
和稳定 seed 创建场景节点，并用指定 string attribute 选择资产名：

```nut
procgen.publishInstances("forest/manual", trees, "asset", "oak");
local count = procgen.getPublishedInstanceCount("forest/manual");
local reused = procgen.getPublishedReusedCount("forest/manual");
procgen.removeInstances("forest/manual");
```

Scene 为每个批次建立 `__pcg/<batchId>` Host，节点带 `pcg`、`pcg.instance` 和
`pcg.asset:<name>` tag。同一批次再次发布会按稳定节点 id reconcile，生成失败不会先
破坏其它批次。默认实例 id 使用 point seed 的局部重复序号，因此插入其它 seed 的点不会
让整批对象失去池化身份；string metadata `instanceId` 可提供项目级稳定 id。发布前会拒绝
重复 id，失败时 Scene Sink 中的旧批次保持不变。
`getPublishedCreatedCount`、`getPublishedReusedCount` 和 `getPublishedRemovedCount`
返回最近一次成功 apply/remove 的实例池变化，可用于流送 profiler 和复用率告警。成员增删
导致 SceneNode arena 重排时，Scene 仍按稳定实例 id 恢复已挂接的渲染、物理和脚本对象，
避免把结构变化误当成整批资源重建。运行时 Cell 使用
`publishCellInstances(prefix, request, points, ...)`
和 `removeCellInstances(prefix, request)`，其批次 id 自动包含 level/x/z。

完整组合见 `examples/pcg-biome`：大 Cell 放置乔木，小 Cell 放置草和岩石，Spline
道路作为排除域，移动生成源会触发带迟滞的生成与清理。

### Point Graph 与子图

需要数据驱动资产或可视化编辑器时，用 `newPointGraph()` 构建与 UE PCG Graph 类似的
惰性 DAG；普通项目仍可继续直接组合前述纯函数。节点以稳定字符串 id 标识，连接到
input slot 0/1，执行时自动拒绝缺失输入和环：

```nut
local graph = procgen.newPointGraph();
graph.addNode("sample", "spatial.sample");
graph.setNodeSpatial("sample", forestVolume);
graph.setNodeFloat("sample", "spacing", 12.0);
graph.setNodeFloat("sample", "jitter", 0.7);
graph.setNodeInt("sample", "seed", 42);

graph.addNode("road-exclusion", "spatial.filter");
graph.setNodeSpatial("road-exclusion", roadSpatial);
graph.setNodeInt("road-exclusion", "invert", 1);
graph.connect("sample", "road-exclusion");

graph.addNode("prune", "self.prune");
graph.setNodeFloat("prune", "radius", 10.0);
graph.connect("road-exclusion", "prune");
if (!graph.validate()) throw graph.getError();
local trees = graph.execute("prune");
```

支持的 operation 可用 `getOperationCount/getOperationId` 枚举，包括：
`input`、`spatial.sample/filter/project`、`merge`、`copy.points`、`transform`、
`density.remap`、`attribute.math.float`、
`filter.float/string`、`attribute.set.float/string`、`density.cull`、
`self.prune`、`jitter`、`biome.generate`、`grammar.generate`、`branch` 和 `subgraph`。

`biome.generate` 通过 `setNodeSpatial` 与 `setNodeBiomeRules` 绑定生成域和 Biome 规则，
反射 `spacing/seed/jitter`；`grammar.generate` 通过 `setNodeShapeGrammar` 绑定 Shape Grammar，
接收控制点输入并反射 `grammar/seed/acceptIncomplete`。setter 会复制规则资产，调用方销毁或
热重载原对象不会悬空；这些外部槽和 PointSet/SpatialData 一样不写入图定义，加载或
`instantiate()` 后必须重新绑定。

`copy.points` 对每个 target 实例化全部 source 点（稳定的 target-major 顺序），组合位置、
yaw、scale、density 与独立 seed；可继承 target metadata，冲突时 source metadata 胜出。
`maxPoints` 在笛卡尔积分配前实施硬上限，防止错误图造成编辑器或流式任务内存爆炸。
`density.remap` 线性映射 density 范围并可钳制输出；`attribute.math.float` 对 float metadata
执行 add/subtract/multiply/divide/min/max，可写入新属性并为缺失输入提供确定性默认值。
零输入范围、非法 operation 和除零会作为节点执行错误报告。

`execute(outputId)` 只求值该输出的祖先节点；没有配置变化时复用缓存。节点参数、输入或
空间数据变化时只失效该节点及其下游，未受影响的分支继续复用结果；`getRevision()` 提供
单调递增的资产/预览缓存版本。序列化会稳定排序参数键，等价图在不同平台产生相同定义字节。
`setNodeFloat`、`setNodeInt` 和 `setNodeString` 按 operation 反射 schema 校验参数名与类型；
未知参数或类型不匹配会被拒绝，资产反序列化也遵循同一规则并保持事务式失败。
大型图可用 `setExecutionNodeBudget(maxNodes)` 限制一次调用新求值的节点数；超出预算会保留
已经完成的上游缓存，下一次提高预算后可继续求值。`requestCancel()` / `resetCancellation()`
为编辑器预览和任务队列提供显式取消边界，`wasCancelled()` 区分预算/取消退出与数据错误。
`setMaxNodeOutputPoints(maxPoints)` 对每个节点实施统一硬上限；空间采样和 Biome 在分配前按
bounds/spacing 计算保守上界，其余节点在结果进入缓存前拒绝并释放超限输出。降低预算会清除
旧缓存，避免大结果通过 cache hit 绕过新限制；`getMaxNodeOutputPoints()` 返回当前上限。
图资产可用 `exposeParameter(name, nodeId, key)` 将节点参数公开为强类型图参数；运行时实例通过
`setParameterFloat/Int/String()` 覆写，`clearParameterOverride()` 恢复资产默认值。覆写不会写入
序列化资产，并且只失效目标节点及下游缓存，适合关卡实例、Biome 和流式 Cell 共享图定义。
`instantiate()` 从资产定义创建独立运行实例，不复制 PointSet/SpatialData/BiomeRules/
ShapeGrammar 外部输入、覆写、缓存、
metric 或取消状态；并行 Cell/后台任务应各自实例化并重新绑定输入，不能共享同一个可变实例。
`getNodeOutput` 可取得任意已执行节点的中间 PointSet；`getMetric*` 和
`debugReport` 提供逐节点输出数量、耗时及 cache hit。任意 node/edge/parameter/input
修改都会失效整图缓存，避免返回旧结果。
调试视图无需复制 PointSet 即可用 `getMetricMinX()`、`getMetricMinY()`、`getMetricMinZ()`、
`getMetricMaxX()`、`getMetricMaxY()`、`getMetricMaxZ()` 绘制节点 bounds，并用
`getMetricAverageDensity()` 显示密度热度；这些摘要在首次执行时计算并随节点 cache 保存，
cache hit 不会再次遍历大型点集。

### Vulkan / WebGPU Point Compute

`ProcgenPointGraph.setComputePolicy("auto" | "gpu" | "cpu")` 控制点处理执行器。默认 `auto`
在输入达到 `setComputeMinimumPoints()`（默认 1024）后，将 `transform` 的位置、法线、旋转和
缩放批量打包到 storage buffer，通过单个 Sequence 完成上传、dispatch 和回读；Vulkan 使用
GLSL/SPIR-V，WebGPU 使用 WGSL。设备尚未初始化、shader 编译、提交或回读失败时会确定性地
回退现有 CPU 实现，`getComputeFallbackReason()` 返回原因。

`getMetricBackend(index)` 返回该节点实际使用的 `cpu`、`vulkan` 或 `webgpu`，`debugReport()`
也包含 `backend=`。这使编辑器和性能采集能够区分 GPU 命中与 CPU fallback。当前 GPU kernel
覆盖 `transform`；其余节点继续走 CPU，后续可在同一执行层扩展为连续节点融合，无需改变图资产。

融合与传输统计由 `getComputeUploadCount()`、`getComputeDispatchCount()`、
`getComputeReadbackCount()`、`getComputeBufferReuseCount()`、`getComputePeakBufferBytes()` 和
`getLastFusedTransformCount()` 提供。计数属于图实例持有的 Compute 执行器并跨 `execute()` 累积，
可用执行前后差值验证一个图段是否只发生一次上传、dispatch 和回读。

子图节点将首个输入写入嵌套图指定的 input node，并返回指定 output node：

```nut
local sub = procgen.newPointGraph();
sub.addNode("in", "input");
sub.addNode("jitter", "jitter");
sub.connect("in", "jitter");
sub.setNodeInt("jitter", "seed", 99);
sub.setNodeFloat("jitter", "x", 2.0);
sub.setNodeFloat("jitter", "z", 2.0);

graph.addNode("detail-subgraph", "subgraph");
graph.connect("prune", "detail-subgraph");
graph.setNodeSubgraph("detail-subgraph", sub, "in", "jitter");
```

### Biome 规则

`newBiomeRules()` 对应 UE PCG Biome Core 的数据驱动分布层。每个 layer 有空间域、
priority 和 density；重叠位置选择 priority 最大的层。全局 exclusion 在层选择前剔除
候选，资产表按 weight 稳定选择，并可设置随机 yaw 与缩放范围：

```nut
local biome = procgen.newBiomeRules();
biome.addLayer("forest", forestVolume, 10, 0.72);
biome.addAsset("forest", "oak", 4.0, 0.8, 1.2, true);
biome.addAsset("forest", "pine", 1.0, 0.9, 1.3, true);

biome.addLayer("town-park", parkVolume, 20, 0.45); // 覆盖 forest
biome.addAsset("town-park", "maple", 1.0, 0.9, 1.1, true);
biome.addExclusion(roadSpatial);

local instances = biome.generate(cellDomain, 12.0, cellSeed, 0.65);
```

输出点写入 `biome` 和 `asset` string attribute，并设置 density、uniform scale、yaw；
同一 domain/spacing/seed/rules 始终得到相同结果。规则对象复制传入的 SpatialData，
调用方销毁原对象不影响生成。`debugReport` 给出 layer、exclusion、candidate 和 output
数量。`examples/pcg-biome` 分别使用 coarse/detail 两套规则并把结果继续送入 PointGraph。

### Shape Grammar 与 Assembly

`newShapeGrammar()` 沿任意 3D polyline 连续拼装模块。一个 symbol 可以注册多个等长、
不同 weight 的资产变体；输出点携带 `module`、`asset` 和 `length` 属性，位置位于模块
中心，yaw 沿当前 Spline 切线：

```nut
local grammar = procgen.newShapeGrammar();
grammar.addModule("Wall", "stone-wall-a", 4.0, 3.0);
grammar.addModule("Wall", "stone-wall-b", 4.0, 1.0);
grammar.addModule("Post", "stone-post", 1.0, 1.0);
grammar.addModule("Gate", "gate", 6.0, 1.0);

local fence = grammar.generate("Post,[Wall,Post]*,Gate", roadSpline, 42, true);
procgen.publishInstances("assembly/fence", fence, "asset", "stone-wall-a");
```

语法支持 `A,B` 顺序、`[A,B]` 分组、`A3` 固定重复、`A*` 尽量填充和 `A+`
至少一次后尽量填充。`acceptIncomplete=false` 时必选模块放不下会失败；设为 true
则只输出完整放得下的模块，绝不产生被截断的实例。`validate` 可在编辑器保存前报告
未知 symbol、未闭合分组和非法 token。

### Graph 资产保存

`serializeDefinition()` 返回带版本头的确定性图定义，保存 topology、edge、float/int/
string 参数以及递归嵌套子图。`deserializeDefinition()` 事务式加载：格式、edge 或环校验
失败时保留当前图不变。

PointSet 和 SpatialData 被视为项目/关卡提供的外部资源槽，不内嵌进图定义；加载后通过
`setNodePoints` / `setNodeSpatial` 重新绑定。这样同一图资产可以用于不同 Cell、地形或
关卡输入，也不会把大型运行时点缓存重复写入资产文件。

editor 与 procgen 同时启用时提供 `PcgPointGraphDomain`（domain id `procgen.point`）。
它使用运行时 operation schema 自动创建通用 `GraphNodeRecord` 的 typed pins 和默认
properties，经现有 `GraphDocument` 编辑、revision 和 persistence 流程后编译成上述
PointGraph 定义。非法 pin 类型、重复节点、坏 edge 和 cycle 会产生 editor diagnostic；
procgen 被裁剪时 `EditorPcgGraph.cpp` 自动从 editor 模块源列表排除。
编译器拒绝不支持的 `schemaVersion`、未知 property、类型不匹配、非有限浮点和超出运行时
整数/浮点范围的值，避免拼写或反序列化错误静默进入运行时资产。
`PcgPointGraphDomain::migrate()` 提供事务式 schema 升级；当前迁移链把 v0 资产升级到 v1，
按运行时反射补齐新增默认 property、规范化 node pin id，并同步重写 edge 端点，同时保留
document revision 与稳定 node/edge id。`compile()` 会自动执行迁移并返回
`editor.pcg.migrated-v0-v1` 信息诊断；未知 operation、重复 legacy pin 或非法 properties
会以稳定 rule id 拒绝，输入资产保持不变，便于批量升级、撤销和版本控制审查。
`PcgPointGraphDomain::preview()` 在隔离的运行时图上绑定临时 PointSet 并只执行指定 output 的
祖先节点，不会污染资产缓存或参数覆写。结果携带 document revision、最终点数，以及每个已
执行节点的耗时、输出点数、cache hit、XYZ 包围盒和平均 density，可直接驱动 graph badge、
viewport bounds 与性能热区覆盖；无效输入、绑定失败、执行失败和取消使用稳定 diagnostic。
预览默认对单节点输出实施 100,000 点硬上限；调用方可通过 `pointBudget` 调低或显式传 0
关闭限制。超限结果在进入预览 metric/cache 前释放并返回稳定 execution-failed diagnostic。
`GraphDocument::setParameters()` 接受 `{ publicName: { node: nodeId, key: parameterKey } }`
黑板对象；编译器校验节点、反射参数类型和重复目标，并把绑定写入 PointGraph 资产，运行时
实例随后可通过 `setParameterFloat/Int/String()` 覆写。

### 事务式 hot reload

命名 `ProcgenContext` 是一次完整重建的 staging 区。`publish` 会复制输出，
`commitSystem` 成功后才原子替换该系统的上次快照；`fail`、`abortSystem`、脚本异常或
未提交的 context 都不会破坏旧结果：

```squirrel
function rebuildForest(seed) {
    local contextResult = procgen.beginSystem("forest", seed);
    if (!contextResult.ok) throw contextResult.status.summary;
    local ctx = contextResult.value;
    try {
        local pointsResult = procgen.sampleGrid(32, 20, 2.0, ctx.seedFor("trees"), 0.8);
        if (!pointsResult.ok) throw pointsResult.status.summary;
        local points = pointsResult.value;
        local treesResult = procgen.selfPrune(points, 1.8);
        if (!treesResult.ok) throw treesResult.status.summary;
        local trees = treesResult.value;
        ctx.trace("self prune", points.getCount(), trees.getCount(), 0.0);
        if (!ctx.publish("trees", trees)) throw ctx.getError();
        local commitResult = procgen.commitSystem(ctx);
        if (!commitResult.ok) throw commitResult.status.summary;
    } catch (error) {
        ctx.fail(error.tostring());
        local rollbackResult = procgen.commitSystem(ctx);
        if (!rollbackResult.ok) throw rollbackResult.status.summary;
        // 失败并关闭 staging；旧快照仍然有效
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
local contextResult = procgen.beginCachedSystem("forest", seed, key);
if (!contextResult.ok) throw contextResult.status.summary;
local ctx = contextResult.value;
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
local treesResult = procgen.selfPrune(candidates, 32.0);
if (!treesResult.ok) throw treesResult.status.summary;
local trees = treesResult.value;
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

- `abort()`、`abortSystem()`、`add()`、`addObject()`、`addObjectAt()`、`appendTransformed()`、`applyToLayer()`、`autotileGrid()`、`beginSystem()`、`buildMesh()`、`clear()`、`clearObjects()`、`commitSystem()`、`copyGroup()`、`deriveSeed()`、`empty()`、`excludeRadius()`、`fail()`、`fill()`、`filterDensity()`、`filterHeight()`、`generate()`、`generateHeightmap()`、`generateImage()`、`generateNormalImage()`
- `generatePbrMaterial()`、`generateTexture()`、`generateTo()`、`getAlgorithmCount()`、`getAlgorithmId()`、`getCell()`、`getDetail()`、`getFloat()`、`getHeight()`、`getInt()`
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
