# 可交互积雪（Snow）

**脚本入口：** `eve.Snow()`

深度场积雪：雪面用一张与地形高度图同尺寸的 `SnowField` 浮点网格表达
（1 = 满雪，0 = 露地）。同一份数据驱动两条渲染路径：

- **真实位移（深坑）**：最终地形高度 = 地形 + 雪深 × 缩放，重建/原地更新
  高度图网格，脚印和弹坑是真实几何凹陷（轮廓、阴影、相交都正确）。
- **可选 POM**：雪深网格可上传为 height texture（R 通道，白 = 隆起）。
  它与几何使用相同分辨率，不会凭空增加颗粒细节；示例只使用小于网格尺寸的偏移。
  `setParallax` 的 scale 是 UV 单位。近景示例默认关闭，P 键可启用约 1 mm 的偏移。
- **恢复**：`addSnowfall` 按完整模拟 dt 抬升雪深，形成“路被雪重新盖住”。

演示场景见 `examples/snow`（`eve run examples/snow`）。

示例在每帧结束时统一同步脏雪场，自动脚印、鼠标交互、重置和降雪都经过同一路径。
降雪按每帧完整 dt 累计；按住鼠标不会逐帧重复挖坑。网格已包含雪面坡度，
因此示例使用几何法线，不再叠加同一雪深生成的强法线贴图。
当前前向输出是 LDR，太阳强度与环境补光需留出余量，避免白雪被裁成纯白。

### 自适应近景示例

`examples/snow/adaptive_mesh.nut` 从 257×257 的雪深网格构建四叉树显示网格。
近景区域为 3.2×3.2 米，精细格为 1.25 cm，平坦区域可合并到 20 cm。
细分由雪深的 min/max 金字塔和地形插值误差控制，脚印完全落在粗块内部也不会漏检。
法线采样邻域一起细分，避免大三角形把坑壁法线拉成放射状条纹。
粗细块使用共享的边缘顶点和中心扇形三角形拼接，没有裙边或互相重叠的补片。
恢复和重置后重新合并；同一个 Mesh 原地更新顶点和索引。
这是基于表面变化的几何细分，尚不包含按屏幕像素误差选择 LOD。

默认展示五个鞋印，固定近景机位；O 键切换环绕。鞋印有收窄的腰部、鞋跟沟槽、
浅纹路及平滑堆雪边缘。压实雪保持冷白色，只有雪深接近零才渐变成露土颜色。
雪深网格是唯一模拟状态，四叉树和渲染数组均为可重建投影。

运行实际渲染回归（需要可用的显示与 Vulkan 环境）：
`python scripts/check_snow_example.py --eve build/win32-debug/src/engine/eve.exe --validation`。
它会验证脚印、弹坑、降雪时间累计、POM 开关、重置、三角形数减少与网格合并，
并检查所有内部边恰好被两个三角形共享，将引擎截图与日志写入
`build/snow-regression/`。

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
snow.applyToHeightmap(sf, terrainHm, outHm, 0.09);   // out = terrain + snow*scale
heightmapTargets.updateSmoothMesh(terrainMesh, gfx, outHm, CELL, HSCALE);
// 用 Smooth 变体：顶点法线来自高度场梯度，坑壁连续着色而不是平直三角片

// 5. 颜色与可选的小幅 POM；坡度法线由上面的平滑网格提供。
local texA = snow.uploadTexture(sf, gfx, "albedo");   // 只建一次
local texH = snow.uploadTexture(sf, gfx, "height");
terrainEnt.setTexture(texA);
terrainEnt.setHeightTexture(texH);
terrainEnt.setParallax(0.001, 8.0, 32.0);             // scale / minLayers / maxLayers
// 6. 阴影：给太阳创建带 castShadow 的 Light3D 平行光（旧 setDirectionalLight 不投影）
local sun = eve.Light3D();
sun.setType("dir");
sun.setDirection(-0.55, 0.62, 0.40);
sun.setColor(1.0, 0.97, 0.92, 0.85);
sun.setCastShadow(true);
// 雪变脏后原地更新（指针不变，无新纹理分配）：
snow.updateTexture(sf, texA, gfx, "albedo");
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
- **位移**：复用 `heightmapTargets.newSmoothMesh / updateSmoothMesh`
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
