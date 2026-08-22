# 虚拟几何（VirtualGeometry）

**脚本入口：** `eve.VirtualGeometry()`

GPU 驱动的虚拟几何管线：把球面等网格切成簇（cluster），按视点做簇级裁剪与
LOD 选择，只绘制可见簇——适合大场景/高密度几何。需要 Vulkan（`isAvailable()`）。

## 基本用法

```squirrel
vg <- eve.VirtualGeometry();
if (!vg.isAvailable()) { print("virtual geometry unavailable\n"); return; }

local r = vg.newRenderer();
r.buildIcosphere(4);              // 细分 4 的 icosphere
r.setViewport(gfx.getWidth(), gfx.getHeight());
r.setCameraSimple(eyeX, eyeY, eyeZ, targetX, targetY, targetZ, fovDeg);

function eve_update(dt) {
    r.setModelYaw(elapsed * 0.4);
    r.update();
}

function eve_render() {
    gfx.clear();
    // 将 r.resolve() 得到的簇几何上传绘制（配合 Gpgpu/Graphics 管线）
}
```

## 目标导向指南

### 查看 LOD 与可见簇

`getLodLevel()` / `getMaxLodLevel()`、`getClusterCount()`、`getVisibleCount()`、
`getTotalTriangleCount()` 提供每帧统计，用于验证视距与裁剪。

### 接入自定义渲染

`resolve()` 返回 `ByteData`（簇三角形数据），可经 `gpgpu` 或 `graphics` 管线
上传绘制；`isReady()` 为 true 时才可 resolve。

## API 快查

### `VirtualGeometry`（模块）

- `getName()`：模块名（"VirtualGeometry"）。
- `isAvailable()`、`newRenderer()`。

### `VirtualGeometryRenderer`

- `buildIcosphere(subdivision)`。
- `setViewport(w, h)`、`setCameraSimple(ex,ey,ez, tx,ty,tz, fovDeg)`、
  `setModelYaw(radians)`。
- `update()`、`resolve()` → ByteData、`isReady()`。
- 统计：`getViewWidth` / `getViewHeight` / `getClusterCount` / `getVisibleCount` /
  `getTotalTriangleCount` / `getLodLevel` / `getMaxLodLevel`。

## 生命周期

- 仅 Vulkan 后端可用（WebGPU 返回 `isAvailable()==false`）。
- `resolve()` 返回的 ByteData 归调用方；每次相机/模型变化后需重新 `update()`
  再 resolve。
