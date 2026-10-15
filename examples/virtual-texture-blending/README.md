# Virtual Texture Blending — 虚拟纹理材质混合 / 流式驻留

本例演示 UE5 Runtime Virtual Texturing 同构的运行时路径：`VirtualTextureStreaming`
收集 feedback → 预算内解码页 → 增量上传 atlas/页表 → 原子 `commitPublish`，再通过
`Material.setVirtualTexture()` 采样。

## 运行

```bash
make run/<platform>-debug GAME=examples/virtual-texture-blending
```

也可以直接运行引擎（Windows 下把路径换成 `build/win32-debug/src/engine/eve.exe`）：

```bash
cd examples/virtual-texture-blending && ../../build/linux-debug/src/engine/eve run
```

## 演示内容

- `gfx.newVirtualTextureStreaming(128, 64, 64, 2, 2, 2)`：虚拟分辨率 128×64，tile 64，
  border 2，物理容量 2 页（另加 fallback 槽），每帧最多解码 2 页。
- 两个程序化图层（岩石 / 苔藓）经 `addLayer` 进入 CPU producer；`initializePhysicalCache`
  只常驻 fallback。
- `eve_update` 写入页 feedback（交替请求 2 页或 1 页），调用 `update()` 做 compact/decode/stage，
  再 `syncToMaterial(gfx, material)` 上传并原子发布。
- 页边界仍走 `virtual_texture.glsl` 的 gutter + `textureGrad` 路径；非驻留页回落 fallback。
- 控制台每隔约 1.5 秒打印当前 generation 与 resident 页数。

## 操作

- 没有键盘交互：相机固定；每约 1.5 秒切换 feedback 集合，可观察 residency 变化。
- `config.nut` 中 `hotReload = true`，保存 `main.nut` 后脚本会自动重载。

## 相关文件

- `main.nut` — streaming 会话、feedback、材质同步与 HUD。
- `config.nut` — 1280×720 窗口与标题，以及 `debug`、`hotReload` 开关。
