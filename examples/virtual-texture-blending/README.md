# Virtual Texture Blending — 虚拟纹理材质混合

本例演示 `Material.setVirtualTexture()`：把两个 64×64 的常驻虚拟页和一个 fallback 槽拼进同一张
3 槽物理图集，用页表把虚拟页映射到物理槽位。每个页槽四周留 2 像素 gutter 重复相邻边缘颜色，
让页边界与页切换在 `textureGrad` 采样下不产生接缝。

## 运行

```bash
make run/<platform>-debug GAME=examples/virtual-texture-blending
```

也可以直接运行引擎（Windows 下把路径换成 `build/win32-debug/src/engine/eve.exe`）：

```bash
cd examples/virtual-texture-blending && ../../build/linux-debug/src/engine/eve run
```

## 演示内容

- 图集由 `eve.Image().newEmptyImageData(w, h, "RGBA8")` 程序化生成：`tile = 64`、`border = 2`，
  每槽 `slotSize = 68`，横向 3 槽共 204×68。槽 0 是 fallback 槽（整面替身、无页边界），
  槽 1、槽 2 是两个物理页，各存整条岩石→苔藓渐变的一半。
- 页表是 2×1 RGBA8：红/绿存物理槽中心（0.5 → 槽 1，0.833333 → 槽 2），蓝 > 0.5 表示该页常驻。
  `virtual_texture.glsl` 正是按 `entry.b` 分流：命中取页内 uv，未命中回落到整面 uv 并缩放梯度。
- 本例只有 2 个虚拟页且都常驻，所以页表两项的蓝通道都是 1，运行时全部走页路径。
- `gfx.newTexture(image, false, false)` 上传 albedo 图集、平面法线图集和页表，然后调用
  `vtMaterial.setVirtualTexture(albedo, normal, pageTable, 2, 1, 3, 1, border / slotSize)`：
  2×1 个虚拟页、3×1 个物理槽、border fraction = 2/68。
- 材质参数为 `setRoughness(0.78)` 与 `setMetallic(0.0)`。
- 地面是 `eve.Renderable3D()` 加 `gfx.newMeshCube(1.0)`，缩放到 (11, 0.35, 7)。
- 相机 `eve.Camera3D()`：eye (0, 7, 9)、target 为原点、fov 52°，环境光 (0.28, 0.30, 0.32)，
  方向光来自 `gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.2, 1.15, 1.05)`。
- `gfx.getRenderControl()` 关闭 `shadow` / `gbuffer` / `msaa` / `atmosphere` 后 `compile()`。
- `eve_render` 在窗口底部用 `gfx.drawSolidRect` 加 `gfx.drawTexturedRect` 内嵌显示整张物理图集，
  可以直接看到 fallback 槽、两个页槽以及页槽边缘的 2 像素 gutter。

## 操作

- 没有键盘或鼠标交互：相机位姿在 `eve_init` 里固定，`main.nut` 不读取任何输入。
- `config.nut` 中 `hotReload = true`，保存 `main.nut` 后脚本会自动重载并重建图集与材质。

## 相关文件

- `main.nut` — 生成图集与页表、设置材质和相机、绘制底部图集内嵌视图。
- `config.nut` — 1280×720 窗口与标题，以及 `debug`、`hotReload` 开关。
