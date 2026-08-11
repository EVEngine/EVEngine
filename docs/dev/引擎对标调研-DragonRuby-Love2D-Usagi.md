# EVEngine 对标调研：DragonRuby / LÖVE / Usagi

> 调研日期：2026-08-11  
> 目的：对照三款同赛道（脚本驱动、代码优先、偏 2D/原型）引擎，梳理 EVEngine 的相对位置，以及仍缺的关键技术与产品能力。  
> 范围说明：以公开文档、官网与本仓库实现/设计文档为准；不评测运行时帧率数字。

---

## 1. 一句话定位

| 引擎 | 定位 | 语言 | 渲染 | 开源 |
|------|------|------|------|------|
| **EVEngine** | Love2D 式解析引擎 + 丰富 C++ 玩法模块；2D / 第三人称 3D / 2D·3D 混合；强调热重载与 DevTools/MCP | Squirrel + C++20 | Vulkan（macOS/iOS 经 MoltenVK） | 是（双许可） |
| **DragonRuby GTK** | 面向独立商业发行的 2D 工具包；「一次写出，全平台导出」 | Ruby（自研运行时） | SDL 系 2D（向 SDL3/后处理着色推进） | 闭源商业（有免费档） |
| **LÖVE (Love2D)** | 极简 2D 框架；把窗口/图/音/物理交给你自己搭架构 | Lua | OpenGL / OpenGL ES（12.0 规划 Metal） | 是（zlib） |
| **Usagi** | PICO-8 气质的像素原型引擎；约束换效率 | Lua 5.5 | 固定低分辨率 2D | 是 |

EVEngine 与三者同属「文件夹即游戏 + 脚本回调」范式，但目标更接近 **「Love2D 的入口体验 + 内建 RPG/地图/3D/调试栈」**，而不是 Usagi 的极简幻想主机，也不是 DragonRuby 的「发行优先」产品形态。

---

## 2. 各引擎能力速览

### 2.1 DragonRuby Game Toolkit

**强项（产品层）**

- **一键跨平台发布**：Windows / macOS / Linux（含 arm64、树莓派、Steam Deck）/ Web / 移动 / 主机路线；从任意宿主 OS 产出多端二进制。
- **发行集成**：itch.io、Steam、App Store / Play 等发布链路按许可档位提供。
- **开发循环**：热加载、内置 Quake 式控制台、状态回放（record/replay）、仿真加速/减速、布局安全区与横竖屏热切换。
- **输入统一**：键鼠/触控/手柄归一；「上次活跃输入设备」；UI 导航与触控·鼠标归一。
- **2D 性能与 API 克制**：大量精灵与碰撞基准；三角形/像素缓冲/声音合成/HD 与 All-Screen 模式。
- **生态**：200+ 持续维护的 sample；社区与文档成熟；商业游戏量级（itch 上数百款）。

**弱项 / 边界**

- 基本是 **纯 2D**；着色器、高级图形仍在演进，不是 3D/PBR 引擎。
- 闭源、分许可档；扩展靠 C Extension / 字节码混淆等付费能力。
- 不提供内建 RPG/Tilemap 编辑器级玩法框架（靠 sample 与自建）。

### 2.2 LÖVE (Love2D)

**强项**

- **API 表面积小、稳定、文档与社区极大**：`love.load/update/draw` + `love.graphics/physics/audio/...` 已成为行业「最小可行 2D 引擎」标准。
- **真正轻量**：启动快、依赖少；适合 jam、教学、自研框架底座。
- **平台**：官方桌面 + Android/iOS；Web 靠社区 love.js / WASM 方案（非一等官方导出）。
- **物理/音频/着色**：Box2D、OpenAL、GLSL 等「刚好够用」的底层模块齐全。
- **扩展文化**：一切高层（ECS、UI、场景、热重载）由社区库补齐，不绑架架构。

**弱项 / 边界**

- **无内建热重载、编辑器、回放、一键多端打包**（需自建或第三方）。
- **无 3D、无玩法框架**（RPG/对话/背包等一律自写）。
- 输入/安全区/多分辨率「发行细节」要自己处理。
- 12.0（Metal、更多图形能力等）尚未正式发布。

### 2.3 Usagi

**强项**

- **约束驱动的原型速度**：默认 320×180、单 `sprites.png`、16×16 格、小 API；减少资产管线决策。
- **一等热重载**：`usagi dev` 对代码/精灵/音效热更且尽量保状态。
- **一键导出**：Linux / macOS / Windows / **Web** 一次打好。
- **内建暂停菜单**：音量、全屏、键位/手柄重映射、设置持久化——「玩家侧基础 UX」开箱即用。
- **省心存档**：一张 Lua table 的 save/load。
- **轻量工具**：Jukebox、Tile Picker、Save Inspector；LSP stub / `USAGI.md` 随项目生成。

**弱项 / 边界**

- 刻意不做「大引擎」：无物理/3D/着色器/复杂 UI/网络等重模块。
- 适合像素原型与小品，不适合中大型 RPG 或 3D。

### 2.4 EVEngine（本仓库现状摘要）

**已具备（相对三者明显更重的一侧）**

- Love2D 式 `eve_init/update/render` + 模块化脚本 API；`eve` CLI（run/create/dev-server/package/…）。
- **Vulkan** 2D 声明式渲染 + **真 3D**（Assimp、PBR、Clustered Forward、IBL、CSM、体积光雾、NPR `stylize`）。
- 玩法模块：Tilemap（Tiled/多投影/寻路/FOV）、Box2D、粒子、ECS、Scene、UI、RPG、Inventory、Dialogue、Avatar、Procgen、Spatial、Database、Network、Thread/Promise、GPGPU/Tensor、IK/骨骼动画/Motion Matching。
- **调试栈**：热重载、暂停/快照、DAP、CallGraph 切片、MCP/AI 面板。
- 平台：Windows / Linux / macOS + Android / iOS 打包路径；原生插件 SDK。

**已知缺口（仓库文档已勾选为未完成或愿景未落地）**

- zip 挂载/完整打包体验、视频资源、统一资源 cache。
- 地图编辑器与 DevTools 反射属性面板、类扫描自动编辑 GUI。
- 2D 帧动画资源管线、存档序列化统一格式、插件依赖生命周期。
- **Web / 主机 / Steam Deck 一等导出**、发行商店集成。
- README 愿景中的 2D 流体、sprite-stacking、完整地图编辑器等尚未对齐实现清单。

---

## 3. 维度对比

图例：● 强 / 成熟 · ◐ 部分或社区方案 · ○ 弱或缺失 · — 非目标

| 维度 | EVEngine | DragonRuby | LÖVE | Usagi |
|------|----------|------------|------|-------|
| 上手曲线（空项目到动起来） | ◐（模块多、需 Vulkan） | ● | ● | ● |
| 热重载（代码+资产） | ● | ● | ○（需自建） | ● |
| 内建调试（控制台/回放/暂停） | ●（DAP/快照/MCP） | ●（控制台+回放） | ○ | ◐（pause/tools） |
| API 克制 / 心智负担 | ○（面很广） | ● | ● | ● |
| 2D 渲染完成度 | ● | ● | ● | ◐（像素约束） |
| 3D / 2D·3D 混合 | ● | — | — | — |
| 物理 | ● Box2D | ◐ 几何/碰撞 API | ● Box2D | — |
| Tilemap / 寻路 / FOV | ● | ○（自建/sample） | ○（自建） | ◐ 瓦片绘制 |
| 玩法框架（RPG/对话/背包） | ● | ○ | ○ | ○ |
| UI | ◐ ImGui 声明式 | ◐ 自绘+导航辅助 | ○（社区） | ◐ 内建 pause |
| 音频 | ● OpenAL | ● + synth | ● | ◐ SFX/音乐 |
| 着色器 / 后处理 | ● Vulkan/SPIR-V/NPR | ◐ 演进中 | ● GLSL | — |
| 打包桌面 | ◐（CLI/platform 模板） | ● 一键多端 | ◐ `.love` + 宿主 | ● 一键 |
| Web 导出 | ○ | ● | ◐ 社区 WASM | ● |
| 移动端 | ● Android/iOS | ●（高阶许可） | ● | — |
| 主机 / Steam Deck | ○ | ● | ○ | ○ |
| 商店/发行集成 | ○ | ● | ○ | ○ |
| 输入统一与安全区 | ◐ SDL 分模块 | ● | ◐ | ●（重映射） |
| 存档系统 | ○/◐（DB 有，游戏存档弱） | ◐ 自建 | ○ 自建 | ● 一等 |
| 示例与教材密度 | ◐ 少量 examples | ● 200+ | ● 海量社区 | ◐ 文档驱动 |
| 社区与发行案例 | ○ 早期 | ● | ● | ○ 新兴 |
| 开源可改引擎 | ● | ○ | ● | ● |
| AI/MCP 调试 | ● | ○ | ○ | ○ |
| 二进制体积 / 依赖 | ○ 重（Vulkan+多库） | ● 小 | ● 小 | ● 小 |

---

## 4. 差异本质（战略层）

```text
Usagi ──────────「约束极简，最快像素原型 + Web」
LÖVE  ──────────「最小框架，架构全交给你」
DragonRuby ─────「2D 商业发行体验最大化」
EVEngine ───────「解析式入口 + 重型玩法/3D/调试模块库」
```

1. **DragonRuby 赢在「产品化发行」**：导出、商店、输入归一、回放、sample 矩阵——EVEngine 目前仍是「引擎能力库」强于「发行操作系统」。
2. **LÖVE 赢在「标准与生态」**：API 稳定、教材多、第三方库多；EVEngine 灵感同源，但 Squirrel + 宽 API 面意味着迁移成本与学习曲线都更高。
3. **Usagi 赢在「默认决策」**：分辨率、精灵表、暂停菜单、存档、导出全部有默认答案；EVEngine 默认决策偏少，能力开关偏多。
4. **EVEngine 独有优势**：真 3D/PBR/NPR、RPG·对话·背包·Tilemap·Procgen、GPGPU/Tensor、DAP+MCP 调试——这些是三者基本不碰或很浅的地带。

---

## 5. EVEngine 缺少的技术与能力（按优先级）

下列「缺少」以**对标三者后的产品竞争力**为准，并交叉本仓库 `模块设计.md` 未完成项。P0 = 直接影响「能否像对标引擎一样被用来做完并发出一款游戏」；P1 = 显著影响 DX；P2 = 中长期差异化或补齐愿景。

### P0 — 发行与闭环（对标 DragonRuby / Usagi 的最大落差）

| # | 能力缺口 | 对标谁 | 说明与建议方向 |
|---|----------|--------|----------------|
| P0-1 | **一键多平台导出体验** | DR / Usagi | 今日有 `eve package` / platform 模板与移动构建，但缺少「一条命令打出 Win/Mac/Linux/Web 可分发物」的打磨与文档。需稳定产物布局、依赖剥离 DevTools、版本戳与冒烟脚本。 |
| P0-2 | **Web / WASM 导出** | DR / Usagi（LÖVE 有社区方案） | 完全空白。Vulkan→WebGPU 或备用 GLES/WASM 后端投入大，但是 jam/demo/itch 曝光的关键路径。可先做「降级 2D Web 后端」而不追求完整 3D。 |
| P0-3 | **zip / 归档挂载与发布包格式** | LÖVE `.love` / Usagi `.usagi` | `filesystem` 勾选未完成「打包/挂载 zip」。需要可分发的单一游戏包 + 只读挂载，对齐「拖文件夹/拖包即玩」。 |
| P0-4 | **玩家向设置与输入重映射** | Usagi pause menu / DR 输入归一 | 键位/手柄重映射、音量、全屏、上次输入设备——应有引擎级可选默认层，避免每个游戏重写。 |
| P0-5 | **游戏存档一等 API** | Usagi | 现有 SQLite/ORM 偏工具库；缺「序列化 `args.state` 式游戏状态 / 版本迁移 / 平台存档目录」的薄 API，并与 RPG/Inventory 序列化打通。 |

### P1 — 开发体验与 2D 基本功（对标 DR / LÖVE / Usagi）

| # | 能力缺口 | 对标谁 | 说明与建议方向 |
|---|----------|--------|----------------|
| P1-1 | **示例与教程矩阵** | DR 200+ samples / LÖVE 生态 | 现有 `basic/ecs/rpg/...` 偏少。应按「精灵移动、相机、物理、Tilemap、UI、打包、热重载陷阱」建可 CI 的 sample 矩阵，并保证版本不破。 |
| P1-2 | **语言服务 / 脚本 DX** | Usagi LSP stubs / LÖVE EmmyLua | Squirrel 侧缺官方 LSP stub、补全与文档悬浮；应随 SDK 生成 `eve` API 声明文件。 |
| P1-3 | **内建 REPL / 运行时控制台** | DragonRuby console | 已有 DAP/MCP/快照，但缺「游戏内用脚本语言即时求值」的控制台（对热改与教学极关键）。可与现有 `eve_eval`/MCP 共用后端。 |
| P1-4 | **录制与回放** | DragonRuby replay | 输入流 + 固定步仿真回放，用于回归难复现 bug 与宣传录屏。可先做 deterministic fixed-update 模式。 |
| P1-5 | **2D 精灵帧动画与图集管线** | LÖVE 社区惯例 / 模块设计未勾完 | `animation` 缺 sprite sheet 帧动画；图集、pivot、切片命名约定应一等支持。 |
| P1-6 | **低分辨率 / 像素完美 / 横竖屏与安全区** | DR All-Screen & fantasy console / Usagi 320×180 | 需官方「integer scale / pixel-perfect / letterbox / notch safe area / portrait」策略，避免每款游戏手搓。 |
| P1-7 | **统一资源 Cache 与热重载语义** | 三家都更「朴素可预期」 | 模块设计中统一 cache/引用计数/热重载标记未完成；应明确软重载下全局表约定与资源失效规则（文档+运行时断言）。 |
| P1-8 | **手柄脚本绑定完整度与输入抽象** | DR / Usagi | 底层 joystick 有，但脚本侧轴/键完整性与「动作映射层」仍弱于对标。 |
| P1-9 | **地图/属性编辑器与类扫描 GUI** | 自身愿景 / Usagi tools | README 承诺可扩展地图编辑器与类扫描自动 GUI；DevTools 反射面板仍为未完成。这是相对 LÖVE「纯代码」的差异化，必须落地才算兑现。 |

### P2 — 生态、平台与愿景对齐

| # | 能力缺口 | 对标谁 | 说明与建议方向 |
|---|----------|--------|----------------|
| P2-1 | **Steam Deck / Linux arm64 / 主机** | DragonRuby | 中长期商业发行需要；可先 Deck/Proton 验证再谈主机认证。 |
| P2-2 | **商店发布辅助** | DragonRuby | itch/Steam 上传脚本、构建元数据、成就/云存档接口（可后置）。 |
| P2-3 | **视频播放** | 模块设计 `video` | 过场与 VN 常用；对标引擎亦常缺，但对 EV 的 Dialogue/Avatar 路线有协同。 |
| P2-4 | **插件依赖解析与热替换** | DR C-ext / 自身 plugins 设计 | 动态库能载，缺依赖声明/生命周期/与 module 注册表对接。 |
| P2-5 | **音频合成 / 像素缓冲类「幻想主机」玩具** | DR synth & pixel buffers / Usagi 调色板 | 非必须，但对 jam 友好与教学演示有帮助。 |
| P2-6 | **2D 流体、sprite-stacking** | 自身 README 愿景 | 对标三家皆无；属于 EV 差异化愿景，需从「宣传项」推进到模块勾选。 |
| P2-7 | **网络异步与事件整合** | LÖVE 社区联网库 / DR web server | TCP/HTTP 已有，缺与 `event` 的异步超时模型；多人不是 P0，但联机原型会碰到。 |
| P2-8 | **二进制体积与「无 Vulkan SDK 可跑」分发** | 三家均更轻 | 降低新人门槛：预编译 SDK 开箱、SwiftShader/软件路径仅开发用、运行时重分发说明。 |
| P2-9 | **API 分层 / 精简配置文件** | Usagi「小 API」/ LÖVE 模块感 | 用 `config.nut` profile（`jam` / `2d` / `rpg` / `3d`）裁剪默认加载模块与文档入口，降低「能力太多反而不知从何看」的问题。 |

### 已领先、应保持的能力（避免在对标中丢掉）

- Vulkan 真 3D + NPR + 2D·3D 混合  
- RPG / Inventory / Dialogue / Avatar / Procgen / Tilemap+FOV 玩法栈  
- DAP + Snapshot + CallGraph + **MCP/AI** 调试闭环  
- ECS 声明式渲染与 Scene/UI 同构 reconcile  
- 桌面 + Android/iOS 同源模块策略  

这些是 EVEngine 的护城河；补齐 P0/P1 时不应以拆掉上述深度为代价，而应做 **「深能力可选、浅路径默认」**。

---

## 6. 建议路线图（能力导向，非排期）

### 阶段 A — 「能发出去的 Love2D 超集」

聚焦：P0-1、P0-3、P0-4、P0-5、P1-6、P1-7、P1-8、P1-1（核心 sample）。  
目标：用 EVEngine 做完一款纯 2D 小品，并在 Win/Linux/macOS 上以单一命令产出可分发构建；输入与存档有默认方案。

### 阶段 B — 「对标 Usagi/DR 的传播面」

聚焦：P0-2（Web 降级后端）、P1-2、P1-3、P1-4、P1-5、P1-9。  
目标：itch 可在线试玩；新人有 LSP/控制台；编辑器愿景有最小可用地图/属性面板。

### 阶段 C — 「商业发行与愿景模块」

聚焦：P2-1、P2-2、P2-3、P2-4、P2-6、P2-8、P2-9。  
目标：Deck/移动商店流程、插件生态、README 愿景项落地、API 分层降低复杂度。

---

## 7. 结论

- **对 Usagi**：EV 能力远超其约束模型，但在「默认分辨率策略、暂停菜单、一键含 Web 的导出、省心存档」上全面落后——这是**产品完成度**差距，不是渲染差距。  
- **对 LÖVE**：EV 已是「框架 + 内容模块」超集，但在**生态、文档密度、API 克制、包格式习惯**上未达到 LÖVE 的「行业默认底座」地位；热重载/调试反而强于原版 LÖVE。  
- **对 DragonRuby**：最大短板是 **跨平台发行操作系统**（Web/多端一键、商店、输入归一、回放、海量 sample），而非玩法或 3D 深度；DR 几乎不做的 3D/RPG/MCP 正是 EV 应继续加大的差异化。  

**总判断**：EVEngine 当前像「研究型/生产型模块丰富的 Love2D 后继者」，下一跳应优先补齐 **导出·存档·输入默认层·Web·示例矩阵·脚本 DX**，把已有深水模块用一条浅学习路径托出水面，而不是继续单向堆叠与对标引擎无关的新子系统。

---

## 8. 参考链接

- DragonRuby：<https://dragonruby.org/> · <https://dragonruby.itch.io/dragonruby-gtk>  
- LÖVE：<https://www.love2d.org/> · Wiki 12.0 草案 · 社区 love.js / love-web-builder  
- Usagi：<https://usagiengine.com/> · <https://codeberg.org/brettchalupa/usagi>  
- 本仓库：`Readme.md`、`docs/dev/模块设计.md`、`docs/dev/命令行设计.md`、`docs/usr/`
