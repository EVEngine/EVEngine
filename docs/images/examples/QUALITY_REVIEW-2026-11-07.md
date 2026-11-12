# Example 画面质量复查（2026-11-07）

[截图画廊](README.md) · [English gallery](README.en.md) · [抓帧清单与 SHA-256](review-2026-11-07/manifest.json)

本轮检查 **228 个示例：197 个生成有效引擎截图，31 个未通过抓帧**。仓库共 237 个 example 目录，其中 228 个含 `main.nut`，9 个为非独立入口目录。

这是“当前仓库脚本 + 本地完整 Debug 构建”的静态视觉复查，不是当前 HEAD 原生编译验收。运行时 `v0.6.1-dev / 1b62421cf4de+dirty`，构建于 2026-11-03 09:38:26 +0800；脚本版本 `d9df15920770e3cc6dc16b38fc5483d8aa995058`。截图全部由引擎 `gfx.saveFramePng()` 输出，没有桌面截图、补绘或颜色后处理。画面问题是观察，原因与改法是后续核查建议。

默认初始化后 120 帧抓取；weather 150 帧、fluid3d 180 帧。galgame、weather、pixelworld、fluid3d 使用仓库已有预设。实时 dt 会影响动画相位；单帧不能验证动作、交互、性能或完整玩法。**抓帧失败只说明本轮运行环境没有取得有效证据，不自动等同引擎或示例本身有缺陷。**

优先级：P1 = 先处理可见异常、空画面或无法辨认的展示；P2 = 改善构图、材质与演示表达；P3 = 补充上下文与对照。实验室型示例不以完整游戏的美术标准打分，但应让读者看懂所展示的能力。

## 建议优先改进的画面

本轮列出 **77 个待改进示例（P1 31 项、P2 39 项、P3 7 项）**。建议先用匹配源码/Shader 的构建复拍 P1 异常，再处理布局、默认镜头和美术表达；同类跨例异常不应直接拆成互不相关的缺陷。

逐项依据本轮新图。验收时保留相同 seed、相机、场景状态的前后对照；动态效果另录开始/过程/结束三状态。

| 优先级 | 示例与截图 | 问题类别 | 画面观察 | 建议改进与复查 |
|---|---|---|---|---|
| P1 | [ai-stage](review-2026-11-07/ai-stage.png) | 默认画面缺少主体 | 只有灰色地平面与背景，无法了解 AI 舞台要展示什么。 | 提供可直接播放的默认角色/任务，并显示启动提示；检查是否需要额外输入。 |
| P1 | [anime-character-lab](review-2026-11-07/anime-character-lab.png) | 空白画面 | 新截图几乎只有单一灰蓝背景，没有可见角色或操作界面。 | 用匹配源码的构建核对资源加载、相机和初始化路径，确认是否需要额外输入后再展示。 |
| P1 | [atmospheric-fog](review-2026-11-07/atmospheric-fog.png) | 地面黑带与接缝 | 中央地面横贯一条明显黑带，远处几何与地面衔接不自然。 | 核对深度输入、雾体合成和有效边界，比较雾开/关同机位；先排除本地运行时差异。 |
| P1 | [building-3d](review-2026-11-07/building-3d.png) | 默认场景为空 | 只有网格地面，没有 README 标题所期待的建筑主体。 | 提供默认建筑或在画面内明确提示生成操作；补生成后和编辑中的代表截图。 |
| P1 | [commandery-rts](review-2026-11-07/commandery-rts.png) | 地图主体缺失 | 左侧地图区域几乎整块深绿，只有右侧状态/建造面板，无法辨认单位和据点。 | 核对地图、单位和投影的默认发布路径，补有可辨认基地、部队与交战反馈的状态。 |
| P1 | [dungeon-generator-3d](review-2026-11-07/dungeon-generator-3d.png) | 地牢表现不完整 | 紫色背景上散布浅色平台，墙体、门洞、家具与房间主题难以辨认。 | 对照语义 cell 与实际发布对象，核对墙/道具资源与材质；补可通行连接和房间近景。 |
| P1 | [editor-host-world](review-2026-11-07/editor-host-world.png) | 编辑器空白 | 新图为近乎纯黑背景，没有 README 所述挂载的 Action/Voxel 面板。 | 核对 EditorHost 包挂载与首帧布局，取得能看见两类面板和命令入口的截图；当前只能证明产生了图像。 |
| P1 | [glass-crystal](review-2026-11-07/glass-crystal.png) | 空白画面 | 新图几乎是单一浅灰色，README 所述的立方体、八面体和水晶球均不可辨认。 | 先核对透明材质/深度/相机曝光以及 shader 与运行时契约，匹配源码构建复跑；不能当成成功光学展示。 |
| P1 | [gpu-agents](review-2026-11-07/gpu-agents.png) | 空白仿真画面 | 新图几乎纯黑，没有可辨认的鱼群、生命网格或花瓣。 | 核对后端注册、stepAll、可见对象提交与相机；补各后端可辨认的默认场景。 |
| P1 | [hex-planet](review-2026-11-07/hex-planet.png) | 星球材质过白与 UI 遮挡 | 球体几乎整片白色，六边形地貌与资源难辨；界面叠在球体前方。 | 先复核与 hex-terrain-3d 共用的材质/顶点编码契约，再调整星球与侧栏布局。 |
| P1 | [hex-terrain](review-2026-11-07/hex-terrain.png) | 块面与明暗异常 | 大片白色/青色交替块面抢过岛屿地貌，中央高亮区层次弱；岛屿边缘出框。 | 先核对水陆材质、光照/色调映射和网格颜色，再调整全景相机；保留同 seed 前后对照。 |
| P1 | [hex-terrain-3d](review-2026-11-07/hex-terrain-3d.png) | 地形过亮与遮挡 | 大块地形几乎全白，地表材质难辨，左侧帮助文字覆盖主要观察区。 | 先检查材质、光照/曝光与运行时契约，再调整相机留边及帮助面板位置。 |
| P1 | [ink-arena](review-2026-11-07/ink-arena.png) | 表面明暗异常 | 墨迹可见但大部分地板/墙面呈黑色，结构边界与材质细节消失，和旧画廊明显不同。 | 固定相机核对基础表面/墨迹着色与运行时契约，补正常光照下的喷墨前后对照。 |
| P1 | [ink-combat](review-2026-11-07/ink-combat.png) | 空白画面 | 截图只有单一米白背景，没有可见角色、战斗目标或喷墨结果。 | 核对默认关卡/主体创建、相机与资源，取得无需额外设置的战斗代表帧。 |
| P1 | [inspector-demo](review-2026-11-07/inspector-demo.png) | 编辑器文字重叠 | Database 面板的字段名、复选项和内容挤在一起，多行文本明显重叠。 | 优先检查行高、列宽、树节点布局与滚动裁剪，再在实际窗口和缩放比例下复验。 |
| P1 | [interior-mapping](review-2026-11-07/interior-mapping.png) | 主体与效果不可辨认 | 新图只有接近侧视的高墙与地面，预期窗格和室内透视几乎看不到。 | 固定正面偏斜的代表相机，暂停自动环绕；若仍不可见，再排查 shader/贴图契约。 |
| P1 | [metroidvania](review-2026-11-07/metroidvania.png) | 素材与色彩异常 | 平台/场景出现大面积荧光绿纯色，人物呈彩色小矩形，和可辨认的动作游戏画面差距大。 | 核对素材加载与占位绘制分支，先恢复人物/平台视觉，再检查命中、跳跃和关卡辨识度。 |
| P1 | [model-converter](review-2026-11-07/model-converter.png) | 空白且缺少依赖反馈 | 新图只有深灰背景；既没有转换模型，也没有 README 约定的缺失 bpy 提示。 | 核对插件/bpy 前置条件和错误 UI；依赖缺失也应显示明确状态，提供有效模型转换后的画面。 |
| P1 | [pcg-biome](review-2026-11-07/pcg-biome.png) | 生成结果不可见 | 深色背景中只见一个很小的黄点，看不到预期树、草、岩石与道路排除区。 | 核对 Cell 生成、实例发布、资产提供者与相机尺度；补近景生成和远处清理对照。 |
| P1 | [rendering-chain-lab](review-2026-11-07/rendering-chain-lab.png) | 场景雾化与效果不可读 | 整个画面被近似均匀蓝灰色覆盖，测试物体很淡，反射链路差异难以辨认。 | 逐级对照渲染路径、曝光、雾与反射输入；先取得清晰基线，再展示各阶段差异。 |
| P1 | [rpg](review-2026-11-07/rpg.png) | 文字重叠与界面层级 | 左侧属性、技能、日志文字在底部相互挤压重叠；主体仍是蓝红方块。 | 优先拆分属性/操作/日志布局并限制文本区域，再补角色与攻击反馈；按实际窗口尺寸验证。 |
| P1 | [skyscraper-generator](review-2026-11-07/skyscraper-generator.png) | 缺少建筑主体 | 画面几乎全是灰蓝背景，仅左上角出现很小的纹理/几何片块，没有完整高楼。 | 先检查生成对象、镜头包围盒与缩放，再核对材质/模型加载，补完整楼体与立面近景。 |
| P1 | [softbody](review-2026-11-07/softbody.png) | 布料与流体主体不可见 | 画面为暗色空面板，未见可拖动布料或流体粒子。 | 核对默认布料/流体创建与绘制路径、控件遮挡和裁剪，补有布料形变与水槽粒子的状态。 |
| P1 | [terrain-gallery](review-2026-11-07/terrain-gallery.png) | 地表颜色异常 | 三个种子均出现彩虹状高饱和地表；水系与生物群系难以按自然材质辨认。 | 与 terrain-preview 共用同种子/材质的最小复现，确认 shader 与运行时匹配，再恢复自然配色。 |
| P1 | [terrain-preview](review-2026-11-07/terrain-preview.png) | 地表颜色异常 | 地形呈青、品红、黄等高饱和分区，和自然地表/河湖展示目标不符。 | 先排查运行时与 shader/顶点布局契约，确认是否误用调试输出；在匹配源码构建上复拍再定位责任。 |
| P1 | [terrain-smooth-mountain](review-2026-11-07/terrain-smooth-mountain.png) | 材质颜色与取景异常 | 山体呈大块饱和蓝色，周围接近白色且场景缩在底部，缺少自然地表层次。 | 核对地形 shader/材质输入与运行时契约，再按全地形包围盒调整相机。 |
| P1 | [topdown-procmap](review-2026-11-07/topdown-procmap.png) | 遮挡与地表颜色异常 | 树冠几乎铺满画面，右侧地表出现黄/品红色带；路径和河岸无法清晰辨认。 | 先复核地表材质，再降低树冠遮挡并提供总览/近景；在相同 seed 下复拍。 |
| P1 | [unity-vfx-port-lab](review-2026-11-07/unity-vfx-port-lab.png) | 特效颜色与透明边缘异常 | 多个预览格主要呈过亮白色像素块，篝火、传送门与烟雾的颜色/层次难区分。 | 与 flame-jet 共同比对配方、贴图采样、颜色与混合链路；匹配运行时后逐效果补峰值帧。 |
| P1 | [urban-generator](review-2026-11-07/urban-generator.png) | 空白城市预览 | 只有灰蓝背景和左上窄条控件，没有可辨认的街道、地块或建筑。 | 确认默认生成是否执行及相机定位，提供启动即可看到的街区与街道/地块图层对照。 |
| P1 | [venom-transform](review-2026-11-07/venom-transform.png) | 演出缺少代表帧 | 当前抓帧为直立 T 姿角色与黑色条状附属物，粉色空房间；看不出完整变装过程。 | 选取开始/过渡/完成三状态，补充动作和材质过渡近景；不能用这一帧断言动画失效。 |
| P1 | [vrm-avatar](review-2026-11-07/vrm-avatar.png) | 角色显示异常 | 画面只有黑色三角形，无法辨认 VRM 角色、材质或骨骼。 | 用匹配源码的构建检查模型导入、顶点/蒙皮布局、材质和相机；取得完整角色后再评动作。 |
| P2 | [ai-game](review-2026-11-07/ai-game.png) | 占位视觉 | 两个矩形角色与横条构成全部主体，训练/对战状态主要靠小字解释。 | 保留算法演示定位，增加角色轮廓、状态标识与攻击/受击反馈。 |
| P2 | [archspace-editor](review-2026-11-07/archspace-editor.png) | 白模展示 | 白色无材质墙体悬在纯色背景，缺少尺度参照、门窗细节和空间用途。 | 补地板、材质、家具与尺度参照，显示编辑对象/操作反馈。 |
| P2 | [archspace-floorplan](review-2026-11-07/archspace-floorplan.png) | 白模展示 | 全白房间与地板对比弱，房间划分和开口不易辨认。 | 使用分区配色或材质，加入房间名、尺寸与平面/透视对照。 |
| P2 | [armored-command-3d](review-2026-11-07/armored-command-3d.png) | 构图与比例 | 大块浅色地面占据画面，坦克和编队太小，骨骼细节与交战反馈难辨。 | 提供交战近景与总览切换，强化履带、炮塔、弹道和命中特效的可读性。 |
| P2 | [automatic-bridge](review-2026-11-07/automatic-bridge.png) | 材质与明暗层次 | 桥、河岸和背景接近灰白，桥面细节、岸线与水面层次不足。 | 以桥面、支墩、地形、水面建立材质分区，改善光照和环境背景。 |
| P2 | [building](review-2026-11-07/building.png) | 场景与面板比例 | 地图缩在左侧小区域，右侧参数面板占据较大空间，建筑和道路细节很小。 | 按窗口自适应扩大画布，提供一组更完整的默认街区和清晰选中反馈。 |
| P2 | [building-tilemap](review-2026-11-07/building-tilemap.png) | 场景内容稀疏 | 少量建筑位于规整方板上，空地和直线路径占比高，难体现完整建造场景。 | 补可读的房屋/围栏/道路组合，提供近景与编辑前后对照。 |
| P2 | [cable-chain-rope](review-2026-11-07/cable-chain-rope.png) | 取景裁切 | 链条从左下方出框，背景单一，无法在一帧看全端点与形变。 | 提供完整链条总览、关节近景和有尺度的悬挂/受力场景。 |
| P2 | [camera-controllers](review-2026-11-07/camera-controllers.png) | 默认镜头过近 | 巨大前景球体被底边裁切，后方对象被遮挡，难以对比相机模式。 | 让默认镜头容纳全部测试物体，补轨道/跟随/自由相机同主体对照和模式标签。 |
| P2 | [cardgame](review-2026-11-07/cardgame.png) | 布局与信息层级 | 牌桌大面积空黑，手牌挤在底部小区域，规则/操作面板窄而拥挤。 | 围绕中央牌桌组织对手、公共牌和手牌，放大牌面与下注状态，减轻侧栏密度。 |
| P2 | [castle-generator](review-2026-11-07/castle-generator.png) | 主体小与材质单一 | 城堡位于大片纯色背景中央，整体偏白，门窗、墙面与地面缺少对比。 | 拉近默认镜头，加入地基和尺度参照，分配屋顶/墙体材质并补接地阴影。 |
| P2 | [climbing-playground](review-2026-11-07/climbing-playground.png) | 能力展示缺少角色与目标 | 只有一排彩色基础方块，画面中难以辨认攀爬者和动作差异。 | 默认聚焦角色与当前障碍，显示接触/攀爬目标，再提供整体障碍布局。 |
| P2 | [daynight](review-2026-11-07/daynight.png) | 天空体积感与场景风格不统一 | 云层像低空横向铺开的宽扁片层，亮区偏白；下方为简单球冠树，天空与地景的完成度不一致。 | 增加不同太阳高度的固定机位对照，复核云层密度/高度/曝光，再补更完整的地景参照。 |
| P2 | [dynamic-water-grid](review-2026-11-07/dynamic-water-grid.png) | 构图与效果对照不足 | 小块水陆地图悬在大面积黑背景中；顶部文字压到场景，单帧不易辨认网格改善之处。 | 扩大地图并将说明移到独立面板，提供网格开/关及岸线近景对照。 |
| P2 | [flame-jet](review-2026-11-07/flame-jet.png) | 火焰色彩与层次不足 | 喷流主要是过亮白块和灰色方形烟片，没有清晰的黄芯、橙红火舌与火星层次。 | 复核粒子颜色/透明混合及素材，调整火芯曝光与烟雾边缘，再记录稳态与峰值。 |
| P2 | [flight-vehicle-controllers](review-2026-11-07/flight-vehicle-controllers.png) | 载具占位视觉 | 汽车只像贴地的浅蓝平板，周围是低对比绿地与方块，运动朝向和车轮难辨。 | 增加车身/轮胎/机翼等最低识别轮廓和驾驶 HUD，分别补汽车与飞机的正常操作画面。 |
| P2 | [fluid3d](review-2026-11-07/fluid3d.png) | 水体形态与尺度不清 | 深色背景里只有小团发光蓝点，连续水面、容器和碰撞关系不清楚。 | 改善表面重建的观察机位和照明，加入容器、尺度参照与粒子/网格模式并排对照。 |
| P2 | [fps-arena](review-2026-11-07/fps-arena.png) | 占位场景缺乏识别与反馈 | 大面积灰墙、灰地面及简单橙色靶子，敌我、命中点、路径与目标层级较弱。 | 先建立地面/墙体/目标材质对比，再补准星、命中反馈与可辨认的关卡地标。 |
| P2 | [geometry-stroke-lab](review-2026-11-07/geometry-stroke-lab.png) | 案例太小且缺少说明 | 几条彩色折带集中在画面中央小范围，难区分路径、笔触宽度与连接差异。 | 分案例放大并标注接头/端帽/宽度参数，增加路径线与生成结果对照。 |
| P2 | [hd2d-riverside](review-2026-11-07/hd2d-riverside.png) | 场景完成度 | 场地呈孤立方板，直线河渠与路径较机械；人物很小，空地占比高。 | 围绕河岸做地形过渡、植被与环境叙事，增加主角近景和景深层次。 |
| P2 | [hex-levels](review-2026-11-07/hex-levels.png) | 文字遮挡与地图语义 | 多行说明覆盖左上地图，六边形灰绿地块缺少入口、路线与目标图例。 | 将说明移到侧栏或可折叠帮助，补图例和关卡目标，按地图边界适配视口。 |
| P2 | [hud-gallery](review-2026-11-07/hud-gallery.png) | UI 尺寸与对比 | 资源条、任务文字和底部技能栏都很小、很暗，主体只剩中央蓝色方块。 | 在实际窗口下检查设计坐标缩放、字体尺寸和安全区；补冷却/扣血状态及多宽高比截图。 |
| P2 | [ik-spider](review-2026-11-07/ik-spider.png) | 主体占比与地面参照 | 蜘蛛缩在大面积深色背景中央，细腿和接地点不易看清。 | 扩大默认镜头里的主体，增加地面/障碍及接触点可视化，补静态与跨障碍状态。 |
| P2 | [lighting-showcase](review-2026-11-07/lighting-showcase.png) | 高光过亮 | 多个白色测试体和地面亮区接近纯白，材质和光照差异被冲淡。 | 固定曝光检查光源强度、环境光与色调映射，补灯光开/关及材质球对照。 |
| P2 | [linear-structures](review-2026-11-07/linear-structures.png) | 对象取景不完整 | 道路/线性结构贯穿画面但两端和左下构件被裁切，只能看到局部片段。 | 按生成结果包围盒提供总览，再给连接处/截面近景和尺寸标识。 |
| P2 | [map-fog](review-2026-11-07/map-fog.png) | 迷雾缺少地图对照 | 新图为大片纯白不规则雾块，缺少可读的底图、已探索区域和选中状态。 | 加入清楚的底图与已解锁/未解锁/正在溶解三区对照，改善云层边缘层次。 |
| P2 | [mesh-modifier-lab](review-2026-11-07/mesh-modifier-lab.png) | 对照缺少可读性 | 13 个变形体缩成居中细条，上下大片空背景，缺少名称和未变形参照。 | 采用有标签的网格布局，增加原型/结果对照和近景，提高主体占比。 |
| P2 | [procgen](review-2026-11-07/procgen.png) | 算法演示感强 | 棕底、亮绿房间和蓝色线条只展示算法结构，未体现最终地图品质。 | 明确标注为算法诊断视图，并配一张应用 tileset 后的结果对照。 |
| P2 | [procgen-nature-quality](review-2026-11-07/procgen-nature-quality.png) | 取景裁切与材质层次 | 树冠被上边框裁切，地面呈大片亮黄，岩石/树叶明暗偏浅。 | 拉远或降低主体，统一自然色盘并增加接地阴影；材质球和场景视图分开。 |
| P2 | [roguelike-generator](review-2026-11-07/roguelike-generator.png) | 地图过小与留白 | 地牢只占画面中央小块，主体灰暗，房间功能与路线读不出来。 | 按地图包围盒适配视口，增加入口/出口/房间类型图例，保留 seed 信息。 |
| P2 | [rpg-classic](review-2026-11-07/rpg-classic.png) | 启动菜单缺少层次 | 存档槽、继续/新游戏/删除按钮挤在中央窄列，两侧大面积空黑，未展示 RPG 场景。 | 重排为有标题、明确主操作和整齐存档卡片的菜单；画廊另补正常新游戏后的场景帧。 |
| P2 | [scad-watermill](review-2026-11-07/scad-watermill.png) | 相机陷入建筑局部 | 画面几乎被柱子、立面和内部构件填满，水车与完整建筑轮廓看不到。 | 先用外部全景容纳水车、建筑与水道，再提供机械/室内近景；避免默认镜头落在结构内部。 |
| P2 | [spline-tube-lab](review-2026-11-07/spline-tube-lab.png) | 缺少参数与路径对照 | 悬空几何体在空背景上零散分布，看不出路径、截面与最终挤出的关系。 | 按案例分区标注，叠加路径控制点与截面，统一尺寸和光照。 |
| P2 | [surface-vine-growth](review-2026-11-07/surface-vine-growth.png) | 生长表现像调试几何 | 浅色直线和圆点附在灰色方块上，分枝、茎干与叶片层次不明显，难形成爬藤的视觉印象。 | 保留接触点调试开关，增加可辨认的茎干/叶片与生长时间阶段，场景体块分配清楚材质。 |
| P2 | [tree-generator](review-2026-11-07/tree-generator.png) | 材质层次与调试图干扰 | 树冠偏浅黄绿、叶片层次弱；左上大块浅色纹理预览分散注意。 | 把纹理预览做成可标注/关闭的调试面板，提供不同树种和叶片近景，改善树干/树叶明暗。 |
| P2 | [uds-sky](review-2026-11-07/uds-sky.png) | 启动时段不适合作单张展示 | 默认 05:00 的新帧几乎全黑，仅少量星点；不利于展示天空和云层能力。 | 保留真实清晨状态，并补白昼/日落固定时间与相机对照；这不等于夜空渲染错误。 |
| P2 | [vehicle](review-2026-11-07/vehicle.png) | 说明面板遮挡玩法区 | 左上说明重复显示且面板占据大块地图，载具很小，射击与上下车状态不易看清。 | 合并帮助文本并可折叠显示，将主要控制/状态移到 HUD 边缘，补载具近景与实际交战帧。 |
| P2 | [waterfall-demo](review-2026-11-07/waterfall-demo.png) | 特效缺少场景上下文 | 近景只见一张蓝白条纹水幕，缺少落差、岩壁、水潭与泡沫的尺度关系。 | 保留 shader 近景，同时增加带岩壁、撞击泡沫和下游水面的完整场景。 |
| P2 | [weather](review-2026-11-07/weather.png) | 天气表达弱于参数面板 | 暴雨预设仍以浅色基础几何体为主，雨线稀疏，湿地面与暴风气氛不明显。 | 增加湿润材质、天空变化与风雨参照；用晴/雨/雪同机位对照评估，不能从单帧判断动态雨量。 |
| P3 | [blade-whirlwind](review-2026-11-07/blade-whirlwind.png) | 特效抓帧时机 | 当前帧主体与剑很小，旋风/刀光没有形成清晰轮廓；单帧不足以判断功能。 | 选择攻击峰值帧和短动画，镜头跟随或固定近景，保留无特效基线。 |
| P3 | [destruction-basic](review-2026-11-07/destruction-basic.png) | 默认状态无法展示破坏 | 只有完整灰色方块，单帧无法说明破碎能力；未验证是否需要操作触发。 | 并排记录破坏前/触发/碎片落地，并加操作提示与碎片近景。 |
| P3 | [mesh-impact-lab](review-2026-11-07/mesh-impact-lab.png) | 形变过程缺少对照 | 单帧只见黄色圆柱，弹丸、命中位置和形变幅度不易辨认。 | 补撞击前/命中/损伤后三帧，在同机位标注命中点和未变形轮廓。 |
| P3 | [railgun-3d](review-2026-11-07/railgun-3d.png) | 抓帧未命中特效阶段 | 当前帧只隐约看见网格，闪光/弹道和烟圈不可辨认；示例默认发射间隔 5.2 秒，不能据此判断功能失效。 | 按发射动作同步取闪光、冷却弹道、烟圈三个关键帧，再保留短动画展示消散过程。 |
| P3 | [snow](review-2026-11-07/snow.png) | 演示上下文不足 | 脚印形变清晰，但整屏近白雪面缺乏人物、尺度和前后变化参照。 | 保留近景，再增加行走角色、未踩踏区域和斜光下的远景对照。 |
| P3 | [sword-slash-trail](review-2026-11-07/sword-slash-trail.png) | 刀光缺少代表帧 | 当前帧角色和剑很小，主要只见圆形台面与标杆，扇形刀光不可辨认。 | 按已有 Slash 动作同步取横斩/斜斩/上挑的峰值帧或短动画，默认近景展示刀根/刀尖轨迹。 |
| P3 | [water-splash-vfx](review-2026-11-07/water-splash-vfx.png) | 水花缺少代表帧 | 当前视图只见网格和暗色底盘，水冠/泡沫不明显；单帧不能确认是否处于爆发间隔。 | 按已有爆发控制同步取水冠展开峰值、嵌套层和消散帧；若仍不可见，再查材质与运行时契约。 |

可保留作为展示基线的候选：[galgame](review-2026-11-07/galgame.png)、[layered-animation](review-2026-11-07/layered-animation.png)、[galaxy](review-2026-11-07/galaxy.png)、[house-collapse](review-2026-11-07/house-collapse.png)、[interactive-shallow-water](review-2026-11-07/interactive-shallow-water.png)、[scad-living-room](review-2026-11-07/scad-living-room.png)、[skyforge-trials](review-2026-11-07/skyforge-trials.png)、[weather-daynight](review-2026-11-07/weather-daynight.png)。这些候选仅指本次静帧的可读性，不是完整功能验收。

## 抓帧失败：先解决运行/版本问题

这些项目保留旧图时，旧图不代表本轮成功。错误节选来自本次运行日志；完整本地日志位于忽略目录 `build/gallery-review/<example>.log`。未定位根因前不赋予美术评分。

| 示例 | 本轮错误节选 | 下一步 |
|---|---|---|
| [animation-clip-editor](../../../examples/animation-clip-editor/README.md) | eve_init failed: game:/main.nut:22 (buildWorkspace): the index 'EditorWorkspacePresenter' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [arpg-loop](../../../examples/arpg-loop/README.md) | eve_init failed: game:/main.nut:126 (<anonymous>): save directory unavailable | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [audio-source-editor](../../../examples/audio-source-editor/README.md) | eve_init failed: game:/main.nut:22 (buildWorkspace): the index 'EditorWorkspacePresenter' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [building_village](../../../examples/building_village/README.md) | eve_init failed: game:/main.nut:253 (<anonymous>): Could not prepare save directory | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [bullet-time](../../../examples/bullet-time/README.md) | eve_init failed: game:/main.nut:193 (buildScene): the index 'setXRayShader' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [character-cape-hair](../../../examples/character-cape-hair/README.md) | eve_init failed: game:/main.nut:235 (syncCapeProxyColliders): the index 'setColliderCenter' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [cinematic-story-camera](../../../examples/cinematic-story-camera/README.md) | eve_init failed: game:/main.nut:103 (setupCamera): the index 'CinemaDirector' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [combat-action-editor](../../../examples/combat-action-editor/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [conveyor-belt](../../../examples/conveyor-belt/README.md) | eve_init failed: game:/main.nut:104 (buildScene): the index 'setAlbedoUvScale' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [crowd](../../../examples/crowd/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [dialogue](../../../examples/dialogue/README.md) | eve_init failed: game:/main.nut:170 (buildUI): the index 'make_default_dialogue_ui' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [emberforge](../../../examples/emberforge/README.md) | eve_init failed: game:/main.nut:87 (ensureSetup): the index 'RPGForge' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [factory](../../../examples/factory/README.md) | eve_init failed: game:/main.nut:165 (<anonymous>): the index 'Logistics' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [growth](../../../examples/growth/README.md) | frame error: game:/main.nut:74 (ensureSetup): the index 'Growth' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [layered-cloth](../../../examples/layered-cloth/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [layered-cloth-gpu](../../../examples/layered-cloth-gpu/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [material-editor](../../../examples/material-editor/README.md) | eve_init failed: game:/main.nut:13 (<anonymous>): Cannot prepare the material editor save directory | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [modern-balcony](../../../examples/modern-balcony/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [ocean-voyage](../../../examples/ocean-voyage/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [pcg-underwater](../../../examples/pcg-underwater/README.md) | eve_init failed: game:/main.nut:52 (setupUnderwaterAudio): wrong number of parameters | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [physics-bake-replay](../../../examples/physics-bake-replay/README.md) | eve_init failed: game:/main.nut:134 (<anonymous>): cannot initialize replay save identity | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [physics-workshop](../../../examples/physics-workshop/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [pixel-perfect-camera](../../../examples/pixel-perfect-camera/README.md) | eve_init failed: game:/main.nut:121 (<anonymous>): the index 'setPixelSnap' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [post-patrol-reinforce](../../../examples/post-patrol-reinforce/README.md) | Run failed: Script execute failed in 'load.nut' [id=2]: load.nut:299 (validate_project_modules): required module is missing: roster | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [realtime-fog](../../../examples/realtime-fog/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [render-fidelity](../../../examples/render-fidelity/README.md) | eve_init failed: game:/main.nut:290 (rfApplyCsmQuality): Unknown exception | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [rpg-balance](../../../examples/rpg-balance/README.md) | 未取得 CAPTURED 标记及有效新 PNG；日志未给出明确的初始化/帧错误，需单独复跑定位退出或超时原因。 | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [smoke-flipbook](../../../examples/smoke-flipbook/README.md) | frame error: game:/main.nut:47 (saveAtlas): Cannot write atlas | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [tower-defense](../../../examples/tower-defense/README.md) | Run failed: Script execute failed in 'load.nut' [id=2]: load.nut:299 (validate_project_modules): required module is missing: towerdefense | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [trap-workshop](../../../examples/trap-workshop/README.md) | eve_init failed: game:/main.nut:356 (selfCheckComposition): the index 'getBeginTriggerCount' does not exist | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |
| [warship](../../../examples/warship/README.md) | Run failed: Script execute failed in 'load.nut' [id=2]: load.nut:299 (validate_project_modules): required module is missing: warship | 用匹配源码的完整构建复跑，按首个错误定位；成功后再评画面。 |

## 机制夹具的展示定位

`basic`、`ecs`、`devlab`、`game-tutorial`、`composable-rebellion`、`physics-force-lab` 以及多数 `pcg-*` 小例子采用方块、色块或进度条验证机制。这种简化不是渲染缺陷；但不适合直接作为引擎美术宣传图。若要提高这些示例的学习质量，优先增加清楚的能力名称、参数、开关前后对照与状态说明，再考虑替换美术素材。`tensor` 的渲染回调明确不绘制内容，计算结果输出到日志；`virtualgeometry` 的 README 也明确仅运行计算、屏幕显示背景色，因此这两个空窗口不作为视觉故障。`pcg-biome` 的预期树草岩石缺失已单列，不能用“机制夹具”解释。

## 全量截图索引

“已抓帧”仅代表成功生成无已知帧错误标记的新 PNG；视觉审查意见以上表为准。点击缩略图可看原分辨率。

| 示例 | 本轮状态 | 引擎截图 |
|---|---|---|
| [advanced-ballistics](../../../examples/advanced-ballistics/README.md) | 已抓帧 | <a href="review-2026-11-07/advanced-ballistics.png"><img src="review-2026-11-07/advanced-ballistics.png" width="180" alt="advanced-ballistics"></a> |
| [ai-game](../../../examples/ai-game/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/ai-game.png"><img src="review-2026-11-07/ai-game.png" width="180" alt="ai-game"></a> |
| [ai-stage](../../../examples/ai-stage/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/ai-stage.png"><img src="review-2026-11-07/ai-stage.png" width="180" alt="ai-stage"></a> |
| [animation-clip-editor](../../../examples/animation-clip-editor/README.md) | 抓帧失败 | 无新图 |
| [anime-character-lab](../../../examples/anime-character-lab/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/anime-character-lab.png"><img src="review-2026-11-07/anime-character-lab.png" width="180" alt="anime-character-lab"></a> |
| [archspace-editor](../../../examples/archspace-editor/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/archspace-editor.png"><img src="review-2026-11-07/archspace-editor.png" width="180" alt="archspace-editor"></a> |
| [archspace-floorplan](../../../examples/archspace-floorplan/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/archspace-floorplan.png"><img src="review-2026-11-07/archspace-floorplan.png" width="180" alt="archspace-floorplan"></a> |
| [armored-command-3d](../../../examples/armored-command-3d/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/armored-command-3d.png"><img src="review-2026-11-07/armored-command-3d.png" width="180" alt="armored-command-3d"></a> |
| [arpg-loop](../../../examples/arpg-loop/README.md) | 抓帧失败 | 无新图 |
| [atmospheric-fog](../../../examples/atmospheric-fog/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/atmospheric-fog.png"><img src="review-2026-11-07/atmospheric-fog.png" width="180" alt="atmospheric-fog"></a> |
| [attack-vfx](../../../examples/attack-vfx/README.md) | 已抓帧 | <a href="review-2026-11-07/attack-vfx.png"><img src="review-2026-11-07/attack-vfx.png" width="180" alt="attack-vfx"></a> |
| [audio-source-editor](../../../examples/audio-source-editor/README.md) | 抓帧失败 | 无新图 |
| [automatic-bridge](../../../examples/automatic-bridge/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/automatic-bridge.png"><img src="review-2026-11-07/automatic-bridge.png" width="180" alt="automatic-bridge"></a> |
| [autotile-production](../../../examples/autotile-production/README.md) | 已抓帧 | <a href="review-2026-11-07/autotile-production.png"><img src="review-2026-11-07/autotile-production.png" width="180" alt="autotile-production"></a> |
| [avatar-document-editor](../../../examples/avatar-document-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/avatar-document-editor.png"><img src="review-2026-11-07/avatar-document-editor.png" width="180" alt="avatar-document-editor"></a> |
| [basic](../../../examples/basic/README.md) | 已抓帧 | <a href="review-2026-11-07/basic.png"><img src="review-2026-11-07/basic.png" width="180" alt="basic"></a> |
| [biome-rules-editor](../../../examples/biome-rules-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/biome-rules-editor.png"><img src="review-2026-11-07/biome-rules-editor.png" width="180" alt="biome-rules-editor"></a> |
| [blade-whirlwind](../../../examples/blade-whirlwind/README.md) | 已抓帧；P3 | <a href="review-2026-11-07/blade-whirlwind.png"><img src="review-2026-11-07/blade-whirlwind.png" width="180" alt="blade-whirlwind"></a> |
| [blender_hot_reload](../../../examples/blender_hot_reload/README.md) | 已抓帧 | <a href="review-2026-11-07/blender_hot_reload.png"><img src="review-2026-11-07/blender_hot_reload.png" width="180" alt="blender_hot_reload"></a> |
| [building](../../../examples/building/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/building.png"><img src="review-2026-11-07/building.png" width="180" alt="building"></a> |
| [building-3d](../../../examples/building-3d/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/building-3d.png"><img src="review-2026-11-07/building-3d.png" width="180" alt="building-3d"></a> |
| [building-tilemap](../../../examples/building-tilemap/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/building-tilemap.png"><img src="review-2026-11-07/building-tilemap.png" width="180" alt="building-tilemap"></a> |
| [building_village](../../../examples/building_village/README.md) | 抓帧失败 | 无新图 |
| [bullet-time](../../../examples/bullet-time/README.md) | 抓帧失败 | 无新图 |
| [bush-fog-volumes](../../../examples/bush-fog-volumes/README.md) | 已抓帧 | <a href="review-2026-11-07/bush-fog-volumes.png"><img src="review-2026-11-07/bush-fog-volumes.png" width="180" alt="bush-fog-volumes"></a> |
| [bush-generator](../../../examples/bush-generator/README.md) | 已抓帧 | <a href="review-2026-11-07/bush-generator.png"><img src="review-2026-11-07/bush-generator.png" width="180" alt="bush-generator"></a> |
| [cable-chain-rope](../../../examples/cable-chain-rope/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/cable-chain-rope.png"><img src="review-2026-11-07/cable-chain-rope.png" width="180" alt="cable-chain-rope"></a> |
| [camera-controllers](../../../examples/camera-controllers/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/camera-controllers.png"><img src="review-2026-11-07/camera-controllers.png" width="180" alt="camera-controllers"></a> |
| [cardgame](../../../examples/cardgame/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/cardgame.png"><img src="review-2026-11-07/cardgame.png" width="180" alt="cardgame"></a> |
| [castle-generator](../../../examples/castle-generator/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/castle-generator.png"><img src="review-2026-11-07/castle-generator.png" width="180" alt="castle-generator"></a> |
| [cave-generator](../../../examples/cave-generator/README.md) | 已抓帧 | <a href="review-2026-11-07/cave-generator.png"><img src="review-2026-11-07/cave-generator.png" width="180" alt="cave-generator"></a> |
| [character-atelier](../../../examples/character-atelier/README.md) | 已抓帧 | <a href="review-2026-11-07/character-atelier.png"><img src="review-2026-11-07/character-atelier.png" width="180" alt="character-atelier"></a> |
| [character-cape-hair](../../../examples/character-cape-hair/README.md) | 抓帧失败 | 无新图 |
| [character-motion-lab](../../../examples/character-motion-lab/README.md) | 已抓帧 | <a href="review-2026-11-07/character-motion-lab.png"><img src="review-2026-11-07/character-motion-lab.png" width="180" alt="character-motion-lab"></a> |
| [cinematic-story-camera](../../../examples/cinematic-story-camera/README.md) | 抓帧失败 | 无新图 |
| [climbing-motion-matching](../../../examples/climbing-motion-matching/README.md) | 已抓帧 | <a href="review-2026-11-07/climbing-motion-matching.png"><img src="review-2026-11-07/climbing-motion-matching.png" width="180" alt="climbing-motion-matching"></a> |
| [climbing-playground](../../../examples/climbing-playground/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/climbing-playground.png"><img src="review-2026-11-07/climbing-playground.png" width="180" alt="climbing-playground"></a> |
| [combat-action-editor](../../../examples/combat-action-editor/README.md) | 抓帧失败 | 无新图 |
| [combat-arena](../../../examples/combat-arena/README.md) | 已抓帧 | <a href="review-2026-11-07/combat-arena.png"><img src="review-2026-11-07/combat-arena.png" width="180" alt="combat-arena"></a> |
| [commandery-rts](../../../examples/commandery-rts/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/commandery-rts.png"><img src="review-2026-11-07/commandery-rts.png" width="180" alt="commandery-rts"></a> |
| [companion-tactics](../../../examples/companion-tactics/README.md) | 已抓帧 | <a href="review-2026-11-07/companion-tactics.png"><img src="review-2026-11-07/companion-tactics.png" width="180" alt="companion-tactics"></a> |
| [composable-editor](../../../examples/composable-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/composable-editor.png"><img src="review-2026-11-07/composable-editor.png" width="180" alt="composable-editor"></a> |
| [composable-rebellion](../../../examples/composable-rebellion/README.md) | 已抓帧 | <a href="review-2026-11-07/composable-rebellion.png"><img src="review-2026-11-07/composable-rebellion.png" width="180" alt="composable-rebellion"></a> |
| [control-rig-editor](../../../examples/control-rig-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/control-rig-editor.png"><img src="review-2026-11-07/control-rig-editor.png" width="180" alt="control-rig-editor"></a> |
| [conveyor-belt](../../../examples/conveyor-belt/README.md) | 抓帧失败 | 无新图 |
| [crowd](../../../examples/crowd/README.md) | 抓帧失败 | 无新图 |
| [crowd-battle](../../../examples/crowd-battle/README.md) | 已抓帧 | <a href="review-2026-11-07/crowd-battle.png"><img src="review-2026-11-07/crowd-battle.png" width="180" alt="crowd-battle"></a> |
| [daynight](../../../examples/daynight/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/daynight.png"><img src="review-2026-11-07/daynight.png" width="180" alt="daynight"></a> |
| [destruction-basic](../../../examples/destruction-basic/README.md) | 已抓帧；P3 | <a href="review-2026-11-07/destruction-basic.png"><img src="review-2026-11-07/destruction-basic.png" width="180" alt="destruction-basic"></a> |
| [destruction-clusters](../../../examples/destruction-clusters/README.md) | 已抓帧 | <a href="review-2026-11-07/destruction-clusters.png"><img src="review-2026-11-07/destruction-clusters.png" width="180" alt="destruction-clusters"></a> |
| [devlab](../../../examples/devlab/README.md) | 已抓帧 | <a href="review-2026-11-07/devlab.png"><img src="review-2026-11-07/devlab.png" width="180" alt="devlab"></a> |
| [dialogue](../../../examples/dialogue/README.md) | 抓帧失败 | 无新图 |
| [dual-grid-material-gallery](../../../examples/dual-grid-material-gallery/README.md) | 已抓帧 | <a href="review-2026-11-07/dual-grid-material-gallery.png"><img src="review-2026-11-07/dual-grid-material-gallery.png" width="180" alt="dual-grid-material-gallery"></a> |
| [dungeon-generator-3d](../../../examples/dungeon-generator-3d/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/dungeon-generator-3d.png"><img src="review-2026-11-07/dungeon-generator-3d.png" width="180" alt="dungeon-generator-3d"></a> |
| [dynamic-water-grid](../../../examples/dynamic-water-grid/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/dynamic-water-grid.png"><img src="review-2026-11-07/dynamic-water-grid.png" width="180" alt="dynamic-water-grid"></a> |
| [ecs](../../../examples/ecs/README.md) | 已抓帧 | <a href="review-2026-11-07/ecs.png"><img src="review-2026-11-07/ecs.png" width="180" alt="ecs"></a> |
| [editor-api-v2](../../../examples/editor-api-v2/README.md) | 已抓帧 | <a href="review-2026-11-07/editor-api-v2.png"><img src="review-2026-11-07/editor-api-v2.png" width="180" alt="editor-api-v2"></a> |
| [editor-host-world](../../../examples/editor-host-world/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/editor-host-world.png"><img src="review-2026-11-07/editor-host-world.png" width="180" alt="editor-host-world"></a> |
| [editor-ui-gallery](../../../examples/editor-ui-gallery/README.md) | 已抓帧 | <a href="review-2026-11-07/editor-ui-gallery.png"><img src="review-2026-11-07/editor-ui-gallery.png" width="180" alt="editor-ui-gallery"></a> |
| [editor-workspace-shell](../../../examples/editor-workspace-shell/README.md) | 已抓帧 | <a href="review-2026-11-07/editor-workspace-shell.png"><img src="review-2026-11-07/editor-workspace-shell.png" width="180" alt="editor-workspace-shell"></a> |
| [ember-guard](../../../examples/ember-guard/README.md) | 已抓帧 | <a href="review-2026-11-07/ember-guard.png"><img src="review-2026-11-07/ember-guard.png" width="180" alt="ember-guard"></a> |
| [emberforge](../../../examples/emberforge/README.md) | 抓帧失败 | 无新图 |
| [enemy-vision](../../../examples/enemy-vision/README.md) | 已抓帧 | <a href="review-2026-11-07/enemy-vision.png"><img src="review-2026-11-07/enemy-vision.png" width="180" alt="enemy-vision"></a> |
| [environment-query](../../../examples/environment-query/README.md) | 已抓帧 | <a href="review-2026-11-07/environment-query.png"><img src="review-2026-11-07/environment-query.png" width="180" alt="environment-query"></a> |
| [explosion-lab](../../../examples/explosion-lab/README.md) | 已抓帧 | <a href="review-2026-11-07/explosion-lab.png"><img src="review-2026-11-07/explosion-lab.png" width="180" alt="explosion-lab"></a> |
| [factory](../../../examples/factory/README.md) | 抓帧失败 | 无新图 |
| [flame-jet](../../../examples/flame-jet/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/flame-jet.png"><img src="review-2026-11-07/flame-jet.png" width="180" alt="flame-jet"></a> |
| [flight-vehicle-controllers](../../../examples/flight-vehicle-controllers/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/flight-vehicle-controllers.png"><img src="review-2026-11-07/flight-vehicle-controllers.png" width="180" alt="flight-vehicle-controllers"></a> |
| [fluid3d](../../../examples/fluid3d/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/fluid3d.png"><img src="review-2026-11-07/fluid3d.png" width="180" alt="fluid3d"></a> |
| [fps-arena](../../../examples/fps-arena/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/fps-arena.png"><img src="review-2026-11-07/fps-arena.png" width="180" alt="fps-arena"></a> |
| [galaxy](../../../examples/galaxy/README.md) | 已抓帧 | <a href="review-2026-11-07/galaxy.png"><img src="review-2026-11-07/galaxy.png" width="180" alt="galaxy"></a> |
| [galgame](../../../examples/galgame/README.md) | 已抓帧 | <a href="review-2026-11-07/galgame.png"><img src="review-2026-11-07/galgame.png" width="180" alt="galgame"></a> |
| [game-tutorial](../../../examples/game-tutorial/README.md) | 已抓帧 | <a href="review-2026-11-07/game-tutorial.png"><img src="review-2026-11-07/game-tutorial.png" width="180" alt="game-tutorial"></a> |
| [geometry-stroke-lab](../../../examples/geometry-stroke-lab/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/geometry-stroke-lab.png"><img src="review-2026-11-07/geometry-stroke-lab.png" width="180" alt="geometry-stroke-lab"></a> |
| [glass-crystal](../../../examples/glass-crystal/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/glass-crystal.png"><img src="review-2026-11-07/glass-crystal.png" width="180" alt="glass-crystal"></a> |
| [gpu-agents](../../../examples/gpu-agents/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/gpu-agents.png"><img src="review-2026-11-07/gpu-agents.png" width="180" alt="gpu-agents"></a> |
| [gpu-formation](../../../examples/gpu-formation/README.md) | 已抓帧 | <a href="review-2026-11-07/gpu-formation.png"><img src="review-2026-11-07/gpu-formation.png" width="180" alt="gpu-formation"></a> |
| [ground-fissure](../../../examples/ground-fissure/README.md) | 已抓帧 | <a href="review-2026-11-07/ground-fissure.png"><img src="review-2026-11-07/ground-fissure.png" width="180" alt="ground-fissure"></a> |
| [growth](../../../examples/growth/README.md) | 抓帧失败 | 无新图 |
| [hd2d-riverside](../../../examples/hd2d-riverside/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/hd2d-riverside.png"><img src="review-2026-11-07/hd2d-riverside.png" width="180" alt="hd2d-riverside"></a> |
| [hex-levels](../../../examples/hex-levels/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/hex-levels.png"><img src="review-2026-11-07/hex-levels.png" width="180" alt="hex-levels"></a> |
| [hex-planet](../../../examples/hex-planet/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/hex-planet.png"><img src="review-2026-11-07/hex-planet.png" width="180" alt="hex-planet"></a> |
| [hex-terrain](../../../examples/hex-terrain/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/hex-terrain.png"><img src="review-2026-11-07/hex-terrain.png" width="180" alt="hex-terrain"></a> |
| [hex-terrain-3d](../../../examples/hex-terrain-3d/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/hex-terrain-3d.png"><img src="review-2026-11-07/hex-terrain-3d.png" width="180" alt="hex-terrain-3d"></a> |
| [house-collapse](../../../examples/house-collapse/README.md) | 已抓帧 | <a href="review-2026-11-07/house-collapse.png"><img src="review-2026-11-07/house-collapse.png" width="180" alt="house-collapse"></a> |
| [housegen](../../../examples/housegen/README.md) | 已抓帧 | <a href="review-2026-11-07/housegen.png"><img src="review-2026-11-07/housegen.png" width="180" alt="housegen"></a> |
| [hud-gallery](../../../examples/hud-gallery/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/hud-gallery.png"><img src="review-2026-11-07/hud-gallery.png" width="180" alt="hud-gallery"></a> |
| [i18n](../../../examples/i18n/README.md) | 已抓帧 | <a href="review-2026-11-07/i18n.png"><img src="review-2026-11-07/i18n.png" width="180" alt="i18n"></a> |
| [ik-spider](../../../examples/ik-spider/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/ik-spider.png"><img src="review-2026-11-07/ik-spider.png" width="180" alt="ik-spider"></a> |
| [ik-spider-3d](../../../examples/ik-spider-3d/README.md) | 已抓帧 | <a href="review-2026-11-07/ik-spider-3d.png"><img src="review-2026-11-07/ik-spider-3d.png" width="180" alt="ik-spider-3d"></a> |
| [impact-arena](../../../examples/impact-arena/README.md) | 已抓帧 | <a href="review-2026-11-07/impact-arena.png"><img src="review-2026-11-07/impact-arena.png" width="180" alt="impact-arena"></a> |
| [impasto-paint](../../../examples/impasto-paint/README.md) | 已抓帧 | <a href="review-2026-11-07/impasto-paint.png"><img src="review-2026-11-07/impasto-paint.png" width="180" alt="impasto-paint"></a> |
| [ink-arena](../../../examples/ink-arena/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/ink-arena.png"><img src="review-2026-11-07/ink-arena.png" width="180" alt="ink-arena"></a> |
| [ink-combat](../../../examples/ink-combat/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/ink-combat.png"><img src="review-2026-11-07/ink-combat.png" width="180" alt="ink-combat"></a> |
| [inspector-demo](../../../examples/inspector-demo/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/inspector-demo.png"><img src="review-2026-11-07/inspector-demo.png" width="180" alt="inspector-demo"></a> |
| [interactive-shallow-water](../../../examples/interactive-shallow-water/README.md) | 已抓帧 | <a href="review-2026-11-07/interactive-shallow-water.png"><img src="review-2026-11-07/interactive-shallow-water.png" width="180" alt="interactive-shallow-water"></a> |
| [interior-mapping](../../../examples/interior-mapping/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/interior-mapping.png"><img src="review-2026-11-07/interior-mapping.png" width="180" alt="interior-mapping"></a> |
| [inventory](../../../examples/inventory/README.md) | 已抓帧 | <a href="review-2026-11-07/inventory.png"><img src="review-2026-11-07/inventory.png" width="180" alt="inventory"></a> |
| [iron-chain-bridge](../../../examples/iron-chain-bridge/README.md) | 已抓帧 | <a href="review-2026-11-07/iron-chain-bridge.png"><img src="review-2026-11-07/iron-chain-bridge.png" width="180" alt="iron-chain-bridge"></a> |
| [ironwarden-retarget](../../../examples/ironwarden-retarget/README.md) | 已抓帧 | <a href="review-2026-11-07/ironwarden-retarget.png"><img src="review-2026-11-07/ironwarden-retarget.png" width="180" alt="ironwarden-retarget"></a> |
| [iso-grid-walk](../../../examples/iso-grid-walk/README.md) | 已抓帧 | <a href="review-2026-11-07/iso-grid-walk.png"><img src="review-2026-11-07/iso-grid-walk.png" width="180" alt="iso-grid-walk"></a> |
| [laser-weapon](../../../examples/laser-weapon/README.md) | 已抓帧 | <a href="review-2026-11-07/laser-weapon.png"><img src="review-2026-11-07/laser-weapon.png" width="180" alt="laser-weapon"></a> |
| [lattice-deform](../../../examples/lattice-deform/README.md) | 已抓帧 | <a href="review-2026-11-07/lattice-deform.png"><img src="review-2026-11-07/lattice-deform.png" width="180" alt="lattice-deform"></a> |
| [layered-animation](../../../examples/layered-animation/README.md) | 已抓帧 | <a href="review-2026-11-07/layered-animation.png"><img src="review-2026-11-07/layered-animation.png" width="180" alt="layered-animation"></a> |
| [layered-cloth](../../../examples/layered-cloth/README.md) | 抓帧失败 | 无新图 |
| [layered-cloth-gpu](../../../examples/layered-cloth-gpu/README.md) | 抓帧失败 | 无新图 |
| [level-designer](../../../examples/level-designer/README.md) | 已抓帧 | <a href="review-2026-11-07/level-designer.png"><img src="review-2026-11-07/level-designer.png" width="180" alt="level-designer"></a> |
| [lighting-showcase](../../../examples/lighting-showcase/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/lighting-showcase.png"><img src="review-2026-11-07/lighting-showcase.png" width="180" alt="lighting-showcase"></a> |
| [linear-structures](../../../examples/linear-structures/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/linear-structures.png"><img src="review-2026-11-07/linear-structures.png" width="180" alt="linear-structures"></a> |
| [map-fog](../../../examples/map-fog/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/map-fog.png"><img src="review-2026-11-07/map-fog.png" width="180" alt="map-fog"></a> |
| [material-editor](../../../examples/material-editor/README.md) | 抓帧失败 | 无新图 |
| [mesh-contact-fusion](../../../examples/mesh-contact-fusion/README.md) | 已抓帧 | <a href="review-2026-11-07/mesh-contact-fusion.png"><img src="review-2026-11-07/mesh-contact-fusion.png" width="180" alt="mesh-contact-fusion"></a> |
| [mesh-impact-lab](../../../examples/mesh-impact-lab/README.md) | 已抓帧；P3 | <a href="review-2026-11-07/mesh-impact-lab.png"><img src="review-2026-11-07/mesh-impact-lab.png" width="180" alt="mesh-impact-lab"></a> |
| [mesh-modifier-lab](../../../examples/mesh-modifier-lab/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/mesh-modifier-lab.png"><img src="review-2026-11-07/mesh-modifier-lab.png" width="180" alt="mesh-modifier-lab"></a> |
| [meteor-impact](../../../examples/meteor-impact/README.md) | 已抓帧 | <a href="review-2026-11-07/meteor-impact.png"><img src="review-2026-11-07/meteor-impact.png" width="180" alt="meteor-impact"></a> |
| [metroidvania](../../../examples/metroidvania/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/metroidvania.png"><img src="review-2026-11-07/metroidvania.png" width="180" alt="metroidvania"></a> |
| [model-converter](../../../examples/model-converter/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/model-converter.png"><img src="review-2026-11-07/model-converter.png" width="180" alt="model-converter"></a> |
| [modern-balcony](../../../examples/modern-balcony/README.md) | 抓帧失败 | 无新图 |
| [modern-battlefield](../../../examples/modern-battlefield/README.md) | 已抓帧 | <a href="review-2026-11-07/modern-battlefield.png"><img src="review-2026-11-07/modern-battlefield.png" width="180" alt="modern-battlefield"></a> |
| [neon-racing](../../../examples/neon-racing/README.md) | 已抓帧 | <a href="review-2026-11-07/neon-racing.png"><img src="review-2026-11-07/neon-racing.png" width="180" alt="neon-racing"></a> |
| [night-city](../../../examples/night-city/README.md) | 已抓帧 | <a href="review-2026-11-07/night-city.png"><img src="review-2026-11-07/night-city.png" width="180" alt="night-city"></a> |
| [ocean-voyage](../../../examples/ocean-voyage/README.md) | 抓帧失败 | 无新图 |
| [outline](../../../examples/outline/README.md) | 已抓帧 | <a href="review-2026-11-07/outline.png"><img src="review-2026-11-07/outline.png" width="180" alt="outline"></a> |
| [particle-playback-lab](../../../examples/particle-playback-lab/README.md) | 已抓帧 | <a href="review-2026-11-07/particle-playback-lab.png"><img src="review-2026-11-07/particle-playback-lab.png" width="180" alt="particle-playback-lab"></a> |
| [pcg-biome](../../../examples/pcg-biome/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/pcg-biome.png"><img src="review-2026-11-07/pcg-biome.png" width="180" alt="pcg-biome"></a> |
| [pcg-depth-of-field](../../../examples/pcg-depth-of-field/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-depth-of-field.png"><img src="review-2026-11-07/pcg-depth-of-field.png" width="180" alt="pcg-depth-of-field"></a> |
| [pcg-detail-overwrite](../../../examples/pcg-detail-overwrite/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-detail-overwrite.png"><img src="review-2026-11-07/pcg-detail-overwrite.png" width="180" alt="pcg-detail-overwrite"></a> |
| [pcg-layer-culling](../../../examples/pcg-layer-culling/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-layer-culling.png"><img src="review-2026-11-07/pcg-layer-culling.png" width="180" alt="pcg-layer-culling"></a> |
| [pcg-location-system](../../../examples/pcg-location-system/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-location-system.png"><img src="review-2026-11-07/pcg-location-system.png" width="180" alt="pcg-location-system"></a> |
| [pcg-runtime-orchestration](../../../examples/pcg-runtime-orchestration/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-runtime-orchestration.png"><img src="review-2026-11-07/pcg-runtime-orchestration.png" width="180" alt="pcg-runtime-orchestration"></a> |
| [pcg-scene-player-culling](../../../examples/pcg-scene-player-culling/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-scene-player-culling.png"><img src="review-2026-11-07/pcg-scene-player-culling.png" width="180" alt="pcg-scene-player-culling"></a> |
| [pcg-snow-wind](../../../examples/pcg-snow-wind/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-snow-wind.png"><img src="review-2026-11-07/pcg-snow-wind.png" width="180" alt="pcg-snow-wind"></a> |
| [pcg-solar-planet](../../../examples/pcg-solar-planet/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-solar-planet.png"><img src="review-2026-11-07/pcg-solar-planet.png" width="180" alt="pcg-solar-planet"></a> |
| [pcg-thunder-strike](../../../examples/pcg-thunder-strike/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-thunder-strike.png"><img src="review-2026-11-07/pcg-thunder-strike.png" width="180" alt="pcg-thunder-strike"></a> |
| [pcg-ui-scaler](../../../examples/pcg-ui-scaler/README.md) | 已抓帧 | <a href="review-2026-11-07/pcg-ui-scaler.png"><img src="review-2026-11-07/pcg-ui-scaler.png" width="180" alt="pcg-ui-scaler"></a> |
| [pcg-underwater](../../../examples/pcg-underwater/README.md) | 抓帧失败 | 无新图 |
| [pcm-2a03](../../../examples/pcm-2a03/README.md) | 已抓帧 | <a href="review-2026-11-07/pcm-2a03.png"><img src="review-2026-11-07/pcm-2a03.png" width="180" alt="pcm-2a03"></a> |
| [physics-bake-replay](../../../examples/physics-bake-replay/README.md) | 抓帧失败 | 无新图 |
| [physics-force-lab](../../../examples/physics-force-lab/README.md) | 已抓帧 | <a href="review-2026-11-07/physics-force-lab.png"><img src="review-2026-11-07/physics-force-lab.png" width="180" alt="physics-force-lab"></a> |
| [physics-workshop](../../../examples/physics-workshop/README.md) | 抓帧失败 | 无新图 |
| [pipe-generator](../../../examples/pipe-generator/README.md) | 已抓帧 | <a href="review-2026-11-07/pipe-generator.png"><img src="review-2026-11-07/pipe-generator.png" width="180" alt="pipe-generator"></a> |
| [pixel-perfect-camera](../../../examples/pixel-perfect-camera/README.md) | 抓帧失败 | 无新图 |
| [pixelworld](../../../examples/pixelworld/README.md) | 已抓帧 | <a href="review-2026-11-07/pixelworld.png"><img src="review-2026-11-07/pixelworld.png" width="180" alt="pixelworld"></a> |
| [post-patrol-reinforce](../../../examples/post-patrol-reinforce/README.md) | 抓帧失败 | 无新图 |
| [primitive-drawing](../../../examples/primitive-drawing/README.md) | 已抓帧 | <a href="review-2026-11-07/primitive-drawing.png"><img src="review-2026-11-07/primitive-drawing.png" width="180" alt="primitive-drawing"></a> |
| [procedural-render-gallery](../../../examples/procedural-render-gallery/README.md) | 已抓帧 | <a href="review-2026-11-07/procedural-render-gallery.png"><img src="review-2026-11-07/procedural-render-gallery.png" width="180" alt="procedural-render-gallery"></a> |
| [procedural-road](../../../examples/procedural-road/README.md) | 已抓帧 | <a href="review-2026-11-07/procedural-road.png"><img src="review-2026-11-07/procedural-road.png" width="180" alt="procedural-road"></a> |
| [procedural-textures](../../../examples/procedural-textures/README.md) | 已抓帧 | <a href="review-2026-11-07/procedural-textures.png"><img src="review-2026-11-07/procedural-textures.png" width="180" alt="procedural-textures"></a> |
| [procgen](../../../examples/procgen/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/procgen.png"><img src="review-2026-11-07/procgen.png" width="180" alt="procgen"></a> |
| [procgen-nature-quality](../../../examples/procgen-nature-quality/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/procgen-nature-quality.png"><img src="review-2026-11-07/procgen-nature-quality.png" width="180" alt="procgen-nature-quality"></a> |
| [procgen-script-editor](../../../examples/procgen-script-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/procgen-script-editor.png"><img src="review-2026-11-07/procgen-script-editor.png" width="180" alt="procgen-script-editor"></a> |
| [procgen-script-pipeline](../../../examples/procgen-script-pipeline/README.md) | 已抓帧 | <a href="review-2026-11-07/procgen-script-pipeline.png"><img src="review-2026-11-07/procgen-script-pipeline.png" width="180" alt="procgen-script-pipeline"></a> |
| [projectile-physics-composition](../../../examples/projectile-physics-composition/README.md) | 已抓帧 | <a href="review-2026-11-07/projectile-physics-composition.png"><img src="review-2026-11-07/projectile-physics-composition.png" width="180" alt="projectile-physics-composition"></a> |
| [railgun-3d](../../../examples/railgun-3d/README.md) | 已抓帧；P3 | <a href="review-2026-11-07/railgun-3d.png"><img src="review-2026-11-07/railgun-3d.png" width="180" alt="railgun-3d"></a> |
| [realtime-fog](../../../examples/realtime-fog/README.md) | 抓帧失败 | 无新图 |
| [render-fidelity](../../../examples/render-fidelity/README.md) | 抓帧失败 | 无新图 |
| [rendering-chain-lab](../../../examples/rendering-chain-lab/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/rendering-chain-lab.png"><img src="review-2026-11-07/rendering-chain-lab.png" width="180" alt="rendering-chain-lab"></a> |
| [rock-generator](../../../examples/rock-generator/README.md) | 已抓帧 | <a href="review-2026-11-07/rock-generator.png"><img src="review-2026-11-07/rock-generator.png" width="180" alt="rock-generator"></a> |
| [roguelike-generator](../../../examples/roguelike-generator/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/roguelike-generator.png"><img src="review-2026-11-07/roguelike-generator.png" width="180" alt="roguelike-generator"></a> |
| [rpg](../../../examples/rpg/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/rpg.png"><img src="review-2026-11-07/rpg.png" width="180" alt="rpg"></a> |
| [rpg-balance](../../../examples/rpg-balance/README.md) | 抓帧失败 | 无新图 |
| [rpg-classic](../../../examples/rpg-classic/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/rpg-classic.png"><img src="review-2026-11-07/rpg-classic.png" width="180" alt="rpg-classic"></a> |
| [rts-cardfront](../../../examples/rts-cardfront/README.md) | 已抓帧 | <a href="review-2026-11-07/rts-cardfront.png"><img src="review-2026-11-07/rts-cardfront.png" width="180" alt="rts-cardfront"></a> |
| [rts-sandbox](../../../examples/rts-sandbox/README.md) | 已抓帧 | <a href="review-2026-11-07/rts-sandbox.png"><img src="review-2026-11-07/rts-sandbox.png" width="180" alt="rts-sandbox"></a> |
| [scad-living-room](../../../examples/scad-living-room/README.md) | 已抓帧 | <a href="review-2026-11-07/scad-living-room.png"><img src="review-2026-11-07/scad-living-room.png" width="180" alt="scad-living-room"></a> |
| [scad-mechanical](../../../examples/scad-mechanical/README.md) | 已抓帧 | <a href="review-2026-11-07/scad-mechanical.png"><img src="review-2026-11-07/scad-mechanical.png" width="180" alt="scad-mechanical"></a> |
| [scad-music-box](../../../examples/scad-music-box/README.md) | 已抓帧 | <a href="review-2026-11-07/scad-music-box.png"><img src="review-2026-11-07/scad-music-box.png" width="180" alt="scad-music-box"></a> |
| [scad-watermill](../../../examples/scad-watermill/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/scad-watermill.png"><img src="review-2026-11-07/scad-watermill.png" width="180" alt="scad-watermill"></a> |
| [scene-builder-game](../../../examples/scene-builder-game/README.md) | 已抓帧 | <a href="review-2026-11-07/scene-builder-game.png"><img src="review-2026-11-07/scene-builder-game.png" width="180" alt="scene-builder-game"></a> |
| [scene-ecs](../../../examples/scene-ecs/README.md) | 已抓帧 | <a href="review-2026-11-07/scene-ecs.png"><img src="review-2026-11-07/scene-ecs.png" width="180" alt="scene-ecs"></a> |
| [scene-editor](../../../examples/scene-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/scene-editor.png"><img src="review-2026-11-07/scene-editor.png" width="180" alt="scene-editor"></a> |
| [script-rule-provider](../../../examples/script-rule-provider/README.md) | 已抓帧 | <a href="review-2026-11-07/script-rule-provider.png"><img src="review-2026-11-07/script-rule-provider.png" width="180" alt="script-rule-provider"></a> |
| [shader-live-preview](../../../examples/shader-live-preview/README.md) | 已抓帧 | <a href="review-2026-11-07/shader-live-preview.png"><img src="review-2026-11-07/shader-live-preview.png" width="180" alt="shader-live-preview"></a> |
| [silpom-ssdm-compare](../../../examples/silpom-ssdm-compare/README.md) | 已抓帧 | <a href="review-2026-11-07/silpom-ssdm-compare.png"><img src="review-2026-11-07/silpom-ssdm-compare.png" width="180" alt="silpom-ssdm-compare"></a> |
| [ski-adventure](../../../examples/ski-adventure/README.md) | 已抓帧 | <a href="review-2026-11-07/ski-adventure.png"><img src="review-2026-11-07/ski-adventure.png" width="180" alt="ski-adventure"></a> |
| [skyforge-trials](../../../examples/skyforge-trials/README.md) | 已抓帧 | <a href="review-2026-11-07/skyforge-trials.png"><img src="review-2026-11-07/skyforge-trials.png" width="180" alt="skyforge-trials"></a> |
| [skyscraper-generator](../../../examples/skyscraper-generator/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/skyscraper-generator.png"><img src="review-2026-11-07/skyscraper-generator.png" width="180" alt="skyscraper-generator"></a> |
| [smoke-flipbook](../../../examples/smoke-flipbook/README.md) | 抓帧失败 | 无新图 |
| [snow](../../../examples/snow/README.md) | 已抓帧；P3 | <a href="review-2026-11-07/snow.png"><img src="review-2026-11-07/snow.png" width="180" alt="snow"></a> |
| [softbody](../../../examples/softbody/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/softbody.png"><img src="review-2026-11-07/softbody.png" width="180" alt="softbody"></a> |
| [softbody3d](../../../examples/softbody3d/README.md) | 已抓帧 | <a href="review-2026-11-07/softbody3d.png"><img src="review-2026-11-07/softbody3d.png" width="180" alt="softbody3d"></a> |
| [spline-tube-lab](../../../examples/spline-tube-lab/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/spline-tube-lab.png"><img src="review-2026-11-07/spline-tube-lab.png" width="180" alt="spline-tube-lab"></a> |
| [sprite-animation-vfx](../../../examples/sprite-animation-vfx/README.md) | 已抓帧 | <a href="review-2026-11-07/sprite-animation-vfx.png"><img src="review-2026-11-07/sprite-animation-vfx.png" width="180" alt="sprite-animation-vfx"></a> |
| [sprite-stack](../../../examples/sprite-stack/README.md) | 已抓帧 | <a href="review-2026-11-07/sprite-stack.png"><img src="review-2026-11-07/sprite-stack.png" width="180" alt="sprite-stack"></a> |
| [squad-boss-3d](../../../examples/squad-boss-3d/README.md) | 已抓帧 | <a href="review-2026-11-07/squad-boss-3d.png"><img src="review-2026-11-07/squad-boss-3d.png" width="180" alt="squad-boss-3d"></a> |
| [stylized-meadow](../../../examples/stylized-meadow/README.md) | 已抓帧 | <a href="review-2026-11-07/stylized-meadow.png"><img src="review-2026-11-07/stylized-meadow.png" width="180" alt="stylized-meadow"></a> |
| [stylized-water-shore](../../../examples/stylized-water-shore/README.md) | 已抓帧 | <a href="review-2026-11-07/stylized-water-shore.png"><img src="review-2026-11-07/stylized-water-shore.png" width="180" alt="stylized-water-shore"></a> |
| [surface-vine-growth](../../../examples/surface-vine-growth/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/surface-vine-growth.png"><img src="review-2026-11-07/surface-vine-growth.png" width="180" alt="surface-vine-growth"></a> |
| [swappable-tilesets](../../../examples/swappable-tilesets/README.md) | 已抓帧 | <a href="review-2026-11-07/swappable-tilesets.png"><img src="review-2026-11-07/swappable-tilesets.png" width="180" alt="swappable-tilesets"></a> |
| [sword-slash-trail](../../../examples/sword-slash-trail/README.md) | 已抓帧；P3 | <a href="review-2026-11-07/sword-slash-trail.png"><img src="review-2026-11-07/sword-slash-trail.png" width="180" alt="sword-slash-trail"></a> |
| [tactics](../../../examples/tactics/README.md) | 已抓帧 | <a href="review-2026-11-07/tactics.png"><img src="review-2026-11-07/tactics.png" width="180" alt="tactics"></a> |
| [tensor](../../../examples/tensor/README.md) | 已抓帧 | <a href="review-2026-11-07/tensor.png"><img src="review-2026-11-07/tensor.png" width="180" alt="tensor"></a> |
| [terrain-editor](../../../examples/terrain-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/terrain-editor.png"><img src="review-2026-11-07/terrain-editor.png" width="180" alt="terrain-editor"></a> |
| [terrain-gallery](../../../examples/terrain-gallery/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/terrain-gallery.png"><img src="review-2026-11-07/terrain-gallery.png" width="180" alt="terrain-gallery"></a> |
| [terrain-navigation-lab](../../../examples/terrain-navigation-lab/README.md) | 已抓帧 | <a href="review-2026-11-07/terrain-navigation-lab.png"><img src="review-2026-11-07/terrain-navigation-lab.png" width="180" alt="terrain-navigation-lab"></a> |
| [terrain-preview](../../../examples/terrain-preview/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/terrain-preview.png"><img src="review-2026-11-07/terrain-preview.png" width="180" alt="terrain-preview"></a> |
| [terrain-smooth-mountain](../../../examples/terrain-smooth-mountain/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/terrain-smooth-mountain.png"><img src="review-2026-11-07/terrain-smooth-mountain.png" width="180" alt="terrain-smooth-mountain"></a> |
| [terrain-stamping](../../../examples/terrain-stamping/README.md) | 已抓帧 | <a href="review-2026-11-07/terrain-stamping.png"><img src="review-2026-11-07/terrain-stamping.png" width="180" alt="terrain-stamping"></a> |
| [tileworld-graph-dungeon](../../../examples/tileworld-graph-dungeon/README.md) | 已抓帧 | <a href="review-2026-11-07/tileworld-graph-dungeon.png"><img src="review-2026-11-07/tileworld-graph-dungeon.png" width="180" alt="tileworld-graph-dungeon"></a> |
| [topdown-procmap](../../../examples/topdown-procmap/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/topdown-procmap.png"><img src="review-2026-11-07/topdown-procmap.png" width="180" alt="topdown-procmap"></a> |
| [tornado-lab](../../../examples/tornado-lab/README.md) | 已抓帧 | <a href="review-2026-11-07/tornado-lab.png"><img src="review-2026-11-07/tornado-lab.png" width="180" alt="tornado-lab"></a> |
| [tower-defense](../../../examples/tower-defense/README.md) | 抓帧失败 | 无新图 |
| [trap-workshop](../../../examples/trap-workshop/README.md) | 抓帧失败 | 无新图 |
| [tree-generator](../../../examples/tree-generator/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/tree-generator.png"><img src="review-2026-11-07/tree-generator.png" width="180" alt="tree-generator"></a> |
| [triplanar-decal](../../../examples/triplanar-decal/README.md) | 已抓帧 | <a href="review-2026-11-07/triplanar-decal.png"><img src="review-2026-11-07/triplanar-decal.png" width="180" alt="triplanar-decal"></a> |
| [uds-sky](../../../examples/uds-sky/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/uds-sky.png"><img src="review-2026-11-07/uds-sky.png" width="180" alt="uds-sky"></a> |
| [ui-theme-editor](../../../examples/ui-theme-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/ui-theme-editor.png"><img src="review-2026-11-07/ui-theme-editor.png" width="180" alt="ui-theme-editor"></a> |
| [ultra-elements](../../../examples/ultra-elements/README.md) | 已抓帧 | <a href="review-2026-11-07/ultra-elements.png"><img src="review-2026-11-07/ultra-elements.png" width="180" alt="ultra-elements"></a> |
| [unit-surface-effects](../../../examples/unit-surface-effects/README.md) | 已抓帧 | <a href="review-2026-11-07/unit-surface-effects.png"><img src="review-2026-11-07/unit-surface-effects.png" width="180" alt="unit-surface-effects"></a> |
| [unity-vfx-port-lab](../../../examples/unity-vfx-port-lab/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/unity-vfx-port-lab.png"><img src="review-2026-11-07/unity-vfx-port-lab.png" width="180" alt="unity-vfx-port-lab"></a> |
| [urban-generator](../../../examples/urban-generator/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/urban-generator.png"><img src="review-2026-11-07/urban-generator.png" width="180" alt="urban-generator"></a> |
| [uv-texture-paint](../../../examples/uv-texture-paint/README.md) | 已抓帧 | <a href="review-2026-11-07/uv-texture-paint.png"><img src="review-2026-11-07/uv-texture-paint.png" width="180" alt="uv-texture-paint"></a> |
| [vehicle](../../../examples/vehicle/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/vehicle.png"><img src="review-2026-11-07/vehicle.png" width="180" alt="vehicle"></a> |
| [venom-transform](../../../examples/venom-transform/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/venom-transform.png"><img src="review-2026-11-07/venom-transform.png" width="180" alt="venom-transform"></a> |
| [virtual-texture-blending](../../../examples/virtual-texture-blending/README.md) | 已抓帧 | <a href="review-2026-11-07/virtual-texture-blending.png"><img src="review-2026-11-07/virtual-texture-blending.png" width="180" alt="virtual-texture-blending"></a> |
| [virtualgeometry](../../../examples/virtualgeometry/README.md) | 已抓帧 | <a href="review-2026-11-07/virtualgeometry.png"><img src="review-2026-11-07/virtualgeometry.png" width="180" alt="virtualgeometry"></a> |
| [vortex-cloud](../../../examples/vortex-cloud/README.md) | 已抓帧 | <a href="review-2026-11-07/vortex-cloud.png"><img src="review-2026-11-07/vortex-cloud.png" width="180" alt="vortex-cloud"></a> |
| [voxel](../../../examples/voxel/README.md) | 已抓帧 | <a href="review-2026-11-07/voxel.png"><img src="review-2026-11-07/voxel.png" width="180" alt="voxel"></a> |
| [voxel-catalog-editor](../../../examples/voxel-catalog-editor/README.md) | 已抓帧 | <a href="review-2026-11-07/voxel-catalog-editor.png"><img src="review-2026-11-07/voxel-catalog-editor.png" width="180" alt="voxel-catalog-editor"></a> |
| [voxel-terrain](../../../examples/voxel-terrain/README.md) | 已抓帧 | <a href="review-2026-11-07/voxel-terrain.png"><img src="review-2026-11-07/voxel-terrain.png" width="180" alt="voxel-terrain"></a> |
| [vrm-avatar](../../../examples/vrm-avatar/README.md) | 已抓帧；P1 | <a href="review-2026-11-07/vrm-avatar.png"><img src="review-2026-11-07/vrm-avatar.png" width="180" alt="vrm-avatar"></a> |
| [warship](../../../examples/warship/README.md) | 抓帧失败 | 无新图 |
| [water-splash-vfx](../../../examples/water-splash-vfx/README.md) | 已抓帧；P3 | <a href="review-2026-11-07/water-splash-vfx.png"><img src="review-2026-11-07/water-splash-vfx.png" width="180" alt="water-splash-vfx"></a> |
| [waterfall-demo](../../../examples/waterfall-demo/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/waterfall-demo.png"><img src="review-2026-11-07/waterfall-demo.png" width="180" alt="waterfall-demo"></a> |
| [weapon-lightning](../../../examples/weapon-lightning/README.md) | 已抓帧 | <a href="review-2026-11-07/weapon-lightning.png"><img src="review-2026-11-07/weapon-lightning.png" width="180" alt="weapon-lightning"></a> |
| [weather](../../../examples/weather/README.md) | 已抓帧；P2 | <a href="review-2026-11-07/weather.png"><img src="review-2026-11-07/weather.png" width="180" alt="weather"></a> |
| [weather-daynight](../../../examples/weather-daynight/README.md) | 已抓帧 | <a href="review-2026-11-07/weather-daynight.png"><img src="review-2026-11-07/weather-daynight.png" width="180" alt="weather-daynight"></a> |
| [world-sweep-ballistics](../../../examples/world-sweep-ballistics/README.md) | 已抓帧 | <a href="review-2026-11-07/world-sweep-ballistics.png"><img src="review-2026-11-07/world-sweep-ballistics.png" width="180" alt="world-sweep-ballistics"></a> |

## 非独立入口目录

以下目录没有 `main.nut`，未当成独立游戏启动：

- `agent`
- `ai-editor`
- `economy`
- `live2d-backend-plugin`
- `native-plugin`
- `particle-effects`
- `shader_effect_package`
- `surface-fluid-dynamic`
- `workspace-recipes`

## 复现与边界

```powershell
./scripts/capture_example.ps1 -Eve <完整构建的eve.exe> -Example hex-terrain -Out build/hex-terrain.png -Frames 120
# 天气预设
./scripts/capture_example.ps1 -Eve <完整构建的eve.exe> -Example weather -Out build/weather.png -Frames 150 -Setup scripts/gallery_presets/weather-storm.nut
```

抓帧钩子组合进标准 `src/scripts/load.nut`，保持正式初始化、更新、渲染与退出路径。只有新文件、CAPTURED 标记及无初始化/帧错误时才收录；失败不覆盖原图。本轮只更新截图与文档，没有修改公共 API、ECS、持久化格式、模块边界或生命周期，没有架构豁免。

## 验证记录

- `make check` 通过（`origin/dev` 基线，源码质量门禁）。
- 228 个入口覆盖无遗漏、无重复；197 张主抓帧及天气雪天补图的 PNG 校验、尺寸与 SHA-256 核对通过。
- 中英文画廊和本页的本地文件链接检查通过；`git diff --check` 通过。
- 示例运行自动写出的截图与 ImGui 配置已恢复，本次变更仅限 `docs/images/examples/`。未重新编译当前 HEAD，未将截图检查当作完整玩法或跨平台原生测试。
