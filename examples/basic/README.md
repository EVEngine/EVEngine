# Basic — 入门示例

引擎能力的最小合集：Tilemap（`maps/demo.json`）、Box2D 物理、粒子与基础 2D 绘制，
并演示软热重载的写法（`eve_init` 一次性初始化 + 全局守卫，`eve_reload` / `eve_asset_reload` 增量处理）。

## 运行

```bash
make basic
# 等价于：make run/<platform>-debug GAME=examples/basic
```

## 目录里的其它玩法

同目录还有两个"替换 `main.nut` 即可试用"的 UI 变体：

- `ui_demo.nut`：多主机声明式 UI 与深色 / 浅色主题切换。
- `ui_component.nut`：`eve.UIComponent` 组件化 UI（HP 条 / 滑块 / 按钮）。

想体验调试器、控制台、快照与 AI 面板，请跑 [`examples/devlab`](../devlab/README.md)。
