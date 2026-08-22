# 可交互积雪（Snow）

**脚本入口：** `eve.Snow()`

深度场积雪：雪面用一张与地形高度图同尺寸的 `SnowField` 浮点网格表达
（1 = 满雪，0 = 露地）。同一份数据驱动两条渲染路径：

- **真实位移（深坑）**：最终地形高度 = 地形 + 雪深 × 缩放，重建/原地更新
  高度图网格，脚印和弹坑是真实几何凹陷（轮廓、阴影、相交都正确）。
- **POM 微细节**：雪深网格上传为材质的 height texture（R 通道，白 = 隆起），
  配合 `Renderable3D.setHeightTexture` + `setParallax` 做视差遮蔽映射，
  补足网格分辨率给不了的脚印纹理、小雪球痕与雪面颗粒。
- **恢复**：`addSnowfall` 逐帧抬升雪深，配合节流重建形成“路被雪重新盖住”。

演示场景见 `examples/snow`（`eve run examples/snow`）。

## 基本用法

```squirrel
snow <- eve.Snow();

// 1. 雪场：和地形高度图同尺寸（例如 procgen.generateHeightmap 的 W×H）
local sf = snow.newField(W, H);
sf.fill(0.85);

// 2. 交互（cx/cz 为网格坐标，可带小数；radius/depth 单位均为格子/0..1）
sf.stampFootprint(playerX / CELL, playerZ / CELL, dirX, dirZ, 1.6, 0.45);
sf.stampImpact(hitX / CELL, hitZ / CELL, 3.0, 0.9);

// 3. 降雪回填（每帧调用，amount = dt × 速率）
sf.addSnowfall(dt * 0.05);

// 4. 真实位移：合成最终高度图并原地重建地形网格
local outHm = procgen.newHeightmap(W, H);
snow.applyToHeightmap(sf, terrainHm, outHm, 0.07);   // out = terrain + snow*scale
editor.updateHeightmapMesh(terrainMesh, gfx, outHm, CELL, HSCALE);

// 5. POM 微细节：上传/原地更新纹理并绑定为 albedo + height
local tex = snow.uploadTexture(sf, gfx);             // 只建一次
terrainEnt.setTexture(tex);
terrainEnt.setHeightTexture(tex);
terrainEnt.setParallax(0.045, 8, 32);                // scale / minLayers / maxLayers
// 雪变脏后原地更新（指针不变，无新纹理分配）：
snow.updateTexture(sf, tex, gfx);
```

## 参数与 API

### Snow 模块

| API | 说明 |
|---|---|
| `newField(w, h)` | 新建空雪场（调用方持有） |
| `applyToHeightmap(field, terrain, out, scale)` | `out(x,y) = terrain + field*scale`，用于网格位移重建 |
| `uploadTexture(field, gfx)` | 上传 RGBA8 纹理（R = POM 高度；G/B/A = 随雪深变暗的雪 albedo） |
| `updateTexture(field, texture, gfx)` | 原地替换纹理像素（返回 false 表示后端不支持，如 WebGPU） |

### SnowField

| API | 说明 |
|---|---|
| `resize(w, h)` / `getWidth()` / `getHeight()` | 网格尺寸 |
| `fill(v)` / `setHeight(x, y, v)` / `height(x, y)` | 读写单格（自动裁剪到 [0,1]；越界读 0、写忽略） |
| `stampFootprint(cx, cz, dirX, dirZ, radius, depth)` | 沿移动方向的椭圆脚印：碗状下陷 + 边缘堆雪 |
| `stampImpact(cx, cz, radius, depth)` | 抛物弹坑：中心深、边缘浅，坑沿外有一圈堆雪 |
| `addSnowfall(amount)` | 全格抬升到 [0,1]，用于降雪恢复 |
| `isDirty()` / `clearDirty()` | 脏标记：有编辑后为 true，重建/上传后手动清除 |

## 实现方式

- **数据**：纯 CPU 浮点网格（`src/modules/snow/SnowField.h`），无图形依赖，
  便于单元测试；渲染桥接在 `Snow` 模块（`uploadTexture` / `applyToHeightmap`）。
- **位移**：复用 `editor.newHeightmapMesh / updateHeightmapMesh` 的高度图网格，
  雪深直接加到高度值上，法线随重建自动重算，所以坑壁明暗正确。
- **POM**：引擎自带 `parallax_map.glsl`（陡峭视差 + 线性细化，TBN 由屏幕导数
  构造），在 `mesh3d.frag` / `mesh3d_clustered.frag` 中先做 UV 位移再采样
  albedo，因此同一纹理既当 albedo 又当 height map 时坑位会自动变暗。
- **纹理更新**：`Graphics::updateTexture`（本模块新增）原地替换像素，
  交互式雪无需每帧新建纹理。

> 注意：POM 是逐像素视差，不做轮廓/阴影的真实形变；深坑请交给真实位移
> （`applyToHeightmap` + 网格重建），POM 负责微细节。G-Buffer（延迟）与
> GPU-driven 片段路径目前未应用 POM，需要时在对应 shader 接入。
