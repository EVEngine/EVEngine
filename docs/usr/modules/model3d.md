# 3D 模型模块

**脚本入口：** `eve.Model3D()`

通过 medialoader/Assimp 载入模型数据，再交给图形模块渲染。

## 基本用法

```squirrel
local models = eve.Model3D();
local mesh = models.newModelDataFromFile("models/hero.glb");
```

## 读取材质并装配渲染对象

`ModelData` 可以直接读取材质参数（base color、metallic/roughness、贴图路径与内嵌贴图），
然后交给 `createRenderable` 一键装配成可渲染的 `Renderable3D`：

```squirrel
local models = eve.Model3D();
local md = models.newModelDataFromFile("models/hero.glb");

// 材质查询（texture 类型名：base_color / diffuse / normals / height / emissive / ...）
local mi = md.getMaterialIndex(0);               // mesh 0 使用的材质槽
local r = md.getMaterialBaseColorR(mi);          // 基色（glTF BASE_COLOR，OBJ 回退 DIFFUSE）
local metallic = md.getMaterialMetallicFactor(mi);
local roughness = md.getMaterialRoughnessFactor(mi);
local alphaMode = md.getMaterialAlphaMode(mi);     // OPAQUE | MASK | BLEND
local alphaCutoff = md.getMaterialAlphaCutoff(mi); // MASK 阈值，默认 0.5
local path = md.getMaterialTexturePath(mi, "base_color");  // 外部路径或 "*N"（内嵌）

// 装配：节点变换烘焙进顶点，材质/贴图自动应用
local renderable = models.createRenderable(gfx, md, 0);
```

内嵌贴图（glTF/FBX 内嵌 PNG/JPEG）用 `getEmbeddedTextureCount()` 查询，
`getEmbeddedTextureImageData(idx)` 解码为 `ImageData`（调用方负责释放）。

## 对象关系与调用时机

`Model3D` 只负责加载 `ModelData`；ModelData 包含 mesh/material 数据和统计信息。GPU mesh、材质实例、摄像机与实际 draw 属于 Graphics。

从碰撞点反查 UV 的同一套 `ModelData` 也可以**程序化改顶点法线**，再烘焙成法线贴图：

```squirrel
local md = models.newModelDataFromFile("models/hero.glb");
md.applyVertexNormalsFrom(0, "radial", 0, 0, 0);   // 从原点指向各顶点（向外）
// 或 md.applyVertexNormals(0, "radial");          // 原点取该 mesh 的 AABB 中心
local nmap = md.bakeNormalMap(0, 512, 512, 0, "tangent");
```

`setVertexNormal(mesh, vertex, x, y, z)` 写单个顶点。`bakeNormalMap` 的 `space` 为 `"tangent"`（PBR 采样）或 `"object"`。无 UV 的 mesh 会失败。
`newModelDataFromFile` 走共享缓存，不要在被多方持有的实例上改法线；从内存 `newModelData` 解码一份再改。

## 目标导向指南

### 载入并检查模型

用 `newModelDataFromFile(path)` 载入 glTF/FBX/OBJ 等支持格式；先读取 mesh/material/face/vertex 数量并检查法线、UV，失败时显示占位模型。资源应在初始化或加载阶段创建。

### 提交模型渲染

将 ModelData 转换为 Graphics 所需 mesh/renderable，设置材质、变换和光照标志，再在渲染阶段调用 3D 渲染入口。模型数据可复用，实例只保存不同变换。

### 从碰撞点反查 UV

用 `getVertexPosition(meshIndex, vertexIndex, component)` 和
`getFaceVertexIndex(meshIndex, triangleIndex, corner)` 按导入模型的原始拓扑构建三角形碰撞体。
射线命中后，将命中三角形索引与模型局部坐标传给
`mapSurfacePointToUv(meshIndex, triangleIndex, localX, localY, localZ, channel)`。返回的
`SurfaceUv` 提供 `getU()`、`getV()`、`getBarycentricA()`、`getBarycentricB()`、
`getBarycentricC()`、`getTriangleIndex()` 和 `getUvChannel()`。命中点必须是模型局部空间；
若渲染实例有变换，需先将世界命中点反变换到局部空间。

## 常见问题

- 在 render 中载入模型：I/O 和解析必须在加载阶段。
- 模型无 normals 仍启用光照：先检查 `hasNormals()`。
- 每个实例重复加载同一文件：共享 ModelData 和 GPU 资源。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `empty()`、`getFaceCount()`、`getMaterialCount()`、`getMeshCount()`、`getName()`、`getVertexCount()`、`hasNormals()`、`hasTexCoords()`
- 表面拓扑与 UV 反查：`getVertexPosition()`、`getFaceVertexIndex()`、`mapSurfacePointToUv()`；`SurfaceUv.getU()`、`getV()`、`getBarycentricA()`、`getBarycentricB()`、`getBarycentricC()`、`getTriangleIndex()`、`getUvChannel()`
- 顶点流：`getTexCoordChannelCount()`、`hasTexCoordChannel()`、`getTexCoord()`、`hasTangents()`、`getTangent()`、`getBitangent()`、`getVertexColorChannelCount()`、`hasVertexColorChannel()`、`getVertexColor()`、`getVertexNormal()`、`setVertexNormal()`、`applyVertexNormals()`、`applyVertexNormalsFrom()`、`bakeNormalMap()`
- 材质：`getMaterialIndex()`、`getMaterialName()`、`getMaterialBaseColorR/G/B/A()`、`getMaterialMetallicFactor()`、`getMaterialRoughnessFactor()`、`getMaterialOpacity()`、`getMaterialTwoSided()`、`getMaterialAlphaMode()`、`getMaterialAlphaCutoff()`、`getMaterialTextureSlotCount()`、`getMaterialTexturePath()`、`getMaterialTextureEmbeddedIndex()`
- 内嵌贴图：`getEmbeddedTextureCount()`、`getEmbeddedTextureName()`、`getEmbeddedTextureWidth()`、`getEmbeddedTextureHeight()`、`getEmbeddedTextureImageData()`
- 蒙皮：`hasBones()`、`getBoneCount()`、`getBoneName()`、`getInverseBindMatrixElement()`、`getBoneWeightCount()`、`getBoneWeightVertex()`、`getBoneWeightValue()`
- 动画剪辑：`getAnimationCount()`、`getAnimationName()`
- `newModelData()`、`newModelDataFromFile()`、`createRenderable()`
- 离线封装：`bakeModel(sourcePath, destinationPath)` 将自包含 GLB/FBX 打包为 `.evmodel`；部署时仍用 `newModelDataFromFile()` 透明加载。

`.evmodel` 是稳定的单文件传输封装，记录原始格式并避免部署时扩展名猜测；它不会自动收集 OBJ/MTL 的外部 sidecar，因此离线生产优先以 GLB 或内嵌贴图 FBX 为输入。GPU Mesh 会保留所有导入 UV、顶点色及 tangent/bitangent 流供自定义管线和后续烘焙使用；内置 PBR shader 当前仍以 UV0 采样。

`createRenderable(gfx, modelData, meshIndex)` 内部等价于 C++ 的
`model3d::buildRenderable`（节点世界变换烘焙进顶点，材质 tint / metallic / roughness 与
albedo、normal、height 贴图自动应用，内嵌/外部贴图均可解析）。

> 注意：Squirrel 绑定按完整参数个数校验，`getMaterialTexturePath` /
> `getMaterialTextureEmbeddedIndex` 需要显式传 `slot`（默认 0）。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/model3d/`](../../../src/modules/model3d/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `model3d`。
