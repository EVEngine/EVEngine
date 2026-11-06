# 实体四视图与动画采样 MCP

日期：2026-10-25。状态：设计及第一阶段实现，尚未完成实体渲染与动画采样。

## 用户目标

Agent 指定人物、模型或具体实体后，一次看到正面、右侧、顶面和当前游戏摄像机的四格图；
指定动画与采样频率后，看到带时间标记的动作序列。输出必须来自引擎渲染，并能追溯实体、姿态、相机和时间。

## 已查明的边界

- `IRenderCapture::capturePngDataUrl` 读取上一张已呈现帧，切换相机后立即读取不等于新视角。
- `IRenderFrameCapture` 已有单次 GPU 获取、不可变 CPU leases、generation、超时和释放机制。
- `GraphicsOffscreenPreviewService` 是 Canvas 桥接，不能据此宣称其覆盖场景 3D、蒙皮、阴影及后处理。
- `AnimClip`、`AnimPose`、`AnimSkin` 提供动画基础，但没有证明任意玩法实体都有可独立重建的动画预览绑定。
- 通用截图不能证明同一模拟时刻，渲染 frameId 也不能替代 simulation tick。

## 目标工具（以下名字为设计，尚未注册）

`eve_entity_views` 接受带 domain、id、generation 的目标引用，输出 jobToken。
以现有领域 resolver 解析到渲染对象；不得新增统一 GameObject 或第二份实体注册表。
支持世界轴取景；实体自身朝向仅在 provider 能报告可靠 basis 时启用，否则明确 Unsupported。
前三格为正交投影，同一单位/像素、同一世界中心；第四格复制当前游戏相机的完整投影和姿态。
第四格不为目标自动重构图，目标在画面外时必须如实报告。
世界轴模式明确使用 +Z 侧观察的 front、+X 侧观察的 right、+Y 侧观察的 top；
front/right 的 up 为 +Y，top 的 up 为 -Z，避免顶视 look-at 与 up 共线。
这些标签表示观察方向，不推断模型作者的正面；近远裁剪面由包围盒和留白计算。

`eve_animation_samples` 接受目标引用、clip 身份、startSeconds、endSeconds、fps 和 view。
第一阶段支持独立 clip pose 采样，明确区别于真实游戏状态机、混合树、IK、物理和脚本驱动后的最终动作。
采样时刻由整数样本序号计算 `start + i / fps`，只纳入不晚于 end 的样本；
不通过累计浮点 dt 或真实时间 sleep 计时。单张静态姿态允许 start=end。
拒绝非有限值、倒置范围、fps<=0 及超过预算的样本数。循环策略必须显式指定。
第一版每任务最多 16 个样本；四视图序列按每页四个时刻分页，每张图最多 16 格。

两个工具都走 request → status → image/release，不在请求处理栈等待 GPU。
状态包括 queued、sampling、rendering、readbackPending、ready、failed、cancelled、expired。
结果发布必须原子化；失败不返回冒充完整成功的部分拼图。

## 同帧、时间与现场保护

四视图从一次冻结的 render packet/pose snapshot 生成，四次渲染之间不推进模拟。
离屏相机与输出目标由检查任务独占，不修改活动游戏相机或真实实体可见性。
动画使用独立姿态、独立 GPU palette 和预览资源，不能临时写入真实角色的 mesh palette。
不触发真实 clip event、root-motion 应用、游戏脚本、碰撞、声音和 RNG。
资源复制或独立渲染不可用时返回 Unsupported，不能隐式降级为推进真实游戏。

序列先计算所有采样姿态的联合包围盒，再固定相机与缩放。
world-root 模式保留根位移；in-place 模式必须明确标注去掉了什么，并记录原始根变换。
四视图截图应包含周围环境的能力与 isolated 目标预览分别报告，不宣称离屏预览等于最终游戏画面。
GPU 后端之间不要求 PNG 字节相同；测试比较投影、时间及几何容差，像素比较使用指定场景的阈值。

## 输出契约

图片是 MCP image item，附带有 schemaId/version 的 JSON。输入未知字段拒绝。
初版不接受旧版导入；将来新增不兼容格式时提供显式迁移，不把任务 token 写入存档。
每格包含 index、rectangle、target、pose revision、simulation tick 或 clip time、view/projection、
朝向基准、包围盒、root transform、backend 与 provider generation。
图片直接标注视角/时间和坐标轴；元数据同时保留精确值。
过期、目标删除、资源热更或 provider 重建应取消未完成任务并报告 stale；
完成的 CPU 图像独立于实体生命周期，但 metadata 必须标明捕获的旧 revision。
上限：单页 2048×2048，16 格，默认 base64 2 MiB、最大 16 MiB；不足时明确失败，不静默丢帧。

## 第一阶段：已接通的通用拼图

`eve_frame_contact_sheet` 接受 `tokens`（1–16 个 ready capture）、`columns`（默认最多 2）、
`cellWidth`/`cellHeight`（默认 512）、`maxBytes`。只使用 final-color RGBA/BGRA CPU 数据。
保持纵横比、不放大小图、黑边填充、按行排列；返回 jobToken，使用 `eve_frame_capture_status` 读取 PNG。
每格返回 token、capture frameId、producerFrameId 和矩形；
`sameSimulationInstantVerified=false` 明确表示此工具不采集视角、不验证同一模拟时刻。
这一阶段尚无图片内标签，不能将通用拼图当成完整实体/动画工具。

复用已有 FrameCapture family、PNG 编码器和 worker job 管理，最多两项图像 worker。
store 的 ready 快照数量上限由 2 扩为 16，原有 256 MiB 总内存预算、单次 GPU 获取和 60 秒 TTL 保留。
释放任意输入 token 取消关联拼图；重启/provider generation 改变使工作失效。
worker 只持有 immutable shared CPU leases，不保存 provider、实体或 GPU 裸指针。
不新增模块、脚本 API、事件或 ECS System。

## 后续实施与验收

1. 验证本阶段：四种纯色的布局与通道顺序、纵横比、内存/编码预算、缺失 provider、
   非 ready/过期 token、释放任意输入、generation 失效、schema 路由。
2. 建立 graphics 内独立 3D render packet 路径，先用带方向标记的非对称模型验证正/侧/顶投影，
   相机视图与同姿态参考图一致；失败注入后相机、实体、目标及 GPU 资源恢复/释放。
3. 接入领域 resolver 与 AnimClip 独立采样，测试已知关键帧、根位移、跨循环边界、
   目标/资源先销毁与任务先销毁、热重载、固定联合包围盒、首尾时刻和浮点边界。
4. 用真实蒙皮模型完成引擎截图验收，再扩展实时状态机、IK、布料、粒子及其它领域 provider。
   必须有 provider present/absent、相关裁剪配置以及 Windows SHARED/OBJECT 链接证据。

## 架构规则适用记录

这是新增/改动范围（机制规范 §8.3），不是全仓存量整改。
§1：未新增模块或裁剪主张；当前无跨域重构，未来独立 provider 仍须裁剪构建证据。
§2：复用 FrameCapture 唯一快照 owner，不创建注册表或平行 PNG 实现。
§3：本次改变工具协议面；capability in 仍为 IRenderFrameCapture；无新 capability out、ECS/Link、事件或脚本面。
以 provider 源码及 EVE_MCP_TOOL_FAMILY 为声明真源，不新增 JSON catalogue。
§4：单 provider、低频显式请求、已知 consumer、原子输出，沿用 capability 请求/不可变快照；无每帧查找。
§5：昂贵编码返回 jobToken，内部 Result + nodiscard + @cost；不以默认参数区分成本档位。
所有权、线程和失败由 CPU lease、主线程 admission、worker Result 定义。没有规则豁免。

## 本阶段验证记录

- 主工程 `make build/win32-debug JOBS=4` 已编译新实现并链接 `EVFoundation.dll`（MSVC /W4 /WX）。
  获得所需 DLL 后停止了其余全引擎构建；不宣称整个 SHARED 引擎或完整 OBJECT 引擎已完成。
- 独立最小 CMake 工程直接编译本次 CPU 实现与四个测试：4/4，通过 101 条断言。
- 同一最小工程链接主工程实际 `EVFoundation.dll`，运行本次完整测试源及现有捕获 family
  schema/provider 测试：6/6，通过 142 条断言。没有用 mock 替换 MCP handler。
  FrameCaptureStore 用合成像素验证协议与拼接；这不是 GPU 三视图或真实动画的视觉证据。
- 测试清单、用例命名、断言级别、依赖分层与改动行架构检查通过。
- `make check` 已运行至脚本测试：529 个测试，15 failures、2 errors、5 skipped，门禁未通过。
  `build/contact-sheet-check.log` 中失败集中在第三方缓存 shell 测试的 Windows 路径处理、
  临时目录清理及 CMake 探针找不到 nmake/C++ 编译器。前面的格式、分层、bindings、
  测试清单、架构、ruff 等检查通过；没有修改这些脚本或将失败改为跳过。
- 在 MSVC 开发环境复核后，Dawn 配置组 9/9 通过；SDK 架构组仍有 1 项失败，
  其生成安装脚本将 Windows 路径中的 `\U` 当作非法转义（另有 1 项已有平台跳过）。
  因此完整门禁仍未通过，不能用定向通过替代门禁结果。
- 构建按仓库配置自动应用了已有 ECS/zeroerr 子模块补丁；这些工作树状态不是本功能的新补丁。
- 尚需后续完成完整双链接路线、跨平台 CI、实体/动画 provider 及引擎截图验收。

## 第二阶段：确定性取景与采样核心

新增 `src/modules/graphics/InspectionPlan.h/.cpp`，作为 graphics 内部的纯值规划函数，
尚未注册 `eve_entity_views` 或 `eve_animation_samples`，也没有宣称已生成实体截图。

- `planInspectionViews` 接受冻结姿态的世界 AABB（动画可传所有姿态的联合 AABB），
  生成 front/right/top 三个同中心、同 world-units-per-pixel 的正交相机。
  使用现有 `ClipSpace.h` 的 RH/ZO/Y-down 投影约定，不新增另一套后端投影公式。
  支持非方形视口，验证所有八个角在实际 float 矩阵下仍位于画幅和深度范围内。
  第四个相机逐值复制原 view/projection/native viewport；后续渲染必须保留其纵横比，
  由 contact sheet 等比缩放补黑边，不能套用诊断视口重新计算游戏投影。
- `planInspectionSamples` 根据 `start + index / fps` 生成最多 16 个时刻；超额明确失败，
  不截断。Once 保留 clip 末帧，Repeat 将恰好 duration 的时刻映射到零。
  循环余数在容差内归零（容差不超过周期的百万分之一），避免小数周期误落末帧。
  时间单位为秒；end 包含在区间内，但不强插不在采样格点上的末帧。
  距 end 在四个按时间量级缩放的 double epsilon 内时吸附 end，避免 .1 + .2 边界误差。
  该函数只生成时刻，不触发动画事件、游戏推进或根位移写回。
- 输入值只借用到调用返回，结果完全自持；无 ECS/资源指针、callback、全局可变状态或
  跨帧生命周期。函数使用 checked Result/nodiscard 和 BACKENDS 导出，工作量为固定三个
  相机/最多 16 时刻，因此无需昂贵操作 token；GPU 和 PNG 作业仍必须走两步任务。
- 本阶段没有新增模块、capability、脚本或工具协议面，没有引入 ECS System、持久格式、
  entity registry 或裁剪主张。无架构规则豁免。

调查确认 `begin3DFrameToCanvas` 会改动共享的 mesh draw/palette ring、shadow pending 和
frame 状态，`end3DFrameToCanvas` 会同步等待 GPU。直接串行调用四次不符合本设计的独立
快照/无干扰要求。`captureVisibilityPixels` 有私有目标，但只输出实体 ID/depth，并同步
等待，不能冒充材质及蒙皮截图。下一步仍需真正的冻结 render packet、私有 GPU palette
及异步完成路径，再接 generation-qualified resolver；本阶段没有偷偷借用这些不等价路径。

验证：`test/unit/graphics/graphics_pipeline_state_tests.cpp` 的 3 个 CPU 用例通过 161 条断言，
覆盖非对称平移 AABB、三视图方向与像素比例、宽窄视口全角点、平面/极小 bounds、非法输入、
原相机精确保留、小数时间末点、循环边界、单帧、16 帧上限及极低 fps。
通过仓库 MSVC wrapper 编译最小探针，/W4 /WX；不是 GPU 视觉验收或完整引擎链接证据。

最终实现也已在主工程 `EVGraphics` 的实际目标编译通过（Windows Debug，严格警告开启）。
本轮在 MSVC 环境运行 `make check`：格式、分层、绑定、测试清单、架构、ruff 均通过；
脚本测试运行 529 项，3 failures、1 error、5 skipped，完整门禁仍未通过。
失败均在 `test_ci_third_party_cache`，日志显示 Bash 无法打开反斜杠 Windows 脚本路径，
以及缓存恢复找不到 target install prefix。记录见 `build/inspection-check.log`；没有跳过或
修改这些测试。此前编译器环境引起的失败在此次正确加载 MSVC 的运行中未再出现。

## 第三阶段：可运行的原生实体四视图（2026-10-26）

已接通 `eve_entity_views` / `eve_entity_views_status` / `eve_entity_views_release`，当前明确为
`mode=geometry` 的孤立实体世界法线诊断图。没有把诊断渲染冒充最终材质画面，也没有声称
GPU skinning 或动画采样已支持。使用方法和所有限制见 `docs/dev/mcp-frame-capture.md`。

实现改变了前一阶段计划中的获取顺序：在 admission 的 owner-thread 调用中同步复制有界的
CPU 可见顶点/索引和相机值，然后排队执行昂贵 GPU raster/readback；返回 token 后绝不再
访问源实体。四视图一次绘入私有 atlas，复用现有 Primitive3D shader 和 FrameReadback 核心，
以及 FrameCapturePreview PNG encoder，不复制 PNG 算法、不借用游戏 render ring。
这样源实体先销毁与任务先取消两种顺序都有明确行为；ECS table 切换也不会将旧 request
重新解析成新 table 的同号实体。新建的 GPU buffer 用 gpuIdle ownership token 只初始化
自身新分配内存，不表示/触发全设备 idle。正常提交/轮询路径没有 waitIdle/waitForFences。

### module-interface 与架构记录

- 模块没有新增；provider 仍属于 graphics/Vulkan，消费者属于 devtools。
- capability out：common 消费方边界 `IEntityInspection`；graphics 在初始化后的首次 render poll
  注册，后端重建/退出在既有队列退休路径撤销；MCP 每次查询。没有 runtime getModInst 或
  每帧字符串 capability 查询，热路径只访问已持有的 service。
- capability in：devtools 查询可选 IEntityInspection；无 provider 时明确错误。
- ECS/Link：只借用当前 Renderable3D generation-qualified handle 进行一次冻结；不新增实体根、
  组件或 System，不保存跨帧 ECS/资源指针。source geometry 是游戏原来的 owner；快照是任务拥有的值。
- events/script：无新增。tool：独立 EntityInspection family，源码旁 `EVE_MCP_TOOL_FAMILY` 声明。
- 跨进程响应：eve.entityViews/v1，拒绝未知输入字段；没有持久格式/迁移。
- 机制选择：单 provider、低频显式操作、已知 MCP consumer、原子发布，采用 capability + token。
  frame capture phase/expiry/CPU lease 仍由 FrameCaptureStore 唯一管理，GPU owner 只管理资源退休。
- checked Result/nodiscard、昂贵请求 @cost、线程/所有权注释及 BACKENDS/FOUNDATION 边界适用；
  无规则豁免。没有宣称完整 L1-L4 trim 验证，provider absent 测试不等同裁剪构建证据。

### 本轮验证

- 主工程 Windows Debug 严格警告构建并链接 EVFoundation / EVPlatform / EVBackends。
  对 Vulkan Graphics.h 的直接消费 TU 更新时间强制重编译，避免 unscanned 依赖的旧布局。
- 新 provider/MCP 代码与集成测试实际链接上述 DLL；原有拼图 + 新 MCP 组合测试共 7 用例、
  153 断言通过，包括 absent provider、未知字段、PNG worker、release 后禁止发布缓存图。
- 真实 Vulkan 生命周期测试 1 用例、30 断言通过：四个 panel 都有可见像素，申请后、GPU
  提交前销毁实体和 Mesh 仍能完成；改变相机不改变冻结的四视图；release 后 CPU lease 保持
  有效；取消任务不发布 ready。启用 EVENGINE_VULKAN_VALIDATION=1，日志无 validation error，
  启动仍有旧驱动 layer API 版本及未消费顶点 attribute 的 warning。
- 独立临时 driver 通过实际 MCP handler 请求/轮询取得 PNG，并退出 0；
  `build/entity-inspection.png` / `.json` 为引擎拥有的真实 GPU 回读和元数据，已目视检查。
  左上正视、右上侧视、左下顶视、右下原相机；不是桌面/Xvfb 截图。
- 尚未完成整个 engine exe/OBJECT 路线、跨平台/裁剪 CI、GPU 蒙皮/动画 provider、真实角色动画验收。

- 本轮 `make check` 已完成：格式、分层、bindings、清单、架构、ruff 通过；529 个脚本测试
  仍有 3 failures、1 error、5 skipped，均为 test_ci_third_party_cache 的 Windows/Bash
  路径或 cache install prefix 问题。完整日志 `build/entity-inspection-check.log`。
  未更改这些测试、未增加豁免，完整门禁不标绿。

## 第四阶段：当前蒙皮姿态冻结（2026-10-26）

实体四视图现在接受内置 GPU skinning 的当前 Mesh 调色板。冻结过程复制调色板和
顶点，使用与内置 shader 一致的四权重归一化、加权矩阵、位置及法线变换，再计算
变形后的世界 AABB。非 LOD Renderable3D 固定使用四权重，不读取其他 draw 遗留的
backend influence limit。所有后续 GPU 工作仍只消费任务私有几何。

限制是显式的：每个 palette 最多 65,536 个仿射矩阵；所有顶点的四个 joint 必须有效
（即使权重为零），权重必须有限、非负且总和大于 1e-8。空 palette、非仿射矩阵、
无效权重/索引失败，不退回 bind pose。这不是指定 AnimClip/时间的独立采样，亦不
推进动画、修改共享 palette 或重新执行 IK；动画序列工具仍未实现。

没有新增公共 API、ECS 状态或跨模块依赖；沿用既有 capability、checked Result、
owner-thread 值快照、异步 token 和私有 GPU 生命周期。没有架构规则豁免。

验证：Windows Debug /W4 /WX 编译并链接 EVFoundation、EVBackends；新增真实 Vulkan
测试通过 33 条断言，双骨骼旋转/平移加权的四视图与手算静态几何逐像素一致，覆盖
非法输入，以及请求后改写 palette、销毁 entity/Mesh。原静态取消测试 30 条断言和
7 个 MCP/拼图测试 153 条断言也通过，总计 9 用例、216 断言。GPU 测试启用 validation，
无 error；仍有启动 layer 版本和未使用 vertex attribute 警告。完整 engine exe、OBJECT
构建和跨平台/裁剪 CI 尚未完成。

本轮 `make check` 已完成：格式、分层、绑定、测试清单、架构与 ruff 通过；529 个脚本
测试仍有 3 failures、1 error、5 skipped，全部失败来自既有 test_ci_third_party_cache
的 Windows/Bash 路径或 target install prefix 问题。日志为
`build/entity-inspection-skin-check.log`，未修改/跳过这些测试，完整门禁未通过。

## 第五阶段：独立片段采样与原生渲染组合（2026-10-26）

`animation/AnimInspection.h` 的 `AnimInspectionBuilder` 接收显式时间表，构建独立的
skin-joint 顺序矩阵序列。调用方复用 `planInspectionSamples(start,end,fps,duration,loop)`
生成时间表。builder 使用 AnimClip 原有 clamped sampler、AnimPose 世界变换和
`boneWorld * inverseBind`，不复制插值算法，不改 clip.loop、播放器时间或 AnimSkin 缓存。
循环片段的 Once 末点可以取 duration 的最后姿态；Repeat 由时间规划器显式映射到零。
根运动留在姿态里；不执行事件、动画图混合、IK 或程序化求解器。

每次 1..16 样本、1..4096 skeleton bones / skin joints，时间有限且 timeline 严格递增；
clip time 必须位于 [0,duration]，不截断。采样采用引擎原生 float 秒精度，元数据保留请求的
双精度时间；跨平台浮点结果按容差比较。skin 的 joint index 和 bone name 必须匹配传入
skeleton，片段仍须由调用方保证按该 skeleton 的 track indices 创作，不自动重定向。
返回值全部自有，源对象销毁后仍有效；所有输入只在同步 owner-thread build 内借用。

`EntityViewRequest.skinPalettes` 是原生可选的逐 part 姿态输入。非空时必须覆盖每个 part，
且每个 mesh 都有 GPU skinning；冻结时按所给 palette 计算，不写入 Mesh。快照不返回这份
输入数组。现有 MCP JSON schema 保持不变，不接收大块 palette JSON。

### module-interface 与适用规则

- animation 直接复用既有 domain 对象；没有新增模块、ECS System、registry 或 Link。
- capability/event/script 面没有增加。tool 面本阶段没有新入口；renderer 消费 common 的
  owning pose value。动画对 graphics 的既有向下依赖保持不变，无反向 include。
- R-MECH-1 / R-MECH-4：单次构建对应一个已知 consumer、低频、全批原子返回，使用 builder + Result；GPU 部分仍
  使用既有 capability/token。遵守成本分层、借用作用域、单一 owner、失败不发布半成品。
- 返回 palette 是不可变用途的导出快照，不是第二份 live 动画状态；无持久格式或迁移。
- 未建立 Renderable3D 与 clip/skeleton/skin 的自动身份关联，MCP 动画任务入口、跨帧统一
  framing 和真实角色资产验收尚未完成；不能把本阶段称为完整动画 MCP 功能。
- R-COST-1 / R-COST-2：显式 builder 和 @cost；不通过默认参数切换成本档位。
- R-BOUND-1 / R-BOUND-3：无新增反向符号依赖，也不将 include 检查称为裁剪证据。
- 无规则豁免，尚不声明新的裁剪构建或完整 OBJECT/engine 链接证据。

本轮验证：主工程 EVAnimation 新源文件以 /W4 /WX 编译通过；EVFoundation / EVBackends
SHARED 链接通过。3 个动画组合用例通过 67 条断言（包含 GPU 轮询状态检查），静态
四视图 30 条、独立姿态覆盖 36 条、7 个 MCP/拼图用例 153 条也通过，合计 12 用例。
动画 GPU 用例验证 0/0.5/1 秒三帧的位置 bounds、live palette 不变、源 AnimSkin 销毁后
输出可用，以及真实 CPU 回读拼图；启用 validation，无 error，仍有既有启动 warnings。
临时 visual driver 输出 `build/animation-inspection-sequence.png`，已目视检查；从左到右
为三个时间点，每格本身包含四视图。平面三角形在侧视和顶视方向退化为线，不是丢帧。
此 artifact 使用逐帧自动取景；右下原相机格可观察平移，尚非统一 union-bounds 取景。

第一次源码门禁指出 unit 目录小文件合并规则；这两个测试实际组合 animation 与 graphics
时间规划器，已归入 module-composition 目录，未加豁免或更改门禁。

最终 `make check`：格式、分层、绑定、测试归类、架构和 ruff 通过；529 个脚本测试
3 failures、1 error、5 skipped，仍为既有 `test_ci_third_party_cache` 的 Windows/Bash 路径
及 cache target install prefix 问题。记录 `build/animation-inspection-check.log`；未跳过
失败项，完整门禁未通过。`git diff --check` 通过。

## PR preparation and Windows source-gate repair

Synced this change to dev c780fb64ec253bd13def7edafcbdbbb55abf8d87 without conflicts.
The cache fixtures now explicitly select Git Bash on Windows and pass POSIX-style
paths to shell scripts; their original rejection assertions remain intact. All
seven applicable cache tests pass, with the existing POSIX-only fixture skipped.
The pure graphics planner cases are consolidated into the existing graphics unit
source to satisfy the source manifest size policy, with no gate exemptions.

The refreshed strict Windows SHARED build of EVFoundation, EVPlatform and
EVBackends passes. Twelve focused MCP, contact-sheet, static/skinned rendering and
animation composition cases pass against those DLLs (285 assertions). GPU tests
use Vulkan validation with no errors. This is not a full engine/EVWorld or OBJECT
build, and no cross-platform or trimmed-build claim is made.
Final source gate passes: make check CI_BASE=c780fb64ec253bd13def7edafcbdbbb55abf8d87;
529 script tests, five existing platform skips. The local nodiscard compiler probe
reports its existing skip because no GCC/Clang driver is discoverable; Linux CI
must verify that diagnostic. No other source gate was bypassed.
