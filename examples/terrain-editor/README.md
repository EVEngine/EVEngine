# Terrain Editor — 运行时生成的地形编辑器

演示"运行时即编辑器"：脚本用 `editor` + `ui` 构件在游戏进程内生成一个地形编辑器——
主窗口实时渲染 3D 地形，工具面板控制抬升 / 下陷笔刷，高度图网格原地更新。

## 运行

```bash
make run/<platform>-debug GAME=examples/terrain-editor
```

## 操作

| 输入 | 作用 |
|---|---|
| 右键拖拽 Viewport | 轨道相机 |
| 滚轮 | 缩放 |
| 场景内按住左键 | 抬升 / 下陷地形 |
| `Raise` / `Lower` 按钮 | 切换笔刷 |
| `Regenerate` 按钮 | 重新生成高度图 |

同样的 UI 可以在发布运行时保留，让玩家 / 编辑器在游戏里直接改内容（开发系统 = 游戏运行时）。
