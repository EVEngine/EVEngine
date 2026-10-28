# PixelWorld Graphics

`pixelworld_graphics` 是可裁剪的 PixelWorld 渲染适配层。它依赖 PixelWorld 与 Graphics，
但 PixelWorld 核心不反向依赖渲染模块。

以下片段要求已有本章关联的 PixelWorld world、有效 Graphics 和
pixelworldGraphics 模块。world 的材料注册及 tick 初始化见 [PixelWorld](pixelworld.md)；
renderer 不代替 world 的模拟。

```squirrel
local renderer = pixelworldGraphics.newRenderer(0, 0, 1024, 1024);

eve_render = function() {
    gfx.clear();
    renderer.sync(world, gfx);
    gfx.drawTexturedRect(renderer.getTexture(), 0.0, 0.0, 1024.0, 1024.0,
                         1.0, 1.0, 1.0, 1.0);
};
```

`sync()` 必须在渲染线程调用，参数只在调用期间借用。它返回本次写入 CPU 图集的脏
chunk 数；revision 未变化时返回 0 且不上传。同一帧所有脏 chunk 作为离散 region
批量写入原 GPU 纹理：Vulkan 只分配一个 staging buffer、录制一个 command buffer 并
提交一次，不会上传 chunk 间的空白包围区域，也不重建纹理、采样器或描述符。
`getTexture()` 返回 Graphics 拥有的借用
句柄，不得比 Graphics 活得更久。

`drawDiagnostics(world, gfx, scale)` 在材料图集上叠加 active/sleep/最高温度 Chunk
边框，并在右上角绘制最近 60 Tick 的耗时柱状图。参数仅在调用期间借用，返回实际绘制
的已分配 Chunk 数；该投影不拥有或修改 PixelWorld 状态。

诊断接口：`getOriginX()`、`getOriginY()`、`getWidth()`、`getHeight()`、
`getRenderedRevision()`、`getTotalUploadedChunks()` 与 `getUploadCount()`。

任务一是显示材料图集：固定模拟 tick 更新 world，渲染前同步已提交 revision。
任务二是诊断睡眠/温度：drawDiagnostics 读取同一 world，不另建诊断 world。
移除/新增 chunk 后仍由 sync 消费脏集；getTexture 是借用资源，先停止绘制并释放
renderer 消费者，再销毁 Graphics。
