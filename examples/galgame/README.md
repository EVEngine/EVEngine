# 潮汐电台 / Tidal Frequency

一个约 10 分钟、完整可玩的原创 Galgame 短篇，而非 API 占位场景。它包含标题界面、
四章剧情、角色演出、分支选择、两个结局、AUTO、SKIP、回想、单槽存读档和热重载。
普通阅读速度下单条路线约 8–12 分钟。

```sh
make run/linux-debug GAME=examples/galgame
```

操作：`Space/Enter`、鼠标左键或触屏推进，选择界面按 `1/2`，`F5/F9` 存读档，`L` 打开回想；
也可以使用界面按钮。存档写入引擎配置的用户存档目录，不会污染游戏资源目录。

## 展示的引擎能力

- Vulkan 2D 纹理与透明混合：三张原创 16:9 背景及带 Alpha 的角色立绘。
- `Dialogue`：UTF-8 打字机、角色舞台槽位与状态推进。
- `Avatar`：Image Avatar 纹理层、入场、呼吸、说话者聚焦与靠近动作。
- `DialogueUX`：历史记录、自动播放和快进状态。
- `UI`：标题、HUD、选择、回想与结局界面。
- 即时演出：雨幕、狂风、雷击闪白、镜头震动、无线电扫描线、场景淡入淡出、星光和黎明。
- `Filesystem.writeText/readText`：脚本安全使用的 UTF-8 文本存档 API。
- Squirrel 热重载：开发时保留运行状态并重建界面。

## 美术资源

`assets/` 中五张 PNG 是为本示例用 OpenAI 内置 ImageGen 生成的原创素材：

- `station_twilight.png`：海边车站背景；
- `lighthouse_radio.png`：暴风雨中的灯塔无线电机房；
- `seawall_dawn.png`：风暴后的黎明海堤；
- `lin.png`：林澄透明立绘；
- `zhou.png`：周岚透明立绘。

生成约束：商业视觉小说风格、原创人物、无品牌/文字/水印；角色要求透明背景。
