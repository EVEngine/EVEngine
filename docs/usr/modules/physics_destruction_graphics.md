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

## 绑定与行为说明

### `DestructionFx`

- `createRenderer(instance)`：返回脚本 VM 拥有的 `GeometryCollectionRenderer`；
  只借用 Instance，失败抛脚本异常。

### `GeometryCollectionRenderer`

- `draw(gfx)`：Attached 用外材色、Detached 用内材色逐骨块画盒代理；Sleeping
  按 `sleepBatchRevision` 合批

## 生命周期

- Renderer 借用 Instance 与 Graphics；销毁 Instance 前先 `setInstance(null)` 或
  丢弃 renderer。
- 不跨帧保留 Body3D 裸指针；合批 Mesh 由 Graphics 拥有。

## 无物理世界的烘焙播放

`DestructionFx.createAnimationRenderer()` 返回 VM 拥有的渲染器 Result，不借用物理实例。
`preparePlayback(player, gfx)` 在加载阶段分配网格，返回 Result；Graphics 必须比渲染器长寿。
`submitPlayback(player)` 在每次 `gfx.render3D()` 前调用并检查 Result，复制当前时间及实例变换的采样结果，
不跨帧保留播放器引用。网格参与同帧正向、阴影和深度渲染；提交失败时禁用上一份提交。
不同场景实例各自持有播放器和渲染器，播放器可以共享一份冻结片段。

```squirrel
local prepared = clip.createPlayback();
if (!prepared.ok) throw prepared.status.summary;
local playback = prepared.value;
local created = destructionFx.createAnimationRenderer();
if (!created.ok) throw created.status.summary;
local renderer = created.value;
local ready = renderer.preparePlayback(playback, gfx);
if (!ready.ok) throw ready.status.summary;
// eve_update(dt): 检查 playback.advance(dt) 的 Result。
// eve_render(): 检查 renderer.submitPlayback(playback)，然后 gfx.render3D()。
```

该提交路径要求片段包含几何。历史盒片段仍通过 `drawAnimation(clip,gfx,seconds,x,y,z)`
在活动 3D pass 中绘制。`prepareAnimation(clip,gfx)` / `submitAnimation(clip,seconds,x,y,z)`
保留旧调用兼容；新实例播放使用上述播放器接口管理时间和完整刚体变换。
