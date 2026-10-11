# 几何破碎表现（DestructionFx）

**脚本入口：** `eve.DestructionFx()`（slot `destructionFx`）

`physics_destruction` 的可选渲染卫星。核心 Instance 可在无 graphics 的构建中
`step`；需要骨块盒代理绘制或 Sleep 合批时再开本模块。

完整仿真 API 见 [`physics_destruction.md`](physics_destruction.md)。

## 基本用法

```squirrel
if (has_module("destructionFx")) {
    local fx = eve.DestructionFx();
    local renderer = fx.createRenderer(instance);
    renderer.setExteriorColor(0.62, 0.58, 0.52, 1.0);
    renderer.setInteriorColor(0.78, 0.42, 0.28, 1.0); // Detached
    renderer.setSleepColor(0.45, 0.45, 0.48, 1.0);
    // eve_render:
    gfx.clear();
    gfx.render3D();
    renderer.draw(gfx);
}
```

示例：[`examples/destruction-basic`](../../../examples/destruction-basic/)。

## API 快查

### `DestructionFx`

- `createRenderer(instance)`：返回脚本 VM 拥有的 `GeometryCollectionRenderer`；
  只借用 Instance，失败抛脚本异常。

### `GeometryCollectionRenderer`

- `setInstance` / `getInstance`
- `setExteriorColor` / `setInteriorColor` / `setSleepColor`
- `draw(gfx)`：Attached 用外材色、Detached 用内材色逐骨块画盒代理；Sleeping
  按 `sleepBatchRevision` 合批
- `lastActiveDrawCount()` / `lastSleepBatchCount()`

## 生命周期

- Renderer 借用 Instance 与 Graphics；销毁 Instance 前先 `setInstance(null)` 或
  丢弃 renderer。
- 不跨帧保留 Body3D 裸指针；合批 Mesh 由 Graphics 拥有。
