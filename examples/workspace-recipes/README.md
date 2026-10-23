# Workspace recipes（开箱创作台）

EVEngine **不交付**固定 Editor App；领域编辑器是可组合的 Workspace 零件。
本目录给出三条**配方入口**，每条只做一层薄壳，指向已有完整示例，降低“有零件不会装”的成本。

| 配方 | 入口 | 完整参考实现 |
|------|------|----------------|
| 战斗动作时间轴 | `combat-action/main.nut` | [`../combat-action-editor`](../combat-action-editor) |
| 音频 Source | `audio-source/main.nut` | [`../audio-source-editor`](../audio-source-editor) |
| 场景检视 | `scene-inspect/main.nut` | [`../scene-editor`](../scene-editor) + [`../composable-editor`](../composable-editor) |

产品契约见 [`docs/dev/2026-09-04-domain-editor-workspace-ui.md`](../../docs/dev/2026-09-04-domain-editor-workspace-ui.md)。
场景对象心智见 [`docs/dev/场景对象模型使用指南.md`](../../docs/dev/场景对象模型使用指南.md)。

运行（与其它 examples 相同）：

```sh
cd examples/workspace-recipes/combat-action
# 配置 VK_ICD / ALSOFT / XDG_RUNTIME_DIR 后：
xvfb-run -a ../../../build/linux-debug/src/engine/eve run
```
