# EVEngine 示例总览

所有示例的统一运行方式（`<platform>` = `win32` / `linux` / `macosx`）：

```bash
make run/<platform>-debug GAME=examples/<name>
```

部分示例另有 Makefile 快捷入口：`make devlab` / `basic` / `ecs` / `rpg` / `inventory` / `building`。
新手建议顺序：`devlab` → `basic` → 你感兴趣的玩法示例 → 程序化生成示例。

## 开发者体验

| 示例 | 演示能力 |
|---|---|
| [devlab](devlab/README.md) | 运行时即编辑器：`--debug` 下的控制台 / REPL、快照、暂停、热重载、错误切片、AI 面板（**旗舰示例，先跑这个**） |
| [ai-game](ai-game/README.md) | AI 驾驶真实游戏：Agent 通过 MCP 读状态 / 改数值 / 截图 / 快照复位（含一键复现脚本 `agent_demo.py`） |
| [ai-editor](ai-editor/README.md) | AI 现场生成项目专属编辑器：`eve mcp` 无头主机 + JSON View + ViewModel 双向绑定（含 `editor_demo.py`） |

## 入门与基础

| 示例 | 演示能力 |
|---|---|
| [basic](basic/README.md) | Tilemap、Box2D 物理、粒子、基础 2D 绘制；另含两个 UI 变体（`ui_demo.nut` / `ui_component.nut`） |
| [ecs](ecs/README.md) | 脚本 ECS（Component / Entity / System）；`gpu_main.nut` 为 compute shader 变体 |
| [scene-ecs](scene-ecs/README.md) | 场景树与脚本 ECS 打通：SceneEntity 行为、eve.view 批量查询 |

## 2D 玩法系统

| 示例 | 演示能力 |
|---|---|
| [rpg](rpg/README.md) | RPG 五系统：属性 / 效果 / 状态 / 技能 / 结算（最小可玩动作 RPG） |
| [inventory](inventory/README.md) | 背包：物品定义、主背包 / 任务栏 / 仓库、装备穿脱、跨包转移 |
| [cardgame](cardgame/README.md) | 卡牌：扇形手牌、拖拽出牌、费用置灰、实时配置面板 |
| [dialogue](dialogue/README.md) | 对话 + Avatar：Squirrel generator 剧情、i18n 翻译表、分层立绘、程序化台词池（.dnut） |
| [galgame](galgame/README.md) | 完整视觉小说《潮汐电台》：原创美术、分支结局、AUTO/SKIP/回想与存读档 |
| [building](building/README.md) | 建筑放置：地形约束、道路邻接、鬼影预览、旋转拆除 |
| [building-tilemap](building-tilemap/README.md) | 在等距 / 六角 tilemap 上放置建筑（2D 精灵穿插） |
| [iso-grid-walk](iso-grid-walk/README.md) | 独立 2.5D PNG 经可插拔 pipeline 生成 TileSet，方格移动与 A* |
| [metroidvania](metroidvania/README.md) | 物理驱动的横版动作游戏：连击、蹬墙跳、空中冲刺、Boss |
| [hex-levels](hex-levels/README.md) | 六边形引擎功能测试关卡：寻路 / FOV / 光照 / 掉落 / WFC（31 关） |
| [i18n](i18n/README.md) | 本地化：翻译表、占位符、复数规则、热重载 |

## 3D 玩法与镜头

| 示例 | 演示能力 |
|---|---|
| [camera-controllers](camera-controllers/README.md) | 第三人称相机：follow / orbit / topdown / firstperson / cinematic |
| [building-3d](building-3d/README.md) | 3D 地面放置：射线求交、网格吸附、鬼影与放置会话 |
| [daynight](daynight/README.md) | 昼夜循环：太阳轨道、程序化天空盒、月光 / 星光 / 火焰 / 萤火虫 |
| [weather](weather/README.md) | 天气系统：雨 / 雪 / 雷暴 / 雾 / 风，实时滑块 |
| [softbody](softbody/README.md) | 布料与 2D 流体解算器（拖拽 / 排斥 / 吸引） |
| [lattice-deform](lattice-deform/README.md) | 3D 晶格缩放变形：squash & stretch、局部鼓起、波浪 |
| [sprite-stack](sprite-stack/README.md) | 伪 3D：把 3D 模型切成多层 RGBA 叠片渲染 |

## 程序化生成

| 示例 | 演示能力 |
|---|---|
| [procgen](procgen/README.md) | 六种地图算法（BSP / Cellular / Drunkard / Maze / 地形 / WFC）+ 纹理配方 |
| [procgen-script-pipeline](procgen-script-pipeline/README.md) | 纯脚本 PointSet 组合、确定性 seed、事务式 hot reload |
| [roguelike-generator](roguelike-generator/README.md) | 种子驱动的房间走廊地牢：autotile、装饰、2D ↔ 2.5D |
| [tree-generator](tree-generator/README.md) | 确定性树木：Weber-Penn / 空间殖民两种骨架算法 |
| [bush-generator](bush-generator/README.md) | 低多边形灌木配方（mesh.bush）与参数实时调节 |
| [bush-fog-volumes](bush-fog-volumes/README.md) | 程序化灌木穿过全局与局部体积雾的深度、透明度和多视角检查 |
| [rock-generator](rock-generator/README.md) | 岩石配方：变形 + 程序化石材纹理 + 自动 LOD |
| [skyscraper-generator](skyscraper-generator/README.md) | 退台塔楼配方：窗格、尖顶、程序化立面纹理 |
| [housegen](housegen/README.md) | 房屋布局生成 + GLB kit 实例化 |
| [linear-structures](linear-structures/README.md) | 线性可拼接结构：栅栏 / 石墙 / 桥 / 长城 / 树篱 / 拒马 |

## 渲染与效果

| 示例 | 演示能力 |
|---|---|
| [outline](outline/README.md) | 屏幕空间描边（G-buffer 深度 + 法线） |
| [waterfall-demo](waterfall-demo/README.md) | 瀑布流动着色器（条纹 / 湍流 / 泡沫） |
| [virtualgeometry](virtualgeometry/README.md) | 虚拟几何体：cluster DAG + GPU 剔除 + 软件光栅化（仅 Vulkan，计算演示） |
| [tensor](tensor/README.md) | 张量编译管线：策略网络 / conv / SDPA / 批量模拟（纯计算） |

## 工具与扩展

| 示例 | 演示能力 |
|---|---|
| [composable-editor](composable-editor/README.md) | 可组合编辑器 SDK：C++ Workspace、动态面板、MVVM、ECS，以及游戏/编辑器共享运行时 |
| [terrain-editor](terrain-editor/README.md) | 运行时生成的地形编辑器：Viewport 内嵌 3D、笔刷、轨道相机 |
| [editor-api-v2](editor-api-v2/README.md) | Editor API V2 同构演示：游戏注入命令，玩家通过 discovery → plan → execute 建造场景 |
| [ai-stage](ai-stage/README.md) | AI 空舞台：scene_director 搭台 kit + MCP，供 Agent 摆物 / 调光 / 截图 / 质检 |
| [model-converter](model-converter/README.md) | 驱动 Blender 把 OBJ 转换成 GLB 并回载（modelconverter 插件） |
| [native-plugin](native-plugin/README.md) | 原生插件：用 SDK 编译动态库，脚本直接调用 |
| [live2d-backend-plugin](live2d-backend-plugin/README.md) | Live2D 后端替换骨架：不改游戏脚本换 Cubism 运行时 |

## 与设计目标对照

想理解这些示例在证明引擎的哪些设计主张，见根目录 [Readme.md](../Readme.md) 的「设计思路」状态表；
开发者体验类的设计价值（热重载、快照、暂停调试、AI/MCP）集中在 `devlab` 与 `ai-stage` 中体现。
