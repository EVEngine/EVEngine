# 程序化房屋（HouseGen）

**脚本入口：** `eve.HouseGen()`

数据驱动的程序化房屋生成：先加载组件库（墙体/屋顶/门窗），再按 `HouseRequest`
（尺寸、层数、风格、地基、屋顶、入口）生成 `HouseLayout`（实例列表 + 诊断），
最后把布局转成可见建筑（模型/方块/占位）。

## 基本用法

```squirrel
hg <- eve.HouseGen();
local components = hg.loadComponentsFromFile("components/house.json");   // 组件库（必需）
if (!components.ok) throw components.status.summary;

local req = hg.newRequest();
req.setSeed(20260822);
req.setPlot(12, 10);
req.setFloors(2);
req.setStyle("cottage");

local layout = hg.newLayout();
local generation = hg.generate(req, layout);
if (generation.ok) {
    local json = layout.toJson();
    print("instances=" + layout.getInstanceCount() + " diag=" + layout.getDiagnosticCount() + "\n");
} else {
    print("failed: " + generation.status.summary + "\n");
}
```

## 目标导向指南

### 批量生成村庄

同 seed 同参数 → 相同布局：循环里换 `setSeed`/`setPlot` 即可稳定复现；
`layout.toJson()` 可落盘存档，`fromJson` 读回后与渲染器对接。

### 接入体素/模型渲染

遍历 `toJson()` 的实例（墙/屋顶/门窗盒体），映射到 `voxel` 方块或
`Renderable3D` 立方体。参考 `test/housegen_render.cpp` 的预览路径。

## API 快查

### `HouseGen`（模块）

- `loadComponentsFromJson(json)` / `loadComponentsFromFile(path)` / `clearComponents()` /
  `getComponentCount()`。
- `newRequest()` / `newLayout()`。
- `generate(request, layout)` → structured Result；失败原因在 Result 的诊断字段中。
- 所有可能失败的脚本调用都返回统一 Result 投影：检查 `ok`，再读取
  `code`、`status`、`diagnostics` 或 `value`；不存在 `lastError()` 旁路。

### `HouseRequest`

- `setSeed(int)`、`setPlot(w, d)`、`setFloors(int)`、`setStyle(string)`、
  `setFootprint(string)`、`setRoof(string)`、`setEntrance(string)`。

### `HouseLayout`

- `toJson()`、`fromJson(json)` → structured Result、`getInstanceCount()`、
  `getDiagnosticCount()`。

## 生命周期

- 必须先加载组件库，否则 `generate` 返回失败 Result，并携带结构化诊断。
- `HouseRequest` / `HouseLayout` 由脚本持有；`generate` 失败后可改参数重试。
- C++ `HouseLayout::instantiate` 返回 ECS `EntityHandle` 的 Result 集合；实体由 ECS
  world 所有，使用前必须通过 `ecs::try_get` 解析，不能缓存实体地址跨越 world mutation。
