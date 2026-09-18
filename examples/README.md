# EVEngine 示例总览

所有示例的统一运行方式（`<platform>` = `win32` / `linux` / `macosx`）：

```bash
make run/<platform>-debug GAME=examples/<name>
```

部分示例另有 Makefile 快捷入口：`make devlab` / `basic` / `ecs` / `rpg` / `inventory` / `building`。
新手建议顺序：`devlab` → `basic` → 你感兴趣的玩法示例 → 程序化生成示例。

## 示例规范（新增示例必读）

`python3 scripts/check_examples.py` 在 CI 的 `source-quality` 阶段强制目录契约，
`scripts/smoke_examples.sh` 在 Linux 阶段强制运行时契约：

1. **目录形式**：`examples/<name>/` 要么是可运行示例（`main.nut` + `config.nut`），要么是在
   `scripts/check_examples.py` 的 `NON_RUNNABLE_EXAMPLES` 中登记了原因的非运行样例
   （C++ 程序 / 插件 / 资产包）。新增目录两者都不满足时 CI 直接失败。
2. **`config.nut` 必须赋值 `config` 表**：引擎先给默认 `config`，再 `dofile("config.nut")`。
   自造的 `window_title` / `window_width` 之类全局不会被读取，窗口会静默退回默认 800×600。
3. **每个示例目录都要有 `README.md`**：说明演示内容、运行命令、操作方式与相关文件。
4. **每个示例都要登记在本文件的表格中**：不允许出现指向不存在目录的死链接。
5. **至少运行 2 秒不报错**：`scripts/smoke_examples.sh` 逐个启动可运行示例，`MIN_RUN_SECONDS`
   （默认 2 秒，CI 同为 2 秒；每个示例实际观察 `RUN_SECONDS`，默认 6 秒）之后进程必须
   仍然存活，且日志中不得出现 `frame error`、
   `Run failed:`、`eve_init failed`、`failed to load:` 等错误标记；提前退出或非零退出码都算失败。
   新示例因此必须真的能启动并进入帧循环，不能靠“跳过”过 CI。

本地自查：

```bash
python3 scripts/check_examples.py                      # 目录 / 文档 / 登记契约
python3 scripts/check_examples.py --print-missing-rows # 打印缺失的登记行
MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh <name>
```

Windows 本地直接跑同样的命令即可：脚本自身按 UTF-8 写 stdout/stderr，`--print-missing-rows`
打印的中文标题不会因为控制台代码页是 GBK 而报错（约定见 `AGENTS.md` 与
`scripts/tests/test_utf8_stdio.py`）。

## 开发者体验

| 示例 | 演示能力 |
|---|---|
| [devlab](devlab/README.md) | 运行时即编辑器：`--debug` 下的控制台 / REPL、快照、暂停、热重载、错误切片、AI 面板（**旗舰示例，先跑这个**） |
| [ai-game](ai-game/README.md) | AI 驾驶真实游戏：Agent 通过 MCP 读状态 / 改数值 / 截图 / 快照复位（含一键复现脚本 `agent_demo.py`） |
| [ai-editor](ai-editor/README.md) | AI 现场生成项目专属编辑器：`eve mcp` 无头主机 + JSON View + ViewModel 双向绑定（含 `editor_demo.py`） |
| [inspector-demo](inspector-demo/README.md) | DevTools 属性检视器：自动扫描脚本类及其属性并渲染为可编辑面板（MVVM） |
| [editor-ui-gallery](editor-ui-gallery/README.md) | 可复用编辑器 UI 套件展示：图标字体、菜单栏、工具栏、搜索侧栏、分屏、卡片、表单控件与游戏内 HUD 共存 |

## 入门与基础

| 示例 | 演示能力 |
|---|---|
| [basic](basic/README.md) | Tilemap、Box2D 物理、粒子、基础 2D 绘制；另含两个 UI 变体（`ui_demo.nut` / `ui_component.nut`） |
| [ecs](ecs/README.md) | 脚本 ECS（Component / Entity / System）；`gpu_main.nut` 为 compute shader 变体 |
| [scene-ecs](scene-ecs/README.md) | 场景树与脚本 ECS 打通：SceneEntity 行为、eve.view 批量查询 |
| [primitive-drawing](primitive-drawing/README.md) | 长期 `Primitive3D` 脚本 API：2D / 3D 基础图形绘制 |

## 2D 玩法系统

| 示例 | 演示能力 |
|---|---|
| [rpg](rpg/README.md) | RPG 五系统：属性 / 效果 / 状态 / 技能 / 结算（最小可玩动作 RPG） |
| [rpg-classic](rpg-classic/README.md) | 经典回合制 RPG 纵向切片：`eve.RPG()` + `eve.Inventory()` + `map` 的数据驱动界面 |
| [inventory](inventory/README.md) | 背包：物品定义、主背包 / 任务栏 / 仓库、装备穿脱、跨包转移 |
| [cardgame](cardgame/README.md) | 可玩的双人德州扑克：四轮下注、AI、七选五牌型、真实烧牌与 52 张 CC0 牌面 |
| [dialogue](dialogue/README.md) | 对话 + Avatar：Squirrel generator 剧情、i18n 翻译表、分层立绘、程序化台词池（.dnut） |
| [galgame](galgame/README.md) | 完整视觉小说《潮汐电台》：原创美术、分支结局、AUTO/SKIP/回想与存读档 |
| [building](building/README.md) | 建筑放置：地形约束、道路邻接、鬼影预览、旋转拆除 |
| [building-tilemap](building-tilemap/README.md) | 在等距 / 六角 tilemap 上放置建筑（2D 精灵穿插） |
| [autotile-production](autotile-production/README.md) | 生产级 autotile：岸线 / 墙体 / 瀑布三层规则与动画帧（零美术资源） |
| [swappable-tilesets](swappable-tilesets/README.md) | RPG Maker 风格图集接入：可替换 tileset + 自动拼接 |
| [pixelworld](pixelworld/README.md) | Noita 风格的分块沙、水、油、火焰与蒸汽像素物质仿真 |
| [iso-grid-walk](iso-grid-walk/README.md) | 独立 2.5D PNG 经可插拔 pipeline 生成 TileSet，方格移动与 A* |
| [hd2d-riverside](hd2d-riverside/README.md) | HD2D 河畔验收：真实像素素材、正交地图与 16 种地面图块 |
| [dynamic-water-grid](dynamic-water-grid/README.md) | 双网格岸线、逐格守恒水量、等距连续水面 Shader 与素材替换契约 |
| [metroidvania](metroidvania/README.md) | 物理驱动的横版动作游戏：连击、蹬墙跳、空中冲刺、Boss |
| [commandery-rts](commandery-rts/README.md) | 将领行政 RTS：框选/编队移动、占领经济点、生产、军饷与叛乱 |
| [rts-sandbox](rts-sandbox/README.md) | RTS 端到端 Squirrel 组合剖面：命令、生产、编队与结算 |
| [hex-levels](hex-levels/README.md) | 六边形引擎功能测试关卡：寻路 / FOV / 光照 / 掉落 / WFC（31 关） |
| [map-fog](map-fog/README.md) | 大地图迷雾：`MapFog` 双层云 + mask（解锁 / 选中 / 溶解） |
| [i18n](i18n/README.md) | 本地化：翻译表、占位符、复数规则、热重载 |
| [sprite-animation-vfx](sprite-animation-vfx/README.md) | 2D 精灵动画与 VFX API 的端到端脚本验证 |
| [vehicle](vehicle/README.md) | 通用载具系统：`eve.Vehicle()` + `eve.Weapon()` 的 2D 顶视完整链路 |
| [crowd](crowd/README.md) | 群体行为：2000 个单位在带障碍流场中行军与平滑转向（Boids） |
| [composable-rebellion](composable-rebellion/README.md) | 可组合玩法：引擎只存事实、叛乱语义全在脚本（15 个 `eve.*` 模块协作） |

## 3D 玩法与镜头

| 示例 | 演示能力 |
|---|---|
| [camera-controllers](camera-controllers/README.md) | 第三人称相机：follow / orbit / topdown / firstperson / cinematic |
| [character-motion-lab](character-motion-lab/README.md) | 三个人物、八种免费动作、跨骨架重定向、同步播放与逐帧观察 |
| [layered-animation](layered-animation/README.md) | 真实 CC0 KayKit 动作叠加：三段动作单独播放 + `AnimLayerMixer`（走路基底 + 脊柱遮罩攻击覆盖 + 受击叠加） |
| [climbing-motion-matching](climbing-motion-matching/README.md) | 攀爬与动作匹配演示（从示例目录内直接运行） |
| [building-3d](building-3d/README.md) | 3D 地面放置：射线求交、网格吸附、鬼影与放置会话 |
| [climbing-playground](climbing-playground/README.md) | 攀爬与跑酷：真实物理探测、低/高跨越、翻越、空中抓边、阻挡与移动平台 |
| [daynight](daynight/README.md) | 昼夜循环：太阳轨道、程序化天空盒、月光 / 星光 / 火焰 / 萤火虫 |
| [weather](weather/README.md) | 天气系统：雨 / 雪 / 雷暴 / 雾 / 风，实时滑块 |
| [weather-daynight](weather-daynight/README.md) | 统一昼夜 + 天气：太阳轨道、程序化天空与雨雪雷暴 |
| [snow](snow/README.md) | 可交互积雪：与高度图同尺寸的 `SnowField` 深度场驱动形变与着色 |
| [softbody](softbody/README.md) | 布料与 2D 流体解算器（拖拽 / 排斥 / 吸引） |
| [softbody3d](softbody3d/README.md) | 3D 软体：Verlet 布料 + 体积体与静态 Box3D 碰撞 |
| [lattice-deform](lattice-deform/README.md) | 3D 晶格缩放变形：squash & stretch、局部鼓起、波浪 |
| [sprite-stack](sprite-stack/README.md) | 伪 3D：把 3D 模型切成多层 RGBA 叠片渲染 |
| [armored-command-3d](armored-command-3d/README.md) | 3D RTS 展示：骨骼坦克、编队命令、齐射与指挥所坍塌 |
| [tactics](tactics/README.md) | 固定镜头 3D 战棋：4 名冒险者对战 6 种骷髅敌人（12×5 方格） |
| [venom-transform](venom-transform/README.md) | 毒液变装效果近似：形变与材质过渡 |
| [anime-character-lab](anime-character-lab/README.md) | anime 风格化着色检查：`stylize.newMeshShader(gfx, "anime")` |
| [avatar-document-editor](avatar-document-editor/README.md) | Azure / Avatar 工作区：可编辑分层母稿 + 真实 `eve.Avatar` 图层渲染 |
| [vrm-avatar](vrm-avatar/README.md) | VRM 角色加载与渲染示例 |
| [voxel](voxel/README.md) | 流式体素地形 + 方块建造：脚本图集、流式 chunk 与 DDA 拾取 |
| [voxel-terrain](voxel-terrain/README.md) | 体素引擎 + 地形生成：32³ chunk、贪婪矩形合并、视锥/视距裁剪、顶点 AO |
| [fluid3d](fluid3d/README.md) | 体积流体实验室：19 个场景模式（水 / 黏性 / 烟雾 / 颗粒 / 射流 / 网格化 / 多相 / 热耦合 / 泡沫 / 移动 SDF / 重叠查询 / 尾流…）+ 70 余个 opt-in 数值验收脚本 |

## 程序化生成

| 示例 | 演示能力 |
|---|---|
| [procgen](procgen/README.md) | 六种地图算法（BSP / Cellular / Drunkard / Maze / 地形 / WFC）+ 纹理配方 |
| [procgen-script-pipeline](procgen-script-pipeline/README.md) | 纯脚本 PointSet 组合、确定性 seed、事务式 hot reload |
| [pcg-biome](pcg-biome/README.md) | UE PCG 风格空间数据、多层运行时 Cell、时间预算与 Scene 实例批次 |
| [pcg-runtime-orchestration](pcg-runtime-orchestration/README.md) | Pcg 运行时编排的数值契约：运行时盖章器 + 生成进度 + 任务队列（无 GPU 场景，等价脚本级单测） |
| [pcg-location-system](pcg-location-system/README.md) | 位置书签：相机位姿 + 玩家位姿 + 控制器 + 场景名的命名书签存取与取景 |
| [pcg-layer-culling](pcg-layer-culling/README.md) | 按渲染层设置相机与阴影剔除距离（layer 8：相机 32 / 阴影 26），远处同层塔必须被剔除 |
| [pcg-scene-player-culling](pcg-scene-player-culling/README.md) | PcgScenePlayer 地形剔除：包围盒 + `terrain` 标签驱动节点可见性，无标签节点保持可见 |
| [pcg-depth-of-field](pcg-depth-of-field/README.md) | Pcg 自动景深对焦：视线命中距离驱动对焦状态机，输出写回相机景深（GBuffer 合成路径） |
| [pcg-detail-overwrite](pcg-detail-overwrite/README.md) | 地形细节覆盖：PointSet 草地点 + 程序化 albedo/normal/mask 烘焙植被，再覆盖细节距离 / 密度 / 每 patch 分辨率 |
| [pcg-snow-wind](pcg-snow-wind/README.md) | 天气雪的世界空间风场：三分量写入与读回校验（非相机跟随偏移） |
| [pcg-thunder-strike](pcg-thunder-strike/README.md) | 雷击事件：确定性 seed 决定落点 / 半径 / 音频片段索引，并逐帧驱动点光源衰减 |
| [pcg-ui-scaler](pcg-ui-scaler/README.md) | UI 父级缩放：按画布高度自适应并夹紧到上限，求值结果直接落地成面板 |
| [pcg-underwater](pcg-underwater/README.md) | 水下效果全链路：深度雾与颜色梯度、时间驱动后期、水焦散 cookie、水下材质、三路音频、粒子与触发体 |
| [roguelike-generator](roguelike-generator/README.md) | 种子驱动的房间走廊地牢：autotile、装饰、2D ↔ 2.5D |
| [dungeon-generator-3d](dungeon-generator-3d/README.md) | `procgen.generate("level.roguelike", ...)` 的 3D 可配置呈现 |
| [tree-generator](tree-generator/README.md) | 确定性树木：Weber-Penn / 空间殖民两种骨架算法 |
| [bush-generator](bush-generator/README.md) | 低多边形灌木配方（mesh.bush）与参数实时调节 |
| [bush-fog-volumes](bush-fog-volumes/README.md) | 程序化灌木穿过全局与局部体积雾的深度、透明度和多视角检查 |
| [rock-generator](rock-generator/README.md) | 岩石配方：变形 + 程序化石材纹理 + 自动 LOD |
| [castle-generator](castle-generator/README.md) | 城堡配方 `mesh.castle`：同心结构、塔楼与程序化材质 |
| [cave-generator](cave-generator/README.md) | 喀斯特洞穴配方 `mesh.cave`：确定性生成与形态参数 |
| [skyscraper-generator](skyscraper-generator/README.md) | 退台塔楼配方：窗格、尖顶、程序化立面纹理 |
| [urban-generator](urban-generator/README.md) | 城市配方 `mesh.urban` / `urban.parcels`：街区与地块 |
| [housegen](housegen/README.md) | 房屋布局生成 + GLB kit 实例化 |
| [linear-structures](linear-structures/README.md) | 线性可拼接结构：栅栏 / 石墙 / 桥 / 长城 / 树篱 / 拒马 |
| [hex-terrain](hex-terrain/README.md) | 无源美术的 3D 六边形世界：`mesh.hexterrain` 配方 |
| [terrain-preview](terrain-preview/README.md) | 直写交换链的 3D 地形预览：三种侵蚀 + 河湖水面 + 自动抓帧 |
| [terrain-gallery](terrain-gallery/README.md) | 三个固定 seed 在相同生成参数、光照与材质下的并排对比 |
| [mesh-modifier-lab](mesh-modifier-lab/README.md) | 13 种网格变形变体并排对照：类型化修饰图（bend / twist / FFD / 切面 / 样条 / 声波）、雕刻笔刷、粘液回弹、Mesh Fit、顶点编辑器 |
| [spline-tube-lab](spline-tube-lab/README.md) | 无源网格生成：样条路径驱动管道 / 带状体 / 自定义截面挤出，含开放与闭合回路、分块、分布采样与行进帧 |
| [mesh-impact-lab](mesh-impact-lab/README.md) | Box3D 命中事件驱动网格塑性冲击：法向冲量 → 形变会话 → 重新上传 |
| [geometry-stroke-lab](geometry-stroke-lab/README.md) | 几何笔刷轨迹：quad / 三棱柱 / 立方体截面、平面与空间输入、最小间距过滤、撤销、CPU 生成 + GPU 上传 |
| [terrain-stamping](terrain-stamping/README.md) | 地形盖章：旋转印章、距离遮罩、对比度、阶地、平滑与热力核，含 Pcg GTS 雪 / 雨 albedo 分支 |

## 渲染与效果

| 示例 | 演示能力 |
|---|---|
| [outline](outline/README.md) | 屏幕空间描边（G-buffer 深度 + 法线） |
| [waterfall-demo](waterfall-demo/README.md) | 瀑布流动着色器（条纹 / 湍流 / 泡沫） |
| [virtualgeometry](virtualgeometry/README.md) | 虚拟几何体：cluster DAG + GPU 剔除 + 软件光栅化（Vulkan/WebGPU） |
| [atmospheric-fog](atmospheric-fog/README.md) | 大气 froxel 体积雾：最小的完整渲染路径与参数调试 |
| [particle-playback-lab](particle-playback-lab/README.md) | 粒子回放实验室：确定性播放、版本化多发射器与视觉验证 |
| [rendering-chain-lab](rendering-chain-lab/README.md) | 渲染链运行时对比：TAA / SSR / RTGI 与自动反射链开关（Space / R） |
| [shader-live-preview](shader-live-preview/README.md) | 实时 GLSL 预览：编辑 `shaders/preview.frag` 保存即热重载 |
| [virtual-texture-blending](virtual-texture-blending/README.md) | 虚拟纹理材质混合：常驻虚拟页 + fallback 槽与无缝 gutter |
| [ink-arena](ink-arena/README.md) | GPU 表面喷墨：片元着色器向持久 RGBA8 画布绘制 |
| [uv-texture-paint](uv-texture-paint/README.md) | 点击网格经 Box3D 三角形命中映射回模型 UV 并绘制贴图 |
| [tensor](tensor/README.md) | 张量编译管线：策略网络 / conv / SDPA / 批量模拟（纯计算） |
| [interior-mapping](interior-mapping/README.md) | 无室内几何的室内映射：预投影 Atlas、切线空间射线–AABB、伪透视缩放、过程化家具遮挡与掠射菲涅尔 |
| [triplanar-decal](triplanar-decal/README.md) | 三维投射贴花：局部空间三平面混合解决单轴投射在立面上的拉伸，含混合锐度对照 |

## 工具与扩展

| 示例 | 演示能力 |
|---|---|
| [composable-editor](composable-editor/README.md) | 可组合编辑器 SDK：C++ Workspace、动态面板、MVVM、ECS，以及游戏/编辑器共享运行时 |
| [scene-editor](scene-editor/README.md) | 可组合场景编辑：场景树、选择与属性联动 |
| [scene-builder-game](scene-builder-game/README.md) | 游戏内嵌场景建造：运行时可组合的场景编辑会话 |
| [level-designer](level-designer/README.md) | 3D 关卡设计工具：引用地形 → 放置白盒 → 调模型 → 出生点 → Motion Matching 试玩 |
| [terrain-editor](terrain-editor/README.md) | 运行时生成的地形编辑器：Viewport 内嵌 3D、笔刷、轨道相机 |
| [voxel-catalog-editor](voxel-catalog-editor/README.md) | MagicaVoxel 风格体素雕刻：占用网格与撤销 |
| [editor-api-v2](editor-api-v2/README.md) | Editor API V2 同构演示：游戏注入命令，玩家通过 discovery → plan → execute 建造场景 |
| [animation-clip-editor](animation-clip-editor/README.md) | 动画剪辑编辑示例：用 UI 无关编辑器 SDK 组装项目专属工作区 |
| [audio-source-editor](audio-source-editor/README.md) | 音频源编辑示例：用 UI 无关编辑器 SDK 组装项目专属工作区 |
| [biome-rules-editor](biome-rules-editor/README.md) | 生物群系规则编辑示例：用 UI 无关编辑器 SDK 组装项目专属工作区 |
| [combat-action-editor](combat-action-editor/README.md) | 战斗动作编辑示例：用 UI 无关编辑器 SDK 组装项目专属工作区 |
| [procgen-script-editor](procgen-script-editor/README.md) | 宿主封装 Squirrel generator（`generators/forest.nut`）的程序化生成编辑器 |
| [ui-theme-editor](ui-theme-editor/README.md) | UI 主题编辑示例：组装命名 Theme 工作区 + 实时预览 |
| [material-editor](material-editor/README.md) | 材质编辑器：中央 UE5 材质球预览 + 右侧 Shading/Surface/Lighting 参数面板 |
| [ai-stage](ai-stage/README.md) | AI 空舞台：scene_director 搭台 kit + MCP，供 Agent 摆物 / 调光 / 截图 / 质检 |
| [blender_hot_reload](blender_hot_reload/README.md) | Blender 保存 → GLB → `SceneLoader.load` → 卸载旧 SceneHost 的热重载往返 |
| [model-converter](model-converter/README.md) | 驱动 Blender 把 OBJ 转换成 GLB 并回载（modelconverter 插件） |
| [archspace-editor](archspace-editor/README.md) | 户型文档烘焙：房间 / 走廊 / 门 / 窗 / 家具 → 带开洞的渲染器中立网格，经 `gfx.newMeshFromArrays` 上传 |

## 插件与脚本包（不是 eve run 项目）

这些目录由 CMake、`eve run -r` 或其它工具单独驱动，不参与 `smoke_examples.sh` 的帧循环契约；
`scripts/check_examples.py` 的 `NON_RUNNABLE_EXAMPLES` 是它们的唯一登记处。

| 示例 | 演示能力 |
|---|---|
| [native-plugin](native-plugin/README.md) | 原生插件：用 SDK 编译动态库，脚本直接调用 |
| [live2d-backend-plugin](live2d-backend-plugin/README.md) | Live2D 后端替换骨架：不改游戏脚本换 Cubism 运行时 |
| [agent](agent/README.md) | Squirrel 强化学习：定义环境、训练策略、选择动作与回放（C++ 侧 `eve.Agent`） |
| [economy](economy/README.md) | `eve.Economy` 无窗口脚本演示：采集循环与满仓浪费（`eve run -r`） |
| [shader_effect_package](shader_effect_package/README.md) | 打包式 shader effect 资产包（`effect.vert` / `effect.frag` + `parameters.json`） |
| [surface-fluid-dynamic](surface-fluid-dynamic/README.md) | C++ 侧表面流体参考实现（确定性 CPU 解算） |

## 与设计目标对照

想理解这些示例在证明引擎的哪些设计主张，见根目录 [Readme.md](../Readme.md) 的「设计思路」状态表；
开发者体验类的设计价值（热重载、快照、暂停调试、AI/MCP）集中在 `devlab` 与 `ai-stage` 中体现。
