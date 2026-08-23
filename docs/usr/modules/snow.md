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
//    脚印/弹坑会在边缘留下随机化隆起的踢雪堆（每格哈希决定堆雪高度）
sf.stampFootprint(playerX / CELL, playerZ / CELL, dirX, dirZ, 1.7, 0.52);
sf.stampImpact(hitX / CELL, hitZ / CELL, 3.0, 0.9);

// 3. 降雪回填（每帧调用，amount = dt × 速率）
sf.addSnowfall(dt * 0.05);

// 4. 真实位移：合成最终高度图并原地重建地形网格
local outHm = procgen.newHeightmap(W, H);
snow.applyToHeightmap(sf, terrainHm, outHm, 0.07);   // out = terrain + snow*scale
editor.updateHeightmapMeshSmooth(terrainMesh, gfx, outHm, CELL, HSCALE);
// 用 Smooth 变体：顶点法线来自高度场梯度，坑壁连续着色而不是平直三角片

// 5. POM 微细节：同一雪场导出三张纹理——albedo（雪/地颜色）、normal（深度梯度
//    法线，让坑壁被光照出来）、height（R = 雪深，白 = 隆起）
local texA = snow.uploadTexture(sf, gfx, "albedo");   // 只建一次
local texN = snow.uploadTexture(sf, gfx, "normal");
local texH = snow.uploadTexture(sf, gfx, "height");
terrainEnt.setTexture(texA);
terrainEnt.setNormalTexture(texN);
terrainEnt.setHeightTexture(texH);
terrainEnt.setParallax(0.06, 8.0, 32.0);              // scale / minLayers / maxLayers
// 6. 阴影：给太阳创建带 castShadow 的 Light3D 平行光（旧 setDirectionalLight 不投影）
local sun = eve.Light3D();
sun.setType("dir");
sun.setDirection(-0.55, 0.62, 0.40);
sun.setColor(1.02, 1.00, 0.97, 1.45);
sun.setCastShadow(true);
// 雪变脏后原地更新（指针不变，无新纹理分配）：
snow.updateTexture(sf, texA, gfx, "albedo");
snow.updateTexture(sf, texN, gfx, "normal");
snow.updateTexture(sf, texH, gfx, "height");
```

## 参数与 API

### Snow 模块

| API | 说明 |
|---|---|
| `newField(w, h)` | 新建空雪场（调用方持有） |
| `applyToHeightmap(field, terrain, out, scale)` | `out(x,y) = terrain + field*scale`，用于网格位移重建 |
| `uploadTexture(field, gfx, kind)` | 上传 RGBA8 纹理；kind = `"height"`（R = 雪深，POM 高度图）、`"albedo"`（雪/地颜色）或 `"normal"`（深度梯度法线） |
| `updateTexture(field, texture, gfx, kind)` | 原地替换对应纹理像素（返回 false 表示后端不支持，如 WebGPU） |

### SnowField

| API | 说明 |
|---|---|
| `resize(w, h)` / `getWidth()` / `getHeight()` | 网格尺寸 |
| `fill(v)` / `setHeight(x, y, v)` / `height(x, y)` | 读写单格（自动裁剪到 [0,1]；越界读 0、写忽略） |
| `stampFootprint(cx, cz, dirX, dirZ, radius, depth)` | 沿移动方向的椭圆脚印：碗状下陷 + 边缘随机化踢雪堆 |
| `stampImpact(cx, cz, radius, depth)` | 抛物弹坑：中心深、边缘浅，坑沿外一圈随机化溅射雪堆 |
| `addSnowfall(amount)` | 全格抬升到 [0,1]，用于降雪恢复 |
| `isDirty()` / `clearDirty()` | 脏标记：有编辑后为 true，重建/上传后手动清除 |

## 实现方式

- **数据**：纯 CPU 浮点网格（`src/modules/snow/SnowField.h`），无图形依赖，
  便于单元测试；渲染桥接在 `Snow` 模块（`uploadTexture` / `applyToHeightmap`）。
- **位移**：复用 `editor.newHeightmapMeshSmooth / updateHeightmapMeshSmooth`
  的高度图网格（顶点法线 = 高度场梯度，坑壁连续着色、无平直三角片），
  雪深直接加到高度值上，法线随重建自动重算。
- **POM**：引擎自带 `parallax_map.glsl`（陡峭视差 + 线性细化，TBN 由屏幕导数
  构造），在 `mesh3d.frag` / `mesh3d_clustered.frag` 中先做 UV 位移再采样
  albedo。雪场导出**三张独立纹理**：height（R = 雪深，驱动 POM）、albedo
  （雪/地颜色渐变）、normal（深度梯度法线，让坑壁与踢雪堆被光照出来）；
  三张都随 `updateTexture` 原地刷新，指针不变。
- **纹理更新**：`Graphics::updateTexture`（本模块新增）原地替换像素，
  交互式雪无需每帧新建纹理。

> 注意：POM 是逐像素视差，不做轮廓/阴影的真实形变；深坑请交给真实位移
> （`applyToHeightmap` + 网格重建），POM 负责微细节。G-Buffer（延迟）与
> GPU-driven 片段路径目前未应用 POM，需要时在对应 shader 接入。
