# 3D 模型模块

**脚本入口：** `eve.Model3D()`

通过 medialoader/Assimp 载入模型数据，再交给图形模块渲染。

## 基本用法

```squirrel
local models = eve.Model3D();
local mesh = models.newModelDataFromFile("models/hero.glb");
```

## 对象关系与调用时机

`Model3D` 只负责加载 `ModelData`；ModelData 包含 mesh/material 数据和统计信息。GPU mesh、材质实例、摄像机与实际 draw 属于 Graphics。

## 目标导向指南

### 载入并检查模型

用 `newModelDataFromFile(path)` 载入 glTF/FBX/OBJ 等支持格式；先读取 mesh/material/face/vertex 数量并检查法线、UV，失败时显示占位模型。资源应在初始化或加载阶段创建。

### 提交模型渲染

将 ModelData 转换为 Graphics 所需 mesh/renderable，设置材质、变换和光照标志，再在渲染阶段调用 3D 渲染入口。模型数据可复用，实例只保存不同变换。

## 常见问题

- 在 render 中载入模型：I/O 和解析必须在加载阶段。
- 模型无 normals 仍启用光照：先检查 `hasNormals()`。
- 每个实例重复加载同一文件：共享 ModelData 和 GPU 资源。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `empty()`、`getFaceCount()`、`getMaterialCount()`、`getMeshCount()`、`getName()`、`getVertexCount()`、`hasNormals()`、`hasTexCoords()`
- `newModelData()`、`newModelDataFromFile()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/model3d/`](../../../src/modules/model3d/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `model3d`。
