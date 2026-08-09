# Tiled 格式 + 非正交 Tilemap 设计

日期：2026-08-07
状态：已确认（方案 2）
存放：`docs/`（仓库忽略 `docs/dev/superpowers/`，故规格文档放此处）

## 目标

在现有 `map` 模块（正交 JSON 子集 + `TileLayer` / `TileRenderSystem`）上：

1. 支持可用的 **Tiled JSON 导出**（含 `base64` + `zlib`/`gzip` 层数据）
2. 支持投影：**isometric**、**staggered**、**hexagonal**（优先实现经典菱形 isometric）
3. **统一 2D 绘制队列**，使同 `layer` 内 tile 与精灵按脚点 Y（`depthY`）穿插排序
4. 解析 **对象层**（位置/类型等），供脚本挂实体；不自动生成渲染实体

## 非目标（v1）

- 无限地图（`infinite` / chunks）
- 外部 tileset（`.tsx` / `source`）
- TMX XML
- GID 翻转/对角旋转绘制（高位仍剥离，不改 UV）
- `compression: "zstd"`（当前无解码依赖；导出请用 zlib；可后补）
- 粒子 / UI 进入统一 2D 队列
- 视锥裁剪 / chunk 批处理（可后加）

## 背景（现状）

- `TileConfig` 已能读接近 Tiled 的正交 JSON：`layers` / `tilesets` / `data` 整数数组 / `firstgid` / opacity / offset
- `TileRenderSystem` 仅正交放置：`wx = originX + tx * tileW`
- `RenderSystem` 与 `TileRenderSystem` 分两次绘制，同层无法穿插
- `data` 模块已有 `base64` 编解码与 `zlib`/`gzip`/`deflate`/`lz4` 压缩

## 方案选择

采用 **方案 2：统一 2D 绘制队列**。

| 方案 | 摘要 | 结论 |
|------|------|------|
| 1 双系统各自 Y 排序 | 改动小 | 无法真正 tile↔精灵穿插 |
| **2 统一队列** | sprite + tile 同一 `(layer, depthY)` 排序 | **采用** |
| 3 每格一个 Renderable2D | 排序天然统一 | 实体爆炸，不适合正式地图 |

## 数据模型与投影 API

### `TileLayer::Config` 扩展

在现有字段上增加（缺省 = 正交，兼容现有 `demo.json`）：

| 字段 | 含义 | Tiled |
|------|------|-------|
| `orientation` | Orthogonal / Isometric / Staggered / Hexagonal | `orientation` |
| `staggerAxis` | X / Y | `staggeraxis` |
| `staggerIndex` | Odd / Even | `staggerindex` |
| `hexSideLength` | 六边形边长 | `hexsidelength` |

渲染仍使用**轴对齐纹理矩形**；菱形/六边形形状画在贴图内，引擎只改世界坐标与排序键。

### 投影（`map/TileProjection.h`）

纯函数，可单测：

- `tileToWorld(cfg, tx, ty) → (wx, wy)` — 与 Tiled 锚点一致（iso：菱形顶；staggered/hex：包围盒顶）
- `tileToDepthY(cfg, tx, ty) → float` — 脚点 Y，作同层排序键
  - orthogonal：`originY + (ty + 1) * tileH`
  - isometric：菱形底边中点 Y
  - staggered / hex：tile 底边 Y
- `worldToTile(cfg, wx, wy) → (tx, ty)` — 拾取用（取最近格）

脚本暴露：`tileToWorld` / `worldToTile` / `depthY`（或等价命名）。

### 对象层

轻量结构（不必是 ECS Entity）：

```text
MapObject { name, type, x, y, width, height, gid? }
```

- 解析 `layers[].type == "objectgroup"`
- **不**自动创建 `Renderable2D`
- 脚本按 `type`/`name` 挂实体，并用对象 `y` 或 `tileToDepthY` 设深度

API（采用 A）：

- `Map::loadFromFile` 填充模块内对象缓存
- `Map::getObjectCount()` / `Map::getObject(i)`（或等价只读访问）
- 热重载同 path 时同步刷新对象缓存

## 统一 2D 绘制队列

### `DrawItem2D`

共享结构（`graphics` 侧，或紧邻 `RenderSystem`）：

```text
canvas, layer, depthY,
x, y, w, h,
texture / UV 或 solid,
tint, camera, lit 相关字段
```

### 收集

1. **Renderable2D**：`depthY = transform.y + sprite.height * sy`（默认脚点 = 底边；可选 anchor 可后加）
2. **可见非空 tile**：`depthY = tileToDepthY(...)`；位置来自 `tileToWorld`；尺寸为 map/tileset 的 tile 像素尺寸；GID 0 不入队

### 排序（稳定）

`(offscreen canvas 优先) → canvas → layer → depthY → (lit / shader / texture 亲和)`

### 绘制入口

- 帧循环走统一入口（例如 `RenderSystem::render2D`）：先收集 sprite + tile，再排序绘制
- `TileRenderSystem::render` 变为向队列贡献 tile，或内联进统一入口；脚本 API 不变
- 粒子 / UI 保持独立 pass

### 脚本约定

- 同层穿插：相同 `layer`，`Transform2D.y` 为脚点世界 Y
- 整层压上/下：用不同 `layer`，不依赖 `depthY`

### 性能（v1）

全量收集非空 tile；视锥裁剪 / chunk 后加。

## Tiled JSON 解码

### Layer `data`

`decodeLayerData(layerObj, expectedCount, outGids, error)`：

1. **整数数组** — 现有路径
2. **`encoding: "base64"` 字符串** — `data::decode("base64", …)`
3. **`compression`**：`zlib` / `gzip` → `data::decompress`；空或无压缩字段则直接解析字节；`zstd` → 明确失败

字节流按小端 `uint32` GID 解析；长度必须等于 `width * height`，否则失败。

### 地图全局

`applyMapGlobals` / `loadMapFile` 读取并写入各 `TileLayer::Config`：

`orientation`, `staggeraxis`, `staggerindex`, `hexsidelength`

多 tile layer 共享同一套投影参数。

### 加载流程

```text
读 JSON
→ 解析 map 全局 + 第一个内嵌 tileset（带 source 的跳过并记录错误/警告）
→ 遍历 layers：
    tilelayer   → TileLayer + decodeLayerData + draw/offset
    objectgroup → 追加 MapObject
    其他        → 忽略
→ 绑定 Resource.path / hot-reload
```

失败时：不留下半初始化 layer（`loadMapFile` 回滚已创建实体）。未知 `orientation` → 失败（不静默回退）。

## 错误处理

| 情况 | 行为 |
|------|------|
| JSON 非法 / 非 object | 失败 + error 字符串 |
| base64 / 解压失败 | 失败 |
| GID 数量不匹配 | 失败 |
| `zstd` | 失败，提示改用 zlib |
| 未知 orientation | 失败 |
| 外部 tileset `source` | 跳过该 tileset；若无可用内嵌 tileset 则无贴图（可 solid 调试色） |

## 测试

- 投影：ortho / iso / staggered / hex 的 `tileToWorld` / `worldToTile` / `depthY` 固定样例
- 解码：明文数组 + base64+zlib 往返
- 对象层：最小 JSON 读出 name/type/xy
- 回归：现有 `test/map.cpp` 正交用例

## 主要改动文件（预期）

- `src/modules/map/TileLayer.h` — Config 字段 + 脚本投影 API
- `src/modules/map/TileProjection.h` (+ `.cpp` 若需要)
- `src/modules/map/TileConfig.cpp` — orientation、压缩 data、objectgroup
- `src/modules/map/Map.h` / `Map.cpp` — 对象缓存 API、渲染入口协调
- `src/modules/map/TileSystem.cpp` — 贡献 tile draw items
- `src/modules/graphics/RenderSystem.h` / `.cpp` — 统一队列 + depthY 排序
- `test/map.cpp`（及必要时新测试文件）

## 成功标准

1. Tiled 导出的正交 / isometric / staggered / hexagonal JSON（base64+zlib）可加载并正确放置
2. 同 layer 内精灵与 tile 按脚点 Y 正确遮挡
3. 对象层可枚举；脚本可据此生成实体
4. 现有正交 demo / 测试不回归
```
