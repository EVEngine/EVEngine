# Example 画面质量复查（2026-11-11 水车磨坊模型重做）

**11-12 九项改善：** [前后与状态实帧](review-2026-11-12/nine-improved/README.md) · [指纹清单](review-2026-11-12/nine-improved/manifest.json)。除 `tree-generator` 外九个示例已修改；下方此前的候选清单保留为基线。

**11-12 新一轮复查：** [另十个待改善场景与当前源码实帧](review-2026-11-12/next10-candidates/README.md)，10/10 重新抓帧成功。下方 11-11 修复记录保留。

[最新 10 项抓帧清单](review-2026-11-11/next10/manifest.json) · [山体抓帧清单](review-2026-11-11/mountain-detail/manifest.json) · [最新城市抓帧清单](review-2026-11-11/urban-detail/manifest.json) · [截图画廊](README.md) · [English gallery](README.en.md) · [11-11 抓帧与指纹清单](review-2026-11-11/manifest.json) · [11-09 清单](review-2026-11-09/manifest.json) · [11-07 完整历史报告](QUALITY_REVIEW-2026-11-07.md)

11-09 已先从本工作区当前源码完整编译引擎，再复跑旧报告的 **31 个 P1，31/31 均取得有效引擎帧**。构建为 `v0.7.0-dev / d9df15920770+dirty`，Windows MSVC Debug、SHARED、严格警告、203/203 模块；依赖复用与源码锁定版本一致的预编译库，不复用旧引擎程序或引擎 DLL。修复前后均使用当前源码构建，每张图由 `gfx.saveFramePng()` 原样输出。

11-09 曾将 21 项标为已修复，但用户复核明确否定其中四项。**撤回 hex-terrain、skyscraper-generator、terrain-smooth-mountain、urban-generator 的上一轮验收结论。** 11-09 的运行和抓帧记录保留；取得有效帧不代表示例视觉质量合格。

其他示例没有在本轮全部重跑。11-07 的全量检查仍为 228 个独立入口、197 个有效抓帧、31 个抓帧失败；这些旧失败不自动视为视觉缺陷。历史截图与清单已归档，未被新图覆盖。

## 水车磨坊墙面复查：补齐连续墙体

用户复查指出墙面仍有问题，确认上一版墙板分段高度和窗洞边界不一致，导致大块意外留空、玻璃像悬浮色块；侧墙末端还超出转角。已从 `design.py` 补成三面连续墙体，窗洞具有完整窗框、过梁、窗台；补齐山墙和檐下，保留参观廊面的刻意开敞。七个关节与传动代码未改动。

![墙面修正后的真实外景](review-2026-11-11/watermill-walls/exterior.png)

[上一版墙面](review-2026-11-11/watermill-walls/before.png) · [水轮近景](review-2026-11-11/watermill-walls/wheel.png) · [传动层](review-2026-11-11/watermill-walls/interior.png) · [磨盘层](review-2026-11-11/watermill-walls/stone-floor.png) · [默认运行](review-2026-11-11/watermill-walls/default.png) · [最新截图与指纹清单](review-2026-11-11/watermill-walls/manifest.json)。当前源码构建的同一引擎运行，五张图逐张复查，固定相机时机构保持运行。5 项几何测试通过（新增三面墙覆盖采样），ruff、原生烟测与 diff 检查通过；本次未重复上一轮完整源码门禁、200 帧传动探针或可选 OpenSCAD 编译。以下模型重做及门禁记录为上一轮历史。

## 11-11 水车磨坊：从 design.py 重做模型

上一轮仅改取景和着色仍不足，本轮重建水轮、建筑与水路模型。修复 SCAD/OBJ 屋顶几何不一致、水轮与坑轮偏轴、磨盘小齿轮与大齿轮高度错开的实际问题。七个关节的层级、枢轴、轴向及原传动比例保留；新增材质分组随对应关节共同运动。

![重做后的外景](review-2026-11-11/watermill-model/exterior.png)

[重做前](review-2026-11-11/watermill-model/before.png) · [水轮近景](review-2026-11-11/watermill-model/wheel.png) · [内部传动层](review-2026-11-11/watermill-model/interior.png) · [磨盘层](review-2026-11-11/watermill-model/stone-floor.png) · [默认运行视角](review-2026-11-11/watermill-model/default.png) · [截图、预设、源码及运行指纹](review-2026-11-11/watermill-model/manifest.json)。旧 next10 记录保留，下面十项表格是上一轮历史，不作为水车当前截图。

当前工作区源码重新编译 `eve` 成功，五张 PNG 均由引擎 `gfx.saveFramePng()` 原样输出并逐张检查。固定相机时水轮和齿轮仍运行。几何/导出测试 4 项通过，原生 smoke 通过；200 帧原生探针验证水流旋转、关闸停转、手动盘车、恢复自动驱动、七个 FK 变换和全部比例。未新增公共引擎 API、模块边界或持久化格式，既有 rig/1 契约不变，无架构例外。

验证边界：未安装 OpenSCAD，未运行可选 CSG 编译；水面保持示意模型，不宣称齿轮接触物理或完整键鼠游览通过。标准 `make check` 仍被本轮未改动的 `Database.h:153/155/157/159/161` 五项既有 API 生命周期文档缺失阻挡；`make check CI_BASE=HEAD` 增量门禁通过，不将默认门禁写成全绿。

## 11-11 下一批 10 项：按顺序改善与复查

按 flame-jet → daynight → 地牢 → 灯光 → 自然材质 → 瀑布 → 银河城 → 天气 → 流体 → 水车的顺序修改。以下“前”为列出清单时用当前源码引擎重新运行的默认图；“后”为本轮修改后的真实引擎帧。不是把旧版本失败直接认定为视觉故障。原始 PNG、程序/DLL 指纹、资源哈希和预设见 [清单](review-2026-11-11/next10/manifest.json)。

| 顺序 | 示例 | 已完成的改进 | 当前源码前 / 后 |
|---|---|---|---|
| 1 | [flame-jet](../../../examples/flame-jet/README.md) | 修正未识别的颜色字段，补柔边纹理并降低火芯叠加；原生资源回归防止再次退化为白色方块。 | [前](review-2026-11-11/next10/baseline/flame-jet.png) · [后](review-2026-11-11/next10/flame-jet.png) |
| 2 | [daynight](../../../examples/daynight/README.md) | 修正相机/云层参数、曝光和面板锚点，补篝火参照；默认黄金时段暂停，Space 能恢复真实昼夜周期。 | [前](review-2026-11-11/next10/baseline/daynight.png) · [后](review-2026-11-11/next10/daynight.png) |
| 3 | [dungeon-generator-3d](../../../examples/dungeon-generator-3d/README.md) | 无外部包时生成正确尺寸的墙体、开放门框、差异化陈设及主题房间配色；加入总览/房间取景。 | [前](review-2026-11-11/next10/baseline/dungeon-generator-3d.png) · [后](review-2026-11-11/next10/dungeon-generator-3d.png) |
| 4 | [lighting-showcase](../../../examples/lighting-showcase/README.md) | 按 EV 校准曝光及灯光/体积介质强度，恢复暖冷光、阴影与粗糙度对比；功能开关保留。 | [前](review-2026-11-11/next10/baseline/lighting-showcase.png) · [后](review-2026-11-11/next10/lighting-showcase.png) |
| 5 | [procgen-nature-quality](../../../examples/procgen-nature-quality/README.md) | 完整树冠入镜，降低过强暖光、土壤黄染和叶片泛白，保留程序化纹理/法线与花卉。 | [前](review-2026-11-11/next10/baseline/procgen-nature-quality.png) · [后](review-2026-11-11/next10/procgen-nature-quality.png) |
| 6 | [waterfall-demo](../../../examples/waterfall-demo/README.md) | 保留动态水幕，加入岩壁、岸边植被、水潭与落水泡沫，并记录近景。 | [前](review-2026-11-11/next10/baseline/waterfall-demo.png) · [后](review-2026-11-11/next10/waterfall-demo.png) |
| 7 | [metroidvania](../../../examples/metroidvania/README.md) | 修正地图相对贴图路径；新增石砌/背景素材与兼容角色轮廓；碰撞图数据未改，输入回放通过。 | [前](review-2026-11-11/next10/baseline/metroidvania.png) · [后](review-2026-11-11/next10/metroidvania.png) |
| 8 | [weather](../../../examples/weather/README.md) | 默认雨天，加入预设控制的湿地材质、水洼/雪地配色与石径，固定同机位对照。 | [前](review-2026-11-11/next10/baseline/weather.png) · [后](review-2026-11-11/next10/weather.png) |
| 9 | [fluid3d](../../../examples/fluid3d/README.md) | 默认自动模拟、拉近相机并接回完整表面重建，调整平滑/透明度；记录初始水花与自然稳定阶段。 | [前](review-2026-11-11/next10/baseline/fluid3d.png) · [后](review-2026-11-11/next10/fluid3d.png) |
| 10 | [scad-watermill](../../../examples/scad-watermill/README.md) | 默认外部环绕取景，修正重复着色及照明；保留室内游览和机械联动，补水轮侧面视图。 | [前](review-2026-11-11/next10/baseline/scad-watermill.png) · [后](review-2026-11-11/next10/scad-watermill.png) |

附加实帧：[火焰初期](review-2026-11-11/next10/flame-start.png) / [持续喷射](review-2026-11-11/next10/flame-late.png) · [正午](review-2026-11-11/next10/day-noon.png) / [夜间](review-2026-11-11/next10/day-night.png) · [关闭体积光](review-2026-11-11/next10/lighting-no-volume.png) · [晴天](review-2026-11-11/next10/weather-clear.png) / [雪天](review-2026-11-11/next10/weather-snow.png) / [暴风](review-2026-11-11/next10/weather-storm.png) · [地牢房间](review-2026-11-11/next10/dungeon-room.png) · [瀑布近景](review-2026-11-11/next10/waterfall-close.png) · [水轮侧面](review-2026-11-11/next10/mill-wheel.png) · [流体初始水花](review-2026-11-11/next10/fluid-initial.png)。流体主图为未布置额外场景、自然运行 360 帧后的状态，其他主图为 120 帧；不同帧数、窗口或取景的图片不用于逐像素相似度评分。

验证：当前源码 eve 与粒子测试目标构建通过；真实火焰效果文件的渐变/透明度/纹理回归通过；10/10 示例标准两秒 smoke 通过，最终地牢版本另复测通过；银河城既有 8 秒移动、跳跃、攻击输入回放无帧/运行错误，地图生成器的路线及贴图路径断言通过；新素材生成脚本 lint 通过。本次改动 `make check CI_BASE=HEAD` 通过。默认 `make check` 仍仅有未改动的 Database.h 五项 API 生命周期注释失败。

边界：银河城改善了兼容角色显示，未冒充 Spine IK 已实现；地牢本机验证的是内置程序化造景，缺失的外部资产包未被宣称验证通过。瀑布水潭/泡沫和天气水洼/雪地颜色属于场景表达，不是新增水物理或积雪模拟。流体保留模拟/表面重建/上传计时及丢弃时间；完整重建有显示开销，未宣称提速或所有粒子边界已消失。图形只在 Windows Vulkan 验证，未运行浏览器、全部原生测试或 hosted CI。静态状态对照和无帧错误回放不等于完成全部功能验收。

沿用 Result 检查、资源生命周期和源文件拥有权；自有素材保留可重复生成脚本。没有改变引擎公共 API、模块边界、ECS 或持久化格式，没有采用架构例外。此前山体、城市及其他修复记录继续保留。

## 11-11 terrain-smooth-mountain：轮廓、岩层与植被续改

当前源码重新构建并运行，保留左侧 Raise/max、右侧 SmoothRaise/宽度 4；近排高度图、远排实际网格烘焙的四面板结构。左右使用相同基础地形、印章设置、局部材质坐标、树木候选和灯光，只改变融合运算。

[![山体最新实帧](review-2026-11-11/mountain-detail/terrain-smooth-mountain.png)](review-2026-11-11/mountain-detail/terrain-smooth-mountain.png)

| 项目 | 本次改进 |
|---|---|
| 山体轮廓 | 提高为 129×129 高度采样与 0.25 间距；弯曲双山脊、侧峰和低幅细节取代圆顶；网格烘焙行叠放两枚同源岩体印章 |
| 岩层材质 | 示例独立 GLSL，按坡度/海拔混合岩石、碎石、草地与少量高处积雪；倾斜层理、暗缝和细粒噪声，降低条纹对比；不修改共享地形材质 |
| 植被分布 | 使用现有 mesh.tree，三份共享网格、固定噪声林斑；融合后采样树根，仅在高度 1–7.8、坡度平方 ≤0.48 的候选位置种植，陡崖与山脊裸露 |
| 对比验证 | 总览、近景、俯视、无树总览、无树近景共 5 张新实帧；可按 V 隐藏树木检查融合边界 |

[前](review-2026-11-11/mountain-detail/before.png) · [当前总览](review-2026-11-11/mountain-detail/terrain-smooth-mountain.png) · [山脊近景](review-2026-11-11/mountain-detail/ridge-close.png) · [俯视](review-2026-11-11/mountain-detail/overhead.png) · [无树总览](review-2026-11-11/mountain-detail/bare-overview.png) · [无树近景](review-2026-11-11/mountain-detail/bare-ridge.png) · [运行/着色器/PNG 指纹与检查记录](review-2026-11-11/mountain-detail/manifest.json)

启动检查：高度图行 9915 个高度样本存在左右差异，最大差约 0.999974；网格烘焙行 3541 个不同样本，最大差约 1.40897；两行四角高度保持相同。四面板分别为 29、30、30、28 棵树；左右筛选规则相同，最终坡度不同可以改变接受数量，同一候选的树型、尺寸和朝向不因列而改变。

验证：当前源码构建、两份 SPIR-V 校验、两项 SmoothRaise/快照恢复原生回归、标准 smoke 均通过；`make check CI_BASE=HEAD` 通过。默认 origin/dev 的五项既有 Database.h 生命周期注释失败不由本例引入。仅验证 Windows Vulkan 实帧，未声称浏览器或每个键盘交互均完成自动化验收。沿用 Result 检查、资源保留、固定局部噪声与公平对比规则；没有改变模块边界、引擎公共方法、ECS 或持久化格式，没有架构例外。

## 11-11 urban-generator：立面、街道与绿化续改

继续完成此前缺少的展示内容，而非止于 P2 分类。已从当前源码重新构建引擎，运行并检查默认场景、反面、俯视、三角地块、椭圆地块、不同种子及平面模式，共 7 张新实帧。[程序/DLL 指纹、预设与 PNG 哈希](review-2026-11-11/urban-detail/manifest.json)。

[![城市细节续改实帧](review-2026-11-11/urban-detail/urban-generator.png)](review-2026-11-11/urban-detail/urban-generator.png)

| 项目 | 已实现与验证 |
|---|---|
| 建筑立面 | 沿真实地块边生成窗格、局部暖色窗、入口和屋顶檐口/设备；反面取景检查立面仍正常 |
| 道路标线 | 沿规划街道生成虚线，在相交街道端点布置斑马线；路口中断中心虚线；调整人行道退界避免盖住车道 |
| 绿化 | 固定种子选取部分地块为公园，生成草地、分面树冠与树干，并在建筑退界带布树 |
| 配色与构图 | 奶油石色、砖红、灰蓝建筑配深色玻璃和沥青、绿色草地；默认停在完整街区的斜视角，俯视镜头为说明面板留出空间 |

[整改前](review-2026-11-11/urban-detail/before.png) · [当前图](review-2026-11-11/urban-detail/urban-generator.png) · [反面](review-2026-11-11/urban-detail/opposite.png) · [俯视](review-2026-11-11/urban-detail/plan.png) · [三角地块](review-2026-11-11/urban-detail/triangle.png) · [椭圆地块](review-2026-11-11/urban-detail/ellipse.png) · [新种子](review-2026-11-11/urban-detail/reseed.png) · [平面模式](review-2026-11-11/urban-detail/flat.png)

细节由可选 `presentationDetails` 参数启用，默认关闭，使用配方种子和实际规划结果合成到同一个网格；平面模式关闭三维细节。原生回归检查关闭时的兼容输出、相同种子的顶点/颜色一致性、坐标有限性、索引边界、五类细节存在以及平面输出。标准 smoke 和源码质量检查结果见本次清单。沿用 Result 检查、种子确定性、单一输出拥有权及默认兼容规则，没有改变模块边界、公共方法签名、ECS 或持久化格式，没有架构例外。

## 11-11 较早一轮四项整改记录（城市以上述续改为准）

本次继续使用当前源码编译的引擎和 DLL。四项默认场景均重新运行 120 帧，另检查六个角度/参数变体。原始 PNG、运行程序及 DLL 指纹、预设和哈希见 [本次清单](review-2026-11-11/manifest.json)。[11-09 报告原文](QUALITY_REVIEW-2026-11-09.md)保留为历史记录。

| 示例 | 本次确认并修正 | 当前评价与剩余改进 | 整改前 / 后 |
|---|---|---|---|
| hex-terrain | 去掉与深度/阴影预通道不一致的顶点位移；移除重复色调映射；海面统一标高；扩大取景 | 白色块面异常已消除，完整岛屿可辨。海岸阶梯和重复植被仍可改善 | [前](review-2026-11-11/baseline/hex-terrain.png) · [后](review-2026-11-11/hex-terrain.png) |
| skyscraper-generator | 修正 ±Z 立面朝内法线、窗片偏移；补每层退台屋顶；把墙/玻璃色板实际贴上网格；旋转改为慢速弧度；按层数适配镜头 | 四面立面与退台可见。仍是规则窗格的生成器示范，立面细节属于后续美术改进 | [前](review-2026-11-11/baseline/skyscraper-generator.png) · [后](review-2026-11-11/skyscraper-generator.png) |
| terrain-smooth-mountain | 修正远裁剪面；不透明地形输出 alpha=1，交由场景统一色调映射；山脊替代圆顶；左右使用相同材质，加说明 | 四面板完整、融合过渡可比较。此为较早一轮记录；山脊、岩层与植被现已继续改善，见上方山体续改 | [前](review-2026-11-11/baseline/terrain-smooth-mountain.png) · [后](review-2026-11-11/terrain-smooth-mountain.png) |
| urban-generator | 增加可选建筑退界、确定性高度变化和顶点配色；修正墙面法线；修复 G-buffer 丢弃顶点颜色；俯视切换与操作说明 | 从连成一块的平台恢复为独立建筑与道路的体块研究。此处为较早一轮结果；窗格、标线、绿化与构图现已继续完成，见上方新实帧。 | [前](review-2026-11-11/baseline/urban-generator.png) · [后](review-2026-11-11/urban-generator.png) |

附加视图：[六边形侧面](review-2026-11-11/hex-side.png) · [背面](review-2026-11-11/hex-back.png) · [高楼背面](review-2026-11-11/tower-back.png) · [12 层密窗](review-2026-11-11/tower-dense.png) · [城市俯视](review-2026-11-11/city-plan.png) · [三角形地块](review-2026-11-11/city-triangle.png)。这些通过公开脚本参数重建/取景，未声称已经逐键验证所有交互。

建筑退界采用向内半平面相交，凹地块得到凸核；没有可建凸核的窄地块会省略，并通过 `omittedFootprints` 元数据报告。新配方参数默认关闭，既有调用不自动改变建筑布局。城市统计栏的 parcels 是原始规划地块数，不等同于最终建筑数。

本次验证：源码构建通过；高楼立面/屋顶、平面海水、城市确定性高度/颜色和 GPU G-buffer 顶点颜色四项原生用例 4/4 通过；五个受影响示例标准 smoke 5/5 通过。`make check CI_BASE=HEAD` 通过；默认 `origin/dev` 仍被未改动的 `Database.h` 五处生命周期注释阻挡。没有新增模块边界、公共方法签名、ECS 或持久化格式；Result 检查和默认兼容要求适用，未采用架构例外。WGSL 已同步，浏览器后端未实跑。

## 11-09 P1 逐项复测记录（四项结论以上文为准）

“修复前”是本轮重新编译源码、尚未应用修复的 120 帧基线，不是旧版本截图。“修复后”是包含本轮修复的构建；墨攻使用注明的正常重播预设，其他项目保持默认初始化后 120 帧。实时 dt 不保证动画相位相同。已修复的 P1 仍可能有表中注明的 P2 美术或布局改进空间。

| 示例 | 结论 | 确认原因、修复与剩余边界 | 当前源码修复前 / 后 |
|---|---|---|---|
| [ai-stage](../../../examples/ai-stage/README.md) | 定位更正 | README 定义为供 AI/MCP 布置的空舞台；默认地面属预期，后续可补引导演示。 | [前](review-2026-11-09/baseline/ai-stage.png) · [后](review-2026-11-09/ai-stage.png) |
| [anime-character-lab](../../../examples/anime-character-lab/README.md) | 前置条件已明确 | 本机没有 assets/witch.obj；新增缺失素材提示与准备步骤。未伪造角色或宣称模型导入通过。 | [前](review-2026-11-09/baseline/anime-character-lab.png) · [后](review-2026-11-09/anime-character-lab.png) |
| [atmospheric-fog](../../../examples/atmospheric-fog/README.md) | 已修复 | CPU 积分和上传图集正确；浮点除法/取模计算切片坐标时在第 24 层读到图集外。改为整数寻址并同步 WGSL；黑带回归由失败变为通过，完整雾场景黑带消失。 | [前](review-2026-11-09/baseline/atmospheric-fog.png) · [后](review-2026-11-09/atmospheric-fog.png) |
| [building-3d](../../../examples/building-3d/README.md) | 定位更正 | 这是交互式建造入口；初始化只有网格和操作提示，尚未放置建筑，不是主体丢失。 | [前](review-2026-11-09/baseline/building-3d.png) · [后](review-2026-11-09/building-3d.png) |
| [commandery-rts](../../../examples/commandery-rts/README.md) | 已修复 | 地面背景原先在单位之后绘制，遮住地图主体；修正绘制顺序及血条底色顺序。 | [前](review-2026-11-09/baseline/commandery-rts.png) · [后](review-2026-11-09/commandery-rts.png) |
| [dungeon-generator-3d](../../../examples/dungeon-generator-3d/README.md) | 降为 P2 | 生成平台和连接可见；墙体、道具与镜头表达仍薄弱，属于展示完整度，未确认渲染对象丢失。 | [前](review-2026-11-09/baseline/dungeon-generator-3d.png) · [后](review-2026-11-09/dungeon-generator-3d.png) |
| [editor-host-world](../../../examples/editor-host-world/README.md) | 已修复 | 原 onUpdate/onRender 不是标准入口；改用 eve_init/eve_render，显示实际挂载的 2 个包和 8 个面板描述。未宣称已组合完整编辑面板 UI。 | [前](review-2026-11-09/baseline/editor-host-world.png) · [后](review-2026-11-09/editor-host-world.png) |
| [glass-crystal](../../../examples/glass-crystal/README.md) | 已修复 | 自定义网格的 Less 拒绝 G-buffer 已写入的同深度表面；默认改为 LessEqual，显式 Less 仍受尊重。 | [前](review-2026-11-09/baseline/glass-crystal.png) · [后](review-2026-11-09/glass-crystal.png) |
| [gpu-agents](../../../examples/gpu-agents/README.md) | 定位更正 | 当前脚本只有初始化、计算更新及日志，没有 eve_render 场景提交；空图不作为 GPU 仿真失败证据。 | [前](review-2026-11-09/baseline/gpu-agents.png) · [后](review-2026-11-09/gpu-agents.png) |
| [hex-planet](../../../examples/hex-planet/README.md) | 已修复 | 修正 Light3D 数组步长造成的 Frame 偏移不一致；地貌颜色恢复。球体与侧栏的遮挡继续按 P2 调整。 | [前](review-2026-11-09/baseline/hex-planet.png) · [后](review-2026-11-09/hex-planet.png) |
| [hex-terrain](../../../examples/hex-terrain/README.md) | 上轮验收撤回 | 统一顶点/片元 Frame 的灯光布局并重新编译着色器；白青块面异常改善，亮度和取景仍可按 P2 优化。 | [前](review-2026-11-09/baseline/hex-terrain.png) · [后](review-2026-11-09/hex-terrain.png) |
| [hex-terrain-3d](../../../examples/hex-terrain-3d/README.md) | 已修复 | 恢复完整 Light3D.spot 字段与后续 Frame 偏移；地表分区可辨。帮助面板遮挡仍属 P2。 | [前](review-2026-11-09/baseline/hex-terrain-3d.png) · [后](review-2026-11-09/hex-terrain-3d.png) |
| [ink-arena](../../../examples/ink-arena/README.md) | 已修复 | 使用修复后的自定义网格深度默认值，墙面和地板恢复，墨迹可与底材区分。 | [前](review-2026-11-09/baseline/ink-arena.png) · [后](review-2026-11-09/ink-arena.png) |
| [ink-combat](../../../examples/ink-combat/README.md) | 取帧更正 | 默认 120 帧落在消散后的空白间隔。新增预设冻结正常重播的 0.38 秒刀光；不是角色/关卡缺失。 | [前](review-2026-11-09/baseline/ink-combat.png) · [后](review-2026-11-09/ink-combat.png) |
| [inspector-demo](../../../examples/inspector-demo/README.md) | 已修复 | Database 单元格不再被压缩到窄列；使用最小列宽、水平滚动与原生控件行高，字段不再叠写。 | [前](review-2026-11-09/baseline/inspector-demo.png) · [后](review-2026-11-09/inspector-demo.png) |
| [interior-mapping](../../../examples/interior-mapping/README.md) | 已修复 | 补齐 Frame 灯光布局并重编译，窗格与室内着色恢复可见；仍可优化自动环绕取景。 | [前](review-2026-11-09/baseline/interior-mapping.png) · [后](review-2026-11-09/interior-mapping.png) |
| [metroidvania](../../../examples/metroidvania/README.md) | 降为 P2 | 素材是机制夹具；当前 Spine 能力不支持夹具中的 IK，日志明确进入彩色矩形兼容显示。绿色场景仍需美术提升，未冒充完整 Spine 动画验收。 | [前](review-2026-11-09/baseline/metroidvania.png) · [后](review-2026-11-09/metroidvania.png) |
| [model-converter](../../../examples/model-converter/README.md) | 前置条件已明确 | 原 setHostPos 把宽高当作 pivot，导致提示面板移出屏幕；修正位置、尺寸和右侧锚点。当前缺少转换插件/bpy，未验证真实模型转换。 | [前](review-2026-11-09/baseline/model-converter.png) · [后](review-2026-11-09/model-converter.png) |
| [pcg-biome](../../../examples/pcg-biome/README.md) | 已修复 | 图执行结果改为模块注册表拥有的完整点集快照；例子使用 executeResult，发布不再被 owned points 检查拒绝。 | [前](review-2026-11-09/baseline/pcg-biome.png) · [后](review-2026-11-09/pcg-biome.png) |
| [rendering-chain-lab](../../../examples/rendering-chain-lab/README.md) | 已修复 | 无效果基线及单功能开关定位到 SSR/RTGI 合成。移除只叠加间接光的 Vulkan 特殊分支，使用已有显式合成路径保留基础场景；完整效果下主体恢复。 | [前](review-2026-11-09/baseline/rendering-chain-lab.png) · [后](review-2026-11-09/rendering-chain-lab.png) |
| [rpg](../../../examples/rpg/README.md) | 已修复 | 为玩家、敌方与日志设置独立尺寸和位置，日志移到右侧，消除属性/日志重叠。方块角色属于机制示意。 | [前](review-2026-11-09/baseline/rpg.png) · [后](review-2026-11-09/rpg.png) |
| [skyscraper-generator](../../../examples/skyscraper-generator/README.md) | 上轮验收撤回 | persist camera 遇到引擎内置 2D camera，跳过了 3D 相机创建；改为 towerCamera 并调整全楼取景。 | [前](review-2026-11-09/baseline/skyscraper-generator.png) · [后](review-2026-11-09/skyscraper-generator.png) |
| [softbody](../../../examples/softbody/README.md) | 已修复 | 将面板背景移到布料/流体之前绘制，恢复网格与粒子；进度条也先画底色。 | [前](review-2026-11-09/baseline/softbody.png) · [后](review-2026-11-09/softbody.png) |
| [terrain-gallery](../../../examples/terrain-gallery/README.md) | 已修复 | 修正程序生成地形着色器 Frame ABI 及嵌入字节码，彩虹地表恢复自然地貌分区。 | [前](review-2026-11-09/baseline/terrain-gallery.png) · [后](review-2026-11-09/terrain-gallery.png) |
| [terrain-preview](../../../examples/terrain-preview/README.md) | 已修复 | 修正地形/水面兼容着色器的 Light3D 布局；相同种子的地貌、河湖颜色恢复。 | [前](review-2026-11-09/baseline/terrain-preview.png) · [后](review-2026-11-09/terrain-preview.png) |
| [terrain-smooth-mountain](../../../examples/terrain-smooth-mountain/README.md) | 上轮验收撤回 | 共用地形着色器布局修复后，饱和蓝色山体恢复自然材质；轮廓和镜头表达仍可继续改善。 | [前](review-2026-11-09/baseline/terrain-smooth-mountain.png) · [后](review-2026-11-09/terrain-smooth-mountain.png) |
| [topdown-procmap](../../../examples/topdown-procmap/README.md) | 已修复 | 修正共用地形着色器后，黄/品红地表异常消除；树冠过密遮挡路径仍按 P2 保留。 | [前](review-2026-11-09/baseline/topdown-procmap.png) · [后](review-2026-11-09/topdown-procmap.png) |
| [unity-vfx-port-lab](../../../examples/unity-vfx-port-lab/README.md) | 已修复 | 权威配方与演示副本的颜色字段改为加载器支持的 colorOverLife；恢复焰色、紫色传送门与烟雾淡出。 | [前](review-2026-11-09/baseline/unity-vfx-port-lab.png) · [后](review-2026-11-09/unity-vfx-port-lab.png) |
| [urban-generator](../../../examples/urban-generator/README.md) | 上轮验收撤回 | 镜头轨道约 150 单位，超出默认 100 的远裁剪面；改为 500 后生成网格可见。地块/道路的材质分区仍可按 P2 改善。 | [前](review-2026-11-09/baseline/urban-generator.png) · [后](review-2026-11-09/urban-generator.png) |
| [venom-transform](../../../examples/venom-transform/README.md) | 取帧更正 | 静态 T 姿属于覆盖材质演示模型；补正常 scrub 状态的开始/中间/结束对照，单个 120 帧不能判定变装流程失效。 | [前](review-2026-11-09/baseline/venom-transform.png) · [后](review-2026-11-09/venom-transform.png) |
| [vrm-avatar](../../../examples/vrm-avatar/README.md) | 定位更正 | README 明确默认是两个三角形的合成 VRM 合约夹具；没有额外 model.vrm 时，不应按完整角色显示故障计 P1。 | [前](review-2026-11-09/baseline/vrm-avatar.png) · [后](review-2026-11-09/vrm-avatar.png) |

## 体积雾黑带的修复取证

[关闭雾](review-2026-11-09/fog-off.png)后黑带消失，而[关闭物体阴影](review-2026-11-09/fog-no-shadows.png)、[不注入局部发光代理](review-2026-11-09/fog-no-proxy.png)仍复现。原生探针确认 CPU 积分和上传图集正确，错误发生在第 24 个切片的浮点图集寻址：有效切片会被映射到不存在的列。改用整数除法/取模后，原先失败的回归通过，[完整雾场景](review-2026-11-09/atmospheric-fog.png)不再出现黑带；没有靠关闭雾替代修复。

## 动态效果与前置条件

墨攻原图抓到了循环中的空白间隔；现在[冻结正常重播的 0.38 秒](review-2026-11-09/ink-combat.png)。变装示例补齐材质覆盖的[开始](review-2026-11-09/venom-start.png)、[中间](review-2026-11-09/venom-mid.png)、[结束](review-2026-11-09/venom-end.png)。这些是正常脚本状态的实帧，不是补绘，也不替代动画、交互或性能验收。

角色实验室尚未准备 `witch.obj`；模型转换器缺少插件/bpy。新图验证的是明确、可读的缺失状态，未宣称已经验证外部模型导入或真实转换。VRM 默认三角形、AI 空舞台、建造入口网格与 GPU 计算日志示例均按实际定位解释。

## 11-09 验证与适用范围

- 完整编译当前引擎与引擎 DLL；共享着色器字节码重新编译，并检查 Frame 字段偏移。
- 原生回归：自定义网格默认深度/显式 Less、非方形雾图集切片边界、反射功能保持不透明主体、图输出拥有权与图回收后的快照、真实配方渐变与末端透明度。5 用例通过，51 个原生断言通过，0 失败、0 跳过；VM 内的脚本断言也通过。
- 着色器契约 Python 测试 3/3 通过；27 个受改动影响的可运行示例通过标准两秒 smoke（配方包没有独立入口，另由原生素材测试验证）。五项正式测试注册按原样投影为聚焦 CTest，逐进程执行，5/5 通过。
- 未执行完整聚合 CTest。额外尝试构建的既有 scene_authoring_contract_test 缺少 EVENGINE_TEST_BINARY_DIR / EVENGINE_SOURCE_DIR 编译定义；未修改这个无关辅助目标，也未将其计为通过。最终源码质量结果见本轮清单的 validation 字段。
- 默认 `make check` 的 `origin/dev` 基线有 5 处既有 `Database.h` API 生命周期注释失败；该文件未被本轮修改。另以 `CI_BASE=HEAD` 核对工作区改动，不将此结果冒充默认基线通过。
- 本轮应用 Result 检查、模块统一拥有权、GC 生命周期、后端能力边界和源码质量要求；没有新增公共接口、模块边界、ECS 模型或引擎持久化格式，没有申请或采用架构例外。WebGPU 只同步了对应代码/布局，未运行浏览器后端；没有执行本地全平台或全量原生套件。

## 上轮 P2 / P3 改进项

下列 46 项来自 11-07 审查，未在本轮逐项重测；以历史图片为依据，不能视作当前源码全部确认。另有本表注明的残留 P2。

| 优先级 | 示例与截图 | 问题类别 | 画面观察 | 建议改进与复查 |
|---|---|---|---|---|
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

## 全量截图索引（本轮 31 项，其余为 11-07 历史帧）

“已抓帧”仅代表成功生成无已知帧错误标记的新 PNG；视觉审查意见以上表为准。点击缩略图可看原分辨率。

| 示例 | 本轮状态 | 引擎截图 |
|---|---|---|
| [advanced-ballistics](../../../examples/advanced-ballistics/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/advanced-ballistics.png"><img src="review-2026-11-07/advanced-ballistics.png" width="180" alt="advanced-ballistics"></a> |
| [ai-game](../../../examples/ai-game/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/ai-game.png"><img src="review-2026-11-07/ai-game.png" width="180" alt="ai-game"></a> |
| [ai-stage](../../../examples/ai-stage/README.md) | 11-09 当前源码；定位更正 | <a href="review-2026-11-09/ai-stage.png"><img src="review-2026-11-09/ai-stage.png" width="180" alt="ai-stage"></a> |
| [animation-clip-editor](../../../examples/animation-clip-editor/README.md) | 11-07；抓帧失败 | 无新图 |
| [anime-character-lab](../../../examples/anime-character-lab/README.md) | 11-09 当前源码；前置条件已明确 | <a href="review-2026-11-09/anime-character-lab.png"><img src="review-2026-11-09/anime-character-lab.png" width="180" alt="anime-character-lab"></a> |
| [archspace-editor](../../../examples/archspace-editor/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/archspace-editor.png"><img src="review-2026-11-07/archspace-editor.png" width="180" alt="archspace-editor"></a> |
| [archspace-floorplan](../../../examples/archspace-floorplan/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/archspace-floorplan.png"><img src="review-2026-11-07/archspace-floorplan.png" width="180" alt="archspace-floorplan"></a> |
| [armored-command-3d](../../../examples/armored-command-3d/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/armored-command-3d.png"><img src="review-2026-11-12/nine-improved/armored-command-3d.png" width="180" alt="armored-command-3d"></a> |
| [arpg-loop](../../../examples/arpg-loop/README.md) | 11-07；抓帧失败 | 无新图 |
| [atmospheric-fog](../../../examples/atmospheric-fog/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/atmospheric-fog.png"><img src="review-2026-11-09/atmospheric-fog.png" width="180" alt="atmospheric-fog"></a> |
| [attack-vfx](../../../examples/attack-vfx/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/attack-vfx.png"><img src="review-2026-11-07/attack-vfx.png" width="180" alt="attack-vfx"></a> |
| [audio-source-editor](../../../examples/audio-source-editor/README.md) | 11-07；抓帧失败 | 无新图 |
| [automatic-bridge](../../../examples/automatic-bridge/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/automatic-bridge.png"><img src="review-2026-11-07/automatic-bridge.png" width="180" alt="automatic-bridge"></a> |
| [autotile-production](../../../examples/autotile-production/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/autotile-production.png"><img src="review-2026-11-07/autotile-production.png" width="180" alt="autotile-production"></a> |
| [avatar-document-editor](../../../examples/avatar-document-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/avatar-document-editor.png"><img src="review-2026-11-07/avatar-document-editor.png" width="180" alt="avatar-document-editor"></a> |
| [basic](../../../examples/basic/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/basic.png"><img src="review-2026-11-07/basic.png" width="180" alt="basic"></a> |
| [biome-rules-editor](../../../examples/biome-rules-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/biome-rules-editor.png"><img src="review-2026-11-07/biome-rules-editor.png" width="180" alt="biome-rules-editor"></a> |
| [blade-whirlwind](../../../examples/blade-whirlwind/README.md) | 11-07；已抓帧；P3 | <a href="review-2026-11-07/blade-whirlwind.png"><img src="review-2026-11-07/blade-whirlwind.png" width="180" alt="blade-whirlwind"></a> |
| [blender_hot_reload](../../../examples/blender_hot_reload/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/blender_hot_reload.png"><img src="review-2026-11-07/blender_hot_reload.png" width="180" alt="blender_hot_reload"></a> |
| [building](../../../examples/building/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/building.png"><img src="review-2026-11-07/building.png" width="180" alt="building"></a> |
| [building-3d](../../../examples/building-3d/README.md) | 11-09 当前源码；定位更正 | <a href="review-2026-11-09/building-3d.png"><img src="review-2026-11-09/building-3d.png" width="180" alt="building-3d"></a> |
| [building-tilemap](../../../examples/building-tilemap/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/building-tilemap.png"><img src="review-2026-11-12/nine-improved/building-tilemap.png" width="180" alt="building-tilemap"></a> |
| [building_village](../../../examples/building_village/README.md) | 11-07；抓帧失败 | 无新图 |
| [bullet-time](../../../examples/bullet-time/README.md) | 11-07；抓帧失败 | 无新图 |
| [bush-fog-volumes](../../../examples/bush-fog-volumes/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/bush-fog-volumes.png"><img src="review-2026-11-07/bush-fog-volumes.png" width="180" alt="bush-fog-volumes"></a> |
| [bush-generator](../../../examples/bush-generator/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/bush-generator.png"><img src="review-2026-11-07/bush-generator.png" width="180" alt="bush-generator"></a> |
| [cable-chain-rope](../../../examples/cable-chain-rope/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/cable-chain-rope.png"><img src="review-2026-11-12/nine-improved/cable-chain-rope.png" width="180" alt="cable-chain-rope"></a> |
| [camera-controllers](../../../examples/camera-controllers/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/camera-controllers.png"><img src="review-2026-11-07/camera-controllers.png" width="180" alt="camera-controllers"></a> |
| [cardgame](../../../examples/cardgame/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/cardgame.png"><img src="review-2026-11-07/cardgame.png" width="180" alt="cardgame"></a> |
| [castle-generator](../../../examples/castle-generator/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/castle-generator.png"><img src="review-2026-11-12/nine-improved/castle-generator.png" width="180" alt="castle-generator"></a> |
| [cave-generator](../../../examples/cave-generator/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/cave-generator.png"><img src="review-2026-11-07/cave-generator.png" width="180" alt="cave-generator"></a> |
| [character-atelier](../../../examples/character-atelier/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/character-atelier.png"><img src="review-2026-11-07/character-atelier.png" width="180" alt="character-atelier"></a> |
| [character-cape-hair](../../../examples/character-cape-hair/README.md) | 11-07；抓帧失败 | 无新图 |
| [character-motion-lab](../../../examples/character-motion-lab/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/character-motion-lab.png"><img src="review-2026-11-07/character-motion-lab.png" width="180" alt="character-motion-lab"></a> |
| [cinematic-story-camera](../../../examples/cinematic-story-camera/README.md) | 11-07；抓帧失败 | 无新图 |
| [climbing-motion-matching](../../../examples/climbing-motion-matching/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/climbing-motion-matching.png"><img src="review-2026-11-07/climbing-motion-matching.png" width="180" alt="climbing-motion-matching"></a> |
| [climbing-playground](../../../examples/climbing-playground/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/climbing-playground.png"><img src="review-2026-11-12/nine-improved/climbing-playground.png" width="180" alt="climbing-playground"></a> |
| [combat-action-editor](../../../examples/combat-action-editor/README.md) | 11-07；抓帧失败 | 无新图 |
| [combat-arena](../../../examples/combat-arena/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/combat-arena.png"><img src="review-2026-11-07/combat-arena.png" width="180" alt="combat-arena"></a> |
| [commandery-rts](../../../examples/commandery-rts/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/commandery-rts.png"><img src="review-2026-11-09/commandery-rts.png" width="180" alt="commandery-rts"></a> |
| [companion-tactics](../../../examples/companion-tactics/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/companion-tactics.png"><img src="review-2026-11-07/companion-tactics.png" width="180" alt="companion-tactics"></a> |
| [composable-editor](../../../examples/composable-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/composable-editor.png"><img src="review-2026-11-07/composable-editor.png" width="180" alt="composable-editor"></a> |
| [composable-rebellion](../../../examples/composable-rebellion/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/composable-rebellion.png"><img src="review-2026-11-07/composable-rebellion.png" width="180" alt="composable-rebellion"></a> |
| [control-rig-editor](../../../examples/control-rig-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/control-rig-editor.png"><img src="review-2026-11-07/control-rig-editor.png" width="180" alt="control-rig-editor"></a> |
| [conveyor-belt](../../../examples/conveyor-belt/README.md) | 11-07；抓帧失败 | 无新图 |
| [crowd](../../../examples/crowd/README.md) | 11-07；抓帧失败 | 无新图 |
| [crowd-battle](../../../examples/crowd-battle/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/crowd-battle.png"><img src="review-2026-11-07/crowd-battle.png" width="180" alt="crowd-battle"></a> |
| [daynight](../../../examples/daynight/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/daynight.png"><img src="review-2026-11-11/next10/daynight.png" width="180" alt="daynight"></a> |
| [destruction-basic](../../../examples/destruction-basic/README.md) | 11-07；已抓帧；P3 | <a href="review-2026-11-07/destruction-basic.png"><img src="review-2026-11-07/destruction-basic.png" width="180" alt="destruction-basic"></a> |
| [destruction-clusters](../../../examples/destruction-clusters/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/destruction-clusters.png"><img src="review-2026-11-07/destruction-clusters.png" width="180" alt="destruction-clusters"></a> |
| [devlab](../../../examples/devlab/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/devlab.png"><img src="review-2026-11-07/devlab.png" width="180" alt="devlab"></a> |
| [dialogue](../../../examples/dialogue/README.md) | 11-07；抓帧失败 | 无新图 |
| [dual-grid-material-gallery](../../../examples/dual-grid-material-gallery/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/dual-grid-material-gallery.png"><img src="review-2026-11-07/dual-grid-material-gallery.png" width="180" alt="dual-grid-material-gallery"></a> |
| [dungeon-generator-3d](../../../examples/dungeon-generator-3d/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/dungeon-generator-3d.png"><img src="review-2026-11-11/next10/dungeon-generator-3d.png" width="180" alt="dungeon-generator-3d"></a> |
| [dynamic-water-grid](../../../examples/dynamic-water-grid/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/dynamic-water-grid.png"><img src="review-2026-11-12/nine-improved/dynamic-water-grid.png" width="180" alt="dynamic-water-grid"></a> |
| [ecs](../../../examples/ecs/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/ecs.png"><img src="review-2026-11-07/ecs.png" width="180" alt="ecs"></a> |
| [editor-api-v2](../../../examples/editor-api-v2/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/editor-api-v2.png"><img src="review-2026-11-07/editor-api-v2.png" width="180" alt="editor-api-v2"></a> |
| [editor-host-world](../../../examples/editor-host-world/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/editor-host-world.png"><img src="review-2026-11-09/editor-host-world.png" width="180" alt="editor-host-world"></a> |
| [editor-ui-gallery](../../../examples/editor-ui-gallery/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/editor-ui-gallery.png"><img src="review-2026-11-07/editor-ui-gallery.png" width="180" alt="editor-ui-gallery"></a> |
| [editor-workspace-shell](../../../examples/editor-workspace-shell/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/editor-workspace-shell.png"><img src="review-2026-11-07/editor-workspace-shell.png" width="180" alt="editor-workspace-shell"></a> |
| [ember-guard](../../../examples/ember-guard/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/ember-guard.png"><img src="review-2026-11-07/ember-guard.png" width="180" alt="ember-guard"></a> |
| [emberforge](../../../examples/emberforge/README.md) | 11-07；抓帧失败 | 无新图 |
| [enemy-vision](../../../examples/enemy-vision/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/enemy-vision.png"><img src="review-2026-11-07/enemy-vision.png" width="180" alt="enemy-vision"></a> |
| [environment-query](../../../examples/environment-query/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/environment-query.png"><img src="review-2026-11-07/environment-query.png" width="180" alt="environment-query"></a> |
| [explosion-lab](../../../examples/explosion-lab/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/explosion-lab.png"><img src="review-2026-11-07/explosion-lab.png" width="180" alt="explosion-lab"></a> |
| [factory](../../../examples/factory/README.md) | 11-07；抓帧失败 | 无新图 |
| [flame-jet](../../../examples/flame-jet/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/flame-jet.png"><img src="review-2026-11-11/next10/flame-jet.png" width="180" alt="flame-jet"></a> |
| [flight-vehicle-controllers](../../../examples/flight-vehicle-controllers/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/flight-vehicle-controllers.png"><img src="review-2026-11-07/flight-vehicle-controllers.png" width="180" alt="flight-vehicle-controllers"></a> |
| [fluid3d](../../../examples/fluid3d/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/fluid3d.png"><img src="review-2026-11-11/next10/fluid3d.png" width="180" alt="fluid3d"></a> |
| [fps-arena](../../../examples/fps-arena/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/fps-arena.png"><img src="review-2026-11-12/nine-improved/fps-arena.png" width="180" alt="fps-arena"></a> |
| [galaxy](../../../examples/galaxy/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/galaxy.png"><img src="review-2026-11-07/galaxy.png" width="180" alt="galaxy"></a> |
| [galgame](../../../examples/galgame/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/galgame.png"><img src="review-2026-11-07/galgame.png" width="180" alt="galgame"></a> |
| [game-tutorial](../../../examples/game-tutorial/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/game-tutorial.png"><img src="review-2026-11-07/game-tutorial.png" width="180" alt="game-tutorial"></a> |
| [geometry-stroke-lab](../../../examples/geometry-stroke-lab/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/geometry-stroke-lab.png"><img src="review-2026-11-07/geometry-stroke-lab.png" width="180" alt="geometry-stroke-lab"></a> |
| [glass-crystal](../../../examples/glass-crystal/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/glass-crystal.png"><img src="review-2026-11-09/glass-crystal.png" width="180" alt="glass-crystal"></a> |
| [gpu-agents](../../../examples/gpu-agents/README.md) | 11-09 当前源码；定位更正 | <a href="review-2026-11-09/gpu-agents.png"><img src="review-2026-11-09/gpu-agents.png" width="180" alt="gpu-agents"></a> |
| [gpu-formation](../../../examples/gpu-formation/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/gpu-formation.png"><img src="review-2026-11-07/gpu-formation.png" width="180" alt="gpu-formation"></a> |
| [ground-fissure](../../../examples/ground-fissure/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/ground-fissure.png"><img src="review-2026-11-07/ground-fissure.png" width="180" alt="ground-fissure"></a> |
| [growth](../../../examples/growth/README.md) | 11-07；抓帧失败 | 无新图 |
| [hd2d-riverside](../../../examples/hd2d-riverside/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/hd2d-riverside.png"><img src="review-2026-11-12/nine-improved/hd2d-riverside.png" width="180" alt="hd2d-riverside"></a> |
| [hex-levels](../../../examples/hex-levels/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/hex-levels.png"><img src="review-2026-11-07/hex-levels.png" width="180" alt="hex-levels"></a> |
| [hex-planet](../../../examples/hex-planet/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/hex-planet.png"><img src="review-2026-11-09/hex-planet.png" width="180" alt="hex-planet"></a> |
| [hex-terrain](../../../examples/hex-terrain/README.md) | 11-11 重新整改；见上文 | <a href="review-2026-11-11/hex-terrain.png"><img src="review-2026-11-11/hex-terrain.png" width="180" alt="hex-terrain"></a> |
| [hex-terrain-3d](../../../examples/hex-terrain-3d/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/hex-terrain-3d.png"><img src="review-2026-11-09/hex-terrain-3d.png" width="180" alt="hex-terrain-3d"></a> |
| [house-collapse](../../../examples/house-collapse/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/house-collapse.png"><img src="review-2026-11-07/house-collapse.png" width="180" alt="house-collapse"></a> |
| [housegen](../../../examples/housegen/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/housegen.png"><img src="review-2026-11-07/housegen.png" width="180" alt="housegen"></a> |
| [hud-gallery](../../../examples/hud-gallery/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/hud-gallery.png"><img src="review-2026-11-07/hud-gallery.png" width="180" alt="hud-gallery"></a> |
| [i18n](../../../examples/i18n/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/i18n.png"><img src="review-2026-11-07/i18n.png" width="180" alt="i18n"></a> |
| [ik-spider](../../../examples/ik-spider/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/ik-spider.png"><img src="review-2026-11-07/ik-spider.png" width="180" alt="ik-spider"></a> |
| [ik-spider-3d](../../../examples/ik-spider-3d/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/ik-spider-3d.png"><img src="review-2026-11-07/ik-spider-3d.png" width="180" alt="ik-spider-3d"></a> |
| [impact-arena](../../../examples/impact-arena/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/impact-arena.png"><img src="review-2026-11-07/impact-arena.png" width="180" alt="impact-arena"></a> |
| [impasto-paint](../../../examples/impasto-paint/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/impasto-paint.png"><img src="review-2026-11-07/impasto-paint.png" width="180" alt="impasto-paint"></a> |
| [ink-arena](../../../examples/ink-arena/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/ink-arena.png"><img src="review-2026-11-09/ink-arena.png" width="180" alt="ink-arena"></a> |
| [ink-combat](../../../examples/ink-combat/README.md) | 11-09 当前源码；取帧更正 | <a href="review-2026-11-09/ink-combat.png"><img src="review-2026-11-09/ink-combat.png" width="180" alt="ink-combat"></a> |
| [inspector-demo](../../../examples/inspector-demo/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/inspector-demo.png"><img src="review-2026-11-09/inspector-demo.png" width="180" alt="inspector-demo"></a> |
| [interactive-shallow-water](../../../examples/interactive-shallow-water/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/interactive-shallow-water.png"><img src="review-2026-11-07/interactive-shallow-water.png" width="180" alt="interactive-shallow-water"></a> |
| [interior-mapping](../../../examples/interior-mapping/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/interior-mapping.png"><img src="review-2026-11-09/interior-mapping.png" width="180" alt="interior-mapping"></a> |
| [inventory](../../../examples/inventory/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/inventory.png"><img src="review-2026-11-07/inventory.png" width="180" alt="inventory"></a> |
| [iron-chain-bridge](../../../examples/iron-chain-bridge/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/iron-chain-bridge.png"><img src="review-2026-11-07/iron-chain-bridge.png" width="180" alt="iron-chain-bridge"></a> |
| [ironwarden-retarget](../../../examples/ironwarden-retarget/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/ironwarden-retarget.png"><img src="review-2026-11-07/ironwarden-retarget.png" width="180" alt="ironwarden-retarget"></a> |
| [iso-grid-walk](../../../examples/iso-grid-walk/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/iso-grid-walk.png"><img src="review-2026-11-07/iso-grid-walk.png" width="180" alt="iso-grid-walk"></a> |
| [laser-weapon](../../../examples/laser-weapon/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/laser-weapon.png"><img src="review-2026-11-07/laser-weapon.png" width="180" alt="laser-weapon"></a> |
| [lattice-deform](../../../examples/lattice-deform/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/lattice-deform.png"><img src="review-2026-11-07/lattice-deform.png" width="180" alt="lattice-deform"></a> |
| [layered-animation](../../../examples/layered-animation/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/layered-animation.png"><img src="review-2026-11-07/layered-animation.png" width="180" alt="layered-animation"></a> |
| [layered-cloth](../../../examples/layered-cloth/README.md) | 11-07；抓帧失败 | 无新图 |
| [layered-cloth-gpu](../../../examples/layered-cloth-gpu/README.md) | 11-07；抓帧失败 | 无新图 |
| [level-designer](../../../examples/level-designer/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/level-designer.png"><img src="review-2026-11-07/level-designer.png" width="180" alt="level-designer"></a> |
| [lighting-showcase](../../../examples/lighting-showcase/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/lighting-showcase.png"><img src="review-2026-11-11/next10/lighting-showcase.png" width="180" alt="lighting-showcase"></a> |
| [linear-structures](../../../examples/linear-structures/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/linear-structures.png"><img src="review-2026-11-07/linear-structures.png" width="180" alt="linear-structures"></a> |
| [map-fog](../../../examples/map-fog/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/map-fog.png"><img src="review-2026-11-07/map-fog.png" width="180" alt="map-fog"></a> |
| [material-editor](../../../examples/material-editor/README.md) | 11-07；抓帧失败 | 无新图 |
| [mesh-contact-fusion](../../../examples/mesh-contact-fusion/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/mesh-contact-fusion.png"><img src="review-2026-11-07/mesh-contact-fusion.png" width="180" alt="mesh-contact-fusion"></a> |
| [mesh-impact-lab](../../../examples/mesh-impact-lab/README.md) | 11-07；已抓帧；P3 | <a href="review-2026-11-07/mesh-impact-lab.png"><img src="review-2026-11-07/mesh-impact-lab.png" width="180" alt="mesh-impact-lab"></a> |
| [mesh-modifier-lab](../../../examples/mesh-modifier-lab/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/mesh-modifier-lab.png"><img src="review-2026-11-07/mesh-modifier-lab.png" width="180" alt="mesh-modifier-lab"></a> |
| [meteor-impact](../../../examples/meteor-impact/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/meteor-impact.png"><img src="review-2026-11-07/meteor-impact.png" width="180" alt="meteor-impact"></a> |
| [metroidvania](../../../examples/metroidvania/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/metroidvania.png"><img src="review-2026-11-11/next10/metroidvania.png" width="180" alt="metroidvania"></a> |
| [model-converter](../../../examples/model-converter/README.md) | 11-09 当前源码；前置条件已明确 | <a href="review-2026-11-09/model-converter.png"><img src="review-2026-11-09/model-converter.png" width="180" alt="model-converter"></a> |
| [modern-balcony](../../../examples/modern-balcony/README.md) | 11-07；抓帧失败 | 无新图 |
| [modern-battlefield](../../../examples/modern-battlefield/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/modern-battlefield.png"><img src="review-2026-11-07/modern-battlefield.png" width="180" alt="modern-battlefield"></a> |
| [neon-racing](../../../examples/neon-racing/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/neon-racing.png"><img src="review-2026-11-07/neon-racing.png" width="180" alt="neon-racing"></a> |
| [night-city](../../../examples/night-city/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/night-city.png"><img src="review-2026-11-07/night-city.png" width="180" alt="night-city"></a> |
| [ocean-voyage](../../../examples/ocean-voyage/README.md) | 11-07；抓帧失败 | 无新图 |
| [outline](../../../examples/outline/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/outline.png"><img src="review-2026-11-07/outline.png" width="180" alt="outline"></a> |
| [particle-playback-lab](../../../examples/particle-playback-lab/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/particle-playback-lab.png"><img src="review-2026-11-07/particle-playback-lab.png" width="180" alt="particle-playback-lab"></a> |
| [pcg-biome](../../../examples/pcg-biome/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/pcg-biome.png"><img src="review-2026-11-09/pcg-biome.png" width="180" alt="pcg-biome"></a> |
| [pcg-depth-of-field](../../../examples/pcg-depth-of-field/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-depth-of-field.png"><img src="review-2026-11-07/pcg-depth-of-field.png" width="180" alt="pcg-depth-of-field"></a> |
| [pcg-detail-overwrite](../../../examples/pcg-detail-overwrite/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-detail-overwrite.png"><img src="review-2026-11-07/pcg-detail-overwrite.png" width="180" alt="pcg-detail-overwrite"></a> |
| [pcg-layer-culling](../../../examples/pcg-layer-culling/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-layer-culling.png"><img src="review-2026-11-07/pcg-layer-culling.png" width="180" alt="pcg-layer-culling"></a> |
| [pcg-location-system](../../../examples/pcg-location-system/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-location-system.png"><img src="review-2026-11-07/pcg-location-system.png" width="180" alt="pcg-location-system"></a> |
| [pcg-runtime-orchestration](../../../examples/pcg-runtime-orchestration/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-runtime-orchestration.png"><img src="review-2026-11-07/pcg-runtime-orchestration.png" width="180" alt="pcg-runtime-orchestration"></a> |
| [pcg-scene-player-culling](../../../examples/pcg-scene-player-culling/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-scene-player-culling.png"><img src="review-2026-11-07/pcg-scene-player-culling.png" width="180" alt="pcg-scene-player-culling"></a> |
| [pcg-snow-wind](../../../examples/pcg-snow-wind/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-snow-wind.png"><img src="review-2026-11-07/pcg-snow-wind.png" width="180" alt="pcg-snow-wind"></a> |
| [pcg-solar-planet](../../../examples/pcg-solar-planet/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-solar-planet.png"><img src="review-2026-11-07/pcg-solar-planet.png" width="180" alt="pcg-solar-planet"></a> |
| [pcg-thunder-strike](../../../examples/pcg-thunder-strike/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-thunder-strike.png"><img src="review-2026-11-07/pcg-thunder-strike.png" width="180" alt="pcg-thunder-strike"></a> |
| [pcg-ui-scaler](../../../examples/pcg-ui-scaler/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcg-ui-scaler.png"><img src="review-2026-11-07/pcg-ui-scaler.png" width="180" alt="pcg-ui-scaler"></a> |
| [pcg-underwater](../../../examples/pcg-underwater/README.md) | 11-07；抓帧失败 | 无新图 |
| [pcm-2a03](../../../examples/pcm-2a03/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pcm-2a03.png"><img src="review-2026-11-07/pcm-2a03.png" width="180" alt="pcm-2a03"></a> |
| [physics-bake-replay](../../../examples/physics-bake-replay/README.md) | 11-07；抓帧失败 | 无新图 |
| [physics-force-lab](../../../examples/physics-force-lab/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/physics-force-lab.png"><img src="review-2026-11-07/physics-force-lab.png" width="180" alt="physics-force-lab"></a> |
| [physics-workshop](../../../examples/physics-workshop/README.md) | 11-07；抓帧失败 | 无新图 |
| [pipe-generator](../../../examples/pipe-generator/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pipe-generator.png"><img src="review-2026-11-07/pipe-generator.png" width="180" alt="pipe-generator"></a> |
| [pixel-perfect-camera](../../../examples/pixel-perfect-camera/README.md) | 11-07；抓帧失败 | 无新图 |
| [pixelworld](../../../examples/pixelworld/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/pixelworld.png"><img src="review-2026-11-07/pixelworld.png" width="180" alt="pixelworld"></a> |
| [post-patrol-reinforce](../../../examples/post-patrol-reinforce/README.md) | 11-07；抓帧失败 | 无新图 |
| [primitive-drawing](../../../examples/primitive-drawing/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/primitive-drawing.png"><img src="review-2026-11-07/primitive-drawing.png" width="180" alt="primitive-drawing"></a> |
| [procedural-render-gallery](../../../examples/procedural-render-gallery/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/procedural-render-gallery.png"><img src="review-2026-11-07/procedural-render-gallery.png" width="180" alt="procedural-render-gallery"></a> |
| [procedural-road](../../../examples/procedural-road/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/procedural-road.png"><img src="review-2026-11-07/procedural-road.png" width="180" alt="procedural-road"></a> |
| [procedural-textures](../../../examples/procedural-textures/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/procedural-textures.png"><img src="review-2026-11-07/procedural-textures.png" width="180" alt="procedural-textures"></a> |
| [procgen](../../../examples/procgen/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/procgen.png"><img src="review-2026-11-07/procgen.png" width="180" alt="procgen"></a> |
| [procgen-nature-quality](../../../examples/procgen-nature-quality/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/procgen-nature-quality.png"><img src="review-2026-11-11/next10/procgen-nature-quality.png" width="180" alt="procgen-nature-quality"></a> |
| [procgen-script-editor](../../../examples/procgen-script-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/procgen-script-editor.png"><img src="review-2026-11-07/procgen-script-editor.png" width="180" alt="procgen-script-editor"></a> |
| [procgen-script-pipeline](../../../examples/procgen-script-pipeline/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/procgen-script-pipeline.png"><img src="review-2026-11-07/procgen-script-pipeline.png" width="180" alt="procgen-script-pipeline"></a> |
| [projectile-physics-composition](../../../examples/projectile-physics-composition/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/projectile-physics-composition.png"><img src="review-2026-11-07/projectile-physics-composition.png" width="180" alt="projectile-physics-composition"></a> |
| [railgun-3d](../../../examples/railgun-3d/README.md) | 11-07；已抓帧；P3 | <a href="review-2026-11-07/railgun-3d.png"><img src="review-2026-11-07/railgun-3d.png" width="180" alt="railgun-3d"></a> |
| [realtime-fog](../../../examples/realtime-fog/README.md) | 11-07；抓帧失败 | 无新图 |
| [render-fidelity](../../../examples/render-fidelity/README.md) | 11-07；抓帧失败 | 无新图 |
| [rendering-chain-lab](../../../examples/rendering-chain-lab/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/rendering-chain-lab.png"><img src="review-2026-11-09/rendering-chain-lab.png" width="180" alt="rendering-chain-lab"></a> |
| [rock-generator](../../../examples/rock-generator/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/rock-generator.png"><img src="review-2026-11-07/rock-generator.png" width="180" alt="rock-generator"></a> |
| [roguelike-generator](../../../examples/roguelike-generator/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/roguelike-generator.png"><img src="review-2026-11-07/roguelike-generator.png" width="180" alt="roguelike-generator"></a> |
| [rpg](../../../examples/rpg/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/rpg.png"><img src="review-2026-11-09/rpg.png" width="180" alt="rpg"></a> |
| [rpg-balance](../../../examples/rpg-balance/README.md) | 11-07；抓帧失败 | 无新图 |
| [rpg-classic](../../../examples/rpg-classic/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/rpg-classic.png"><img src="review-2026-11-07/rpg-classic.png" width="180" alt="rpg-classic"></a> |
| [rts-cardfront](../../../examples/rts-cardfront/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/rts-cardfront.png"><img src="review-2026-11-07/rts-cardfront.png" width="180" alt="rts-cardfront"></a> |
| [rts-sandbox](../../../examples/rts-sandbox/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/rts-sandbox.png"><img src="review-2026-11-07/rts-sandbox.png" width="180" alt="rts-sandbox"></a> |
| [scad-living-room](../../../examples/scad-living-room/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/scad-living-room.png"><img src="review-2026-11-07/scad-living-room.png" width="180" alt="scad-living-room"></a> |
| [scad-mechanical](../../../examples/scad-mechanical/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/scad-mechanical.png"><img src="review-2026-11-07/scad-mechanical.png" width="180" alt="scad-mechanical"></a> |
| [scad-music-box](../../../examples/scad-music-box/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/scad-music-box.png"><img src="review-2026-11-07/scad-music-box.png" width="180" alt="scad-music-box"></a> |
| [scad-watermill](../../../examples/scad-watermill/README.md) | 墙面复查修正；外景/水轮/室内复查 | <a href="review-2026-11-11/watermill-walls/exterior.png"><img src="review-2026-11-11/watermill-walls/exterior.png" width="180" alt="scad-watermill"></a> |
| [scene-builder-game](../../../examples/scene-builder-game/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/scene-builder-game.png"><img src="review-2026-11-07/scene-builder-game.png" width="180" alt="scene-builder-game"></a> |
| [scene-ecs](../../../examples/scene-ecs/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/scene-ecs.png"><img src="review-2026-11-07/scene-ecs.png" width="180" alt="scene-ecs"></a> |
| [scene-editor](../../../examples/scene-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/scene-editor.png"><img src="review-2026-11-07/scene-editor.png" width="180" alt="scene-editor"></a> |
| [script-rule-provider](../../../examples/script-rule-provider/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/script-rule-provider.png"><img src="review-2026-11-07/script-rule-provider.png" width="180" alt="script-rule-provider"></a> |
| [shader-live-preview](../../../examples/shader-live-preview/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/shader-live-preview.png"><img src="review-2026-11-07/shader-live-preview.png" width="180" alt="shader-live-preview"></a> |
| [silpom-ssdm-compare](../../../examples/silpom-ssdm-compare/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/silpom-ssdm-compare.png"><img src="review-2026-11-07/silpom-ssdm-compare.png" width="180" alt="silpom-ssdm-compare"></a> |
| [ski-adventure](../../../examples/ski-adventure/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/ski-adventure.png"><img src="review-2026-11-07/ski-adventure.png" width="180" alt="ski-adventure"></a> |
| [skyforge-trials](../../../examples/skyforge-trials/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/skyforge-trials.png"><img src="review-2026-11-07/skyforge-trials.png" width="180" alt="skyforge-trials"></a> |
| [skyscraper-generator](../../../examples/skyscraper-generator/README.md) | 11-11 重新整改；见上文 | <a href="review-2026-11-11/skyscraper-generator.png"><img src="review-2026-11-11/skyscraper-generator.png" width="180" alt="skyscraper-generator"></a> |
| [smoke-flipbook](../../../examples/smoke-flipbook/README.md) | 11-07；抓帧失败 | 无新图 |
| [snow](../../../examples/snow/README.md) | 11-07；已抓帧；P3 | <a href="review-2026-11-07/snow.png"><img src="review-2026-11-07/snow.png" width="180" alt="snow"></a> |
| [softbody](../../../examples/softbody/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/softbody.png"><img src="review-2026-11-09/softbody.png" width="180" alt="softbody"></a> |
| [softbody3d](../../../examples/softbody3d/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/softbody3d.png"><img src="review-2026-11-07/softbody3d.png" width="180" alt="softbody3d"></a> |
| [spline-tube-lab](../../../examples/spline-tube-lab/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/spline-tube-lab.png"><img src="review-2026-11-07/spline-tube-lab.png" width="180" alt="spline-tube-lab"></a> |
| [sprite-animation-vfx](../../../examples/sprite-animation-vfx/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/sprite-animation-vfx.png"><img src="review-2026-11-07/sprite-animation-vfx.png" width="180" alt="sprite-animation-vfx"></a> |
| [sprite-stack](../../../examples/sprite-stack/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/sprite-stack.png"><img src="review-2026-11-07/sprite-stack.png" width="180" alt="sprite-stack"></a> |
| [squad-boss-3d](../../../examples/squad-boss-3d/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/squad-boss-3d.png"><img src="review-2026-11-07/squad-boss-3d.png" width="180" alt="squad-boss-3d"></a> |
| [stylized-meadow](../../../examples/stylized-meadow/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/stylized-meadow.png"><img src="review-2026-11-07/stylized-meadow.png" width="180" alt="stylized-meadow"></a> |
| [stylized-water-shore](../../../examples/stylized-water-shore/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/stylized-water-shore.png"><img src="review-2026-11-07/stylized-water-shore.png" width="180" alt="stylized-water-shore"></a> |
| [surface-vine-growth](../../../examples/surface-vine-growth/README.md) | 11-12 九项改善；当前源码实帧 | <a href="review-2026-11-12/nine-improved/surface-vine-growth.png"><img src="review-2026-11-12/nine-improved/surface-vine-growth.png" width="180" alt="surface-vine-growth"></a> |
| [swappable-tilesets](../../../examples/swappable-tilesets/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/swappable-tilesets.png"><img src="review-2026-11-07/swappable-tilesets.png" width="180" alt="swappable-tilesets"></a> |
| [sword-slash-trail](../../../examples/sword-slash-trail/README.md) | 11-07；已抓帧；P3 | <a href="review-2026-11-07/sword-slash-trail.png"><img src="review-2026-11-07/sword-slash-trail.png" width="180" alt="sword-slash-trail"></a> |
| [tactics](../../../examples/tactics/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/tactics.png"><img src="review-2026-11-07/tactics.png" width="180" alt="tactics"></a> |
| [tensor](../../../examples/tensor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/tensor.png"><img src="review-2026-11-07/tensor.png" width="180" alt="tensor"></a> |
| [terrain-editor](../../../examples/terrain-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/terrain-editor.png"><img src="review-2026-11-07/terrain-editor.png" width="180" alt="terrain-editor"></a> |
| [terrain-gallery](../../../examples/terrain-gallery/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/terrain-gallery.png"><img src="review-2026-11-09/terrain-gallery.png" width="180" alt="terrain-gallery"></a> |
| [terrain-navigation-lab](../../../examples/terrain-navigation-lab/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/terrain-navigation-lab.png"><img src="review-2026-11-07/terrain-navigation-lab.png" width="180" alt="terrain-navigation-lab"></a> |
| [terrain-preview](../../../examples/terrain-preview/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/terrain-preview.png"><img src="review-2026-11-09/terrain-preview.png" width="180" alt="terrain-preview"></a> |
| [terrain-smooth-mountain](../../../examples/terrain-smooth-mountain/README.md) | 11-11 山体细节续改已实帧检查 | <a href="review-2026-11-11/mountain-detail/terrain-smooth-mountain.png"><img src="review-2026-11-11/mountain-detail/terrain-smooth-mountain.png" width="180" alt="terrain-smooth-mountain"></a> |
| [terrain-stamping](../../../examples/terrain-stamping/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/terrain-stamping.png"><img src="review-2026-11-07/terrain-stamping.png" width="180" alt="terrain-stamping"></a> |
| [tileworld-graph-dungeon](../../../examples/tileworld-graph-dungeon/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/tileworld-graph-dungeon.png"><img src="review-2026-11-07/tileworld-graph-dungeon.png" width="180" alt="tileworld-graph-dungeon"></a> |
| [topdown-procmap](../../../examples/topdown-procmap/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/topdown-procmap.png"><img src="review-2026-11-09/topdown-procmap.png" width="180" alt="topdown-procmap"></a> |
| [tornado-lab](../../../examples/tornado-lab/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/tornado-lab.png"><img src="review-2026-11-07/tornado-lab.png" width="180" alt="tornado-lab"></a> |
| [tower-defense](../../../examples/tower-defense/README.md) | 11-07；抓帧失败 | 无新图 |
| [trap-workshop](../../../examples/trap-workshop/README.md) | 11-07；抓帧失败 | 无新图 |
| [tree-generator](../../../examples/tree-generator/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/tree-generator.png"><img src="review-2026-11-07/tree-generator.png" width="180" alt="tree-generator"></a> |
| [triplanar-decal](../../../examples/triplanar-decal/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/triplanar-decal.png"><img src="review-2026-11-07/triplanar-decal.png" width="180" alt="triplanar-decal"></a> |
| [uds-sky](../../../examples/uds-sky/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/uds-sky.png"><img src="review-2026-11-07/uds-sky.png" width="180" alt="uds-sky"></a> |
| [ui-theme-editor](../../../examples/ui-theme-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/ui-theme-editor.png"><img src="review-2026-11-07/ui-theme-editor.png" width="180" alt="ui-theme-editor"></a> |
| [ultra-elements](../../../examples/ultra-elements/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/ultra-elements.png"><img src="review-2026-11-07/ultra-elements.png" width="180" alt="ultra-elements"></a> |
| [unit-surface-effects](../../../examples/unit-surface-effects/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/unit-surface-effects.png"><img src="review-2026-11-07/unit-surface-effects.png" width="180" alt="unit-surface-effects"></a> |
| [unity-vfx-port-lab](../../../examples/unity-vfx-port-lab/README.md) | 11-09 当前源码；已修复 | <a href="review-2026-11-09/unity-vfx-port-lab.png"><img src="review-2026-11-09/unity-vfx-port-lab.png" width="180" alt="unity-vfx-port-lab"></a> |
| [urban-generator](../../../examples/urban-generator/README.md) | 11-11 细节续改已运行检查 | <a href="review-2026-11-11/urban-detail/urban-generator.png"><img src="review-2026-11-11/urban-detail/urban-generator.png" width="180" alt="urban-generator"></a> |
| [uv-texture-paint](../../../examples/uv-texture-paint/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/uv-texture-paint.png"><img src="review-2026-11-07/uv-texture-paint.png" width="180" alt="uv-texture-paint"></a> |
| [vehicle](../../../examples/vehicle/README.md) | 11-07；已抓帧；P2 | <a href="review-2026-11-07/vehicle.png"><img src="review-2026-11-07/vehicle.png" width="180" alt="vehicle"></a> |
| [venom-transform](../../../examples/venom-transform/README.md) | 11-09 当前源码；取帧更正 | <a href="review-2026-11-09/venom-transform.png"><img src="review-2026-11-09/venom-transform.png" width="180" alt="venom-transform"></a> |
| [virtual-texture-blending](../../../examples/virtual-texture-blending/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/virtual-texture-blending.png"><img src="review-2026-11-07/virtual-texture-blending.png" width="180" alt="virtual-texture-blending"></a> |
| [virtualgeometry](../../../examples/virtualgeometry/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/virtualgeometry.png"><img src="review-2026-11-07/virtualgeometry.png" width="180" alt="virtualgeometry"></a> |
| [vortex-cloud](../../../examples/vortex-cloud/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/vortex-cloud.png"><img src="review-2026-11-07/vortex-cloud.png" width="180" alt="vortex-cloud"></a> |
| [voxel](../../../examples/voxel/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/voxel.png"><img src="review-2026-11-07/voxel.png" width="180" alt="voxel"></a> |
| [voxel-catalog-editor](../../../examples/voxel-catalog-editor/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/voxel-catalog-editor.png"><img src="review-2026-11-07/voxel-catalog-editor.png" width="180" alt="voxel-catalog-editor"></a> |
| [voxel-terrain](../../../examples/voxel-terrain/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/voxel-terrain.png"><img src="review-2026-11-07/voxel-terrain.png" width="180" alt="voxel-terrain"></a> |
| [vrm-avatar](../../../examples/vrm-avatar/README.md) | 11-09 当前源码；定位更正 | <a href="review-2026-11-09/vrm-avatar.png"><img src="review-2026-11-09/vrm-avatar.png" width="180" alt="vrm-avatar"></a> |
| [warship](../../../examples/warship/README.md) | 11-07；抓帧失败 | 无新图 |
| [water-splash-vfx](../../../examples/water-splash-vfx/README.md) | 11-07；已抓帧；P3 | <a href="review-2026-11-07/water-splash-vfx.png"><img src="review-2026-11-07/water-splash-vfx.png" width="180" alt="water-splash-vfx"></a> |
| [waterfall-demo](../../../examples/waterfall-demo/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/waterfall-demo.png"><img src="review-2026-11-11/next10/waterfall-demo.png" width="180" alt="waterfall-demo"></a> |
| [weapon-lightning](../../../examples/weapon-lightning/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/weapon-lightning.png"><img src="review-2026-11-07/weapon-lightning.png" width="180" alt="weapon-lightning"></a> |
| [weather](../../../examples/weather/README.md) | 11-11 逐项改善并检查实帧 | <a href="review-2026-11-11/next10/weather.png"><img src="review-2026-11-11/next10/weather.png" width="180" alt="weather"></a> |
| [weather-daynight](../../../examples/weather-daynight/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/weather-daynight.png"><img src="review-2026-11-07/weather-daynight.png" width="180" alt="weather-daynight"></a> |
| [world-sweep-ballistics](../../../examples/world-sweep-ballistics/README.md) | 11-07；已抓帧 | <a href="review-2026-11-07/world-sweep-ballistics.png"><img src="review-2026-11-07/world-sweep-ballistics.png" width="180" alt="world-sweep-ballistics"></a> |


更多非独立入口、旧抓帧失败与复现说明见[历史完整报告](QUALITY_REVIEW-2026-11-07.md)。
