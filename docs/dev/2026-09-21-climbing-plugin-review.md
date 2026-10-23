# Climbing System 对照与接触系统改进

参考用户提供的 `Climbing System.unitypackage`，主要阅读包内 Dias Games 的
`ClimbAbility`、`ClimbIK`、转角、跳抓、Ledge 和 Ladder 脚本。没有运行 Unity 场景，
没有把插件代码或资源复制进仓库。示例继续使用仓内 CC0 KayKit 模型和动画；HUD 字体
来自仓内 DejaVu Sans，并附带其 NOTICE。

## 对照结论与实现

| 插件机制 | EVEngine 改进 |
| --- | --- |
| 独立左右手 IK 权重 | 修复脚/骨盆汇总权重误激活已释放双手的问题；仅旧式 aggregate-only 输入保留兼容路径 |
| 逐脚墙面检测、无支撑时释放 | 输出每脚 owning 世界目标与有效位；非梯子逐脚 ray query，仅接受目标 body 且法线一致的面 |
| 作者指定梯子支撑 | 每 tick 从图解析 body-local foot socket，生成左右脚目标，支持平移和旋转 |
| 跳抓/横移时分阶段转手 | 保存源节点观察引用，按每手首个正权重窗口 smoothstep 到目标 socket；目标 reservation 仍是唯一占用凭证 |
| HangWeight 平滑 | 示例以注入固定 dt 在 0.12 秒内过渡每肢权重，复制基础动画姿态后再应用 IK，避免累积修改 Motion Matching 的 pose |

C++ pose adapter 支持可选左右腿，默认禁用。有效目标和对应权重同时存在才求解。
所有活动目标、模型变换、pose 大小及连续骨骼链先验证，再修改 pose，失败不留下半套
手脚约束。骨盆权重仍是已有通道，本次没有新增骨盆求解器。

转移时源、目标 socket 都从同代图解析，不缓存 Physics 指针。到达目标后清除源引用。
Runtime schema v5 保存 `execution.sourceAnchor`；恢复先验证完整输入和引用，再发布。
v4 及旧版本迁移为历史的 target-only 表现，活动动作的窗口不受 definition 热重载影响。
脚部目标是本 tick 投影，不持久化。

## 角色集成与证据

`examples/climbing-motion-matching` 默认显示宽墙接触样例。F2 切换宽墙、自由悬挂、
单脚窄墙、梯子、爬上、内外转角和背跳，空格执行对应动作，R 返回原跑酷赛道。
`contactLabAutoAdvance=true` 可在固定更新中重复运行八场景循环，调用与按键相同的
正式 runtime 入口，没有使用 `eve_eval`。

图和 profile 位于 `assets/contact-*.json`。KayKit 的短肢体使用 0.2 m 胶囊半径、
零额外挂墙偏移；有效骨骼端点在到达挂点时检查距目标不超过 0.05 m。此前默认偏移让
脚部目标不可达，已用实际端点输出校准。每次阶段改变记录左右脚有效位；事件在固定
更新末尾消费，避免反复切换后填满事件队列。

真实桌面验证已连续完成五轮以上循环，没有 frame error 或可达性错误：

| 场景 | 物理/接触结果 |
| --- | --- |
| 宽墙 | 双脚有效；左右手分阶段横移后到达目标 |
| 自由悬挂 | 双脚无目标，脚部权重为零，双手保持抓握 |
| 窄墙 | 仅左脚有效，右脚保持动画姿态 |
| 梯子 | 双脚使用作者挂点，上梯后重新解析目标 |

四张图由引擎 MCP `eve_screenshot` 获取并检查，未使用桌面或 Xvfb 截图。
本机保存在 Codex visualizations 的本任务 `climbing/` 目录；原始调试日志位于
`/tmp/eve-climbing-lab-live.log`。截图是四种接触样例的证据，复杂动作的轨迹和视觉验收补充结果见下文。

## 架构规则与门禁

- R-FACE-2：`scripts/architecture_contracts.json` 增加 climbing 的 module-interface；
  提供/消费能力、脚本类、事件、线程、热路径和成本明确声明。既有 ECS/Link 条目按
  scope 复用，没有第二份状态契约。
- R-MECH-1/2：每角色、每 tick、已知消费者、同步预检后应用；沿用 owning projection
  与 Animation adapter。新增热路径不查询字符串服务。每脚最多一个物理 query，计入
  active budget；没有新 provider 依赖。
- Result/生命周期：沿用 Result、nodiscard、同步借用和代际句柄；源挂点不持有第二个
  reservation；图/物理对象销毁和恢复失败有运行时测试。
- 时间/状态权威：runtime 拥有动作和接触权重；ECS、脚本与姿态为投影。显示权重有独立
  owner/reset，只影响蒙皮，不反馈给碰撞或动作判定。没有读取墙钟或新增 RNG。
- 持久化：runtime v5；旧版本迁移、错误源引用的原子拒绝、活动 definition reload 和
  目标到达后的源引用清理都有针对性断言。
- R-BOUND-3：`make profile PROFILE=headless PROFILE_PLATFORM=macosx
  PROFILE_BUILD_ROOT=/tmp/eve-climbing-profile PROFILE_JOBS=2` 的 configure/build/smoke
  已通过；manifest 闭包不含 climbing。这里只声明 L1 编译裁剪，不声明 L2/L3/L4。
- 新检查器验证能力名字、脚本类、Observer、可选依赖探针、manifest absent profile 和
  新增热路径 lookup；回归用例覆盖缺失/错误声明。scope 扫描限制到声明路径前缀。

没有申请或采用架构规则例外。

## 当前验证状态与剩余工作

- 最新 macOS Debug 完整引擎和 test runner 构建成功。
- `make test/macosx-debug FILTER=climbing`：111 项全部通过，包含新增 presentation、
  转移完成清理、v4 transfer 迁移及既有 ECS/存档/服务组合用例。
- `ctest --test-dir build/macosx-debug -R '^animation\.' --output-on-failure -j 4`：
  183 项全部通过，覆盖 TwoBoneIK、姿态、蒙皮和动画资产。
- 最新 `make check` 全部通过，包含 183 项脚本测试。macOS 使用
  `TMPDIR=/private/tmp` 避免无关临时目录符号链接差异。
- 默认手动配置的 `MIN_RUN_SECONDS=2 RUN_SECONDS=8 bash scripts/smoke_examples.sh
  climbing-motion-matching` 通过；另有五轮以上自动循环的物理、IK 与 framebuffer 证据。
  自动循环配置在验证后已恢复为 false。
- 最初的局部 pose runner 与旧实现对照能检出汇总权重误激活双手的回归；
  沙箱下资源写目录、SDL 显示器不可用的用例均在实际本机环境重跑，没有跳过。

爬上场景已通过正式 climbUp 入口完成，最终落点断言为 `(36, 2.5, 4)`，
手脚约束释放，角色在平台上保持位置；引擎 MCP 截图已检查并保存为
`climbing/eve-climbing-contact-climb-up.png`。新增了第五个手动/自动场景，修复了动作完成
后普通移动逻辑把角色从样例区拉回赛道的问题。脚本编译与 contactPresentation 两项
针对性测试通过，默认配置已恢复为手动模式。

补充真实 90° 内外转角后，原先直线插值在凸角处被胶囊碰撞拒绝。现由同一个内部轨迹函数
供预检和执行使用：经过两侧支撑平面的交点，分段 smoothstep；双手采用同一绕角原则。
两项真实实体墙回归逐步检查胶囊净距和手目标的墙外位置，旧实现可复现失败。

示例新增内转角、外转角、向身后另一面墙跳跃三个场景，共八个，每轮 40 秒。
转角显示朝向随 authored turn 插值，仅影响蒙皮；背跳在飞行期间释放约束，接近目标时
恢复抓握。实际桌面循环中三个场景均发出 anchor_reached，挂点处骨端可达性检查通过。
内转角镜头从空侧观察，背跳从墙间侧面观察；截图使用引擎 framebuffer。
使用现有 CC0 跳跃基础姿态加 IK，尚非专用攀爬动画素材的最终动作质量。

全量复核修正了新增测试的两个断言前提：换手窗口起点仍处于源目标，需在下一 tick
检查已移动；恢复会转移 reservation claim，重放旧存档前必须释放现有 owner。
保留旧凭证冲突检查，没有放宽运行时的代际安全约束。

## 2026-10-23 提交前复核

- 使用已有 macOS Debug 测试产物复跑 climbing：111/111 通过；本次未重新编译。
- `make check CI_BASE=434ae72e4` 通过，包含 183 项脚本测试。该 SHA 是本次改动的原开发基线。
- 默认 `make check` 对最新 `origin/dev`（`0dade008d`）失败，架构检查报告大量跨基线差异；
  当前分支落后 591 个提交。原基线通过不能代表最新 dev 集成通过。
- 因此以草稿 PR 提交，合并前仍需同步最新 dev、解决冲突并重跑构建和门禁。
  上文动画、裁剪和视觉证据属于此前开发验证，本次未重跑这些项目。
