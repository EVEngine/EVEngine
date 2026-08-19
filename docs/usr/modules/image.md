# 图像模块

**脚本入口：** `eve.Image()`

解码图片文件 / 创建像素缓冲，并操作原始像素数据。

## 基本用法

```squirrel
local fs = eve.Filesystem();
local image = eve.Image();

// 从虚拟文件系统读取数据并解码（返回已绑定的 ImageData）。
local data = image.newImageData(fs.read("Textures/hero.png"));
print("size=" + data.getWidth() + "x" + data.getHeight() + " fmt=" + data.getFormat() + "\n");

// 或按 VFS 路径经统一资源缓存解码（同一路径共享一份 ImageData）：
local data2 = image.newImageDataFromFile("Textures/hero.png");

// 或创建空白像素缓冲并逐像素编辑：
local canvas = image.newEmptyImageData(64, 64, "RGBA8");
canvas.setPixel(10, 10, 1.0, 0.0, 0.0, 1.0);
```

`ImageData` 是 Image / Font / Model3D 共用的脚本类：`FontData.newGlyphImageData(...)` 和
`ModelData.getEmbeddedTextureImageData(...)` 返回的对象可以直接调用下面的像素与旋转接口。

## 对象关系与调用时机

`eve.Image()` 负责创建和解码 `ImageData`：

- `newImageData(data)`：解码 `fs.read()` / `fs.newFileData()` 得到的编码数据，返回新建的 `ImageData`（脚本持有）。
- `newImageDataFromFile(path)`：从 VFS 路径经**统一资源缓存**解码——同一路径重复加载共享一份 `ImageData`，
  文件变化时原地刷新（见 `docs/dev/superpowers/specs/2026-08-20-unified-resource-cache.md`）。
  该实例由缓存持有，脚本不要自行释放；需要独立副本时用 `clone()`。
- `newEmptyImageData(w, h, format)`：创建全透明/黑色画布，配合 `setPixel` / `getPixelR/G/B/A` 做 CPU 像素编辑，
  再用 `gfx.newTexture(imageData)` 上传为纹理。

`ImageData::rotate(radians, filter, expand)` 按**逆映射**旋转像素：遍历目标图每个像素，用逆旋转矩阵回算源坐标，
再按 `filter` 采样。

| 参数 | 含义 |
|------|------|
| `radians` | 弧度；与 `Math.rotate2*` 同号（Y 向下时视觉为顺时针，同 LÖVE） |
| `filter` | `"nearest"` / `"linear"` / `"rotsprite"`（Xenowhirl：Scale2x×3 → 偏移搜索 → 最近邻缩回） |
| `expand` | `true` 时画布扩到容纳整图 AABB；`false` 保持原尺寸（可能裁切） |

返回同格式的新 `ImageData`（调用方拥有）；源范围外采样为透明黑。`rotsprite` 只挑选已有调色板颜色，不引入插值新色。

## 目标导向指南

### 加载并上传一张 PNG 为纹理

```squirrel
local image = eve.Image();
local gfx = eve.Graphics();
local tex = gfx.newTexture(image.newImageDataFromFile("Textures/hero.png"));
```

同一路径重复调用只解码一次；文件修改后由热重载在原地刷新。

### CPU 生成 / 修改像素

用 `newEmptyImageData(w, h, "RGBA8")` 建画布，`setPixel(x, y, r, g, b, a)` 写入，`getPixelR/G/B/A` 读回；
`clone()` 深拷贝，`paste(src, dx, dy, sx, sy, sw, sh)` 拷贝子区域，`rotate(...)` 旋转。注意像素坐标越界会抛异常。

## 常见问题

- 把 `newImageData(data)` 与 `newEmptyImageData(w, h, format)` 混淆：前者解码编码数据，后者创建空白画布。
- 传给 `newImageData` 的不是 `fs.read()` 返回的数据对象：会得到类型转换错误。
- 修改 `newImageDataFromFile` 返回的像素：它由缓存共享，修改会影响同路径后续引用；需要独立副本先 `clone()`。
- 每帧把字形重新转纹理：应缓存 ImageData/Texture。

## API 快查

- `Image`：`getName()`、`newImageData(data)`、`newImageDataFromFile(path)`、`newEmptyImageData(width, height, format)`、`isCompressed(data)`
- `ImageData`：`getWidth()`、`getHeight()`、`getFormat()`、`getSize()`、`getPixelSize()`、`isSRGB()`、`inside(x, y)`、
  `clone()`、`paste(src, dx, dy, sx, sy, sw, sh)`、`rotate(radians, filter, expand)`、
  `getPixelR(x, y)`、`getPixelG(x, y)`、`getPixelB(x, y)`、`getPixelA(x, y)`、`setPixel(x, y, r, g, b, a)`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/image/`](../../../src/modules/image/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `image`。
