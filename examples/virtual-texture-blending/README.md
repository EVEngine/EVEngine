# Virtual Texture Blending — 虚拟纹理材质混合

本例演示材质侧图层编写：`enableVirtualTexture` + `addVirtualTextureLayer`。页反馈、解码预算、
atlas/页表上传与原子发布由引擎在 `Material.bind` / `render3D` 中自动完成，脚本不维护流式会话。

## 运行

```bash
make run/<platform>-debug GAME=examples/virtual-texture-blending
```

也可以直接运行引擎（Windows 下把路径换成 `build/win32-debug/src/engine/eve.exe`）：

```bash
cd examples/virtual-texture-blending && ../../build/linux-debug/src/engine/eve run
```

## 演示内容

- `vtMaterial.enableVirtualTexture(128, 64, 64, 2, 2, 2)`：虚拟分辨率 128×64，tile 64，
  border 2，物理容量 2 页，每帧最多解码 2 页。
- 两个程序化图层（岩石 / 苔藓）经 `addVirtualTextureLayer` 进入材质拥有的 producer。
- 绘制时引擎自动收集 feedback、解码并同步 GPU 页表；无需 `update` / `syncToMaterial`。
- 材质参数：`setRoughness(0.78)`、`setMetallic(0.0)`；地面为缩放后的立方体网格。

## 操作

- 没有键盘交互：相机固定。
- `config.nut` 中 `hotReload = true`，保存 `main.nut` 后脚本会自动重载。

## 相关文件

- `main.nut` — 图层编写、材质与场景。
- `config.nut` — 1280×720 窗口与标题，以及 `debug`、`hotReload` 开关。
