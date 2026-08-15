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

- `addObject()`、`addObjectAt()`、`applyToLayer()`、`buildMesh()`、`clearObjects()`、`fill()`、`generate()`、`generateImage()`、`generateMesh()`、`generateNormalImage()`
- `generateTexture()`、`generateTo()`、`getAlgorithmCount()`、`getAlgorithmId()`、`getCell()`、`getFloat()`、`getHeight()`、`getInt()`
- `getLayer()`、`getMeshRecipeCount()`、`getMeshRecipeId()`、`getMeta()`、`getName()`、`getObjectCount()`、`getObjectGid()`、`getObjectHeight()`、`getObjectName()`、`getObjectType()`
- `getObjectWidth()`、`getObjectX()`、`getObjectY()`、`getPalette()`、`getPaletteGid()`、`getPath()`、`getSeed()`、`getString()`
- `getTarget()`、`getTextureRecipeCount()`、`getTextureRecipeId()`、`getWidth()`、`gridToJson()`、`has()`、`hasAlgorithm()`、`hasMeshRecipe()`、`hasTextureRecipe()`
- `lastError()`、`newGrid()`、`newOutput()`、`newParams()`、`resize()`、`setCell()`、`setFloat()`、`setInt()`
- `setLayer()`、`setMeta()`、`setPalette()`、`setPaletteGid()`、`setPath()`、`setSeed()`、`setSize()`、`setString()`
- `setTarget()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/procgen/`](../../../src/modules/procgen/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `procgen`；跨模块 hex 关卡见 [`test/hex_level_simulation.cpp`](../../../test/hex_level_simulation.cpp)、[`test/hex_level_data.cpp`](../../../test/hex_level_data.cpp)、[`examples/hex-levels/`](../../../examples/hex-levels/)。
