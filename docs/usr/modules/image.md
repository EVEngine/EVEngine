# 图像模块

**脚本入口：** `eve.Image()`

说明 Image 的 C++ 能力与当前 Squirrel 绑定边界；当前脚本入口只公开模块身份查询。

## 基本用法

当前 `eve.Image()` 的脚本绑定只公开模块身份查询；`newImageData` 等 C++ 方法尚未绑定到 Squirrel。游戏脚本可以通过 `FontData.newGlyphImageData(...)` 获得已绑定的 `ImageData`；任意图片文件的直接解码目前主要供 C++ 和引擎内部资源加载流程使用：

```squirrel
local image = eve.Image();
print(image.getName() + "\n");
// 若已从 FontData 得到 ImageData，可交给 gfx.newTexture(imageData)。
```

不要照搬 `Image.h` 中只面向 C++ 的方法到脚本。需要直接编辑像素时，应先确认目标方法已在 `Image::expose` 中注册。通过 `FontData` 暴露的 `ImageData` 目前支持 `getWidth` / `getHeight` / `getFormat` / `getSize` / `rotate`。

## 对象关系与调用时机

C++ `Image` 能创建和解码 `ImageData`，但当前 `Image::expose()` 仅绑定 `getName()`。用户文档严格描述脚本可用面，避免把内部接口误写成 Squirrel API。

`ImageData::rotate(radians, filter, expand)`（C++，脚本经 Font 绑定的 ImageData 也可调用）按**逆映射**旋转像素：遍历目标图每个像素，用逆旋转矩阵回算源坐标，再按 `filter` 采样。

| 参数 | 含义 |
|------|------|
| `radians` | 弧度；与 `Math.rotate2*` 同号（Y 向下时视觉为顺时针，同 LÖVE） |
| `filter` | `"nearest"` / `"linear"` / `"rotsprite"`（Xenowhirl：Scale2x×3 → 偏移搜索 → 最近邻缩回） |
| `expand` | `true` 时画布扩到容纳整图 AABB；`false` 保持原尺寸（可能裁切） |

返回同格式的新 `ImageData`（调用方拥有）；源范围外采样为透明黑。`rotsprite` 只挑选已有调色板颜色，不引入插值新色。

## 目标导向指南

### 判断脚本能否直接处理图片

先检查 `Image::expose`：当前脚本侧不能调用 `newImageData` 解码任意文件。因此普通游戏资源应通过 Tilemap、模型加载器或其他已绑定资源入口载入，不要假设 C++ public 方法可用。

### 从字体字形创建纹理

通过 `FontData.newGlyphImageData(codepoint)` 得到已绑定的 `ImageData`，再传给 `gfx.newTexture(imageData)`。保留纹理对象，避免每帧重新栅格化同一字形。

## 常见问题

- 直接调用 `image.newImageData()`：当前会报“方法不存在”。
- 每帧把字形重新转纹理：应缓存 ImageData/Texture。
- 把源码 public 方法当脚本方法：始终核对 `expose()`。

## API 快查

- `getName()`：返回模块名。
- C++ 侧的 `newImageData(...)`、`isCompressed(...)`、`newCubeFaces(...)` 和 `newVolumeLayers(...)` 当前不是脚本 API。
- `ImageData.rotate(radians, filter, expand)`：CPU 像素旋转（经 Font 绑定的 ImageData）；`filter` 为 `"nearest"` / `"linear"` / `"rotsprite"`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/image/`](../../../src/modules/image/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `image`。
