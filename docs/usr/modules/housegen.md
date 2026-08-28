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

## 生成能力

### 房间划分（室内）

`requiredRooms` 传入多个房间名（如 `["living","kitchen","bedroom"]`），且组件库提供
`interior_wall` / `interior_door` 分类时，每一层会真正划分为多个带标签的房间：
递归二分占地，沿分隔线生成内墙 + 居中门洞，`layout.rooms` 携带每个房间的
`type`/`x`/`y`/`z`/`width`/`depth`。若库缺少内部分区组件，则回退为整层一个房间并写入
诊断。

### 楼梯 / 楼层连通

`floors > 1` 时生成器自动在占地内部找一个单元格作为楼梯井，每层在该列放置一个
`stairs` 组件（替换该格的楼板），形成上下贯通的楼梯竖井；顶部用屋顶覆盖。组件库必须
提供 `stairs` 分类，且占地要有内部单元格，否则多楼层生成失败。

### 风格包（资产包）

组件可通过 `tags` 标记风格。`hasCompletePack` / `completePacks` 判定一组
`foundation`/`floor`/`wall`/`door`/`roof` 是否齐备；`HouseRequest.setStyle(name)` 若指定
不完整的包会直接失败（不会与无风格组件混搭），`getStylePacks()` 可枚举可用完整包。

### 自由轮廓占地（多边形）

`setFootprint("polygon")` + 提供 `perimeter`（角坐标 `[0,width]×[0,depth]` 的闭合多边形，
至少 3 点）可生成任意形状的建筑，墙体沿轮廓外缘生成、楼板/屋顶填充内部，其余能力
（房间、楼梯、风格包）全部复用。对应 UE PCG 中"沿 spline 轮廓建楼"的经典工作流。

## procgen 集成（持久化 + 热重载）

housegen 在 L5 消费 procgen 的确定性身份与工件仓库协议（`HousePersistence`）：

- `requestBuildKey(req)` / `layoutBuildKey(layout)` 返回确定性身份文本（含全部输入，
  含多边形 perimeter），用于热重载比较：输入不变则身份不变，避免重复生成。
- `publishLayout(layout)` 把布局作为 procgen 工件原子发布，返回工件 id 文本；
  `findLayout(idText)` 读回（返回布局 JSON）；`snapshotLayouts()` / `restoreLayouts(json)`
  做存档 round-trip；`layoutCount()` / `clearLayouts()` 管理仓库。
- 相同输入发布同一定义确定性身份，重复发布返回 Conflict。

## API 快查补充

- `HouseGen`：`requestBuildKey` / `layoutBuildKey` / `publishLayout` / `findLayout` /
  `snapshotLayouts` / `restoreLayouts` / `clearLayouts` / `layoutCount` / `getStylePacks`。
- `HouseRequest`：`setFootprint` 支持 `"polygon"`，多边形顶点通过 C++ `perimeter` 提供。
- `HouseLayout`：`rooms` 的 `HouseRoom` 含 `z` 楼层字段。
