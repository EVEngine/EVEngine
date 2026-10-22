# 自动桥梁生成器：十种桥型

同一套 `BridgeParameters → planBridge → BridgePlan → bakeBridge → MeshBuild` API 生成十种桥型。
`mesh.autoBridge` 把同一流程接入现有 EveScript `procgen.buildMesh`，通过 `bridgeType` 切换结构。

本目录包含十张 AI 参考图、十套共用生成器生成的网格和当前引擎的实渲染截图。
参考图提示词见 [references/prompts.json](references/prompts.json)。示例直接调用 `mesh.autoBridge`，
通过 `gfx.saveFramePng` 保存截图；OBJ 只是额外导出，不参与示例生成路径。

已按参考图对齐桥型、相对尺度、构件布置和材质类别：基础/盖梁、桥台/翼墙、工字钢截面、
桁架连接板与螺栓、砌石缝、木桥分板，以及配套河床、水面和散石。参考图是概念设计，
对齐目标是可重复生成的视觉结构与构件特征，不是像素级重建，也不提供工程承载验算。

| 键 | bridgeType | 主体生成逻辑 | 参考图 | 引擎截图 |
|---|---|---|---|---|
| 1 | `beam` | 桥面 + 纵梁 + 分跨桥墩 | [梁桥](references/beam.png) | [实际网格](renders/beam.png) |
| 2 | `slab` | 单跨板式桥面 + 桥台 | [板桥](references/slab.png) | [实际网格](renders/slab.png) |
| 3 | `arch` | 桥下抛物线拱肋 + 拱上立柱 | [上承式拱桥](references/arch.png) | [实际网格](renders/arch.png) |
| 4 | `tiedArch` | 桥上抛物线拱肋 + 吊杆 + 系杆 | [系杆拱桥](references/tiedArch.png) | [实际网格](renders/tiedArch.png) |
| 5 | `throughTruss` | 桥面上方平行弦杆 + 交替斜杆 + 横联 | [下承式桁架桥](references/throughTruss.png) | [实际网格](renders/throughTruss.png) |
| 6 | `deckTruss` | 桥面下方平行弦杆 + 交替斜杆 | [上承式桁架桥](references/deckTruss.png) | [实际网格](renders/deckTruss.png) |
| 7 | `suspension` | 双塔 + 抛物线主缆 + 垂直吊索 + 边跨索 | [悬索桥](references/suspension.png) | [实际网格](renders/suspension.png) |
| 8 | `cableStayed` | A 形双塔 + 扇形直索 | [斜拉桥](references/cableStayed.png) | [实际网格](renders/cableStayed.png) |
| 9 | `cantilever` | 桥墩处加深的变高度桁架 + 中间浅桁架 | [悬臂桁架桥](references/cantilever.png) | [实际网格](renders/cantilever.png) |
| 0 | `trestle` | 分块桥板 + 密集支架 + 可关闭交叉撑 | [木栈桥](references/trestle.png) | [实际网格](renders/trestle.png) |

```sh
make run/macosx-debug GAME=examples/automatic-bridge
# Linux / Windows: replace macosx-debug with linux-debug / win32-debug
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh automatic-bridge
```

按 **1–9 / 0** 切换桥型；按 **G** 依次生成十种桥型并用引擎自身截图接口保存 PNG。
示例只在切换时生成，正常帧不重新构建。`main.nut` 展示脚本调用；[export_gallery.cpp](export_gallery.cpp)
展示无图形依赖的 C++ 调用与 OBJ 导出，作为链接 EVEngine procgen/common 后的独立程序使用，参数为输出目录。

## 共同调用方式

```cpp
using namespace eve::procgen::road;
auto preset = bridgePreset(BridgeKind::CableStayed);
if (!preset.ok()) { /* 处理 preset.status() */ return; }
auto parameters = std::move(preset).takeValue();
parameters.start = {-56.f, 10.f, 0.f};
parameters.end = {56.f, 10.f, 0.f};
parameters.width = 9.f;
parameters.towerHeight = 28.f;
auto planned = planBridge(parameters);
if (!planned.ok()) { /* 处理 planned.status() */ return; }
auto plan = std::move(planned).takeValue();
// 可增删构件、改变端点、尺寸、截面或 widthAxis，调整生成逻辑。
auto mesh = bakeBridge(plan);
if (!mesh.ok()) { /* 处理 mesh.status() */ return; }
// mesh.value() 独占 CPU 网格；后续单独上传、导出或缓存。
```

```squirrel
local result = procgen.newParams();
if (!result.ok) throw result.status.summary;
local params = result.value;
params.setString("bridgeType", "cableStayed");
params.setFloat("deckWidth", 7.0);
params.setFloat("towerHeight", 16.0);
local generated = procgen.buildMesh("mesh.autoBridge", params);
if (!generated.ok) throw generated.status.summary;
local cpu = generated.value;
// 遍历 cpu.copyGroup(i)，分别设置材质，再 procgen.uploadMesh(group, gfx)。
```

## 可调参数

起终点是世界坐标，单位为米，Y 向上；桥面沿两点直线插值，支持斜向和坡度。

默认值由 `bridgeType` 对应的 `bridgePreset` 统一提供；显式参数覆盖预设。
`generated/presets.json` 是导出时的精确参数报告，不参与运行时读取。

| 参数 | 作用 |
|---|---|
| startX/Y/Z、endX/Y/Z、groundY | 起终点与基础平面 |
| deckWidth、sidewalkWidth、deckThickness、girderHeight | 行车面、侧带、桥板与纵梁 |
| spans、panels、curveSegments、cableSides | 跨数、结构分格、曲线与圆索细分 |
| archRise、archRibWidth/Depth | 拱高、拱肋截面 |
| trussHeight、memberWidth/Depth、portalInset、cantileverCenterRatio | 桁架高度、截面、斜端门架、悬臂中央浅段比例 |
| towerHeight、towerInset、towerLegWidth、towerTopWidthRatio | 塔高、塔位、塔腿、塔顶收分 |
| cableRadius、cableSag | 缆索半径、悬索相对垂度 |
| pierWidth/Depth、pierCapHeight、foundationHeight/Scale | 墩身、盖梁、扩大基础 |
| abutmentDepth、wingWallLength | 桥台纵深与翼墙长度 |
| railingType、railingHeight/Width、railingBars | metal/concrete/masonry/timber 护栏及横杆 |
| plankCount、uvMeters | 木桥分板、纹理每次重复的米数 |
| piers、railings、markings、crossBracing、connectionDetails | 支墩、护栏、黄白标线、横撑、连接板螺栓开关 |

`spans` 用于梁桥、拱桥、上承式桁架桥和栈桥；索桥和悬臂桥的主墩由 `towerInset` 决定。
`BridgePlan.members` 可在烘焙前增删，支持 Box/Round/IBeam/TaperedBox 截面、材质和独立截面方向。
分组名使用 `part@material`，让同一套渲染代码自动分配材质。

## API 契约与验证边界

- CPU 核心没有 graphics/map/scene 依赖，不查询可选 provider，无回调、锁、ECS、事件或共享可变状态。
  返回的计划和网格各自独占数据；调用时只借用输入，可在不同线程处理独立输入。
- `planBridge` 是规划阶段，`bakeBridge` 是昂贵烘焙阶段；网格上传独立。两者均为 `[[nodiscard]] Result`，
  失败不发布部分网格。构件计划是临时内存结构，没有引入持久格式或新的恢复协议。
- 连接机制是一次性的强类型值传递；脚本面复用现有配方注册及 Result 投影，工具面复用配方 schema。
  没有新增模块或 capability/ECS/事件协议，也没有建立平行 JSON 架构目录。
- 输入的水平跨度为 12–10000 米；拱、桁架的下缘必须高于基础平面。有限值、类型、枚举、尺寸、计数和输出预算均验证。
  最大 16384 构件、400 万个顶点与索引元素。对 `BridgePlan` 的修改在烘焙时再次验证。
- 同输入拓扑确定，无时间或 RNG 依赖；跨平台浮点位置比较使用相对容差 1e-5。
  自定义 `widthAxis` 控制截面朝向，避免竖直桥台的“横宽变纵宽”问题。
- `test/unit/procgen/procgen_auto_bridge.cpp` 覆盖十型、有效索引、非退化三角形、绕序/法线、桥台方向、重复生成、坡桥、
  构件编辑、无效输入、配方参数及开关。独立 CPU 验证程序编译了实际道路核心源码并运行这些用例。
- 当前工作树 macOS Debug 引擎已构建，十种配方完成直接生成和引擎截图。
  六个专用 CPU 用例验证全部桥型、确定性、斜桥、预设一致性、原子失败及自定义截面。
  具体数量、源码和产物散列见 `evidence.json`。
- 示例水面使用同一 PBR 网格渲染流程；不会在后处理之后追加原生 Water 绘制。
  未实现碰撞、导航、实时地形采样、力学计算；没有修改渲染器生命周期。

完整提示词与参考图片保留在 `references/`，导出网格在 `generated/`，引擎截图在 `renders/`。
