# 风格化渲染（Stylize）

**脚本入口：** `eve.Stylize()`

NPR / 风格化后处理与网格着色：卡通、手绘水彩、水墨、像素。后处理以
`StylePass`（单 Pass）/ `StyleChain`（多 Pass ping-pong）作用于屏幕或离屏
Canvas；3D 网格着色用 `newMeshShader`；另有纯 CPU 的 `processImage` 供离线
工具与测试使用。

风格定义由模块统一注册，运行时通过 `StyleInstance` 保存参数覆盖；shader
仍由 Graphics 管理并可在多个实例之间复用。参数 schema 提供默认值和有效范围，
`setFloat` 会自动限制到范围内。

## 基本用法

```squirrel
local source = gfx.newCanvas(960, 540);
local output = gfx.newCanvas(960, 540);
local toon = stylize.newInstance("cartoon");
toon.setFloat("outlineStrength", 1.4);
local recipe = stylize.newRecipe();
recipe.add(toon);
recipe.compile(gfx);

function eve_render() {
    gfx.setCanvas(source);
    gfx.clear();
    // ...绘制场景...
    gfx.setCanvas(null);

    recipe.applyCanvas(gfx, source, output);
    gfx.drawCanvas(output, 0, 0, 960, 540);
}
```

## 目标导向指南

### 像素风滤镜

`stylize.newInstance("pixel")` + `setFloat("pixelSize", ...)` 即可量化像素；
同一 stage 的多个效果用 `newRecipe()` 组合。

### 3D 网格卡通描边

```squirrel
local toon = stylize.newInstance("cartoon");
local shader = toon.newMeshShader(gfx);   // ink / xray 同理
local mat = gfx.newMaterial();
mat.setShadingModel("custom");
// 挂到 Renderable3D：r.setMaterial(mat) / r.setPart(...)
```

## API 快查

### `Stylize`（模块）

- `getName()`：模块名（"Stylize"）。
- 风格注册表：`getStyleCount` / `getStyleId(i)` / `hasStyle(id)` /
  `hasMeshStyle(id)` / `supports(style, feature)` / `getStyleParamCount` /
  `getStyleParamName`。
- 实例：`newInstance(styleId)` → `StyleInstance`。
- 配方：`newRecipe()` → `StyleRecipe`，按 priority 编译并自动管理中间 Canvas。
- 后处理：`newPass(styleId)`、`newPassFromShader(id, spvOrWgsl)`、`newChain()`、
  `newPostShader(styleId)`。
- 网格：`newMeshShader(styleId)`。
- CPU：`processImage(styleId, image, params...)` → 新 ImageData。

### `StylePass`

- `getStyle()`、`hasParam(name)`、`setFloat/getFloat`、`setTime/getTime`。
- `apply(gfx)` / `applyCanvas(gfx)` / `applyTo(gfx, target)` / `applyCanvasTo(gfx, canvas)`。
- `getShader()`。
- 调度契约：`getStage()`、`getPriority()`、`setPriority(value)`、
  `requiresInput(name)`。

### `StyleInstance`

- schema：`getStyle()`、`getParamCount()`、`getParamName(i)`、
  `getParamDefault(name)`、`getParamMin(name)`、`getParamMax(name)`、`hasParam(name)`。
- override：`setFloat/getFloat`、`isOverridden(name)`、`reset(name)`、`resetAll()`。
- technique：`newPass(gfx)` 创建后处理 Pass；`newMeshShader(gfx)` 创建网格 shader。
- 调度契约：`getStage()`、`getPriority()`、`requiresInput(name)`。

### `StyleChain`

- `clear()`、`add(pass)`、`getPassCount()`、`getPass(i)`、
  `apply(gfx)` / `applyCanvas(gfx)`。

`StyleChain` 是兼容层；新代码优先使用 `StyleRecipe`。

### `StyleRecipe`

- `add(instance)` / `clear()` / `getStyleCount()` / `getStyle(i)`。
- `compile(gfx)` 会按 `getPriority()` 稳定排序，并拒绝混用不同 injection stage。
- `isCompiled()` / `getStage()`。
- `apply(gfx, source, dest)` / `applyCanvas(gfx, source, dest)`；多 Pass 所需的
  ping-pong Canvas 由 recipe 按目标尺寸自动创建和复用。

## 生命周期

- Pass/Chain 由脚本持有；`applyCanvas` 在绘制完场景后调用一次。
- `getStage()` 返回 `afterOpaque` / `beforeTransparent` / `beforeTonemap` /
  `afterTonemap`；`requiresInput` 只报告 shader 实际读取的输入。
- `supports(style, "gbuffer")` 可查询 definition 是否要求 depth 或 normal。
- 内置风格 id：`cartoon` / `watercolor` / `ink` / `pixel` / `xray`。
