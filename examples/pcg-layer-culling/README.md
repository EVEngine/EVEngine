# PCG Layer Distance Culling（按层距离剔除）

验证 **按渲染层设置剔除距离**：相机对 layer 8 分别设置相机剔除距离 32.0 与阴影剔除
距离 26.0，同层物体只有在距离内才参与绘制与阴影。场景刻意放了一座距离 70 的远塔
（layer 8），它必须被剔除。

## 运行

```bash
cd examples/pcg-layer-culling
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-layer-culling
```

## 验证内容

- `camera.setLayerCullDistance(8, 32.0)` 与 `camera.setShadowLayerCullDistance(8, 26.0)`；
- 地面用 layer 0，三根近景立柱与前塔用 layer 8，另有一座 `z = -70`、缩放 8×10×8 的
  远塔同样在 layer 8 —— 它超出 32 的相机距离，必须被剔除；
- 相机放置在 `(0, 5, 14)`，看向 `(0, 0.8, -12)`，裁剪面 0.1–120.0。

成功标记：`PCG_LAYER_CULLING_READY layer=8 cameraDistance=32 shadowDistance=26 farTower=culled`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 相机层剔除距离设置、场景搭建、成功标记 |
| `config.nut` | 引擎 `config`：960×640、`debug = true`、`hotReload = true` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
