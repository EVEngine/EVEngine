# PcgScenePlayer Terrain Culling（场景地形剔除）

验证 `eve.Scene` 上的 **Pcg 地形剔除接口**：给场景节点设置包围盒并打上 `terrain` 标签，
然后用当前相机与画布尺寸调用 `applyPcgTerrainCullingAt`，只有视锥外的地形节点被隐藏，
**没有标签的节点必须保持可见**。

## 运行

```bash
cd examples/pcg-scene-player-culling
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-scene-player-culling
```

## 验证内容

- `eve.Scene` 声明式建树：`beginBuild` / `beginNode` / `setBuildPosition` /
  `setBuildScale` / `end`，再 `mountBuildAs("pcg-scene-player")`；
- 三个节点：`terrain-visible`（视锥内）、`terrain-culled`（`x = 48`，视锥外）、
  `landmark-control`（无 `terrain` 标签的对照组）；
- 通过 `getNodeRef(id).setBounds(-0.5, -0.5, -0.5, 0.5, 0.5, 0.5)` 给地形节点设包围盒，
  再用 `addTag("terrain")` 打标签；三个节点各自 `linkRenderable3D` 绑定可见物体；
- `applyPcgTerrainCullingAt("pcg-scene-player", camera, 960.0, 640.0, "terrain")` 返回本次
  改动的节点数，`!result.ok` 时 `throw`；
- 读回 `getNodeVisible(...)`：视锥外地形必须被剔除，视锥内地形与无标签对照组必须可见。

成功标记：`PCG_SCENE_PLAYER_CULLING_READY changed=... visible=... culled=... control=...`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 场景建树、包围盒与标签、剔除调用与可见性断言 |
| `config.nut` | 引擎 `config`：960×640、`debug = true`、`hotReload = true` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
