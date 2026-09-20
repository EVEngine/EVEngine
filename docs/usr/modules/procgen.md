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

相邻 cell 必须作为同一可见更新提交时，使用
`synchronizeCellInstancesAtomic(prefix, runtimes, requests, assetAttribute, defaultAsset)`。
`runtimes[i]` 与 `requests[i]` 一一对应；函数先从每个 `RuntimeGeneration` 取得当前完整快照，
再通过 Scene capability 离线准备全部目标树。只有所有 cell 的 PointId、batch id 和 revision
都通过校验后才统一提交。返回值是实际更新的 cell 数；返回失败时没有任何参与 batch 被修改。
这一入口刻意使用脚本数组表达事务集合，不引入 UE 风格的公开 PCG 图 DSL。

cleanup 边界同时退出多个 cell 时，先调用
`completeCellCleanupAtomic(prefix, runtimes, requests)`。它先验证所有 Scene batch 和 scheduler ticket，
再在 Scene 尚未切换可见状态的 prepare 窗口内一次提交各 RuntimeGeneration，最后以不可失败步骤
切换 Scene 并发送生命周期回调。任一 Scene 准备、stale ticket、重复 cell、错误 scheduler 或 provider
失败都会使 Scene 与全部 runtime 保持原状。`removeCellInstancesAtomic` 和
`RuntimeGeneration.completeCleanupsAtomic` 仍可用于只管理单一 owner 的底层流程。

大半径、多 level 或多个 source 不应让 `refreshGenerationSources()` 一次扫描完整覆盖区域。
设置 `setRefreshWorkBudget(candidateCells)` 后，source 更新会启动稳定快照规划；脚本逐帧调用
`continueGenerationRefresh()`，并用 `isRefreshPending()` 与 `getCommittedRefreshRevision()` 观察进度。
规划完成前生成/清理队列保持旧快照，完成后统一切换；规划期间的新 source 更新合并到下一快照，
不会不断重启当前工作。预算 0 保留原同步语义。

owner thread 上分阶段生成取得 `nextGenerate()` request 后，应在昂贵阶段之间调用
`isRequestCurrent(request)`。视点、frustum 或 source 集合变化使 cell 不再需要时，scheduler 会立即
换发 ticket；查询随即返回 false，调度方可停止后续噪声、网格或资产构建。后台 worker 只持有值语义 job，
不得直接读取 scheduler；即使后台计算继续，最终
`completeGeneration()` 仍会拒绝过时 ticket。`getCancelledGenerationCount()` 和 `debugReport()` 中的
`cancelledGeneration` 用于观察这类被提前淘汰的在途工作。

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

#### 读回已有地形文件（`loadTerrainFile` / `loadTerrainBytes`）

`bakeTerrainAsset` 写出的是内存 `ByteData`，脚本没有字节写入口；反过来，脚本可以**读**
已有地形文件并直接拿到可采样的高度场：

```squirrel
// format 传 "auto"（默认）按 magic 判定，也可显式传 "evtr" / "evtrn"。
local loaded = gen.loadTerrainFile("assets/terrain/ridge.evtrn", "auto");
if (!loaded.ok) throw loaded.status.summary;

local hm = loaded.value;        // 与 newHeightmap 同类型的 ProcgenHeightmap
local width = hm.getWidth();    // 也作为 loaded.width / loaded.height 返回
local spacing = loaded.hasSpacing ? loaded.spacingX : 2.0;   // EVTR 不存米/格
local height = hm.sampleBilinear(3.0, 4.0);                  // EVTRN 已是米
```

支持的两种持久化编码：

| 编码 | 来源 | 高度表示 | 自带 `spacing` |
| --- | --- | --- | --- |
| `EVTR` | `TerrainAsset::bake` 的分块归档（magic `EVTR`） | UNORM16，按归档 header 的 `minHeight`/`maxHeight` 反量化为米 | 否（`hasSpacing == false`） |
| `EVTRN` | `eve.terrain/1` 资产旁的 `heightfield.bin`（magic `EVTRN\0\1\0`） | 原始 float32 米 | 是 |
| `raw8` | Pcg 无头方形 RAW | UNORM8，X 外层/Z 内层 | 否 |
| `raw16le` | Pcg IBM byte order RAW | 小端 UNORM16，X 外层/Z 内层 | 否 |
| `raw16be` | Pcg Mac byte order RAW | 大端 UNORM16，X 外层/Z 内层 | 否 |

返回值除 `value`（高度场代理）外还带 `spacingX`、`spacingZ`、`hasSpacing`、`minHeight`、
`maxHeight`、`format`、`width`、`height`。`loadTerrainBytes` 同上，但直接吃内存字节
。RAW 没有 magic，必须显式指定格式；解码器要求精确平方样本数，避免 Pcg 使用 ceil(sqrt) 后读取文件尾外。
（`file(path,"r").read()` 得到的 Squirrel 字符串是二进制安全的）。

失败时返回结构化 `Result`：文件缺失是 `not_found`，magic 不匹配是 `parse_error`，无法识别的
magic 是 `unsupported`，尺寸与字节数不一致同样是 `parse_error`——都不会返回半填充的高度场。
`examples/level-designer` 用它实现「引用已有地形资产」，格式说明见该例的
`assets/terrain/README.md`。


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

PhotoMode 的流式设置由 `PcgTerrainStreamingAuthority` 连接到上述缓存。调用方通过
`setTarget(cache, worldUnitsPerSample)` 借用缓存并声明一个高度采样点对应的世界尺度，
再用显式 authority 租约接管 `m_pcgLoadRange` 与 `m_pcgImpostorRange`。两个值保留
Pcg 的世界空间半径语义，转换到 chunk 半径时向上取整；`streamAround()` 使用常规范围
驱动真实块驻留，`tierForDistance()` 则将距离分为 Regular、Impostor 和 Unloaded。
缓存必须比 authority 活得更久，销毁或换场景前应先解除目标或销毁 authority。

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
p.setString("leafMode", "clusters");       // clusters | cards | canopy | none
p.setFloat("leafDensity", 0.75);
p.setFloat("height", 6.0);
p.setFloat("crownRadius", 2.0);

local treeResult = gen.buildMesh("mesh.tree", p);
if (!treeResult.ok) throw treeResult.status.summary;
local tree = treeResult.value;
```

四种叶片模式：

- `clusters`（推荐）：在每根结果枝的合适位置放置一个"叶片丛"。每个叶片丛是一个球体，内部由
  `clusterPlanes` 张绕 Y 轴分层旋转、各自带轻微倾斜和偏移的叶片平面组成；平面上的叶片中心来自
  蓝噪声（Poisson-disk）采样，所以叶片分布均匀，没有成团和空洞。叶片顶点法线取自丛球面
  （`normalRounding=1`），因此一堆平面卡片仍然按球体受光，得到有体积感的树冠，而不是一片片
  各自为政的平板。透明区域不产生任何几何体——空的地方就是没有叶片，既省几何体也避免了
  半透明排序问题。叶片同时输出两个绕向，因此背面剔除的管线也不会把它剔掉。
- `cards`：沿枝条随机撒独立叶片，最省事但容易看出成团/空洞。
- `canopy`：用椭球叶片团块堆出树冠，最便宜、最"低多边形"。
- `none`：只生成枝干骨架。

叶片丛参数：

- `clusterSize`：丛半径占 `crownRadius` 的比例，默认 `0.30`。
- `clusterSeparation`：相邻丛心的最小间距（单位：丛半径），默认 `0.55`。调大→丛更少更大，
  调小→丛更多更碎。
- `clusterPlanes` / `clusterCaps`：叶片平面数（默认 `10`）与封住两极的近水平面数（默认 `2`）。
- `clusterTilt`：环形平面偏离竖直方向的最大倾角（度），默认 `26`。
- `clusterLeafScale`：丛内单片叶长相对 `leafSize` 的比例，默认 `0.85`。
- `clusterSpacing`：蓝噪声中心间距（单位：叶长），默认 `0.80`；`leafDensity` 在此基础上缩放。
- `clusterLeaves`：每张平面的叶片数上限，默认 `28`。若请求的间距会超出该上限，采样器会放大
  间距而不是截断，所以留下的叶片依然均匀。
- `clusterLimit`：单棵树的丛数上限，默认 `120`。

丛心按"随机顺序 + Poisson 排斥"从结果枝锚点中挑出：枝条越均匀地填满树冠，叶片丛就越均匀，
不会扎堆在先生成的枝条上；`clusterLimit` 则给出几何体量的硬上限。

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
- UE PCG 扩展：`clearCache()`、`clearGenerationSources()`、`clearParameterOverride()`、`continueGenerationRefresh()`、`disconnect()`、`exposeParameter()`、`getActiveCellCount()`、`getAssetCount()`、`getAssetName()`、`getCacheHitCount()`、`getCellRevision()`、`getCommittedRefreshRevision()`、`getCompiledSegmentCount()`、`getComputeBufferReuseCount()`、`getComputeDispatchCount()`、`getComputeFallbackReason()`、`getComputeMinimumPoints()`、`getComputePeakBufferBytes()`、`getComputePolicy()`、`getComputeReadbackCount()`、`getComputeUploadCount()`、`getDirectionWeight()`、`getExclusionCount()`、`getExecutionCount()`、`getExecutionNodeBudget()`、`getExecutionPlanBuildCount()`、`getFailedCellCount()`、`getFrameTimeBudget()`、`getFrustumBehindRadius()`、`getFrustumHalfAngle()`、`getGeneratingCount()`、`getGenerationSourceCount()`、`getGenerationSourceId()`、`getInputNode()`、`getLastFusedTransformCount()`、`getLayerCount()`、`getLayerDensity()`、`getLayerName()`、`getLayerPriority()`、`getLevelCellSize()`、`getLevelCleanupRadius()`、`getLevelCount()`、`getLevelGenerationRadius()`、`getMaxActiveCells()`、`getMaxGenerating()`、`getMaxGenerationRetries()`、`getMaxY()`、`getMetricBackend()`、`getMetricCount()`、`getMetricMilliseconds()`、`getMetricNodeId()`、`getMetricOutputCount()`、`getMinY()`、`getModuleCount()`、`getModuleSymbol()`、`getNodeCount()`、`getNodeId()`、`getNodeOperation()`、`getOperationInputCount()`、`getOperationParamCount()`、`getOperationParamDefault()`、`getOperationParamKey()`、`getOperationParamKind()`、`getParameterCount()`、`getParameterFloat()`、`getParameterInt()`、`getParameterKind()`、`getParameterName()`、`getParameterString()`、`getPendingCleanupCount()`、`getPendingGenerateCount()`、`getRefreshWorkBudget()`、`getRevision()`、`getTicket()`、`getVariantAsset()`、`getVariantCount()`、`getVariantLength()`、`hasCell()`、`hasLayer()`、`hasModule()`、`hasNode()`、`hasParameterOverride()`、`intersectSpatial()`、`isFrustumCullingEnabled()`、`isMetricCacheHit()`、`isRefreshPending()`、`pointData()`、`refreshGenerationSources()`、`removeLayer()`、`removeModule()`、`removeNode()`、`requestCancel()`、`resetCancellation()`、`retryFailedCells()`、`setComputeMinimumPoints()`、`setComputePolicy()`、`setExecutionNodeBudget()`、`setMaxActiveCells()`、`setMaxGenerationRetries()`、`setNodeString()`、`setParameterFloat()`、`setParameterInt()`、`setParameterString()`、`setRefreshWorkBudget()`、`wasCancelled()`。

## 使用要点

### 高度图印章与顺序蒙版（CPU）

`ProcgenHeightmap.blendMask(source, mode, strength, invert)` 在接收者上顺序组合
同尺寸蒙版，返回标准 Result，`value` 是变化的采样数。`mode` 为
0 Multiply、1 Maximum、2 Minimum、3 Add、4 Subtract。先按 `invert`
选择 `source` 或 `1-source`，执行运算，再按 `[0,1]` 内的 `strength`
从原值插值；中间值不钳制，所以连续调用的顺序有意义。

`TerrainStampSettings()` 是脚本持有的参数对象：

- `setGrid(originX, originZ, spacingX, spacingZ)` 设置接收高度图的世界原点与采样间距。
- `setCenter(x, z)`、`setSize(width, depth)`、`setRotation(radians)` 设置印章位置和旋转。
- `amplitude`、`baseHeight`、`blendStrength` 为可写浮点字段，默认值分别为 1、0、0.5。

脚本浮点参数使用 `1.0` 这样的浮点字面量；当前 VM 为单精度。C++ 世界坐标
配置保留 double，脚本不能提供超出 VM 数值精度的坐标。

`ProcgenHeightmap.applyStamp(stamp, settings, operation, localMask, globalMask)`
返回标准 Result，`value` 为变化的采样数。`operation` 为 0 Raise、1 Lower、
2 Blend、3 Set、4 Add、5 Subtract。Raise/Lower 取高度包络；Blend 使用
`blendStrength`；Add/Subtract 对已有高度加减印章高度。

印章和两个蒙版使用同一旋转矩形内的 UV，分别双线性采样，分辨率可不同。
局部蒙版作用于 `stamp * localMask`，再乘 `amplitude`、加 `baseHeight`；
全局蒙版钳制到 `[0,1]` 后控制最终高度变化。值为 1 的单像素蒙版表示无过滤。
旋转矩形外不变；地形 `(x,y)` 的世界位置为
`(originX+x*spacingX, originZ+y*spacingZ)`，相邻瓦片可共享边界坐标。

```squirrel
local terrain = procgen.newHeightmap(65, 65).value;
local stamp = procgen.newHeightmap(2, 2).value;
stamp.setHeight(0, 0, 0.0); stamp.setHeight(1, 0, 0.0);
stamp.setHeight(0, 1, 0.0); stamp.setHeight(1, 1, 1.0);
local one = procgen.newHeightmap(1, 1).value;
one.setHeight(0, 0, 1.0);
local settings = eve.TerrainStampSettings();
settings.setCenter(32.0, 32.0);
settings.setSize(40.0, 24.0);
settings.setRotation(0.3);
settings.amplitude = 12.0;
local applied = terrain.applyStamp(stamp, settings, 0, one, one);
assert(applied.ok);
```

调用同步执行，不保留传入对象。调用者必须独占目标高度图；同一对象作为输入
与输出也按修改前数据计算。输入尺寸、有限值、枚举与参数先检查，结果溢出也
拒绝整个操作，不留下部分写入。内存分配失败按 C++ 分配异常处理。
该接口使用原生地形高度单位，不模拟 Unity packed height、自适应基底或
MixHeight；本接口自身不创建编辑器撤销记录，事务历史由拥有式 workspace/session 提供。

### 蒙版生成与曲线纹理

以下都是 `ProcgenHeightmap` 方法，修改接收者并返回标准 Result（`value` 为变化
的采样数）。运算先构建候选结果，再统一发布；源、曲线和目标允许别名。
所有输入必须有限，曲线必须是非空的一行高度图。

- `transformMask(source, curve)`：以源标量为 UV，通过 Clamp/Bilinear 曲线纹理变换。
- `rangeMask(source, minimum, maximum, filterCurve, strengthCurve)`：先计算
  `smoothstep(minimum, maximum, source)`，再依次采样过滤曲线和强度曲线。
  `minimum < maximum`；用世界高度范围即可生成高度适应度。
- `deriveSlope(heights, spacingX, spacingZ, heightScale)`：从按世界间距缩放的高度
  导数生成 `1-normal.y`，**不是坡度角度**。输出可继续传入 `rangeMask`。
  内部用中心差分，边缘单边差分；单采样轴的导数为零。分块处理时应提供
  邻接 halo 再裁切，不能把没有邻块的边缘差分当成跨瓦片连续性保证。
- `distanceMask(settings, axis, filterCurve, strengthCurve)`：距离过滤后再应用
  强度曲线。`axis` 为 0 圆、1 X、2 Z、3 圆角方形。

`TerrainDistanceMaskSettings()` 提供浮点字段 `offsetX`、`offsetZ`、`scaleX`、
`scaleZ`、`rotation`（弧度）、`roundness`，以及布尔字段 `tiling`。
默认偏移和旋转为零，缩放为 1，圆角指数为 0.5，不重复。缩放非零、圆角指数
严格为正。圆角方形范围外的过滤值为零，仍需经过强度曲线，因而不一定输出黑色。

曲线按 GPU 纹理坐标采样：第 i 个 texel 中心在 `(i+0.5)/width`。因此两像素
曲线 `[0,1]` 在 UV=0.25 处为 0、0.5 处为 0.5、0.75 处为 1，不能视作
两个端点之间的普通线性函数。可以提供原包导出的 256×1 曲线数据；目前没有
实现 Unity AnimationCurve 资产解析。距离蒙版的输出 UV 也取像素中心。

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/procgen/`](../../../src/modules/procgen/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `procgen`；跨模块 hex 关卡见 [`test/hex_level_simulation.cpp`](../../../test/hex_level_simulation.cpp)、[`test/hex_level_data.cpp`](../../../test/hex_level_data.cpp)、[`examples/hex-levels/`](../../../examples/hex-levels/)。

### Terrain contrast

`ProcgenHeightmap.applyContrast(mask, strength, featureSize)` returns a structured Result with the changed sample count. C++ exposes `applyTerrainContrast` in `TerrainEffect.h`. The mask must match the destination dimensions and contain values in [0,1]; strength is finite and nonnegative (values above 1 extrapolate; Pcg defaults to 2), and featureSize is a nonnegative texel offset. Fractional offsets use clamped bilinear sampling. The nine-tap average weights the center and axial samples by 1 and diagonal samples by 0.75 (total weight 8). The output is `height + 0.5 * (height - average) * strength * mask`.

The operation reads an immutable snapshot, including when the mask aliases the destination, and publishes atomically. Invalid inputs and float overflow leave the destination unchanged. Heights remain in native caller units without Unity packing or saturation. Filtering across tiles requires a supplied neighbor halo and cropping. Execution is synchronous with exclusive destination access and no retained references, callbacks, RNG or time dependency.

### 地形效果（CPU）

`TerrainEffect.h` 中的原生操作均同步执行，借用输入，不保留指针、回调或外部状态。
目标必须由调用者独占；先计算完整候选再发布，失败不留下部分高度修改。
Squirrel 接口返回标准 Result，`value` 为最终变化的采样数。蒙版与目标尺寸相同，
混合权重在 `[0,1]`；重复效果中的蒙版始终使用调用开始时的数据，允许输入别名。
这批操作没有新 ECS、持久数据格式、可选服务、时钟或 RNG。

- `applySmooth(mask, settings)`，参数对象 `eve.TerrainSmoothSettings()`：
  `radius=10`（非负 texel 间距）、`verticality=0`（`[-1,1]`）、`strength=1`。
  先水平后垂直，每遍为中心加七对加权采样。`verticality=-1` 只降低、`1` 只抬高。
  保留原 Shader 两遍都使用 X 方向 texel 大小的行为：非方形栅格的垂直采样间距
  为 `radius*height/width`。Clamp/Bilinear 边缘寻址；提供 halo 后裁切时须保持
  该采样比例，不能假定不同宽高比的独立块自动无缝。
- `applyRidges(mask, settings)`，参数对象 `eve.TerrainRidgeSettings()`：
  `mixStrength=0.5`、`exponent=16`、`strength=1`、`minimum=0`、`maximum=0.5`、
  `passes=18`。每遍先做水平区间幂变换，再将更新后的值用于垂直区间幂变换，
  与四邻域均值混合，最后按蒙版插值并裁剪。`passes` 是实际应用次数，
  Pcg 编辑器迭代参数 N 对应 `floor(N)+2` 次；0 次保持不变。
  裁剪在蒙版插值之后，蒙版为零也会裁剪超出 bounds 的值。
  世界高度调用者须设置 `minimum/maximum`；默认值对应原 Shader 的标量域。
- `applyTerrace(mask, settings)`，参数对象 `eve.TerrainTerraceSettings()`：
  `count=100`、`bevel=0`、`strength=1`。count 是高度单位的倒数，必须为正。
  高度乘 count 后按最近偶数舍入；只有正方向余量严格大于 `1-bevel` 时保留
  原高度，随后插值。没有加入原源码中已注释掉的 jitter 或未使用的外侧 bevel。
  半整数舍入遵循 [HLSL round 文档](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-round)。
- `applyPower(mask, power)`：指数是 `4-power`，不是 power 本身。
  不自动归一化输入；遮罩内负高度，以及指数非正时的零高度，返回失败以避免
  未定义幂运算。零权重样本不求幂。超出有限 float 范围也拒绝整个操作。
- `applyHeightCurve(mask, curve, minimum, maximum)`：曲线为非空一行 LUT，
  使用 `smoothstep(minimum,maximum,height)` 和 Clamp/Bilinear texel-center 采样。
  曲线值乘 `maximum-minimum` 后与原高度按蒙版插值；保留原公式不加回 minimum
  的行为。minimum 必须小于 maximum。
- `applyHeightMix(local, global, settings)`，参数对象 `eve.TerrainHeightMixSettings()`：
  `minimum=0`、`maximum=1`、`midpoint=0.5`、`strength=0.5`、
  `clipMinimum=0`、`clipMaximum=0.5`。增量为
  `(local-midpoint)*(maximum-minimum)*strength*global`，加到原高度后裁剪。
  local 为有限标量，可超出 `[0,1]`；global 和 midpoint 为单位范围，strength
  可大于 1（原编辑器允许到 2）。允许 minimum 等于 maximum，表示零高度跨度。
  global 为零仍执行最终裁剪。

```squirrel
local target = procgen.newHeightmap(33, 33).value;
local mask = procgen.newHeightmap(33, 33).value;
for (local y=0; y<33; ++y) for (local x=0; x<33; ++x) {
    target.setHeight(x, y, x == 16 && y == 16 ? 0.5 : 0.0);
    mask.setHeight(x, y, 1.0);
}
local smooth = eve.TerrainSmoothSettings();
smooth.radius = 1.0;
assert(target.applySmooth(mask, smooth).ok);
local terrace = eve.TerrainTerraceSettings();
terrace.count = 100.0;
assert(target.applyTerrace(mask, terrace).ok);
```

所有算子保留调用者传入的标量域，不自动模拟 Unity 的高度纹理打包。非线性
效果若要与原参数对应，调用方必须提供对应的高度标量域和裁剪范围。这里处理
已光栅化的整个操作区域；旋转笔刷、范围外保留及多瓦片的上下文仍由上层提供。
CPU 使用固定采样顺序和 double 中间值，float 存储；跨平台/CPU-GPU 比较需使用
数值容限，不承诺位级一致。这些接口尚不能作为原版 GPU 或可视化验收的证据。

### 八邻域热侵蚀

`ProcgenHeightmap.applyThermal(sediment, settings)` 对接收高度图和指定泥沙累计图
同时计算，再统一发布；返回标准 Result，`value` 是任一输出发生变化的网格数。
C++ 入口为 `TerrainErosion.h` 中的 `applyTerrainThermal`。两个目标必须是尺寸
相同的不同对象，由调用者独占；高度为非负有限值，泥沙允许有限的负值。
输入或中间结果无效、浮点溢出、内存分配失败，都不发布任何一个输出。

`eve.TerrainThermalSettings()` 字段：`spacingX=1`、`spacingZ=1`、`heightScale=1`、
`reposeSlope=11.430052`、`dt=0.00025`、`iterations=3`。间距和高度比例严格为正；
坡度阈值、dt、迭代数非负。reposeSlope 是休止角的正切，不是角度值。
例如默认值约为 tan(85°)。dt 是调用者注入的热侵蚀子步时长；对齐 Pcg 的
组合模拟时，它等于 thermalTimeDelta * hydroTimeDelta。没有隐式时钟或 RNG。

每个子步读取旧高度快照，用 Clamp 邻域的四个正向样本和四个对角样本求坡度。
仅绝对坡度严格大于 reposeSlope 的高度差参与求和；对角权重为 0.707，正向
权重为 1。差值先乘 heightScale。移动量为 `clamp(dt*sum/16,-height/2,height/2)`，
从高度扣除并加到泥沙图中。泥沙正值表示移除，负值表示加回，不能将它直接
解释为非负悬浮泥沙浓度。零高度格子的移动上限也为零，这是原公式的行为。

本接口保留原 Thermal.compute 的标量语义，不替换现有 TerrainPipeline 的
保守 talus 重分配。高度与泥沙之和在每格保持平衡（受 float 舍入影响），不
承诺仅高度图的总质量守恒。重复调用可传回这两张输出继续迭代。无保留引用、
回调或跨帧可变状态；同步调用期间不能并发观察或修改任一目标。仍不代表完整
水力/热侵蚀联合管线或 Unity GPU 对照已经通过。

### 水流状态与分阶段计算

`eve.TerrainWaterField()` 创建一个独占拥有水深、速度和四向通量的空状态。
`reset(width, height, depth)` 返回 Result，成功后重置网格及均匀水深，并清空速度
和通量；尺寸乘积必须不超过 int 上限。`getWidth()` / `getHeight()` 返回尺寸。
`advance(terrain, settings)` 推进一步，成功返回任意字段发生变化的网格数。
它只借用地形，既不修改也不保留地形引用，暂不进行泥沙溶解或沉积。

`sample(x,z)` 返回 Result，成功 value 是独立的值记录，包含 `depth`、`velocityX`、
`velocityZ`、`fluxRight`、`fluxLeft`、`fluxBottom`、`fluxTop`，不是内部缓冲区引用。
越界或空状态查询明确失败。C++ 类型为 TerrainWaterField，内部使用 Pimpl；
禁止复制，可移动，移出后的对象为空且可 reset。脚本持有状态直到释放对象，
无需外部注册或跨域 Link。reset/advance 要求独占访问；空闲时才允许只读查询。

`eve.TerrainWaterSettings()` 提供以下有限浮点参数：

- `spacingX=1`、`spacingZ=1`、`heightScale=1`、`waterScale=1`，必须为正。
- `dt=0.05`，非负，0 表示不改变状态；没有隐式时钟或 RNG。
- `precipitation=0.000004`、`evaporation=0.000004`，非负。
- `flowAcceleration=-0.00049`，有符号值，对应原调用方的 flowRate*gravity。

参数是 Shader 接收到的值，不隐式重复 UI 转换。Pcg 调用方对 UI 雨量/蒸发
乘 1e-5，对 flowRate 乘 1e-4 再乘 gravity，对 simulationScale 乘 0.001
再与 terrain texel spacing 相乘。保留原公式的流量符号和 clamped 邻域。

计算明确分为完整通量场、速度与水深更新两个阶段，避免源 GPU 核同次 dispatch
内读取尚未完成的邻格通量。平均水深严格为零时速度定义为零。保留原源码最后
以旧水深加降雨减蒸发覆盖输运水深的行为，因此它不是保守浅水模拟器。
内部 double 中间计算、float 存储，固定阶段顺序；不承诺与原版存在竞争读写的
GPU 核位级等价。任意输入错误或溢出均不提交已算出的局部通量/速度/水深。

```squirrel
local flow = eve.TerrainWaterField();
assert(flow.reset(terrain.getWidth(), terrain.getHeight(), 0.0).ok);
local water = eve.TerrainWaterSettings();
water.precipitation = 0.000008;
assert(flow.advance(terrain, water).ok);
local sample = flow.sample(0, 0);
assert(sample.ok);
print(sample.value.depth + "\n");
```

水力侵蚀仍需接入泥沙、热侵蚀调度与诊断层；不能以此水流阶段宣称完整水力模拟
已经移植，或替代渲染水面的波浪/反射/折射功能。

`eve.generateTerrainWaterFlowMap(target,source,settings)` 对应 Pcg 独立的旧版
`WaterFlowMap.CreateWaterFlowMap`，不复用上述水流状态。`TerrainWaterFlowMapSettings` 字段为
`dropletVolume`、`absorptionRate`、`smoothIterations`。它从每个非边缘 texel 投放水滴，按 X-major、
Z-minor 顺序查找八邻域中严格更低的第一个点；没有低点时只抬高私有地形副本。每一步总是沉积完整
absorptionRate，因此最后一步可以超过初始体积。完成后按原 `HeightMap.Flip` 交换 X/Z 轴，再执行
顺序相关的原地四邻域平滑并钳制到 [0,1]。矩形输入要求目标尺寸为 source height × source width。
输入无效、数值不再前进或溢出时目标保持不变；API 无随机数、隐式时间或保留引用。

`eve.generateTerrainVelocityFlowMap(target,source,iterations)` 对应同一 Pcg `HeightMap` 类型的另一项
`FlowMap` 运算。它从每格 `0.0001` 水量开始，以源码固定 `TIME=0.2` 累积左、右、下、上四向通量，
限制单步总流出不超过当前水量，更新邻格流入后从最终通量计算二维速度幅值，并按整图 min/max 归一化；
范围小于 `1e-12` 时输出全零。目标必须同尺寸，可以与 source 为同一对象；负迭代或任何非有限中间值
返回失败并保留目标。该 API 与 WaterFlowMap 水滴追踪及 `TerrainWaterField` 水力状态均为独立合同。

### 去除高度图台阶

`eve.analyzeTerrainTerraces(classes,source,settings)` 与
`eve.removeTerrainTerraces(target,source,classes,settings)` 移植 Pcg 4.2.2
`HeightmapTerraceRemover` 的两阶段工具。分类值为 0=Black、1=Red、2=Green、3=Blue，优先级与源码一致：
terrace、flat、mountain、slope terrace、black。边缘保持 Black。

`TerrainTerraceRemovalSettings` 暴露 `perlinScale`、`perlinStrength`、
`slopeTerraceThreshold`、`flatThreshold`、`verticalGradientThreshold`、
`minimumTerraceThreshold`、`maximumTerraceThreshold`、`excludeRed`、`excludeBlack`、
`terrainWorkflow` 和 `noiseSeed`。处理顺序固定为：选中区域 3×3 平均、梯度衰减 Perlin 噪声、
`1/2/1` 三向滤波、sigma=1 的 5×5 高斯模糊。`terrainWorkflow=true` 保留源码
`0.00042/0.0035` 的噪声强度比例。EVEngine 使用具名 seed 的原生 Perlin 实现，保持确定性；Unity
`Mathf.PerlinNoise` 的私有排列不作为跨引擎逐像素合同。

两个调用都先完成验证与候选计算再发布。apply 允许 target 与 source 相同，但分类图必须是独立对象；
失败不会留下部分平滑结果，也不保留任何输入引用。

### HeightMap 曲率与坡向派生图

`eve.generateTerrainHeightmapCurvature(target,source,mode)` 精确对应 Pcg `HeightMap.CurvatureMap`，mode
沿原枚举为 0=Average、1=Horizontal、2=Vertical。它以归一化像素间距计算 dx/dy、dxx/dyy、dxy，按
Pcg 水平/垂直曲率公式限制到 ±10000，再映射至 [0,1]。平坦或退化分母按源码得到中值 0.5。

`eve.generateTerrainHeightmapAspect(target,source,mode)` 对应 `HeightMap.Aspect`，mode 为 0=Aspect、
1=Northerness、2=Easterness。完整坡向除以 360；北向性和东向性分别用 cosine/sine 映射到 [0,1]。
实现保留源码自定义 Sign 和边缘 clamp 规则，包括平坦样本的 0.5 full-aspect 结果。

两项都要求至少 2×2 的有限同尺寸栅格，允许 target/source 别名，先读取完整旧快照并在成功后原子发布；
非法枚举、尺寸或数值失败不会修改目标。它们是 `HeightMap.cs` CPU 派生图，不等同于 shader 风格的
`generateCurvatureMask` 径向模糊操作。

### HeightMap 邻域修复

`eve.filterTerrainHeightmapNeighborhood(target,source,radius,mode)` 对应 Pcg `HeightMap.DeNoise`、
`GrowEdges` 与 `ShrinkEdges`，mode 为 0、1、2。DeNoise 只处理拥有完整邻域的内部像素，把离群值限制到
邻居最小值与最大值之间；GrowEdges/ShrinkEdges 在边界跳过越界邻居，并把像素向邻域极值移动一半。

实现刻意保留 Pcg 的 X 优先、Y 次优先原地遍历语义：同一调用中靠后的像素会读取已经更新的靠前像素。
target 可以与 source 相同；实现先复制输入快照，在全部验证成功后发布，因此非法半径、枚举、尺寸或非有限值
不会留下部分结果。DeNoise 要求 radius 至少为 1。

`eve.smoothTerrainHeightmap(target,source,iterations)` 对应四邻域 `HeightMap.Smooth`，保留边缘 clamp、
[0,1] clamp 和每次写回立即影响后续 X→Y 样本的行为。`eve.smoothTerrainHeightmapRadius(target,source,radius)`
对应缩放滑窗版本，像源码一样把 radius 至少提升到 5，并保留每列第一个完整窗口只计算而不写回的行为。
`eve.convolveTerrainHeightmap(target,source,kernel)` 对应原地卷积；只有完整覆盖 kernel 的像素会改变，结果
限制到 [0,1]，近零 divisor 使用 1。为避免 Unity 方法对畸形数组的越界行为，原生 API 明确要求有限、
奇数边长的正方形 kernel。

`eve.generateTerrainHeightmapSlope(target,source)` 精确对应 `HeightMap.SlopeMap`：使用归一化像素间距、
边缘重复采样、源码固定 0.5 高度比例和 `g/sqrt(1+g²)` 映射。它与旧 `GetSlopeMap` 的前向差分不同；
后者在最后一行/列直接访问越界，因此没有作为安全公共 API 暴露。

`eve.quantizeTerrainHeightmap(target,source,divisor)` 对应标量 `HeightMap.Quantize(float)`，使用 Unity
`Mathf.Round` 的 midpoint-to-even 规则。除数必须有限且非零；别名安全，失败不修改 target。

### HeightMap 栅格代数

`eve.applyTerrainHeightmapScalarArithmetic(target,source,operand,operation,clampResult,min,max)` 和
`eve.applyTerrainHeightmapRasterArithmetic(target,source,operand,operation,clampResult,min,max)` 覆盖 Pcg
的 Add、Subtract、Multiply、Divide 及全部 Clamped 重载；operation 依次为 0–3。栅格尺寸不同时，右操作数
按 Pcg 的 `x/targetWidth`、`y/targetHeight` 坐标和源尺寸双线性采样。原生合同拒绝除零和非有限结果，
避免把 C# Infinity 写入后续地形管线；错误时 target 保持不变。

`eve.lerpTerrainHeightmap(target,source,values,mask)` 对应 `HeightMap.Lerp`，values 和 mask 可各自采用不同
尺寸并按相同规则重采样；插值量按 `Mathf.Lerp` 限制到 [0,1]。三项均允许 target 与任一输入别名。

`eve.transformTerrainHeightmap(target,source,transform,parameter)` 覆盖基础 `HeightMap.Invert`、
`Normalise`、`Power`、`Contrast`，transform 为 0–3。Normalise 对常量图保持原值；Power 的 parameter 是
源码直接指数，不是 terrain effect 中的 `4-power`；Contrast 使用 `(h-0.5)*parameter+0.5` 且不隐式 clamp。

`eve.copyTerrainHeightmap(target,source,mode)` 对应 `HeightMap.Copy`，mode 为 0=AlwaysCopy、
1=CopyIfLessThan、2=CopyIfGreaterThan；条件比较以 target 调用前的当前值为基准。异尺寸 source 使用 Pcg
归一化坐标重采样。`eve.copyTerrainHeightmapClamped(target,source,min,max)` 对应 CopyClamped，先采样再限制。
两项都以 target 尺寸为输出权威，允许 source/target 别名，非法枚举或区间失败时不修改 target。

`eve.flipTerrainHeightmap(target,source)` 对应 Pcg `HeightMap.Flip` 的矩阵转置，会把 W×H 输出调整为
H×W；它不是水平或垂直镜像。实现先复制 source 并构造完整候选，因而支持原地调用。

`eve.measureTerrainHeightmap(source,measure)` 统一提供当前样本的 Minimum、Maximum、Sum、Average、
BaseLevel（measure 0–4）。Sum/Average 保留 Pcg X→Z 顺序的 float 累加；BaseLevel 从 0 开始扫描四条边，
因此全负边界仍返回 0。每次调用直接读取权威样本，不要求调用者维护 Unity 的 dirty 统计缓存。

`eve.quantizeTerrainHeightmapTerraces(target,source,startHeights,curves)` 对应 Pcg
`Quantize(float[],AnimationCurve[])`。startHeights 是 N×1 严格递增栅格；curves 是 W×N 栅格，每行以
线性采样表示一条 Unity 曲线。区间仍从最高 terrace 向下搜索，因此公共边界归入较高层。输入高度必须由
首个 start 到 1 完整覆盖；这把 Unity 的负索引异常转成原子 InvalidArgument。

`eve.measureTerrainHeightmapSlope(source,x,y,mode)` 暴露 Pcg 三个同名但不同单位的点查询：mode 0 是
整数格点向 +X/+Z 的梯度长度；mode 1 是归一化坐标、0.9 texel 中心差并乘 10000 后限制到 [0,90]；
mode 2 是归一化坐标四向绝对高差平均再乘 400。整数模式要求右/下邻居存在，归一化模式要求坐标在 [0,1]。

`eve.fillTerrainHeightmap(target,value)` 对应 `SetHeight` 并将统一高度限制到 [0,1]；
`setTerrainHeightmapSafe` 把越界整数坐标限制到最近边界后写入。`setTerrainHeightmapRow` 的 row 固定 X、
需要 target-height 长度的一维 strip；`setTerrainHeightmapColumn` 固定 Z、需要 target-width strip，与 Pcg
命名和数组方向一致。`resetTerrainHeightmap` 把对象清为 0×0。所有非 reset 写入均先验证并原子发布。

`eve.sampleTerrainHeightmapSafe(source,x,z)` 将整数坐标限制到最近边界，对应 `GetSafeHeight`。
`sampleTerrainHeightmapNormalized(source,x,z)` 接受 [0,1] 坐标并严格采用 Pcg 的 `x*width,z*depth`
双线性规则；这与 EVEngine `sampleBilinear` 的像素坐标合同不同。`heightmap.hasData()` 验证正尺寸及完整
样本存储，`heightmap.isPowerOfTwo()` 要求两个维度均为正二次幂。

Pcg 的 MinVal/MaxVal/SumVal、UpdateStats 和 dirty 标志是同一 Unity 编辑器缓存协议。EVEngine 的
measure API 每次读取权威样本，因此不会暴露可能过期的统计缓存或手工 dirty 开关。元数据由带
schema/version 的 TerrainAsset、TerrainFile 或上层资产定义拥有，不附着无结构字节到通用 Heightmap。


### 泥沙与联合侵蚀

`heightmap.applyLegacyDistributedErosion(settings)` 对应 `HeightMap.Erode`：只扫描内部格点，若最大
四向下降量位于阈值区间，就从中心移走其一半，并按所有正下降量的比例同步分配到四邻域。
`TerrainLegacyDistributedErosionSettings` 提供 minimumThreshold、maximumThreshold 和 iterations。

`heightmap.applyLegacySteepestErosion(hardness, settings)` 对应活跃的 `HeightMap.ErodeThermal`：按 X 外层、
Z 内层原地扫描，严格依次比较下、左、右、上邻居，将最大下降量的一半乘 `(1-hardness)` 后移给首个
最陡邻居。hardness 可使用不同尺寸，并按 Pcg 的归一化宽深双线性规则采样。
`TerrainLegacySteepestErosionSettings` 提供 iterations、talusMinimum 和 talusMaximum。
两个调用都原子发布、使用显式迭代数，且不依赖隐式时钟或随机数。

`eve.TerrainLegacyHydraulicSettings()` 与
`heightmap.applyLegacyHydraulic(sediment, hardness, rain, settings)` 对应旧版
`HeightMap.ErodeHydraulic`。设置包含 `iterations`、正整数 `rainFrequency` 与 [0,1]
`sedimentDissolveRate`。每隔 rainFrequency 轮加入 rain；四向通量保留跨轮状态并使用固定 TIME=0.2；
更新水深后，按八邻域严格下降量与 `(1-hardness)` 搬运泥沙，地形限制到 [0,1]，泥沙保留有符号值。
该调用先快照输入并私下完成所有迭代，成功才同时发布地形与泥沙；无隐式时钟或随机数。
它与下面的 GPU 来源 Water→Sediment→Thermal 管线及 `WaterFlowMap` 水滴累积图是三套独立合同。

`eve.TerrainSedimentSettings()` 提供 `effect=-1`、`depositRate=0.00004`、
`bankDeposit=1`、`bedDeposit=5`。这些是实际计算系数，不是 Pcg UI 的缩放前数值。

- `heightmap.applySediment(sediment, velocityX, velocityZ, waterSettings, sedimentSettings)`
  返回结构化 Result，成功值为两个输出中发生变化的格子数。
- `waterField.advanceHydraulic(heights, sediment, waterSettings, sedimentSettings, thermalSettings, iterations)`
  顺序执行水流、泥沙、热侵蚀，并返回三个所有者中发生变化的格子数。
  任一轮失败均保留调用前的地形、泥沙和水流状态。

所有栅格必须同尺寸且有限，地形非负；地形与泥沙必须是不同对象。
调用同步执行、无回调，调用方负责线程串行访问；不保留传入对象的引用。
独立泥沙操作允许速度栅格与输出别名，始终读取调用前快照。
`waterSettings.dt` 控制沉积和回溯；热侵蚀使用独立的 `thermalSettings.dt`，
若匹配 Pcg 调度，应由调用方先乘水流时间步。

该泥沙算子保留参考源码的特殊权重、混合坐标索引与叠加行为，
不是标准双线性输运：无速度且无沉积时非负泥沙每轮翻倍，即使 dt 为零。
源码硬度固定为零，溶解项无效，因此不暴露不起作用的溶解系数。
平坦地形的未定义归一化明确取坡度零，越界整数采样取零。
联合调度始终读取最新完整阶段并发布最后一轮，未复制参考 C# 固定输出缓冲区
及重置热侵蚀缓冲索引的问题；不承诺与原 Unity GPU 执行逐位相等。


`waterField.exportChannel(target, channel)` 将一个完整水流通道原子写入同尺寸、
有限的目标栅格，返回变化格子数的 Result；不调整尺寸，不保留引用，失败不修改目标。
通道编号为 0 深度、1 X 速度、2 Z 速度、3 右向通量、4 左向通量、5 下向通量、6 上向通量。
输出保留原始符号和单位，不取绝对值、不归一化。调用需独占目标且禁止并发修改水流场。
Pcg 的 SimpleHeightBlend 读取红通道，因此原 WaterVelocity 遮罩对应 1，
WaterFlux 对应 3，而不是速度长度或总通量。导出栅格可直接传给现有
`transformMask` / `blendMask` 操作；完整 PaintContext UV 与原始地形混合仍需上层组合。


### 空间笔刷混合

`eve.TerrainBrushBlendSettings()` 配置独立的地形与笔刷仿射 UV。
地形字段为 `heightXX`、`heightXZ`、`heightZX`、`heightZZ`、
`heightOffsetX`、`heightOffsetZ`；笔刷字段为 `brushXX`、`brushXZ`、
`brushZX`、`brushZZ`、`brushOffsetX`、`brushOffsetZ`。
两个矩阵默认单位矩阵，偏移默认零，`strength=1`。
坐标公式为 `(XX*u+XZ*v+offsetX, ZX*u+ZZ*v+offsetZ)`，输入 UV 是目标像素中心。

`target.blendBrush(oldHeights, newHeights, brush, settings)` 返回变化格子数的 Result。
三个输入可独立分辨率，采用 Clamp/Bilinear 像素中心采样；笔刷 UV 超出闭区间
[0,1] 时输出采样后的旧地形，区间内输出 `lerp(old,new,strength*brush)`。
权重不钳制，支持参考公式的外插；输入已是原生标量，不做 Unity 高度打包。
所有栅格和系数必须有限，溢出不提交；目标可与输入别名，读取调用前快照。
调用同步、无回调、不保留引用，调用方需独占目标并保证输入不被并发写入。
这是 SimpleHeightBlend 的空间组合步骤，侵蚀遮罩的反转和后续滤镜仍需显式组合。


### 强度滤镜与侵蚀遮罩组合

`target.applyStrength(source, curve, mode, strength, invert)` 对同尺寸输入先查一行曲线，
再按 invert 取 `1-curveValue`，然后计算模式结果，最后从输入向结果插值。
mode 为 0 替换、1 最大值、2 最小值、3 相加、4 相减。参考 shader 将 0 命名为
Multiply，但实际公式是替换曲线值，不是相乘。strength 必须在 [0,1]；结果不钳制。

`target.generateErosionMask(oldHeights, erosion, brush, spatial, curve, mode, strength, userInvert)`
先执行空间笔刷混合，再执行强度滤镜，按 Pcg 侵蚀分支将滤镜反转设为 `!userInvert`。
erosion 是已计算的泥沙或通过 exportChannel 导出的右向通量/X 速度；此调用不推进模拟。
spatial 为 TerrainBrushBlendSettings，曲线为有限非空一行栅格。
两步整体原子提交，后一步失败也不会留下前一步结果；返回变化格子数的 Result。
输入与目标允许别名，使用调用前快照；同步独占目标、输入不可并发修改，不保留引用或调用回调。
该组合采用原生标量，仍需调用方根据世界/地形范围提供 UV 映射和模拟参数。


### Grow/Shrink 遮罩

`target.growShrinkMask(source, curve, distance)` 返回变化格子数的 Result。
source 与 target 同尺寸且有限，curve 为有限非空一行强度曲线。
distance 是世界距离除以操作范围所得的 UV 半径，正值增长、负值收缩；
`2*abs(distance)*sourceWidth` 必须不超过 int 最大值以表达采样索引。
两个方向步长均为 `1/sourceWidth`，X 外层、Z 内层遍历；每个候选值先按径向
smoothstep 与当前累积值混合，再做最大/最小选择，因此不是普通形态学滤波。
越界候选跳过，采样使用 Clamp/Bilinear 像素中心；最终经过 curve，距离零也不省略曲线。
整数索引和 double 偏移明确定义采样序列，不复现 shader 浮点累加误差。
最坏采样成本随半径增长；实现跳过确定越界区间，但不改变区间内的访问顺序。
失败保留目标，别名输入读取快照。同步独占目标、不可并发修改输入，无回调或引用留存。


### 曲率遮罩

`eve.TerrainCurvatureSettings()`：`radius=0.001` 为 UV 模糊半径，`worldUnits=-400`
缩放有符号高差，`intensity=0.7` 为正幂指数，`steps=32` 为每方向样本数，
`directions=16` 为方向数。半径非负，采样数为正，乘积必须小于 INT_MAX。

`target.generateCurvatureMask(input, heights, curve, settings, mode)` 返回变化格子数的 Result。
目标与 input 同尺寸，heights 可独立分辨率，curve 为有限非空一行强度曲线。
中心及各径向样本平均后，计算 `clamp(abs(pow((origin-blur)*worldUnits,intensity)),0,1)`，
再查曲线并按 mode 混合：0 相乘、1 最大、2 特殊 Smaller、3 相加、4 相减。
Smaller 的原公式是 `filter < 1-input ? filter : input`，可能增加输入，不能当作普通 min。
该 shader 不使用 strength 或 invert。负底数的非整数幂显式失败；没有提前对底数取绝对值。
正幂溢出的无穷幅值按曲率饱和公式变为 1，最终标量输出溢出则不提交。
径向循环以整数索引固定样本数，不承诺原 shader 浮点循环的逐位等价。
所有栅格和系数必须有限。输出别名读快照，失败原子保留目标；同步独占目标，
输入禁止并发修改，不保留引用、无回调。径向模糊设计的 MIT 归属与许可保留在实现中。


### 凹凸遮罩

`eve.TerrainConcavitySettings()` 提供 `featureSize=10`（高度纹素距离）与
`concavity=1`（有符号凹凸系数）。featureSize 为正有限且可表示为 uint32；
邻域偏移取整数截断值，边缘衰减使用未截断值。

`target.generateConcavityMask(input, heights, curve, settings, mode)` 返回变化格子数的 Result。
目标与 input 同尺寸，heights 的坐标映射在两个轴上都使用 heightWidth/inputWidth，
映射坐标须可表示为 uint32。曲线是有限非空一行数组，读取
`floor(clamp(value,0,1)*(curveWidth-1))`，不做双线性曲线采样。
mode 为 0 相乘、1 最大、2 最小、3 相加、4 相减，未加入 strength 或 invert。
原计算的无符号邻域运算及包含 width/height 的上界保持；越界纹素读零，
零长度梯度定义为零。边缘衰减后再倍增、钳制并查曲线。
使用 double 中间值，不承诺原 GPU 逐位一致。所有输入有限，失败不提交目标；
别名读调用前快照，同步独占目标，无回调或引用留存，输入不得并发修改。


### 图像遮罩

`eve.TerrainImageMaskSettings()` 提供 `offsetX=0`、`offsetZ=0`、`scaleX=1`、
`scaleZ=1`、`rotation=0`（弧度）、`tiling=false`，以及选择颜色 `red=1`、
`green=1`、`blue=1` 和 `accuracy=0`。缩放不能为零，accuracy 在 [0,1]，所有值有限。

`target.generateImageMask(input, red, green, blue, alpha, curve, settings, filter, mode)`
接收四个同尺寸有限 RGBA 标量平面，独立于目标分辨率；目标和 input 同尺寸。
返回变化格子数的 Result，目标可与任意输入别名，所有失败均不提交。
filter 为 0 RGB 最大通道（不是亮度）、1 Lab 颜色选择、2 红、3 绿、4 蓝、5 Alpha。
mode 为 0 相乘、1 最大、2 最小、3 相加、4 相减，不额外使用 strength/invert。

图像在目标像素中心做中心偏移、缩放和旋转后双线性采样；平铺保留源码负整数
映射到 1 的约定。不平铺时范围外筛选值为零，再经过一行 curve，因此最终未必为零。
颜色选择保留源码 sRGB→XYZ→Lab 与 CIE76 距离，距离钳制到 [0,100]；
匹配条件严格为 `difference < (1-accuracy)*100`，命中后得到
`1-difference/100*accuracy`。accuracy=1 连相同颜色也不命中。
传入色彩数值不做隐式解码或额外色彩空间转换，Alpha 不参与 Lab 距离。
调用同步独占目标，输入不得并发修改；无回调和引用留存，图像解码/GPU 资源仍由调用方管理。


`eve.generateTerrainImageMaskFromImage(target, input, imageData, curve, settings, filter, mode)`
是完整宿主的 ImageData 适配入口，返回变化格子数的 Result。目标可与 input/curve 别名。
它按 ImageData 当前像素格式读取 RGBA 浮点平面，再调用相同图像遮罩核心；
不额外做 sRGB 转换或垂直翻转。输入须为可读取且存储完整的图像，非有限像素、
无效设置和尺寸不匹配都不会修改目标。同步独占目标，图像及输入不得并发修改；
不保留 ImageData、像素指针或 GPU 资源。完整宿主需要 image 模块，核心裁剪配置
继续使用 generateImageMask 的四平面接口，不隐式提供图像解码回退。

### 碰撞遮罩栈

`eve.TerrainCollisionMaskStack()` 接收调用方已经从场景、树或对象缓存烘焙出的有限标量栅格。
`addLayer(mask,type,active,invert)` 深拷贝一层；type 为 0=RadiusTree、1=RadiusTag、
2=LayerGameObject、3=LayerTree。`getLayerCount()` 包含禁用层，`clear()` 清空所有层。

`apply(target,input,curve,mode)` 对每个输出像素按 Clamp/Bilinear 重采样所有 active 层，从白色开始依次
取最小值，随后应用一行强度曲线和 ImageMask blend。Radius 类型在 `invert=true` 时反转；Pcg 的
Layer baked texture 以白色表示占用，因此 Layer 类型使用相反的 invert 约定。目标与 input 尺寸一致，
各层可使用不同分辨率；输入会被快照，允许 target alias。非法栅格、类型、模式或有限范围失败时不
修改 target。栈不持有场景对象或渲染缓存；几何查询和缓存失效由场景/provider 负责。

### 多边形笔刷遮罩

`eve.TerrainPolygonMask()` 按顺序拥有控制节点。`addNode(worldX,worldZ,radius,strength)` 复制有限世界
坐标、正半径和 strength 元数据；`getNodeCount()` 返回节点数，`clear()` 清空。`rasterize(target,brush,
originX,originZ,spacingX,spacingZ,type)` 将节点绘制到目标网格，type 为 0=Open、1=Closed。Open 只绘制
节点笔刷；Closed 在至少三个节点时先用 Pcg `SH_PolygonFill.shader` 相同的奇偶射线规则填白多边形，
再按节点顺序叠加笔刷。每次叠加钳制上限到 1，并把小于 0.25 的结果清零。笔刷红通道由 Heightmap
标量表示，节点方形范围外明确为零，范围内使用 Clamp/Bilinear 纹理中心采样。

Pcg 4.2.2 的 `PolyMask.cs` 会把 `PolyMaskNode.Strength` 传给 `ApplyBrushStroke`，但该函数和
`SH_AdditiveBrush.shader` 都不设置或读取 strength；EVEngine 保留字段以便资产转换，同时按该版本
可观察行为不以它缩放笔刷。地形射线吸附、编辑器选择、可视化网格和缓存失效属于编辑器/provider，
核心不持有场景对象。输出先在私有缓冲完成后一次发布，非法网格、类型、非有限值或间距失败不修改目标。

### WorldBiome baked-mask cache

`eve.TerrainBakedMaskCache()` 以 `(terrainId,maskGuid)` 为稳定复合键拥有已烘焙有限 Heightmap。
`store` 深拷贝或原子替换条目并标记 fresh；`markDirty(maskGuid)` 使所有地形上的同 GUID 条目显式 stale；
`copyMask` 仅导出 fresh 条目，并按目标分辨率使用 Clamp/Bilinear 采样。missing 与 dirty 都返回结构化
NotFound，且保留旧输出。provider 完成世界图规则重算后再次 `store` 即重建条目。`erase` 删除精确复合键，
`clear` 清空全部缓存，`getEntryCount` 包含 dirty 条目。

这对应 Pcg `GetWorldBiomeMask`/`BakedMaskCache` 的缓存、跨分辨率 Graphics.Blit 与 GUID 失效边界。
世界图规则求值和磁盘资产读取属于上层 provider；缓存不持有场景、Terrain、GPU texture、回调或文件句柄。
provider 先销毁时 dirty/missing 明确失败，cache 先销毁时只释放其值副本；热重载通过 markDirty 后重新 store，
不会把旧图无声当作当前结果。

`ProcgenHeightmap.applyGlobalSpawnerMask(input,source,curve,settings,mode)` 对应 ImageMask 的
`GlobalSpawnerMaskStack` 分支。source 是被引用 Spawner 的完整蒙版栈输出；接口把标量复制为通道语义，
强制选取 Red，再复用 ImageMask 的 UV 偏移、缩放、旋转、tiling、强度曲线和五种 blend。settings.filter
不会改变标量结果，其余字段按 `generateImageMask` 验证。调用只借用 source，不持有另一个 Spawner 或
RenderTexture；目标与 input 可别名，失败时不发布部分结果。

### NoiseMask

`ProcgenHeightmap.generateNoiseMask(input,curve,settings,type,mode)` 生成 NoiseMask 并立即按强度曲线与
五种 ImageMask blend 合并。type 为 0=Perlin、1=Value、2=Billow、3=Ridge、4=Voronoi。
`TerrainNoiseMaskSettings` 提供 `translationX`、`translationZ`、`scaleX`、`scaleZ`、弧度 `rotation`、
`octaves`、`amplitude`、`frequency`、`persistence`、`lacunarity`、`warpIterations`、`warpStrength`、
`warpOffsetX`、`warpOffsetZ` 与显式 `seed`。
octaves 和 warpIterations 支持 Pcg fBm 风格的分数最后一步，范围为 `[0,16]`；默认值来自包内
PcgNoiseSettings/FbmFractalType。输出按固定扫描与固定 hash 流确定，不读取帧时钟或全局随机状态。

本实现复用 EVEngine `NoiseField` 的 CPU hash/Perlin 基础，因此保留 Pcg 的五类形态、TRS、fBm 与 warp
参数语义，但尚不声明与 Unity HLSL `sin` hash 逐像素相同；跨后端比较采用数值与形态容限。所有输入
先验证并在私有缓冲计算，非法枚举、零 scale、非有限参数或溢出不修改目标。

### Smooth ImageMask

`ProcgenHeightmap.smoothMask(input,verticality,blurRadius)` 对应 ImageMask 的 Smooth 分支。它按
`SmoothHeight.shader` 固定七对权重先水平再垂直处理；`verticality=-1` 只降低，`0` 取加权平均，`1`
只抬高。垂直 pass 保留原 shader 使用 X texel size 的行为，非方形栅格也按该约定采样。shader 虽读取
HeightTransformTex，但最终返回值没有使用它，因此该入口不接受强度曲线或额外 blend。


### 世界坐标到笔刷 UV

`eve.TerrainSampleGrid()` 描述高度纹理的样本中心，`width=1`、`height=1`；
`setOrigin(x,z)` 设置首样本世界坐标，`setSpacing(x,z)` 设置正采样间距，默认均为 1。

`spatial.configureWorld(world, contextWidth, contextHeight, heightGrid)` 将世界配置转换为
TerrainBrushBlendSettings 的两个 UV 矩阵，返回变化系数数目的 Result。
world 是 TerrainStampSettings：其 grid 描述目标首样本与间距，center/size/rotation
描述笔刷世界矩形；高度操作、amplitude/baseHeight/blendStrength 不参与映射。
heightGrid 描述传给 blendBrush 的旧高度纹理，spatial.strength 保持不变。

转换显式处理半像素偏移，使相同网格生成单位高度 UV 映射；旋转以 X/Z 平面逆映射
将世界位置变为笔刷坐标。C++ 先在 double 中计算原点差，再转换为有限 float 系数；
Squirrel 当前是 float VM，传入大绝对坐标前需调用方完成世界原点偏移，不能恢复已丢失精度。
尺寸、间距和笔刷范围必须为正，坐标有限；跨度或系数溢出时不修改 spatial。
调用同步独占 spatial，不保留输入引用，也不会移动地形或修改任何栅格。


### 地形生成会话

`eve.TerrainGenerationSession()` 创建独立、初始为空的地形会话。支持记录原生印章和下述七种标量地形效果。
`reset(baseline)` 复制有限基线并清空历史；`stamp(stamp, settings, operation, localMask, globalMask)`
复制印章、设置和两个遮罩为命令输入，成功后追加记录。操作编号与 applyStamp 相同。
`copyTerrain(output)` 将权威地形复制到同尺寸有限栅格；修改这个副本不会改变会话。

`undo()`、`redo()` 改变已应用历史前缀并从基线回放，`replay()` 重算当前前缀。
`setOperationEnabled(index, enabled)` 切换命令并重算；`operationEnabled(index)` 返回布尔值 Result。
`getOperationCount()` 包含待重做的后缀，`getAppliedCount()` 包含前缀中的禁用命令。
index 只是会话内索引，不是跨重置/分支的稳定 ID。撤销后成功追加新操作会截断重做后缀，
追加失败不会截断；切换早期命令导致后续溢出时，历史和地形都保持原样。

`setAccess(access)` 返回 void Result，0 为 Editable、1 为 Locked；`getAccess()` 返回当前策略。
锁定拒绝 reset、stamp、undo、redo、replay 和命令开关，允许读取快照和解锁。
其余修改操作返回变化地形样本数 Result，reset 返回基线样本数，失败返回结构化错误。
所有状态由会话单独持有，公共读取为副本；调用在所属线程串行执行，无外部引用留存或回调。
撤销/回放复制候选状态后完整重算，内存和耗时随历史输入大小增长；同一构建下用固定输入顺序
保证浮点重算一致性，不依赖隐式时钟/RNG。不承诺跨平台逐位一致。
这不是现有分块点集 RuntimeGeneration 调度器的替代，也不包含后台线程或其他操作种类。

`snapshotJson()` 写出严格的 `eve.procgen.terrain-generation-session` schema version 1，拥有 baseline、
全部命令输入与设置、enabled 标志、已应用 cursor 和访问策略。`restoreJson(json)` 拒绝未知根字段、
版本、非法枚举、非有限数值、尺寸/数量越界、截断或尾随 payload；它先以全部命令启用验证完整历史，
再按保存的 enabled/cursor 重建最终地形、泥沙和七个水通道，成功后一次交换。Pending 会话和 Locked
目标拒绝恢复；快照不会保存临时 replay 候选，恢复后的调度状态为 Idle。

调用方可用 `beginReplay()` 启动分步重放，再用 `stepReplay(maxOperations)` 按正数命令预算推进。
状态枚举为 0=Idle、1=Pending、2=Completed、3=Cancelled、4=Failed；`getReplayStatus()` 和
`getReplayCompletedOperations()` 可轮询当前生命周期与已访问命令数。Pending 阶段只修改私有候选，
`copyTerrain`、`copySediment` 和 `exportWater` 继续返回最后一次已发布状态；完成全部命令时三类数据
一次发布。`cancelReplay()` 丢弃 Pending 候选，失败也不修改权威状态。Pending 时拒绝历史编辑、访问
策略切换和第二次 begin；取消或终态之后可以重新开始。这提供与 Pcg coroutine 对应的显式可观察
调度边界，但由所属线程逐步驱动，不创建后台 worker，也不跨线程调用脚本。


地形会话还提供 `contrast(mask,strength,featureSize)`、`smooth(mask,settings)`、
`ridges(mask,settings)`、`terrace(mask,settings)`、`power(mask,power)`、
`heightCurve(mask,curve,minimum,maximum)`、`heightMix(localMask,globalMask,settings)`。

`eve.bakeTerrainCurveTexture(output, curve)` 将单行 Heightmap 曲线 LUT 写入单行 ImageData，
对应 Pcg `TerrainToolsUtility.AnimationCurveToRenderTexture`。采样坐标为 `i / outputWidth`；
为保持源实现行为，像素 0 保持透明黑，但返回范围从曲线在 0 的值开始计算。成功 Result 的
`value` 包含 `minimum`、`maximum` 和 `writtenPixels`。输入必须是非空单行图像与有限单行曲线；
失败不会改变输出图像。
这些接口使用对应的 Terrain*Settings 和原生效果公式，复制栅格、曲线与设置作为历史输入；
成功返回变化地形样本数 Result。所有命令共用锁定、禁用、撤销、重做和原子回放契约。
修改调用方的遮罩或曲线不影响已有命令。混合历史重算失败时也不修改命令开关和当前地形。
会话快照记录足以重建水流/泥沙状态的完整命令输入，但仍不记录异步资源操作。


### TerrainGenerationSession 侵蚀历史

`reset(baseline)` 现在同时初始化零泥沙、零水深、零速度和零通量。标量地形命令保留
已有模拟状态；热侵蚀和水力侵蚀使用历史中前一步的状态。所有输出与历史一起原子发布，
撤销、重做、禁用操作和回放覆盖全部通道。共享的内部候选结果仅为不可变快照，没有对外
可写别名。调用者仍须在所属线程串行访问；时间步长和迭代次数显式传入。

- `TerrainGenerationSession.resetSimulation(depth)`：记录可撤销的模拟重置，清零泥沙、
  通量和速度，将水深设为有限非负值；地形不变。需要一次全新侵蚀时先调用它。
- `TerrainGenerationSession.thermal(settings)`：记录热侵蚀，累积泥沙，保留水流状态。
- `TerrainGenerationSession.hydraulic(water, sediment, thermal, iterations)`：记录完整水流、
  泥沙、热侵蚀迭代；复制设置，延续已有模拟状态，不隐式应用 Unity UI 单位换算。
- `TerrainGenerationSession.copySediment(output)`：复制当前有符号泥沙到匹配尺寸的有限高度图。
- `TerrainGenerationSession.exportWater(output, channel)`：复制原始水流标量通道；整数通道
  0=Depth、1=VelocityX、2=VelocityZ、3=FluxRight、4=FluxLeft、5=FluxBottom、6=FluxTop。

新增修改方法均返回 `Result<int>` 的**地形变化样本数**；模拟状态改变但地形不变时可为 0。
输出方法返回输出变化样本数，锁定时仍可读；修改方法在锁定时拒绝执行。
非法输入、后续命令失败或分配异常不会提交部分模拟状态或截断重做历史。
会话 reset 清空历史；resetSimulation 是历史中的命令，两者语义不同。
该实现沿用前文记录的原生侵蚀内核约定，不复刻源 C# 包装层固定输出缓冲索引错误，
不声明与未定义的 GPU 调度行为逐位一致。会话仍无异步世界创建或植被资源历史；分步重放
只调度本会话已拥有的地形/侵蚀命令。


### TerrainDetailLayer 整数草地密度

`TerrainDetailLayer` 是独立的整数细节密度所有者，不是 `Grid2D` 的语义编号层。
一个实例对应一张完整对齐的地形细节图，不持有资源注册表或图形对象。

- `TerrainDetailLayer.reset(width, height, count)`：初始化正尺寸与非负整数数量，返回单元数。
- `TerrainDetailLayer.getWidth()`、`TerrainDetailLayer.getHeight()`：查询尺寸，空实例返回 0。
- `TerrainDetailLayer.sample(x, z)`：返回 `Result<int>` 的数量副本。
- `TerrainDetailLayer.apply(fitness, settings, mode, seed)`：从匹配尺寸的有限 Heightmap 生成密度，
  返回变化单元数。mode 为 0=Replace、1=Add、2=Remove；seed 为显式有符号 32 位随机种子。
- `TerrainDetailSettings.minimumFitness`：严格适应度下限，默认 0.5，范围 [0,1]。
- `TerrainDetailSettings.fadeStart`：低于此值按适应度概率稀疏，默认 0.6，范围 [0,1]。
- `TerrainDetailSettings.density`：规则密度乘全局密度后的有效值，默认 16，有限非负且小于 INT_MAX。
- `TerrainDetailSettings.namespaceId`：稳定资源层身份；零为兼容默认层，非零用于同一 terrain 上独立保存和清理多个 detail prototype。
- `TerrainDetailPlacementSettings.densityNamespaceId`：选择要导出为实例的密度资源层；`namespaceId` 继续定义输出点及其 `spawnNamespace` 来源身份。

Replace 先清零目标资源层；Add/Remove 保留未通过适应度或随机门槛的单元。通过的单元按
`InverseLerp(minimumFitness,1,fitness)*density` 增减，然后钳制到 [0,density]，
按最接近的偶数舍入。这意味着 Add 也可能降低原先超过目标密度的数量，这是源码行为。
随机稀疏采用源码 XorshiftPlus（种子 0 按 1 处理，负种子按 uint32 位模式），X 外层、
Z 内层遍历，只在适应度位于淡出带时抽样。每次 apply 重建独立 detail-thinning 流；
同构建、相同整层输入和种子可重复，不承诺分割区域后仍与单次完整层调用一致。
所有输入先验证，候选数量计算完再提交；失败或分配异常不改变原层。
调用者在所属线程串行访问，不保留借用，也不提供可写数量视图。
资源层由稳定 namespace 拥有；实际草叶生成和渲染通过下述 PointSet 导出及 graphics 接口完成。


### 草地密度到 PointSet

`eve.exportTerrainDetailPoints(output, layer, heights, settings)` 把 TerrainDetailLayer 的
每个整数数量展开为现有 PointSet，成功返回发射点数，失败保持原 output 不变。
高度图可以与密度图分辨率不同，端点覆盖同一世界矩形，双线性取样后乘高度比例。
输出使用现有 `asset` 字符串属性，并记录 `detailCell`（行优先单元编号）和
`detailOrdinal`（单元内序号）。资源引用只传递给下游，不在此加载资产。

`TerrainDetailPlacementSettings` 字段：

- `originX`、`originZ`：世界矩形起点，默认 0。
- `width`、`depth`：正的世界宽度、深度，默认 1。
- `heightScale`：有限高度比例，默认 1。
- `minimumScale`、`maximumScale`：正的实例缩放范围，默认均为 1。
- `seed`：位置、旋转、缩放三个独立命名随机流的显式 32 位种子，默认 1。
- `namespaceId`：非零实例身份命名空间，默认 1；实际场景须按资源/瓦片分配不同值。
- `maxPoints`：显式输出预算，默认 1000000，允许 0。超限拒绝整个输出，不截断。
- `asset`：非空资源引用，必须由调用者指定。

ID 来自命名空间、单元和单元内序号，与总密度及遍历行号无关。修改其他单元不会
移动或重编号现有点；改变网格尺寸或命名空间改变身份范围。零 ID 保留，极端命名空间
映射为零时明确失败，调用者应改用其他命名空间。位置采用单元内原生随机分布，
朝向使用 Y 轴角度、法线向上；这部分替代 Unity 内部实例化，不宣称 Unity 位置一致。
原输入只在本次调用借用，输出在私有 PointSet 完成后一次移动发布；没有场景对象
所有权或回调。调用者在所属线程串行访问所有输入输出。


### PointSet 草地渲染接入

`eve.bakeTerrainGrass(field, points, asset, width, height)` 是完整宿主适配器：按 `asset`
属性精确选择 PointSet 中的实例，保留其根位置与独立横向/纵向缩放，上传到既有 GrassField，
返回 `Result<int>` 的上传数量。无匹配实例时清空已发布网格。非空资源选择器、有限
根位置、正宽高、非零稳定 ID 以及大于 0.001 的横向/纵向缩放是前置条件；非法参数不改变
现有可见草地。X/Z 缩放必须相同；Y 缩放独立，不再丢失草地宽高差异。

内置路径采用 GrassField 四帧草地贴图；图像路径由调用者显式上传已解码资源，asset
只做分组选择。64 位身份留在 PointSet；未染色实例折叠为可精确存入 float 的 24 位
动画相位键，染色实例把 RGB 量化为 RGB8 并由局部根坐标生成动画相位，因此整体模型
变换不会改变风相位。这不是场景对象身份的替代。法线和 yaw 不参与当前向上 billboard 的构建。

C++ `GrassField::bakePoints(points,width,height)` 接受已有 `grass::Point` 数组，不再
进行 Poisson 重新采样。CPU 验证/网格构建及 GPU 创建在发布字段前完成；失败保持
原字段。新增 GPU 对象按现有约定由 Graphics 管理到其析构，包括中断上传产生的对象。
调用必须发生在 Graphics 所属线程、draw/capture 回调之外；不保留输入数组或 PointSet。
此适配器需要完整 graphics provider，核心裁剪构建只包含密度与 PointSet 算法。


`eve.bakeTerrainGrassImage(field, points, asset, width, height, image)` 使用一张已有
RGBA8 ImageData 替代内置动画 atlas。实例分组选取和失败原子性与 bakeTerrainGrass
相同，图像必须具有完整 RGBA8 存储；包括空点组也先校验图像。上传保留 RGB/alpha，
不合成帧、不使用 RGB 推断透明度，采用单帧 UV 和中性白色/灰色明暗调制。
原图仅在上传期间借用，GPU 纹理由 Graphics 拥有。C++ 对应
`GrassField::bakeTexturedPoints(points,width,height,image)`，与内置路径共享同一发布实现。
该图像 billboard 支持类型化风参数和健康/枯萎颜色；颜色 alpha 仍由纹理所有。
`eve.bakeTerrainGrassFoliage(field,points,asset,width,height,albedo,normal,mask,settings)` 提供
Pcg/PW 静态植被材质路径。三张图必须是同尺寸完整 RGBA8；遮罩通道为 metallic、occlusion、
thickness、smoothness。`GrassFoliageSettings` 控制线性色调、alpha cutoff、normal strength、相机距离
淡出和高度雪覆盖。EVEngine 使用自身方向光、环境光与级联阴影合同，未复制 Unity Standard 内部实现。
全部 DetailRenderMode 仍不属于此适配器。


独立宽高参数：`TerrainDetailPlacementSettings.minimumWidth`、`maximumWidth`、
`minimumHeight`、`maximumHeight` 默认均为 1，都是正有限乘数，最大值不得小于最小值。
分别用 width/height 命名随机流选择，与已有 minimumScale/maximumScale 统一比例相乘；
输出 X/Z 为横向比例、Y 为纵向比例。修改宽高范围不改变位置、朝向、ID 或其他单元。
乘积不能用正 float 表示时拒绝完整输出。实际物理宽高还要乘上传时传入的 billboard 宽高。

颜色参数 `healthyR`、`healthyG`、`healthyB`、`healthyA`、`dryR`、`dryG`、`dryB`、`dryA`
均为线性 [0,1] 端点；`noiseSpread` 为 [0,1]，
`noiseSeed` 为显式有符号 32 位种子。每个点按世界矩形内的归一化坐标采样确定性的平滑
格点噪声，再在 dry 与 healthy 之间插值。`noiseSpread=0` 固定取中点。该噪声是对 Unity
Terrain 内部不可见实现的原生重建，保证 EVEngine 内重放稳定，不声明与 Unity 数值逐点一致。
颜色配置不参与位置、缩放或稳定 ID；任何非法端点或 spread 都在发布前拒绝并保留旧输出。

### 地形树木放置

`eve.exportTerrainTreePoints(output, fitness, heights, settings)` 将一张归一化适应度图与独立
分辨率的高度图展开为带资源属性的 PointSet。它按 `spacing / spawnDensity` 扫描世界矩形，
并使用 Pcg XorshiftPlus 相同的条件抽样顺序执行 `failureRate`、`jitterPercent`、
`minimumFitness` 羽化、缩放随机值、Y 偏移和 0..360 度朝向。成功返回实例数；任何非法
配置、超出 `maxPoints` 或属性发布错误都保留旧 output。

`TerrainTreePlacementSettings` 包含以下脚本字段：`originX`、`originZ`、`width`、`depth`、
`heightScale`、`spacing`、`spawnDensity`、`jitterPercent`、`failureRate`、`minimumFitness`、
`snapToTerrain`、`seaLevel`、`customOffset`、`minimumYOffset`、`maximumYOffset`、
`minimumWidth`、`maximumWidth`、`minimumHeight`、`maximumHeight`、`widthRandomPercentage`、
`heightRandomPercentage`、`healthyR`、`healthyG`、`healthyB`、`healthyA`、`dryR`、`dryG`、
`dryB`、`dryA`、`bendFactor`、`boundsRadius`、`seed`、`namespaceId`、`maxPoints` 和 `asset`。

`setScaleMode(mode)` / `getScaleMode()` 使用 0=Fixed、1=Fitness、2=Random、
3=FitnessRandomized。`setYOffsetMode(mode)` / `getYOffsetMode()` 使用 0=TerrainHeight、
1=SeaLevel、2=Custom。`snapToTerrain=true` 总是采用采样地形高度；关闭后才按 Y 模式选择
地形、海平面或自定义基准，再叠加随机偏移。

输出点携带世界位置、独立宽高、yaw、适应度颜色、局部 bounds，以及 `asset`、
`treeCandidate`、`bendFactor` 属性。稳定 ID 由 namespace 和扫描单元生成；修改外观、颜色
或缩放范围不改变实例身份和位置。当前 `bendFactor` 是供树木材质/风适配器读取的下游
元数据；调用者可按原型创建 `gfx.newTreeWindShader()`、配置 `eve.TreeWindProfile`，再把同一
shader 赋给该原型的 Renderable3D。显式适配避免普通 Renderable3D 猜测资源类型或引入第二份
原型状态。

`eve.removeTerrainTreePoints(output, input, fitness, settings)` 实现 Pcg Remove：仅处理
`asset` 匹配且位于配置矩形内的点，在当前 fitness **严格大于** `minimumFitness` 时移除，
并完整保留其他资源、矩形外点、行顺序和属性。output 可以与 input 是同一 PointSet。

`TerrainTreeRescaleSettings` 与 `eve.rescaleTerrainTreePoints(output, input, settings)` 实现
原型刷新。字段 `previousMinimumWidth`、`previousMaximumWidth`、`previousMinimumHeight`、
`previousMaximumHeight` 描述上次生成区间；`minimumWidth`、`maximumWidth`、`minimumHeight`、
`maximumHeight`、`bendFactor`、`boundsRadius`、`asset` 描述新原型。它也提供
`setScaleMode()` / `getScaleMode()`。Fixed 直接采用新最小值；其他模式按旧区间
InverseLerp 并钳制到 [0,1]，再映射到新区间，与 Pcg 原型刷新逻辑一致。刷新只改变匹配
点的宽高、bounds 和 bendFactor；身份、位置、旋转、颜色及其他属性保持不变，失败不发布。

`grass::Point.widthScale` 为独立横向比例，0 表示沿用已有 height scale；直接上传时
非零值须大于 0.001。内部顶点 normal.z 保留旧 0/1 的等比编码，新横向比例 w 在
普通层编码为 -w、暗层编码为 1+w。Vulkan/WGSL 都先解码宽度和层标记，再各自应用
横向、纵向比例；正的小比例不再被 shader 强制抬到 0.05。此为运行时网格编码，
未引入持久格式或新的权威数据副本。

### 多地形高度图事务

`mapTerrainOperationMultiTile(tiles,settings,domain,worldMap,names)` 提供不修改资源的通用 C++
窗口计算。`TerrainOperationTile` 只描述名称、世界矩形、二维分辨率和地图域；返回值沿输入顺序给出
每块 tile 的 local/operation 像素矩形。`Heightmap` 域按 Pcg 规则使用 `resolution-1` 的 tile
步长并补一个共享接缝像素；`Texture`、`TerrainDetail`、`Tree`、`GameObject` 与 `BakedMask`
使用完整 resolution，边界没有重复单元。不同域因此可共享筛选、旋转 AABB、白名单和溢出检查，
同时保留各自正确的接缝语义。

`applyTerrainStampMultiTile(tiles,stamp,settings,localMask,globalMask,worldMap,names)` 是 C++
批量入口。每个 `TerrainHeightTile` 提供唯一名称、借用 Heightmap、世界原点/尺寸以及普通或
world-map 域。所有 tile 必须使用相同高度图分辨率和世界像素间距，原点按完整 tile 跨度对齐；
不同分辨率不再像 Pcg 那样静默跳过，而是返回 InvalidArgument 并保持全部 tile。

操作像素范围遵循 Pcg MultiTerrainOperation 的高度图规则：世界范围以 floor/ceil 转为像素，
使用 `resolution-1` 作为相邻 tile 偏移，并包含共享接缝像素。仅与旋转矩形 AABB 边缘接触的
邻 tile 不计入；实际修改仍由旋转 stamp 精确判断。普通与 world-map tile 分域，C++ 可传精确
名称 allow-list。报告包含 operation 矩形、各 tile 的 local/operation 矩形、受影响 tile 数与
改变样本数。

`eve.TerrainMultiTileWorkspace()` 是拥有式脚本会话。`addTile(name,heightmap,x,z,width,depth,worldMap)`
复制输入；成功 stamp 后 topology 固定。`stamp(stamp,settings,operation,localMask,globalMask,worldMap)`
先复制整个工作区，在候选 tile 上完成所有计算、报告和历史分配后整体交换。任何晚期数值或
分配失败都不会留下部分 tile。`copyTile` 只导出副本；`undo`/`redo` 恢复所有 tile 与对应报告，
在 undo 后 stamp 会截断 redo 分支。`getTileCount`、`getLastAffectedTiles`、
`getLastChangedSamples`、`getLastMappingCount`、`getOperationCount` 和 `getAppliedCount` 提供只读状态。

`applyTerrainDetailMultiTile(tiles,operationFitness,detailSettings,operationSettings,seed,worldMap,names)`
把通用窗口直接用于细节层。共享 fitness raster 的尺寸必须等于计算出的 operation 矩形；每块
`TerrainDetailLayer` 先复制后计算，全部成功才共同发布。Replace 与 Pcg 一致：受影响 tile 的旧层
先整体清零，只在映射矩形内生成；Add/Remove 保留矩形外数据。所有 tile 按描述符顺序共享同一个
显式 XorshiftPlus 流，因此淡出带的条件随机抽样不会在 tile 边界重新播种。

脚本使用 `eve.TerrainMultiDetailWorkspace()`。`addTile` 复制输入层，`apply` 接受共享 fitness、
detail settings、世界操作矩形、mode、seed 与地图域；`copyTile` 导出独立副本。`undo`/`redo`
恢复所有层及其报告，undo 后的新 apply 截断 redo 分支。只读计数接口与高度图工作区保持一致。
两个会话都单线程拥有全部状态，不保留外部 raster、场景对象、回调或隐藏时间/RNG。

`applyTerrainTreesMultiTile` 将 Tree-domain operation raster 应用于多个 `PointSet`。Add/Replace
沿用 Pcg `SetTerrainTrees` 在该阶段的追加语义，Remove 仅删除 operation 矩形内匹配 asset 且
fitness 严格超过阈值的点。所有 tile 共用 settings.seed 初始化的一个 XorshiftPlus 流；扫描范围
在局部窗口末端增加一个生成步长，使 jitter 能从 tile 外回落到边缘。全部候选 PointSet、属性和
总数完成后才共同发布。

脚本使用 `eve.TerrainMultiTreeWorkspace()` 深拷贝每块 tile 的初始 PointSet 与高度图；`apply`
的 mode 为 0=Add、1=Replace、2=Remove。`copyTile`、`undo`、`redo` 和统计接口与其他多地形
工作区一致，历史快照同时覆盖所有 tree owner。

### 地形对象规则

`TerrainObjectPlacementSettings` 对应 Pcg `ResourceProtoGameObject` 与 SpawnRule 的外层扫描；
一个 prototype 可通过 `addInstance(TerrainObjectInstanceSettings)` 添加多个子资源。每个子资源
独立配置数量区间、失败率、XYZ 偏移、地形/海平面/自定义 Y 基准、固定/随机/fitness/
fitness-randomized 缩放、XYZ 旋转、沿坡面 Y 偏移、前向坡度对齐及完整坡度旋转。

外层脚本字段包括 `originX`、`originZ`、`width`、`depth`、`heightScale`、`spacing`、
`spawnDensity`、`jitterPercent`、`startOffsetX`、`startOffsetZ`、`failureRate`、`minimumFitness`、
`minimumInstanceFitness`、`minimumDirection`、`maximumDirection`、`boundsRadius`、
`boundsCheckQuality`、`prototypeScale`、`boundsCollisionCheck`、`seaLevel`、`seed`、`namespaceId`、
`maxPoints` 与 `prototype`。子资源字段包括 `asset`、`minimumInstances`、`maximumInstances`、
`failureRate`、`minimumOffsetX`、`maximumOffsetX`、`minimumOffsetY`、`maximumOffsetY`、
`minimumOffsetZ`、`maximumOffsetZ`、`customOffset`、`commonScale`、`minimumScale`、`maximumScale`、
`minimumScaleX`、`maximumScaleX`、`minimumScaleY`、`maximumScaleY`、`minimumScaleZ`、
`maximumScaleZ`、`scaleRandomPercentage`、`scaleRandomPercentageX`、`scaleRandomPercentageY`、
`scaleRandomPercentageZ`、`minimumRotationX`、`maximumRotationX`、`minimumRotationY`、
`maximumRotationY`、`minimumRotationZ`、`maximumRotationZ`、`yOffsetAlongSlope`、
`alignForwardToSlope` 与 `rotateToSlope`。`setScaleMode` 和 `setYOffsetMode` 接受对应枚举序号。

`exportPoints(output,fitness,heights)` 使用一个显式 Pcg XorshiftPlus 流，依次执行规则失败、
半幅 jitter、中心 fitness、自碰撞、bounds 范围平均 fitness、方向、子资源数量与失败、偏移后
fitness、缩放、Y 偏移和旋转。结果进入原生 PointSet，携带 `asset`、`objectPrototype`、
`objectCandidate`、`objectResource`、法线、变换和 bounds。稳定 ID 来自 namespace、候选位置、
资源序号和实例序号；资产解析与 Scene/ECS 所有权由下游负责。`removePoints` 按 prototype、
世界矩形和严格大于阈值的 fitness 原子删除。

跨地形版本使用 `eve.TerrainMultiObjectWorkspace()`。`addTile` 深拷贝 PointSet 与高度图；
`apply(fitness,settings,operation,mode,worldMap)` 的 mode 为 0=Add、1=Replace、2=Remove。
所有映射 tile 按确定顺序共享一个 seed 随机流及一个自碰撞中心集合，因此接缝不会重新播种或
允许重叠。Replace 先删除操作窗口内同 prototype 的旧点再生成；Remove 只删除 fitness 严格大于
`minimumFitness` 的匹配点。`copyTile`、`undo`、`redo` 和统计方法与其他拥有式工作区一致。
工作区不保留外部指针，且一次操作的所有 PointSet 和历史只在成功后整体发布。

坡度对齐在 EVEngine 中由采样地形法线推导 Euler 旋转；它保持 Pcg 的选项和数据依赖，但不声明
与 Unity Quaternion 组合逐位相同。bounds 范围 fitness 使用 EVEngine 明确定义的含边界栅格采样。

### 地形纹理层权重

`eve.TerrainSplatmap()` 拥有任意层数的浮点权重。`initialize(width,height,layers,defaultLayer)`
建立唯一 topology 并把全部权重赋给默认层；`paint(paint,layer)` 以同尺寸归一化 Heightmap 覆盖
目标层，其他层保持相对比例并共同归一化。原目标层为 1、其他层全零时，剩余权重确定性均分；
单层 splatmap 只能保持权重 1。所有 texel 先在候选缓冲计算，再一次发布。

`copyLayer(layer,output)` 将一个有效层完整复制到同尺寸 Heightmap，并返回变化样本数。这是 Pcg
ImageMask `TerrainTexture` 分支的原生数据桥：调用方按稳定资源引用解析出层，再把该标量图交给
`generateImageMask` 的变换、曲线和混合阶段。层不存在或输出 topology 不匹配时不修改 output；
返回的是独立副本，后续绘制 splatmap 不会隐式改变已导出的遮罩。

`eve.TerrainTextureAlignSettings()` 以 `terrainAOriginX`、`terrainAOriginZ`、`terrainAWidth`、
`terrainADepth`、`terrainBOriginX`、`terrainBOriginZ`、`terrainBWidth`、`terrainBDepth` 保存两块
地形的世界原点和尺寸，并提供 `blendStrength`、`blendWidth`、`adjacencyTolerance`。默认相邻容差
为 Pcg 的 1.5 世界单位。`eve.alignTerrainSplatTextures(a,b,settings)` 按 Pcg `TerrainTextureAligner` 的边定义
识别 X/Z 相邻关系，在边缘向内逐行执行 SmoothStep 衰减及同步二维噪声调制的双向层混合，并逐像素
重新归一化。两张 splatmap 使用私有候选副本，只有完整成功后才同时发布；非相邻、自别名、非法
几何或参数不会留下单边修改。返回值是两张图合计发生变化的 texel 数。

C++ `paintTerrainSplatLayerMultiTile` 使用 Texture-domain 窗口将一张共享 operation raster 映射到
多块 `TerrainSplatmap`，验证不同 tile 的所有 owner、topology、目标层及操作尺寸后统一提交。
脚本 `eve.TerrainMultiSplatWorkspace()` 深拷贝每块 splatmap；`paint`、`copyTile`、`undo`、`redo`
和统计接口与高度、detail、tree 工作区采用相同事务语义，历史快照同时恢复全部纹理层。
该公式依据 Pcg `SetSplatmap` 对 Unity `CopyTerrainLayer` pass 1 的数据流重建；Unity 内置 shader
源码不在 Pcg 包中，因此不声明逐位 GPU 等价。

### 相邻地形高度缝合

`eve.TerrainHeightStitchSettings()` 提供 `extraSeamSize` 和 `maxDifference`；后者保留 Pcg profile
字段，但 Pcg 4.2.2 当前活动算法同样没有读取它。`TerrainMultiTileWorkspace.stitch(terrainA,terrainB,settings)`
按两块地形原点差选择 North、South、West 或 East，验证共享边、分辨率、采样间距和栅格对齐，再复现
`StitchBordersWithSeam`：从两侧内边界建立线性高度，向接缝逐点增加混合权重，最终用最靠近接缝的
两个点平均并写入两侧共享边。操作成功后形成一个覆盖全部 owned tile 的 undo/redo 快照。

C++ `stitchTerrainHeightmaps(a,b,settings)` 可用于已有编辑事务；它同步借用两个不同 Heightmap，先在
两个候选副本完成计算，再同时发布。无共同边、部分边未按样本对齐、非法 seam 或非有限数据不会修改输入。

`TerrainWorldWorkspace.setHeightWorldUnits(heightWorldUnits,worldHeightSpan)` 对应 Pcg Terrain Height
Adjuster。它将目标值转换为 `clamp(heightWorldUnits/worldHeightSpan,0,1)`，覆盖工作区内每块地形的
全部归一化高度，并将整个世界作为一次可撤销事务发布。成功值是实际变化的样本总数；非有限高度、
非有限或非正垂直尺寸在创建候选状态前失败。

`eve.TerrainSplatPalette()` 用 `addColor(r,g,b,a)` 按层添加归一化线性颜色，`getColorCount()` 返回
当前颜色数；颜色数必须与 splat
层数相同。`eve.bakeTerrainSplatAlbedo(output,splatmap,palette)` 将全部层权重混合进同尺寸可写
`ImageData`，成功时返回像素数。实现先写私有 ImageData 副本再 adopt，因此 palette、尺寸或格式
验证失败不会改变旧图像。生成的 RGBA8 可直接传给 `gfx.newTexture`，示例右侧 terrain 已使用此路径。

`eve.GtsHeightBlendSet()` 按 splat 层顺序拥有原始高度 raster，并通过
`addLayer(height,contrast,brightness,increase)` 保存 GTS alpha 高度变换。层数限制为 1–8，输入会复制。
`eve.applyGtsHeightBlend(output,input,set,blendFactor)` 先计算每层“变换后高度 × 原权重”，再执行 GTS 的
最大高度、transition、epsilon 与归一化公式，一次发布所有层权重。失败时 output 保持不变。

`eve.GtsPackedLayerSettings()` 配置 `triPlanar`、`stochastic`、`triPlanarSizeX`、`triPlanarSizeZ`、`tileSizeX`、`tileSizeZ`、`offsetX`、`offsetZ`、`tintR`、`tintG`、
`tintB`、`normalStrength`、`aoMin`、`aoMax`、`smoothnessMin`、`smoothnessMax`、`geoAmount` 和
`detailAmount`，以及 `heightContrast`、`heightBrightness`、`heightIncrease`、`displacementContrast`、
`displacementBrightness`、`displacementIncrease` 和 `tessellationAmount`。`GtsPackedLayerSet.addLayer` 复制每层
albedo+height 与 normal+AO+smoothness 纹理；`eve.bakeGtsPackedLayers` 按归一化 splat 权重重复双线性采样
并一次生成 albedo、renderer-ready packed normal、geological strength 和 detail strength。平面路径使用地形
局部 UV；stochastic 路径复现 GTS 的 `TriangleGrid` 与固定 sin/hash 偏移；triplanar 路径使用世界空间
`ZY/XZ/XY` 投影、`(4,15,4)` 法线指数、轴符号翻转、`0.33/0.67` 偏移，并按 C# 上传规则将 size 乘 10。
四个输出原子提交。
`eve.bakeGtsPackedLayerDisplacement(displacement,tessellation,heights,splat,layers,cameraX,cameraY,cameraZ,
tessellationMultiplier,originX,originY,originZ,spacingX,spacingZ)` 对权重最高的四层复用同一平面 UV，读取
albedo alpha 并按每层 displacement 参数变换，再分别加权位移和 tessellation amount。它复现 GTS shader
的 100000 平方距离硬截止，并在最后乘全局 tessellation multiplier；相机和地形变换均为显式输入。
两个 Heightmap 原子提交，随后传给 weather PBR 时雪位移按原 shader 顺序覆盖混合。

`eve.generateGtsGlobalBlendDistance(output,heights,cameraX,cameraY,cameraZ,blendDistance,blendRange,
originX,originY,originZ,spacingX,spacingZ)` 精确生成 GTS 共用的近远混合 raster：世界空间相机距离平方乘
`blendDistance/10000`，饱和后取 `blendRange` 次幂。相机位置和地形变换全部显式输入，便于回放时在同一
相机状态重建；失败保留旧 output。

`eve.GtsColorMapSettings()` 配置 `alphaIntensity`、`colorIntensity`、`nearIntensity` 与
`farIntensity`。`eve.bakeGtsColorMapAlbedo(output,colorMap,globalBlendDistance,settings)` 使用与输出同尺寸的
颜色图及归一化近远混合 Heightmap，按 GTS 公式限制颜色和 alpha 后混入现有 albedo。它应在天气层之前调用。
`eve.GtsMacroVariationSettings()` 配置三档 `sizeA/B/C`、最低亮度 `intensity` 和 `objectSpace`；
`eve.bakeGtsMacroVariationAlbedo(output,map,settings,originX,originZ,spacingX,spacingZ)` 按
`sizeA/1000`、`sizeB/10000`、`sizeC/100000` 三次重复双线性采样红通道并相乘，随后在天气层之后调制
albedo。两个步骤均同步、原子，不读取相机或隐藏时钟。

`eve.GtsGeologicalSettings()` 保存 GTS Geological 的 `enabled`、`objectSpace`、`nearStrength`、
`nearNormalStrength`、`nearScale`、`nearOffset`、`farStrength`、`farNormalStrength`、`farScale` 和
`farOffset`。`eve.bakeGtsGeologicalSurface(albedo,packedNormal,
heights,layerStrength,globalBlendDistance,geoAlbedo,geoNormal,settings,originY)` 沿地形高度轴重复采样 near/far
颜色条和 DXT5nm 法线条，用显式的逐像素地质层强度及近远距离混合它们。颜色图在 `[0,1]` 饱和，法线结果
继续保持 renderer-ready packed RG，原有 AO 与 smoothness 通道不变。albedo 与 packedNormal 使用候选副本
一起提交，任一 raster、纹理或参数非法时两者都不改变。

`eve.GtsDetailNormalSettings()` 提供 `enabled`、`objectSpace`、`nearTiling`、`nearStrength`、
`farTiling` 与 `farStrength`。`eve.bakeGtsDetailSurface(albedo,packedNormal,detailGreyscale,
layerStrength,globalBlendDistance,detailNormal,settings,originX,originZ,spacingX,spacingZ)` 以世界或对象 XZ
坐标进行 near/far repeat-bilinear 采样，按距离混合 RG 法线，再以逐像素层权重叠加到 packed normal。
它同时按 GTS 的法线 XY 平方长度给基础 albedo 添加微阴影，并输出未加权 `detailGreyscale`。
随后用 `eve.bakeGtsWeatherAlbedoDetailed(...,detailGreyscale,originX,originZ,spacingX,spacingZ)` 可复现 GTS
对雪纹理额外施加的细节衰减。三个 detail 输出一起提交，失败不产生半更新。

`eve.GtsSnowSurfaceSettings()` 和 `eve.GtsRainSurfaceSettings()` 保存 GTS profile 的表面天气参数。
雪参数为 `enabled`、`power`、`minimumHeight`、`blendRange`、`slopeBlend`、`age`、`scale`、
`colorR`、`colorG`、`colorB`；雨参数为 `enabled`、`power`、`minimumHeight`、`maximumHeight`、
`darkness`。
`eve.bakeGtsWeatherAlbedo(output,heights,snowAlbedo,snowMask,snow,rain,originX,originZ,spacingX,spacingZ)`
按 GTS 顺序先混合雪色，再施加雨水变暗。雪使用世界高度平滑带、由高度梯度求得的坡度遮罩、积雪年龄、
强度、重复纹理和 tint；雨使用最小/最大高度各 30 个世界单位的固定过渡，并将强度限制为 0.9。
调用同步且原子，失败保留旧 output。

`eve.bakeGtsWeatherPbr(normal,mask,displacement,tessellation,heights,snowNormal,snowMask,rainData,snow,rain,timeSeconds,originX,originZ,spacingX,spacingZ)`
补充 GTS 的其余表面输出。`normal` 采用可直接交给 `Renderable3D.setNormalTexture` 的 packed 格式：
RG 是以 `[0,1]` 编码的切线法线 XY，B 是 AO，A 是 smoothness；绑定后调用
`setPackedNormalMask(true)` 让 Vulkan/WGSL 重建法线 Z 并逐像素读取 AO/光滑度。`mask` 同时保留原始
四通道 GTS 材质数据，两个 Heightmap 分别保存位移与细分强度。雪支持 `normalStrength`、`setMaskRemapMin`、
`setMaskRemapMax`、`heightContrast`、`heightBrightness`、`heightIncrease`、`displacementContrast`、
`displacementBrightness`、`displacementIncrease` 和 `tessellationAmount`。雨支持 `speed`、`smoothness`
和 `scale`。雨滴相位只读取显式 `timeSeconds`，因此回放不会依赖隐藏引擎时钟。四个输出先在私有副本
完成，任一纹理、数值或结果非法时全部保持原状。

### 世界创建与统一历史

`eve.TerrainWorldCreationSettings()` 对应 Pcg `WorldCreationSettings` 的运行时拓扑字段：`tilesX`、
`tilesZ`、`tileSize`、`tileHeight`、`centerX`、`centerZ`、`heightmapResolution`、
`controlTextureResolution`、`detailResolution`、`treeResolution`、`objectResolution`、`splatLayers`、
`defaultSplatLayer`、`defaultDetailDensity`、`worldMap`、`namePrefix` 与 `nameSuffix`。高度分辨率必须为
`2^n+1`，control/detail 分辨率必须为 `2^n`。tile 原点按 Pcg 公式从完整世界中心向负半轴偏移；
普通名称为 `prefix_x_z-suffix`，world-map 使用 Pcg 的 `World Map_x_z-suffix` 前缀。

`eve.TerrainWorldWorkspace()` 同时拥有每块 tile 的 Heightmap、TerrainSplatmap、TerrainDetailLayer、
tree PointSet 与 object PointSet。`create` 原子建立零高度世界；`stamp`、`paintSplat`、`applyDetail`、
`applyTrees`、`applyObjects` 复用对应多 tile 内核。每次成功操作的历史快照覆盖五个领域，故
`undo`/`redo` 不会产生从未共同存在过的高度与资源组合。`copyHeightmap`、`copySplatmap`、
`copyDetail`、`copyTrees`、`copyObjects` 仅导出独立副本；tile 名称和原点查询返回结构化 Result。
工作区单线程拥有状态，不保留外部 raster、回调、场景对象或隐藏时间源。

`flatten()` 对所有 tile 的高度执行 Pcg 式归零并形成一次可撤销操作。`TerrainWorldClearSettings()`
通过 `details`、`trees`、`objects`、`probes` 选择生成域，`clearSpawns(settings)` 在所有 tile 上一次性清除所选域；
至少要选择一项。`sourceNamespace=0` 清理任意来源；非零时只清理由对应 detail/tree/object/probe placement
`namespaceId` 生成的资源层或实例。`TerrainDetailLayer.sampleResource(namespaceId,x,z)` 查询单个资源层，`clearResource(namespaceId)`
只删除该层并返回发生变化的单元数；普通 `sample` 返回所有资源层之和。Splat 权重不属于 Pcg
`ClearSpawns`，因此不会被该操作修改。

查询 API 为 `getTileName(index)`、`getTileOriginX(index)`、`getTileOriginZ(index)`、`getTileCount()`、
`getLastChangedSamples()`、`getLastAffectedTiles()`、`getOperationCount()` 与 `getAppliedCount()`。

### 有序 Spawner 计划

`eve.TerrainSpawnPlan()` 以添加顺序拥有 terrain modifier stamp、texture、detail、tree 和 object 规则。
`addSplat`、`addDetail`、`addTrees`、`addObjects` 接收稳定且唯一的 `ruleId`，并深拷贝 paint/fitness raster、
领域设置、操作窗口、目标纹理层、模式和显式种子；
之后修改调用方对象不会改变计划。`setEnabled(ruleId,enabled)` 按稳定身份切换规则，`getRuleCount()`
返回规则总数。

`addModifierStamp(ruleId,stamp,settings,operation,localMask,globalMask)` 加入 Pcg TerrainModifierStamp 资源，
其中 operation 使用与 `applyStamp` 相同的 0..5 枚举。
印章及局部/全局蒙版均在加入时深拷贝，并与其他资源规则按同一顺序事务执行。

`TerrainWorldWorkspace.spawn(plan)` 按顺序执行全部启用规则，但只复制一次完整世界并只发布一次；任意
后续规则失败会丢弃此前规则的候选结果，历史中只出现一个混合生成操作。空计划或全部禁用的计划会
返回结构化失败，不制造无意义历史。执行仍使用各领域的跨 tile 映射、稳定资源 namespace 和显式 RNG。

`snapshotJson()` 输出确定性的 `eve.procgen.terrain-spawn-plan` schema version 3 JSON。根对象严格只允许
`schema`、`version`、`payload`；payload 是完整规则、raster、设置、顺序与启用状态的固定小端二进制
十六进制表示。`restoreJson(json)` 拒绝未知根字段、未知版本、非有限数值、非法枚举、重复规则 ID、
尺寸/数量上限、截断或尾随 payload，并在完整候选通过后一次交换。Version 0 作为旧格式迁移入口，
其规则没有 enabled 字节，恢复时统一迁移为启用；version 0/1/2 均可恢复，再次保存会写为 version 3。

### Probe 资源点

`eve.generateTerrainProbes(output,fitness,heights,settings,type)` 导出 Pcg Probe 资源的拥有式 PointSet；
type 为 0=Reflection、1=Light。`eve.TerrainProbePlacementSettings()` 提供 `name`、`originX`、
`originZ`、`width`、`depth`、`heightScale`、`spacing`、`jitterPercent`、`minimumFitness`、
`seaLevelActive`、`seaLevel`、`reflectionOffset`、`lightOffset`、`reflectionResolution`、
`reflectionClipDistance`、`reflectionShadowDistance`、`seed`、`namespaceId` 和 `maxPoints`。

扫描使用显式 Pcg XorshiftPlus 流。ReflectionProbe 在启用海平面时位于
`max(terrainHeight,seaLevel)+reflectionOffset`；禁用时保留源码的 `500+seaLevel+0.2` 回退。
LightProbe 仅在地形严格高于 seaLevel 时生成，并增加 lightOffset。输出携带稳定 ID、probe 类型、
资源名、namespace、反射分辨率、裁剪距离和阴影距离；场景节点及实际反射捕获由 graphics/scene provider
消费这些值，不由 procgen 保存。

`TerrainSpawnPlan.addProbes(ruleId,fitness,settings,operation,mode,type)` 和
`TerrainWorldWorkspace.applyProbes(...)` 将 Probe 纳入与其他生成域相同的跨 tile 事务；mode 为
0=Add、1=Replace、2=Remove。Replace/Remove 只匹配操作窗口内相同 `probeResource` 的点，整次操作失败时
所有 tile 保持原值。`copyProbes`、`clearSpawns`、undo/redo 和分步 SpawnPlan 都读写完整世界快照中的
独立 Probe 状态。SpawnPlan version 3 持久化 Probe 类型、资源名、操作模式、fitness 与全部设置。

`eve.publishTerrainProbes(batchId,points)` 通过可选 `IProcgenProbeSink` 将完整 PointSet 原子发布给 graphics。
Reflection 点创建并注册拥有式 `ReflectionProbeCapture`，Light 点作为同批稳定光照采样位置保留；再次发布
同一 batch 会先构造完整候选再替换。`removeTerrainProbeBatch` 销毁批次及捕获资源，
`tickTerrainProbeBatches(faceBudget,filterBudget,filterSamples)` 按显式预算推进六面捕获和过滤。
`getTerrainProbeBatchCount`、`getTerrainReflectionProbeCount`、`getTerrainLightProbeCount` 提供可观察状态；
graphics provider 缺失时发布、删除和 tick 返回 `Unsupported`，查询返回零。

### Biome Preset 生成栈

`eve.TerrainBiomePreset()` 对应 Pcg `BiomePreset.m_spawnerPresetList`。`addSpawner(entryId,plan,
activeInBiome,activeInStamper,autoAssignResources)` 按列表顺序深拷贝一个非空 Spawner 计划；entry ID
必须稳定且唯一。两个 active 标志分别控制 biome controller 自动生成和 stamper 联动生成，
`setActiveInBiome`、`setActiveInStamper` 按稳定 ID 修改。`getAutoAssignResources` 保留 Pcg
`m_autoAssignPrototypes` 的资源准备意图，实际资源仍由各规则的稳定 namespace/asset 引用拥有。
`getSpawnerCount()` 返回 preset 当前拥有的 Spawner 条目数。

`TerrainWorldWorkspace.spawnBiome(preset)` 与 `spawnStamper(preset)` 只选择对应 scope 的条目，再按
entry 顺序和内部 rule 顺序编译为一个计划。不同 Spawner 可复用局部 rule ID；编译时会用长度定界的
entry ID 形成无歧义复合身份。整个栈仍只发布一个完整世界快照，因此跨 Spawner 的后置失败不会留下
先前纹理或植被结果。没有对应 active 条目时返回结构化失败。

长生成栈可用 `beginSpawn(plan)`、`beginSpawnBiome(preset)` 或 `beginSpawnStamper(preset)` 启动，再用
`stepSpawn(maxRules)` 按正数规则预算推进。状态 0=Idle、1=Pending、2=Completed、3=Cancelled、4=Failed；
`getSpawnStatus()` 和 `getSpawnCompletedRules()` 提供轮询。Pending 候选深拷贝完整世界及历史，规则执行
产生的纹理、detail、tree、object 中间结果均不可通过 copy API 观察；最后一条规则成功后才形成一次
历史提交。`cancelSpawn()` 或任意规则失败会丢弃完整候选。Pending 时 create、所有同步修改、undo/redo
均返回结构化失败，避免无声替换或分叉候选；读取最后发布快照仍然可用。

`snapshotJson()` 输出确定性的 `eve.procgen.terrain-biome-preset` schema version 1，包含有序条目、三个
标志和每项完整的嵌套 SpawnPlan snapshot。`restoreJson(json)` 严格拒绝根或条目的未知字段、重复/空 ID、
非法嵌套计划及尺寸上限，并在全部条目通过后一次交换。Version 0 没有 `autoAssignResources` 字段，迁移
时采用 Pcg 原型自动准备的兼容默认值 true；再次保存写为 version 1。
# Pcg PhotoMode 地形质量

`PcgTerrainPhotoModeAuthority` 通过显式 `setAuthority(true)` 成为照片模式 Terrain 域的唯一所有者。它保存并验证 draw-instanced、detail density、detail distance、heightmap pixel error 与 basemap distance。`selectLod` 将当前 pixel-error 直接用于既有 screen-space-error 地形 LOD 选择；`textureTier` 在相机距离越过阈值时返回命名的 Basemap 材质级别。细节实例渲染器读取 `state()` 中的 density、distance 和 instancing 标志，使同一份状态驱动网格、材质与植被细节。撤销或析构会释放 capability listener。

## GTS 地形网格切片

`GtsMeshSplitResult` 保存 `splitGtsMesh(output,source,xSplits,zSplits,pivot)` 产生的行优先网格切片。
切割数量表示平面数，因此列数和行数分别为 `xSplits+1`、`zSplits+1`。每个源三角形会真正沿 X/Z
边界裁剪，交点同步插值位置、法线和 UV，跨越边界的三角形会进入相邻切片。

`pivot` 为 0 时保留源坐标，1 使用每块的最小 X/Z，2 使用中心 X/Z。通过 `getColumnCount()`、
`getRowCount()`、`getTileCount()` 查询布局，`getTileOffsetX(index)`、`getTileOffsetZ(index)` 返回恢复
世界位置的偏移，`copyTileMesh(index)` 返回调用者拥有的独立 `ProcgenMeshBuild`。无效索引返回 null，
无效拓扑、非有限顶点、越界索引及非法切分数通过 Result 报错，旧 output 保持不变。

`simplifyGtsMesh(output,source,quality,options)` 提供 GTS 网格导出的确定性 QEM 简化核心。`quality` 是
`[0,1]` 三角形比例；折叠同步更新位置、法线和 UV，移除退化三角形后压紧顶点，并保留三角形组。
`GtsMeshSimplificationOptions.preserveBorderEdges` 可锁定开放边界，`preserveUvSeamEdges` 可锁定同位置但
UV 不同的 seam，`preserveUvFoldoverEdges` 可锁定 smart-link 中位置及 UV 均相同的 foldover 顶点组，
`preserveSurfaceCurvature` 将法线变化纳入折叠代价。迭代使用 `aggressiveness` 控制的 GTS 阈值逐步放宽；
每次坍缩会检查受影响面的新旧法向点积并拒绝翻面。边界保护允许内点坍缩到边界端点，因此不会移动
边界轮廓。输出和输入可为同一 MeshBuild；所有验证与候选简化完成后才替换输出。

`buildDefaultGtsTerrainLods(output,source,xSplits,zSplits,pivot)` 把切片和逐级简化组合成 GTS 默认四层
LOD：质量依次为 1、0.5、0.25、0.125，屏幕相对高度为 0.95、0.7、0.6、0.02。每一级以上一级
结果为输入。`GtsTerrainLodSet` 拥有全部行优先 tile，通过布局查询、tile offset、
`getLevelQuality(level)`、`getLevelTransitionHeight(level)` 以及 `copyLevelMesh(tile,level)` 供脚本消费；
无效 mesh 索引返回 null。C++ 的
`buildGtsTerrainLods` 还接受逐层独立的简化选项，并在整个结果成功前不发布部分层级。

启用 `enableSmartLink` 时，简化器会按 `vertexLinkDistance` 连接重合的开放边界顶点。内部三角形分别保存
几何拓扑索引与属性索引，因此绑定后的顶点共享 QEM 拓扑和坍缩位置，但仍输出各自的法线与 UV。
重合顶点 UV 不同标记为 seam，UV 相同标记为 foldover；对应 preserve 选项会阻止该分类边被坍缩。

运行时可用 `selectLevel(relativeHeight)` 按 GTS/Unity 的屏幕相对高度选择层级；低于最后一个 transition
返回 -1 表示剔除，非法输入返回 -2。`getLevelSwitchDistance(level,worldDiameter,verticalFovDegrees)` 将
同一阈值换算成 `Renderable3D.setMeshLod` 使用的相机距离，`selectLevelForCamera` 可直接按距离、包围体
直径和垂直 FOV 选择。换算采用透视投影关系，因而不依赖固定分辨率。

`GtsTerrainLodSet.snapshotJson()` 输出 `eve.procgen.gts-terrain-lod` version 2，完整包含布局、逐层质量、
transition、全部简化选项、tile offset、位置/法线/UV/index 流和每个三角形的 group 名称。
`restoreJson(json)` 严格拒绝未知字段、未来版本、尺寸超限、非有限数值、无效索引或不匹配的 stream，
并只在所有 tile 和 level 解码成功后一次替换当前结果。

`Procgen.configureGtsTerrainTileLods(lods,tile,renderable,graphics,worldDiameter,verticalFov,originX,originY,originZ)`
先把指定非空 tile 的全部层级上传到同一 Graphics owner；全部成功后一次清空并配置 Renderable3D 的
四层 LOD、三个切换距离、末级剔除距离和带 pivot offset 的世界位置。上传失败或参数无效时 renderable
保持不变。上传 mesh 由 Graphics 持有，Renderable3D 只保存借用引用。

`GtsTerrainLodRuntime.replace(lods,procgen,graphics,worldDiameter,verticalFov,originX,originY,originZ)` 为所有
非空 tile 创建候选 Renderable3D；候选在完整配置前保持不可见，全部成功后才显示并销毁旧批。
`getRenderable(tile)` 按原 row-major 槽位返回 ECS 借用实体，空 cell、无效或 stale 槽位返回 null；
`getTileCount()` 和 `getRevision()` 查询已提交状态。`clear()` 销毁全部实体并返回销毁数量。
`GtsTerrainLodRuntime.applyMaterial(material)` 把同一个 Graphics 所有的材质借用引用应用到当前所有存活
tile，并返回实际更新数量；运行时不取得材质所有权。`planGtsTerrainLodAssets(output,lods,terrainName,
meshFolder)` 生成确定性的导出清单。`GtsTerrainLodAssetPlan` 通过 `getEntryCount()`、`getTileIndex(index)`、
`getLevelIndex(index)`、`getObjectName(index)`、`getMeshName(index)` 和 `getRelativePath(index)` 查询条目。
对象名兼容 GTS 的 `<terrain> SubTile<tile>_LOD_<level>`，mesh 名兼容其无分隔 level 后缀；EVEngine
清单路径使用 `<folder>/<mesh>.eve.mesh.json` 作为稳定的作者侧标识；它不是可直接加载的独立文件。
`asset_procgen::prepareGtsTerrainLodPackage` 会把全部非空层实际编码为 `eve.mesh/3` definition 与 EVMESH/3
bulk，并生成可交给 `buildEvaArchive`、AssetCooker 和 EvpackGraphicsLoader 的完整 `.eva` 候选包。非法名称、
路径、数组、索引或预算不会发布部分候选。

`GtsTerrainLodRuntime.replaceAndHideSource(lods,procgen,graphics,sourceTerrain,worldDiameter,verticalFov,
originX,originY,originZ)` 在完整 LOD 批成功发布后隐藏源 Renderable3D，并记住它进入交接时的可见性。
替换失败不会修改源或旧批；换用另一个源时先恢复前一源，`clear()` 和运行时析构也恢复当前源。
源引用保存为 ECS generation handle，因此源先销毁时 `getSourceTerrain()` 返回 null，后续清理不会访问
陈旧实体。受运行时管理的 LOD tile 不能同时作为源。

`buildGtsTerrainBaseMesh(output,heightmap,saveResolution,sizeX,sizeY,sizeZ)` 完成 GTS 转换的高度场前置阶段。
`saveResolution` 为 0=Full、1=Half、2=Quarter、3=Eighth、4=Sixteenth，对应 1/2/4/8/16 的源样本步长。
输出覆盖完整 terrain 尺寸与 `[0,1]` UV，Y 为高度样本乘 `sizeY`；法线从实际导出三角形重新计算。
高度场间隔不能被步长整除、样本非有限、尺寸非法或网格超预算时返回结构化错误，旧 output 保持不变。

`buildDefaultGtsTerrainLodsFromHeightmap(output,heightmap,saveResolution,sizeX,sizeY,sizeZ,subTiles,pivot)`
原子执行 Pcg 的完整默认转换链。`subTiles` 与 GTSProfile 一致，表示 X/Z 两轴各自的切割平面数，因此
默认值 3 产生 4×4 个 row-major tile；允许范围为原编辑器的 0–5。任一基础网格、切片或顺序简化阶段
失败都不替换旧 LOD set。

`GtsTerrainMeshSettings` 对应原包的 `GTSMeshSettings`，默认保存 Full、4 个 LOD、
`100/50/25/12.5` 百分比和 3 个 split。LOD count 限制为 1–4，质量允许编辑器同样的 0–100；最后一个
激活 LOD 总使用 0.02 剔除阈值，因此 1/2/3/4 层配置分别得到正确的 GTS transition 序列。
`snapshotJson/restoreJson` 使用严格的 `eve.procgen.gts-terrain-mesh-settings` version 1 schema，未知字段、
非法范围或错误数组长度不会修改旧设置。`buildGtsTerrainLodsFromHeightmap(output,heightmap,settings,...)`
直接消费这份配置。
脚本用 `getSaveResolution/setSaveResolution`、`getLodCount/setLodCount`、`getSubTiles/setSubTiles`、
`getLodQuality/setLodQuality` 和 `getLodTransitionHeight` 查询或修改各字段。

### GTS 地形导出配置

`GtsTerrainExportSettings` 对应原包的 `GTSExportTerrainSettings` 中源地形与 impostor 两组 LOD
配置。`appendSourcePreset(mode,level,quality,transition)` 与
`appendImpostorPreset(mode,level,quality,transition)` 中 mode 取 0=Impostor、1=LowPoly；它们精确应用
原版 `SetLODToImpostorMode` / `SetLODToLowPolyMode` 的采样分辨率、纹理分辨率、锐边、烘焙方式、
法线图、顶点色与材质策略。transition 必须按列表严格递减，quality 使用 `[0,1]`。
`getSourceLodCount()` 和 `getImpostorLodCount()` 返回两组数量。

`snapshotJson()` / `restoreJson(json)` 使用严格的
`eve.procgen.gts-terrain-export-settings` version 3 schema，保存顶层工作流、每层所有可执行字段和 QEM 参数；未知字段、
非法枚举、纹理分辨率、非递减阈值或尺寸超限会失败；version 1 自动迁移并采用原 Pcg 顶层默认值；version 2 的 OBJ 面格式迁移为 Triangles，恢复只在整个候选有效后提交。C++ 的
`compileSourceLevels()` 将源导出记录直接编译成现有顺序 LOD 网格管线使用的设置。

`buildGtsTerrainExportLodsFromHeightmap(output,heightmap,settings,sizeX,sizeY,sizeZ,subTiles,pivot)`
使用 source LOD 第一项自己的 SaveResolution 构造基础网格，再按完整 source 列表逐级简化；设置中的
分辨率不再只是持久化元数据。`buildGtsTerrainColliderMeshFromHeightmap(output,heightmap,settings,...)`
使用 workflow 的独立 colliderResolution 和 colliderSimplifyQuality 生成 MeshCollider 三角网格；只有
addTerrainCollider=true 且 colliderType=Mesh 时接受调用。两条入口都在最终候选完成后替换 output。

`encodeGtsTerrainObj(heightmap,saveResolution,sizeX,sizeY,sizeZ,faceMode)` 返回确定性的 UTF-8 Wavefront
OBJ 文本。faceMode 为 0=Triangles、1=Quads；坐标按原 Pcg 导出器写成
`(-terrainZ,height,terrainX)`，`vt` 同样保持原版的 `(v,u)` 顺序和一基 face 索引。它使用固定经典 locale
和足以往返 float 的精度，不依赖系统小数点设置。导出设置 version 3 保存 workflow 的 objFaceMode；
version 2 迁移时采用原默认 Triangles。

`encodeGtsMaskedTerrainObj(heightmap,maskmap,saveResolution,sizeX,sizeY,sizeZ,faceMode,threshold,invert)`
复现 Pcg `MaskedTerrainMesh` 的 masked OBJ 路径。它以每个输出格左下角的归一化 mask 样本分类整格：
低于 threshold 属于 outside，其余属于 inside；invert=false 输出 inside，true 输出 outside。原版默认调用
threshold=0.2。maskmap 可以使用与 terrain 不同的尺寸，采样采用原版 `normalized * dimension` 双线性规则；
输出继续使用原版的 X/Z 顶点去重、UV 缩放和逆时针面序。

`bakeGtsTerrainVertexColors(output,source,bakedTexture,edgeMode,smoothingIterations,terrainSizeX,terrainSizeZ,linearize)`
执行 Pcg `ProcessEdgesAndColorBaking`。Smooth 保留共享顶点并按世界 X/Z 采样；Sharp 为每个三角形角展开顶点、
重算法线，并把三角形三个纹理样本的平均色写给整面。平滑迭代使用原版四邻域、边缘 clamp 算法；linearize
显式对应 SRP OrthographicBake 的 `.linear` 分支。颜色是 `ProcgenMeshBuild` 的可选 RGBA 顶点流，可通过
`hasVertexColors()` 和 `getColor(vertex,component)` 查询。裁剪、QEM、group copy、变换及 LOD version 2
快照都会保留颜色；旧 version 1 LOD 快照仍可迁移读取。

### Pcg Mask Map Export

`combinePcgMaskMapChannels(output, red, green, blue, alpha, activeChannels)` 与 Pcg 的
`PW_ChannelCombine.shader` 一致：四张输入图各自仅取 red 分量，按 RGBA bit mask 写入目标；未启用通道写 0，
输入尺寸或 bit mask 非法时目标保持不变。`placePcgMaskMapTileInto(atlas, tile, x, y, width, height)` 使用
像素中心双线性缩放并事务写入 atlas 矩形，对应 `OneCombinedTexture` 的逐 terrain 拼接。文件编码继续使用
ImageData 既有 PNG/JPG/EXR 输出接口。

### Pcg 通用 Mesh LOD Generator

`PcgMeshLodProfile.appendLevel(transitionHeight, fadeWidth, quality, combineMeshes, combineSubMeshes)`
按近到远加入最多四级配置；transition 必须严格递减，三个标量均使用 0..1 范围。
`buildPcgMeshLods(output, sourceMesh, profile)` 为每一级从同一份原始 MeshBuild 独立执行简化，完整成功后
才替换 `PcgMeshLodSet`。可用 `copyLevelMesh(index)` 取得拥有式副本，或用 `selectLevel`、
`getSwitchDistance` 检查投影阈值。

`procgen.configurePcgMeshLods(lods, renderable, graphics, worldDiameter, verticalFovDegrees)` 会先上传全部层，
然后一次替换 Renderable3D 的 LOD 链并设置最远层裁剪距离。上传或参数校验失败时 renderable 保持原状。
`profile.setFadePolicy(mode, animate, duration)` 保存 Unity `LODFadeMode`：0=None、1=SpeedTree、
2=CrossFade。各级 `fadeWidth` 与全局策略会随 output 原子写入 Renderable3D。距离模式在切换点前按当前
级距离区间乘 fadeWidth 生成互补权重；末级可渐隐到 cull。动画模式由
`Renderable3D.advanceMeshLodTransition(distance, dt)` 使用调用方注入的 dt 确定推进。

`profile.setLevelRendererState(level, skinQuality, shadowMode, receiveShadows, motionMode,
skinnedMotionVectors, lightProbeUsage, reflectionProbeUsage)` 保存 Pcg `LODLevel` 的逐级 renderer 策略。
枚举数值与 Unity 一致，包括 `Bone4=4` 与 `CustomProvided=4`。`configurePcgMeshLods` 在所有 mesh 上传后
把整条网格链和状态一次写入 Renderable3D；选中级别的 Off/On/ShadowsOnly、receive-shadow、Object/
ForceNoMotion 和 reflection-probe Off 已进入主渲染路径。

`PcgMeshLodBackup.capture(renderable)` 在替换前保存完整 MeshRenderer 状态以及实体 id/generation；
`restore(renderable)` 仅对同一实体 generation 原子恢复并消费快照，错误目标不会改变自身或消费备份。
原 mesh、parts、material 等资源仍由同一个 Graphics 实例拥有，Graphics 与这些资源必须比 backup 活得更久。
`discard()` 可只丢弃备份。该对象对应 Pcg `LODBackupComponent` 与 `DestroyLODObject` 的恢复职责。
脚本查询与矩阵接口为 `getAnimateCrossFading()`、`getCrossFadeAnimationDuration()`、`getFadeMode()`、
`getFadeWidth(level)`、`getLevelRendererState(level,field)`、`getQuality(level)`、
`getTransitionHeight(level)`、`getSourceCount()`、`getElement(row,column)`、`setElement(row,column,value)`；
备份可用 `getEntityId()`、`getEntityGeneration()` 与 `isCaptured()` 检查身份和状态。

`PcgMeshTransform` 保存 root-relative 4×4 row-major 矩阵；用
`PcgMeshCombinePlan.appendSource(mesh, transform, defaultMaterialId)` 按 renderer 顺序复制输入。
`combinePcgStaticMeshes(output, plan)` 依次变换顶点并按首次遇到的 material id 合并 triangle group；任一输入
带颜色时，无颜色输入会补白色 RGBA。`buildPcgCombinedMeshLods(output, plan, profile)` 先组合一次，再让所有
启用了 `combineMeshes` 的 level 从同一份组合结果独立简化。混合开启/关闭 combine 的 profile 会明确失败，
避免生成与 Pcg renderer 拆分结构不一致的结果。两个入口都只在完整成功后替换 output。
### Pcg Terrain Watcher

`PcgTerrainWatcher` 将 Pcg 编辑器中基于 hierarchyChanged 的 Terrain 创建/删除检测改为
显式、确定性的快照事务。调用 `beginScan()` 后按场景稳定顺序调用 `addTerrain(id)`，最后以
`commitScan()` 原子发布；首次提交只建立缓存，不产生 Created 事件。
`PcgTerrainWatcher.getTerrainCount()` 返回当前缓存规模；后续结果通过 `getChangeCount()`、
`getChangeTerrainId(index)` 和 `getChangeType(index)` 读取，其中类型 0 为
Created、1 为 Removed。新增项按本次扫描顺序排列，随后删除项按上次扫描顺序排列。
`cancelScan()` 丢弃候选；重复/空 id 或错误调用顺序返回 Result 且不改变已发布集合。

### Pcg Tree Manager

`PcgTreeManager` 汇总多个地形生成出的世界空间树位置。先以 `reset(minX, minZ, width, depth)`
建立 Pcg Quadtree 对应的半开世界边界，再用 `addTree(x, z, prototype)` 或
`addTrees(pointSet, prototype)` 加入树；批量输入会完整校验后一次发布。
`countInRange(x, z, range)` 与 Pcg 源码一致使用包含边界的轴对齐正方形查询，而不是圆形距离；
`PcgTreeManager.getCount()` 返回当前总树数。管理器复制坐标与 prototype index，不保留 PointSet。

### Pcg Biome Controller 自动加载边界

`eve.PcgBiomeController()` 保存一个 biome 的世界位置、生成半径与 TerrainLoader 模式。`configure`
使用 Pcg 原始公式计算 regular 立方边界：边长为 `range * 2 - 0.5`，用于避免范围恰好落在 tile
边缘时加载多余相邻地形；正数 impostor range 直接加到该边长，零值关闭 impostor 边界。模式编号与
原包一致：0 Disabled、1 EditorSelected、2 EditorAlways、3 RuntimeAlways。

`fitToTerrain(minX, originY, minZ, sizeX, sizeY, sizeZ)` 使用单块地形 X/Z 中心、地形原点 Y 和 X
半宽；`fitToAllTerrains` 使用聚合边界完整中心及 X 半宽。`tierAt` 返回 0 Regular、1 Impostor 或
2 Unloaded，可直接驱动调用方的地形驻留调度。`getRange`、`getLoadMode`、`getRegularCenterX`、
`getRegularCenterY`、`getRegularCenterZ`、`getRegularSize` 与 `getImpostorSize` 提供脚本侧状态读取。
所有更新先验证完整候选，失败时保持旧边界。
### Pcg Runtime Stamper

`eve.PcgRuntimeStamper()` 移植 Pcg Pro `RuntimeStamper.cs` 的完整运行时编排。调用
`configure(address, showGui, showDebug)` 后，由宿主资源系统加载高度图并传给
`loadStamp(stamp, resourceName)`，再用 `execute(target, originX, originZ, spacingX, spacingZ)`
执行清平、适配完整地形范围、应用高度 6、零旋转和中心到边缘的线性距离遮罩。执行采用候选副本，
任一步骤失败都不会留下部分修改。资源路径会把反斜杠统一为 `/`；缺失资源可通过
`reportMissingStamp()` 进入可观察的失败状态。`getUpdateTimeAllowed()` 固定返回 Pcg 原示例的
`1/15` 秒预算，`updateLayout()` 保留 300x20 底部居中进度标签的位置语义。脚本可通过
`getStatus`、`getStampAddress`、`getProgressText`、`getLabelCenterX` 与 `getLabelCenterY` 读取状态。

#### 第二百一十九批：RuntimeStamper

- 对照 `RuntimeStamper.cs` 保留资源地址默认值、路径归一化、加载/失败/完成状态与进度文本。
- 将 `FlattenTerrain -> FitToTerrain -> height=6 -> linear distance mask -> rotation=0 -> Stamp`
  组合为一次原子操作，同时复用 EVEngine 的 `Heightmap` 与 `applyTerrainStamp`。
- 控制器拥有 stamp 副本，不跨帧保留调用方指针；无回调、无锁、无隐式时间或随机数。

### Pcg Spawn Progress

`eve.PcgSpawnProgress()` 保存跨 spawner 的规则进度。`updateRule` 设置名称、总规则计数和当前
spawner 计数，`updateRuleFraction` 更新当前规则的小数进度；`getProgress` 使用
`(totalCompleted + fraction) / totalCount`。`getTitle`、`getSubtitle`、`getStatus` 提供 UI 所需快照，
`requestCancel` 发布取消请求，`clear` 隐藏并清空状态。所有更新先验证候选，非法计数或进度不修改旧值。
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
