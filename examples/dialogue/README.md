# Dialogue + Avatar 示例

视觉小说风格最小演示：Squirrel **generator** 写剧情，`eve.Dialogue` 管台词/选项，
`eve.Avatar` 的 Image 分层角色站在舞台槽位上。

## 运行

```sh
make run/linux-debug GAME=examples/dialogue
# 或 macOS / Windows 对应的 run/<platform>-debug
```

## 操作

- **空格 / 鼠标左键 / 触屏**：推进打字机或下一句
- **1 / 2**：在选项阶段选择
- **数字键 1 / 2**（非选项阶段）：切换英文 / 中文文案（`eve.I18n` 翻译表热切换）

## 要点

- **没有新脚本语言**——`scene_intro` 就是普通 Squirrel generator，`yield "wait"` / `yield "choice"` 与 C++ 状态机握手。
- **对话文案走 i18n 翻译表**——台词、角色名、选项、UI 提示都来自 `locales/en.json` / `locales/zh.json`，支持运行时热切换与热重载。
- Image Avatar：`body` + `face` + `blush` 三层；表情用 `defineExpression("shy", "blush=1;face=1")`。
- Live2D / VRoid 工厂在本例未加载真实模型；API 见 `docs/对话与Avatar模块设计.md`。
