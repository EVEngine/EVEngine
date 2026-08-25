# SpriteStack 2D 叠片渲染

SpriteStack 使用一组带透明通道的水平切片，在普通 2D 渲染管线中逐层偏移绘制，形成有体积感的 2.5D 图像。它不创建 3D 网格、3D 相机、深度缓冲或 3D 材质。

## 基本用法

```squirrel
local layers = spritestack.slicePrimitive("cylinder", 20, 128, 128, "y", 0.0);
local stack = spritestack.newStack(gfx);
stack.setLayerCount(layers.len());
for (local i = 0; i < layers.len(); i++) stack.setLayerImage(gfx, layers[i], i);
// 也可逐层加载：stack.setLayerFile(gfx, "assets/layer.png", 0);
// 横向图集可一次拆层：stack.setLayersFromAtlas(gfx, atlas, 20);

stack.setPosition(480.0, 390.0); // 2D 屏幕坐标
stack.setSize(180.0, 180.0);     // 每张切片的像素尺寸
stack.setThickness(4.0);         // 相邻切片的像素偏移
stack.setRotation(25.0);         // 2D 旋转角度，单位为度
stack.setTint(0.32, 0.82, 0.68, 1.0);
stack.setVisible(true);
stack.setShadowEnabled(true);
stack.setShadowOpacity(0.42);
stack.setShadowOffset(12.0, 9.0);
stack.setOutline(2.0, 0.02, 0.03, 0.04);

function eve_render() {
    gfx.clear();
    stack.render(gfx);
}
```

`render(gfx)` 直接按屏幕像素解释坐标；需要世界坐标时调用 `stack.renderWithCamera(gfx, camera)`。批对象同样提供 `batch.renderWithCamera(gfx, camera)`。

## 图层和批处理

```squirrel
local count = stack.getLayerCount();
local texture = stack.getLayerTexture(0);
stack.setLayerTexture(texture, 0);
stack.setLayerUV(0, 0.0, 0.0, 1.0, 1.0);
local spacing = stack.getThickness();

local batch = spritestack.newBatch(gfx);
batch.add(stack);
batch.remove(stack);
batch.add(stack);
batch.render(gfx);
batch.clear();
```

批对象提交多个 `SpriteStack2D`；实际纹理由 Graphics 的 2D texture batch 合并。切片从底层到顶层绘制，因此透明边缘和层间遮挡仍遵循 2D painter order。

## 模型切片

如果已有 `ModelData`，可以离线或加载时生成切片：

```squirrel
local model = model3d.newModelDataFromFile("assets/rock.obj");
local layers = spritestack.sliceModel(model, 24, 128, 128, "y", 0.0);
```

## 切片 API

- `slicePrimitive(kind, layerCount, imageW, imageH, axis, thickness)`：
  程序化几何体切片，`kind` ∈ `"box" | "cylinder" | "sphere" | "cone"`；
  `axis` ∈ `"x" | "y" | "z"`；`thickness <= 0` 时按包围盒自动均分。
- `sliceModel(modelData, layerCount, imageW, imageH, axis, thickness)`：
  对 `model3d.newModelDataFromFile(...)` 的模型切片。
- 两者都返回 `ImageData` 数组（RGBA8），可直接传给 `gfx.newTexture` 或
  `stack.setLayerImage`。

## 使用要点

- 切片的 `ImageData` 归脚本持有：传入 `setLayerImage` 后叠片内部已转为 GPU
  纹理，可释放原图。
- 叠片使用 alpha 混合 + 深度测试（不写深度）的管线，同堆切片每帧按相机距离
  由远到近排序；跨叠片的重叠由绘制顺序决定。
- Vulkan 与原生 WebGPU 均可用；WebGPU 使用专用 WGSL 卡片 shader。当前 Web/WASM
  精简模块组仍不包含 SpriteStack。
- 不要每帧重新切片或重新上传纹理；只在物体形状变化时重建。
- `updateMeshVertices`（合批内部使用）与 GPU 同步，重建频率越高开销越大。

**源码：** [`src/modules/spritestack/`](../../../src/modules/spritestack/)
**示例：** [`examples/sprite-stack/`](../../../examples/sprite-stack/)
**相关测试：** [`test/spritestack.cpp`](../../../test/spritestack.cpp)

这里的模型只用于生成 RGBA 切片；运行时仍走 2D 管线。
