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

### 3D 角色技能 mesh 特效

除静态 `StyleInstance` 外，C++ API 提供 `MeshEffectInstance` 管理目标强句柄、
样式参数以及淡入、持续、淡出生命周期。目标仍由 graphics 拥有和解析，播放
时间仅通过调用方传入的 `dt` 推进。

```cpp
auto effect = stylize->createMeshEffect("ember");
effect->bindTarget(MeshEffectTargetHandle(slot, generation));
effect->setPlayback({0.08f, 0.35f, 0.15f, false});
effect->style().setFloat("burnAmount", 0.72f);
effect->play();
effect->update(dt);
```

`TrailEmitter` 接收刀根和刀尖的世界坐标，执行最小距离过滤、寿命淘汰与瞬移
断轨，并生成后端无关的三角形 Ribbon 快照：

```cpp
auto trail = stylize->createTrailEmitter();
trail->update(dt);
trail->append(bladeRoot, bladeTip);
TrailMeshSnapshot ribbon = trail->buildMesh();
```

当前接口提供 CPU/runtime 核心。角色蒙皮复用、overlay draw、透明排序和
Ribbon GPU buffer 上传由 `MeshEffectRenderer` 负责。renderer 必须在已打开的
3D frame 内调用；它会复用角色原 Mesh 的蒙皮数据，并为 Ribbon 使用 graphics
已有的动态 mesh ring buffer：

```cpp
auto renderer = stylize->createMeshEffectRenderer(*graphics);

graphics->begin3DFrameToCanvas(target);
auto overlayResult = renderer->submitOverlay(*effect, resolvedMeshSource);
auto trailResult = renderer->submitTrail(*slashEffect, trail->buildMesh());
graphics->end3DFrameToCanvas();

if (!overlayResult.ok() || !trailResult.ok()) {
    // Inspect the structured Status/Diagnostic before continuing.
}
```

```squirrel
local toon = stylize.newInstance("cartoon");
local shader = toon.newMeshShader(gfx);   // ink / xray 同理
local mat = gfx.newMaterial();
mat.setShadingModel("custom");
// 挂到 Renderable3D：r.setMaterial(mat) / r.setPart(...)
```

常用技能常见视觉风格可直接通过 `StyleInstance` 新建并映射为角色网格材质：

- `slash`：刀光/斩击轨迹
- `ember`：燃烧/灼烧反馈
- `aura`：护盾、增益减益光环
- `rim`：边缘发光，`dissolve`：溶解退场，`hologram`：全息，`snow`：覆盖式覆盖

第三阶段提供 `SkillMeshEffect` 组合层，统一持有生命周期和可选 Ribbon。内置
`WeaponSlash`、`ImpactFlash`、`ChargeAura`、`BurningBody` 四种配方：

```cpp
auto skill = stylize->createSkillMeshEffect(SkillMeshEffectKind::WeaponSlash);
skill->bindTarget(targetHandle);
skill->play();
skill->update(dt);
skill->appendBlade(bladeRoot, bladeTip);

auto overlay = renderer->submitOverlay(skill->effect(), resolvedMeshSource);
auto ribbon = renderer->submitTrail(skill->effect(), skill->trail().buildMesh());
```

`SkillMeshEffect` 不解析或持有场景对象；目标销毁、句柄代次校验和绘制顺序仍由
调用方负责。所有时间由调用方注入，配方在相同 `dt` 与刀刃采样序列下确定性一致。

```squirrel
local fx = stylize.newInstance("slash");
fx.setFloat("coreR", 1.0);
fx.setFloat("coreG", 0.95);
fx.setFloat("coreB", 1.0);
fx.setFloat("intensity", 1.8);
fx.setFloat("speed", 1.2);
local fxShader = fx.newMeshShader(gfx);
local mat = gfx.newMaterial();
mat.setShadingModel("custom");
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
- Mesh 风格扩展：`rim` / `dissolve` / `hologram` / `snow` / `slash` / `ember` / `aura`。
