# 用 EveScript 完成一个游戏：从规则到交付

本教程面向 SDK 用户。语言基础见 [EveScript](EVESCRIPT.md)，接口专题见
[模块手册](MODULES.md)。普通游戏开发不需要编译 C++；本教程的参考工程是
[examples/game-tutorial](../../examples/game-tutorial/)，完整源码包含配置、主入口和规则
模块。它用方向键收集目标，提供 HUD、重新开始、分数存档和热重载。

## 1. 先建立可运行工程

确认 `eve -v`，使用与运行时同源的 `share/eve/ai/eve-api.json` 查接口。可以
`eve create my-game`，或复制参考工程到自己的目录再改标题和保存目录身份。
仓库用户运行 `eve run examples/game-tutorial`；SDK 用户运行复制后的目录。

工程结构：

```text
game-tutorial/
  config.nut
  main.nut
  scripts/rules.nut
  game.agent.json
  README.md
```

[config.nut](../../examples/game-tutorial/config.nut) 设置 960×540 窗口、hotReload，声明
gfx/keyboard/ui；Image 没有默认槽位，由 main.nut 显式构造 eve.Image()；文件系统和计时器是引擎启动依赖。配置中的字符串是脚本槽位，
不是 CMake 模块名。导入脚本不能把缺少的原生模块加入当前 SDK。

先验证冷启动能进帧循环，再逐步加入内容。不要把初始化报错藏在 catch 后继续运行一个
不完整世界；必需服务缺失就明确失败，可选音效才选择无声降级。

## 2. 把权威状态与表现分开

规则模块的 new_state 返回普通表：schema、version、x、y、score。
main 中只有 `persist tutorial_state` 是玩法权威状态。纹理、HUD 文字与目标位置都是
投影：纹理由图像数据创建；HUD 读 score；目标由 score 推导，没有第二份可变目标计数器。

```squirrel
import { new_state, update_state } from "./scripts/rules.nut"
persist tutorial_state = new_state()
```

import 路径相对当前脚本，必须是字面量。规则 module 显式 export 函数，不把内部变量
写进全局表。与正式项目一样，main 只做编排；输入、资源和文件系统不进入纯规则函数。

规则函数接收 dt 和窗口范围，方便用同样输入做断言。它不是每次都重新从 wall clock
读取时间，也不调用未知的全局随机函数。正式 replay/联机项目还需固定模拟 tick、稳定
事件顺序、命名 RNG 和 backend 容差；本例的可变 dt 移动不作联机确定性承诺。

## 3. 在 update 处理输入与规则

keyboard.isDown 表示持续按下，适合移动；key_just_pressed 表示刚按下，适合保存和重开。
每个边沿键每帧由一个输入层查询一次，再分发给多个系统，不反复查询同一边沿函数。

```squirrel
local dx = 0.0, dy = 0.0
if (keyboard.isDown("Left")) dx -= 1.0
if (keyboard.isDown("Right")) dx += 1.0
if (keyboard.isDown("Up")) dy -= 1.0
if (keyboard.isDown("Down")) dy += 1.0
update_state(tutorial_state, dx, dy, dt, config.width, config.height)
```

二维坐标原点左上，Y 向下，220 是像素/秒，乘 dt 才是一帧位移。规则函数归一化斜向
输入以免加速，限制玩家位置在窗口内，再做两个矩形的重叠判断。命中后 score 增加，
目标切到另一处，不会持续重叠而每帧重复加分。生产项目多实体时，将检测、事件和
延迟销毁分阶段，见 [ECS](modules/entity.md)。

用 Physic World 时遵守该模块的步进规则，位置由物理拥有。不要同时每帧直接改 renderable
位置与刚体位置来得到两个互相漂移的角色。单位、碰撞体、传感器和接触回调见
[Physics](modules/physics.md)。

## 4. 初始化资源，render 只绘制

参考工程在 build_view 创建 8×8 RGBA8 像素，再上传纹理：

```squirrel
local image = eve.Image()
local pixels = image.newEmptyImageData(8, 8, "RGBA8")
pixels.setPixel(0, 0, 0.2, 0.7, 1.0, 1.0)
local texture = gfx.newTexture(pixels, false, false)
```

newTexture 的两个 repeat 参数在脚本中显式传入，不能从 C++ 默认实参推断可以省略。
程序生成图像让工程无需下载资源。换成实际精灵时，在 init/关卡加载阶段用
image.newImageDataFromFile("textures/hero.png")，再上传。CPU ImageData 与 GPU Texture
是不同对象，借用/缓存/释放规则见 [Image](modules/image.md)和[Graphics](modules/graphics.md)。

同一 Graphics owner 存活的普通脚本重载中，工程保留 hero_texture 并调用
updateTextureFromImageData 原位上传；这个脚本投影返回 void，失败抛异常。Texture 属于
Graphics，当前没有脚本 releaseTexture；只清脚本变量不会释放 GPU 纹理。设备重建必须
重建资源缓存，不能跨新设备复用旧句柄。退出停止绘制，Graphics owner 负责销毁纹理。

加载失败保留旧资源或阻止进入关卡，并报告具体路径。每帧从磁盘加载、创建纹理或编译
shader 会制造卡顿；render 只提交背景、世界、角色、效果和 UI，正常循环的 present
由引擎负责，不再手动重绘第二次。

```squirrel
gfx.clear()
gfx.drawTexturedRect(hero_texture, tutorial_state.x, tutorial_state.y,
                    32.0, 32.0, 1.0, 1.0, 1.0, 1.0)
ui.beginFrameAndRender()
```

world/screen/canvas 坐标必须明确。引入 camera 后使用其坐标换算接口，不把鼠标像素
直接当世界坐标。3D 模型还需要 ModelData、网格/材质与 Renderable 的连接；实际资源
加载、动画与表现专题见模块章节，不能凭 Unity/Godot API 名猜接口。

## 5. 构建 HUD 并消费动作

示例将 HUD 固定到右上角（setHostPos 四参数、setHostSize 两参数），避免初始角色和
第一目标被窗口遮住；布局与绘制要用真实截图核对。

build_view 只在 init/reload 构建树，稳定 ID 为 score/status/restart/save/load，host 名为
tutorial。update 调用 consumeClick，完整事件路径为 `tutorial/save` 等；setText 更新
当前 host 的显示，不重新每帧构建整棵树。

```squirrel
ui.beginBuild()
ui.beginWindow("Collect Squares", "root")
ui.text("Score: 0", "score")
ui.button("Save", "save")
ui.end()
ui.mountBuildAs("tutorial")
```

在多 host 项目中明确当前 selected host 和事件归属；多个消费者不要各自把同一个 click
队列耗尽。按钮和 S 键调用同一个 save_game，避免两个行为分支各自实现规则。
复用组件、MVVM、列表稳定 key、卸载监听和主题详见 [UI](modules/ui.md)。
UI 不能绕过购买或生产事务直接修改库存；真正命令返回成功后才更新结果提示。

## 6. 保存数据而不是运行时对象

参考例子的教学存档格式为 `tutorial.collect|1|score`，只保存分数；读档后位置回到起点。
schema id 是 tutorial.collect，version 是 1，未知版本/额外字段/非规范整数/负数或
大于 1,000,000 的分数均拒绝。它是严格的游戏自有格式，不是 JSON，也不冒充 RPG 快照。
新增字段时需升级版本并显式迁移。

encode_save/decode_save 在规则模块；解码先构造候选，成功后才替换 tutorial_state。
decode 失败时当前游戏保持不变，不逐个字段边读边写。load_game 先试主档，再试备份，
两个都坏时显示失败并保留当前状态。

fs.setIdentity 与 setupWriteDirectory 选择本游戏的用户写目录，检查返回值；不能写进
只读 APK 或 `.eve` 包。writeTextAtomic 在脚本中返回整数 1/0。保存前只将通过校验的
旧主档轮换到备份，备份写失败就取消本次保存，再原子提交新主档并显示结果。
Web 的原子替换目前明确失败，提示失败不能被普通缓存写入悄悄替代。

原子文件提交不等于跨多个文件或多个领域的事务。RPGParty、背包、装备和战棋快照
请按[跨模块存档](SAVE.md)使用既有域接口，不把 C++ Result 和脚本的 1/0 混为一谈。
设备掉电、文件截断、版本不兼容、缺失定义都要测试，而不只是“生成了一个文件”。

## 7. 热重载不是冷启动，也不是存档

persist 保留当前进程的普通玩法表；hero_texture 是独立的借用资源缓存，不是玩法权威。
init 创建表现资源，reload 验证 schema/version 后
原位更新纹理与重建 HUD。关闭再启动会丢掉 persist，只有显式 load 才读回已保存分数。
本例不保留旧 class 实例，不声称所有原生 handle 都能跨 Runtime generation 存活。

正式关卡拥有订阅、任务、原生世界与实体，在退出/切换时停止更新、取消订阅、解除 Link
并按所有权释放资源。异步 continuation 可能被重载取消；长期流程保存进度并重新启动，
不要让旧闭包在新世界继续修改数据。大型场景资源迁移先验证完整候选再发布。

## 8. 将玩法继续扩展

- 需要物品与购买：Inventory 定义/Bag → 业务事务 → Economy/商店入口 → HUD 投影。
- 需要技能与受击：Attributes/Definitions → Action grant/activation → Combat 命中/结算。
- 需要策略经营：RTS/Building + Economy/Production/Orders；数据只由对应域拥有。
- 需要剧情：Dialogue 的 .dnut 内容和进度，不把 .dnut 当第二套通用脚本语言。
- 需要生成地图：Procgen 输出 → Graphics 表现；要碰撞再接 procgen_physics，不强迫
  纯地图生成依赖 Physics。

先做一个完整产品回路，再扩展内容。测试输入、规则、错误返回、恢复和实际画面，让
每条新路径都有可重复的任务证据；不是只出现几个新的 API 名。

## 9. 调试、验证与发布

编辑器用 `eve language-server --root <目录>`，运行时用
`eve run --debug --dap-port=4711 <目录>`。LSP/编译检查消除语法与参数错误；日志和
堆栈定位实际绑定失败。当前源码的 `eve test` 命令仍是空实现，不能作为游戏测试已通过
的证据。仓库测试使用 Make/CTest，独立游戏自己建立明确的测试 runner。

按以下顺序检查参考工程：

1. 冷启动，向四个方向和斜向移动，碰撞一次只增加一次分数，窗口边界不越出。
2. R 与 Restart 按钮产生同样重开状态，键盘边沿不重复触发。
3. 保存 → 重开 → 读档恢复分数；确认位置回起点是格式规定，而不是缺失字段错误。
4. 截断主档验证备份恢复；未知 schema/version 和额外字段都拒绝，两个档坏时保留旧状态。
5. 保存脚本验证重载保留位置/分数且 HUD/纹理可用，再冷启动验证初始化。
6. `eve zip <目录>` 后运行 `.eve`，检查静态 import 与资源路径。
7. `eve package <目录> -o <输出目录> --sdk <SDK根>` 后在目标机器运行，移动端使用
   SDK 平台模板并验证真机日志、输入和保存路径。

仓库烟测使用 `MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh game-tutorial`。
它证明进帧循环且无已知错误标记，不证明所有交互或画面。截图通过引擎拥有的 MCP 截图，
有 game.agent.json 时通过 eve_play 的真实动作/观察验证；不要把 eval 改分数当通关证据。

### 通过正式游戏动作验证

工程的 [game.agent.json](../../examples/game-tutorial/game.agent.json) 声明 collect-state
观察和 restart/save/load 动作。三个动作都调用键盘和 HUD 使用的同一个游戏函数；
不会通过 eve_eval 写分数。以 `eve run --debug --mcp-port=7529 <目录>` 启动后，向
MCP 工具 eve_play 传 request，例如：

```json
{"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"collect-state"}
```

```json
{"schemaId":"evengine.play-request","schemaVersion":1,"op":"act","action":"save"}
```

再 observe 验证状态，capture 取得引擎帧。op=act 结果成功只证明函数执行完成，
磁盘失败还要检查 HUD 状态和文件结果；save_game 会显示失败，不抛弃诊断。
当前 eve_play 在 .eve 归档路径下不能发现 game.agent.json，正式动作验证先运行目录工程。
manifest 本身不是测试通过报告；真实移动仍需键盘交互，规则断言不能代替输入验收。

本教程源码与脚本断言见 [script_user_doc_examples.cpp](../../test/integration/script-api/script_user_doc_examples.cpp)；
更新教程时同步核对实际文件，完整验证记录在 [Review](REVIEW.md)。
