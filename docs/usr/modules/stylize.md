# 风格化渲染（Stylize）

**脚本入口：** `eve.Stylize()`

NPR / 风格化后处理与网格着色：卡通、手绘水彩、水墨、像素。后处理以
`StylePass`（单 Pass）/ `StyleChain`（多 Pass ping-pong）作用于屏幕或离屏
Canvas；3D 网格着色用 `newMeshShader`；另有纯 CPU 的 `processImage` 供离线
工具与测试使用。

## 基本用法

```squirrel
st <- eve.Stylize();

// 全屏卡通后处理
local pass = st.newPass("cartoon");
pass.setFloat("edge", 0.5);

function eve_render() {
    gfx.clear();
    // ...绘制场景...
    pass.applyCanvas(gfx);   // 作用于当前 swapchain（引擎帧末自动合成）
}
```

## 目标导向指南

### 像素风滤镜

`st.newPass("pixel")` + `setFloat("size", ...)` 即可量化像素；多个风格叠加用
`st.newChain()` → `chain.add(pass)` → `chain.applyCanvas(gfx)`。

### 3D 网格卡通描边

```squirrel
local shader = st.newMeshShader("cartoon");   // 或 "ink"
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
- 后处理：`newPass(styleId)`、`newPassFromShader(id, spvOrWgsl)`、`newChain()`、
  `newPostShader(styleId)`。
- 网格：`newMeshShader(styleId)`。
- CPU：`processImage(styleId, image, params...)` → 新 ImageData。

### `StylePass`

- `getStyle()`、`hasParam(name)`、`setFloat/getFloat`、`setTime/getTime`。
- `apply(gfx)` / `applyCanvas(gfx)` / `applyTo(gfx, target)` / `applyCanvasTo(gfx, canvas)`。
- `getShader()`。

### `StyleChain`

- `clear()`、`add(pass)`、`getPassCount()`、`getPass(i)`、
  `apply(gfx)` / `applyCanvas(gfx)`。

## 生命周期

- Pass/Chain 由脚本持有；`applyCanvas` 在绘制完场景后调用一次。
- `supports(style, "gbuffer")` 可查询是否需要深度/法线输入（`RenderControl`
  开启 `gbuffer` 后可用）。
- 内置风格 id：`cartoon` / `watercolor` / `ink` / `pixel`。
