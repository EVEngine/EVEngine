# Sprite-Stacking 伪 3D

**脚本入口：** `eve.SpriteStack()`

把三维模型沿某个轴切成 `layerCount` 张薄片图（RGBA），运行时刻把这些薄片按
世界坐标叠起来渲染，从设计视角看就是有体积的伪 3D 物体——经典 sprite-stacking
技术，常用于低多边形 / 像素风游戏的角色与物件。

## 基本用法

```squirrel
// 1. 切片：程序化几何体（cylinder / sphere / cone / box），无需资产文件
local layers = spritestack.slicePrimitive("cylinder", 20, 128, 128, "z", 0.0);

// 也支持对 3D 模型文件切片（model3d 模块）
// local md = model3d.newModelDataFromFile("assets/tree.obj");
// local layers = spritestack.sliceModel(md, 20, 128, 128, "z", 0.0);

// 2. 建叠片对象并填充层纹理
local stack = spritestack.newStack(gfx);
stack.setLayerCount(layers.len());
for (local i = 0; i < layers.len(); i++)
    stack.setLayerImage(gfx, layers[i], i);

stack.setThickness(0.13);       // 相邻切片的世界间距
stack.setSize(2.4, 2.8);        // 切片四边形尺寸（世界单位）
stack.setPosition(0.0, 0.0, 0.0);
stack.setYaw(0.0);
stack.setTint(0.35, 0.78, 0.72, 1.0);
stack.setMode("vertical");      // "vertical" | "horizontal"
stack.setShadowEnabled(true);   // 投影假阴影
stack.setOutline(0.05, 0.02, 0.03, 0.04);  // 描边
```

渲染时机：在 `gfx.render3D()` 之后、`present()` 之前调用，切片会进入 3D 前向
通道，和场景内其他网格正确深度遮挡。

```squirrel
function eve_render() {
    gfx.clear();
    gfx.render3D();
    stack.render(gfx);          // 自动使用当前激活的 Camera3D
    // stack.renderWithCamera(gfx, myCamera); // 也可显式指定相机
}
```

## 两种切片模式

| 模式 | 切片轴 | 运行时刻渲染 | 适合 |
|------|--------|--------------|------|
| `"vertical"`（默认） | `"z"` 等水平轴 | 朝向相机的竖直 billboard，yaw 旋转叠片的深度布局 | 经典「面包切片」伪 3D，正面视角最扎实 |
| `"horizontal"` | `"y"`（俯视） | 水平四边形，3/4 相机看到真实体积视差 | 俯视 / 斜 45° 视角的场景物件 |

切片轴与模式要匹配：`slicePrimitive(..., "z", ...)` + `"vertical"`，
或 `slicePrimitive(..., "y", ...)` + `"horizontal"`。

## 对象与参数

- `newStack(gfx)`：新建空叠片（脚本 VM 持有；不要直接构造 `eve.SpriteStack3D`）。
- 图层：`setLayerCount(n)`、`setLayerTexture(tex, i)`、`setLayerImage(gfx, img, i)`、
  `setLayerFile(gfx, path, i)`、`setLayersFromAtlas(gfx, atlasTex, count)`（单张横向
  条带纹理按列切成 count 层）。
- 变换：`setThickness(t)`、`setSize(w, h)`、`setPosition(x, y, z)`、`setYaw(rad)`。
- 外观：`setTint(r, g, b, a)`、`setAlphaCutoff(v)`、`setVisible(bool)`、`setMode(mode)`。
- 渲染：`render(gfx)` 或 `renderWithCamera(gfx, camera)`。

## 高级功能

### 叠片投影阴影

`setShadowEnabled(true)` 后，每层切片沿光方向压到地面平面（`setShadowPlaneY(y)`），
用切片本身的剪影画半透明黑色，形成接触阴影：

```squirrel
stack.setShadowEnabled(true);
stack.setShadowOpacity(0.38);
stack.setShadowLight(-0.45, -1.0, -0.35);  // 光照方向（指向地面）
stack.setShadowPlaneY(-1.53);              // 地面高度
```

这是经典的假阴影（不是 shadow map），适合固定视角的伪 3D 场景；内部会把阴影
平面抬高 2cm 避免与地面深度冲突。

合批时阴影同样生效：`SpriteStackBatch` 会把投影阴影按黑色色调单独分组烘焙，
不额外增加 draw call。

### 叠片描边

`setOutline(width, r, g, b)` 给每层画一个放大 k 的黑色剪影垫在切片后面，形成
风格化描边（配合 `stylize` 的 ink / pixel 后处理很搭）：

```squirrel
stack.setOutline(0.05, 0.02, 0.03, 0.04);
```

合批时描边同样生效（按描边色调单独分组）。

### 叠片参与 G-Buffer（AO / 描边后处理）

默认叠片只画进前向通道，AO / 描边这类读取 G-Buffer 深度的后处理看不到它们。
开启 `setGbufferEnabled(true)` 后，叠片会通过 `RenderSystem3D` 的扩展绘制钩子
写进 G-Buffer（alpha 剔除管线），描边和 AO 就能沿切片剪影生效：

```squirrel
stack.setGbufferEnabled(true);
gfx.getRenderControl().enable("outline");
```

### 叠片投射 CSM 阴影

默认叠片不写 shadow map。开启 `setCastShadow(true)` 后，切片通过 alpha 剔除的
shadow 管线写进级联阴影贴图（CSM），在地面等接收阴影的网格上留下**剪影形状的
阴影**，而不是整块矩形：

```squirrel
stack.setCastShadow(true);
```

需要场景里有 `castShadow` 的平行光；如果没有 `Light3D`，会回退用
`gfx.setDirectionalLight(...)` 设置的旧版平行光作为阴影光源。与 `setShadowEnabled`
的投影假阴影相互独立，可同时开启。

### 多叠片合批 `SpriteStackBatch`

多个叠片如果共享图层纹理（`setLayerTexture` 传入同一个 `Texture`），可以用
`newBatch(gfx)` 把它们合成每帧一次 draw call（按纹理 + 色调分组）：

```squirrel
local batch = spritestack.newBatch(gfx);
batch.add(stackA);
batch.add(stackB);
// eve_render 中：batch.render(gfx);
```

合批会在叠片内容变化（位置 / 缩放 / 纹理 / 色调）时重建网格（`Graphics::updateMeshVertices`
原地更新）；每帧都旋转/移动的叠片建议直接用 `stack.render`，静态道具用合批收益最大。
合批会一并烘焙逐叠片的阴影与描边（它们按各自色调分组）。

### Atlas 条带打包工具

`scripts/pack_sprite_stack_atlas.py` 把一组等尺寸的层 PNG 横向打包成一张条带，
运行时用 `setLayersFromAtlas(gfx, atlasTex, count)` 直接按列切回：

```bash
python3 scripts/pack_sprite_stack_atlas.py layer_00.png layer_01.png ... \
    -o atlas.png --json atlas.json
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
