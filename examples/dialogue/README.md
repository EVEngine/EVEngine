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
- **3**：切换场景（scene 变量区随场景切换自动清空）
- **4 / 5 / 6**：月夜圆角九宫格 / 信笺九宫格 / 脚本动态通讯面板
- 选项也支持鼠标点击；切换外观保留当前台词、打字进度和选项。

## 要点

- **没有新脚本语言**——`scene_intro` 就是普通 Squirrel generator，`yield "wait"` / `yield "choice"` 与 C++ 状态机握手。
- **对话文案走 i18n 翻译表**——台词、角色名、选项、UI 提示都来自 `locales/en.json` / `locales/zh.json`，支持运行时热切换与热重载。
- Image Avatar：两张生成的写实透明人物立绘，使用 `portrait` 纹理层；当前为静态立绘，不演示表情贴图切换或口型。素材与生成提示词见 `assets/README.md`。
- Live2D / VRoid 工厂在本例未加载真实模型；API 见 `docs/对话与Avatar模块设计.md`。
- **程序化对话（.dnut）**——`pools.dnut` 由 C++ 解析器 `dlg.loadPoolsFromDnutFile` 解析注册；`mood` / `hour` 变量驱动加权选词，`meta` 自动切表情/动作，修改 `pools.dnut` 触发资产热重载。

## 呈现扩展与验证

`DefaultDialogueUI` 随启动脚本打包。修改 `src/scripts/default_dialogue_ui.nut`
后需要重新构建引擎；游戏不依赖源码目录路径。
`setPresentation` 配置皮肤、位置和宽高；`layout(frame)` 自定义逐帧位置，
`drawBackground(frame)` 绘制背景，`draw(frame)` 可接管全部绘制且无需 UI 模块。
通讯模式展示脚本图形与 UI 文字、按钮的组合。
详细契约见 `docs/usr/modules/dialogue.md`。

在仓库根目录运行 `python examples/dialogue/skins/generate.py` 可确定性重建
两张原创皮肤；它们包含连续拉伸区和内容边距标记。

使用引擎配套 Squirrel 解释器运行 `sq test/dialogue_presentation.nut`，覆盖大量选项、
独立快照、切换皮肤、无效资产、回调异常、布局、卸载、语音自动推进和无 UI 路径。
脚本测试不能替代 GPU 视觉验收。
