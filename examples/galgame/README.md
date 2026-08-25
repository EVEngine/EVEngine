# 潮汐电台 / Tidal Frequency

一个完整可玩的原创 Galgame 短篇，而非 API 占位场景。它包含标题界面、打字机台词、
角色立绘、分支选择、两个结局、AUTO、SKIP、回想、单槽存读档和热重载。

```sh
make run/linux-debug GAME=examples/galgame
```

操作：`Space/Enter`、鼠标左键或触屏推进，选择界面按 `1/2`，`F5/F9` 存读档，`L` 打开回想；
也可以使用界面按钮。存档写入引擎配置的用户存档目录，不会污染游戏资源目录。

## 展示的引擎能力

- Vulkan 2D 纹理与透明混合：原创 16:9 背景及带 Alpha 的角色立绘。
- `Dialogue`：UTF-8 打字机、角色舞台槽位与状态推进。
- `Avatar`：Image Avatar 纹理层与舞台同步。
- `DialogueUX`：历史记录、自动播放和快进状态。
- `UI`：标题、HUD、选择、回想与结局界面。
- `Filesystem.writeText/readText`：脚本安全使用的 UTF-8 文本存档 API。
- Squirrel 热重载：开发时保留运行状态并重建界面。

## 美术资源

`assets/` 中三张 PNG 是为本示例用 OpenAI 内置 ImageGen 生成的原创素材：

- `station_twilight.png`：海边车站背景；
- `lin.png`：林澄透明立绘；
- `zhou.png`：周岚透明立绘。

生成约束：商业视觉小说风格、原创人物、无品牌/文字/水印；角色要求透明背景。
