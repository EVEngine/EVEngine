# 战斗结算与伤害数字

## Damage Numbers Pro 4.41 分析

依据用户提供的 unitypackage 的 pathname 与 C# 源码只读分析（242 个资源条目）。
`DamageNumber` 是通用生命周期，`DamageNumberMesh` / `DamageNumberGUI` 分别接
世界 Mesh 和 Canvas；使用 TextMeshPro 文本及 Unity 动画/相机能力。

功能分组：

- 数值与排版：数字格式、左/右/上/下文本、按数值着色与缩放。
- 生命周期：淡入/淡出、缩放、偏移、摇晃、旋转、速度与跟随。
- 相机：面向相机、距离/FOV/正交缩放、穿墙。
- 高密度管理：对象池、spam group、合并/替换、碰撞推开、提前销毁。
- 制作工具：预制体、材质与动画预设、Unity Inspector。

本实现复用 EVEngine 字体图集与 drawText，不导入插件的 C#、字体、材质或美术资源。
首版优先实际战斗反馈：伤害/暴击/治疗/未命中、漂浮/抛物线、入场弹出与淡出、
短时间同组同样式合并、数量预算、阴影与描边。Unity 编辑器与材质效果不属于移植范围。

## Combat 的唯一结算路径

`DamageRuntime::apply` → `settlement::SettlementPipeline::settle` → 调用局部的
`CombatSettlementPolicy` → `PreparedApply`。健康与韧性仍只存在于调用者的 `CombatState`。
策略持有的 before/candidate 仅为同步事务准备与回滚，不建立第二个运行时属性存储。
默认路径与自定义 pipeline 路径共用实现；preview 在隔离的 CombatState 副本上执行同一
canonical pipeline，因此返回实际可应用的、已限幅的血量与韧性伤害。

原 `IDamageRule` 在 TargetMitigation 阶段执行，provider 缺席使用请求值；成功与失败
均保持原 Result 责任。护甲/护盾没有凭空增加一套 CombatState 字段，游戏可以配置公共
pipeline 的阶段。生命和韧性原子提交；Trigger 失败不提交，EventLog append 失败回滚。
受击优先级维持 Death → Knockdown → Stagger → Flinch。

`DamageOutcome.settlement` 是规范数值结果与版本化事件，旧 `appliedHealthDamage`
只保留为兼容的输出快照（来自 settlement.applied），不参与任何入账。
`DamageRequest.critical` 是调用方确定的标记，伤害请求已经包含暴击倍率，避免再乘一次。
`tick` 由调用方注入，动作通知从 ActionNotifyContext 传入，不读取墙钟。
脚本 `applyDamage` 增加末尾 `(critical, tick)`，返回的 `settlement` 来自通用事件 payload
codec，而非独立维护字段映射。`applyTimelineDamage` 使用最近一次 advance 的 tick。

## 架构与机制判据

- R-BOUND-1..4：combat → settlement 是硬链接依赖，登记在 manifest；不把 include 图
  当作裁剪证据。minimal + combat + font + devtools 是本次集成构建配置。
- R-COHESION-1/6：流程、限幅、事件、回滚的 owner 是 settlement；战斗策略实现其接口，
  不反向依赖 combat，也不新建 provider registry。
- R-FACE-1..5：模块边界条目登记在 architecture_contracts.json。新增交换是结算
  E2 硬依赖、E4 canonical result event 和 E5 脚本结果；不添加 ECS 或 Link。
- R-MECH-1：每次命中、单一已知目标、多阶段必须原子提交，因此使用同步 policy +
  PreparedApply；显示请求由游戏在提交成功之后单向投递。
- R-MECH-2..6：热路径不进行 module/capability 字符串查找，无新 feature flag。
- R-COST-1..5：入账调用完成一次有界阶段流程；渲染帧的昂贵快照显式返回 owning vector，
  正常绘制直接遍历 live entries，不依赖快照分配。
- 无持久 popup、无实体指针、无新 ECS System；场景切换/热重载清空临时显示。
- 所有 mutation 使用 Result；失败注入覆盖规则错误、冲量溢出、阶段失败与提交后日志失败。
- 不引入全局 RNG，表现运动使用注入 dt 与显式散射值；相同输入顺序可复现。

## 验证入口

`make check` 为源码门禁。`EVENGINE_BUILD_PROFILE_CHECKS=ON` 的
`eve_combat_presentation_check` 是实际引擎库链接的专用组合测试，避免默认全套 GPU 用例。
最终构建、测试和引擎截图结果以本次交付日志为准，不以本设计文档声明为通过证据。

## 2026-10-23 本机验证记录

- macOS Debug：`eve` 与 `eve_combat_presentation_check` 构建成功；组合测试 16 cases、
  400 assertions 通过。覆盖 canonical settlement、健康/韧性回滚、数值显示与脚本绑定。
- `make check/damage-numbers`：独立核心链接测试 1/1 通过，不链接 renderer 或 combat。
- `MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh impact-arena`：
  存活完整 6 秒，1 passed / 0 failed，无已知运行错误标记。
- `examples/impact-arena/agent_check.py`：三波共 12 kills，24 criticals、5 dodges、
  1 avoided，胜利剩余 106/320 生命；观察到请求 46 / 实际 38。随后验证 idle death 与 reset。
  `eve_agent_session_*` 会话收据完成，战斗、胜利、死亡三张图均使用 engine-readback。
- 本工作树 HEAD 为 `434ae72e4c5715e84c8a8623311e76a459a00f5b`，是当前
  `origin/dev` 的祖先。源码门禁使用该实际共同祖先作为 `CI_BASE`，避免将后续上游提交
  反向差异当作本任务新增；没有新增 baseline/allowlist 或跳过检查。
- 本次适用 Result/nodiscard、唯一状态 owner、注入时间、同步事务回滚、模块六面契约、
  热路径成本和裁剪证据规则。未引入 ECS System、跨域 Link 或持久化格式；无主动架构豁免。
  未声称已验证 Windows/Linux 或 WebGPU 后端。

本机日志与 readback 图片位于 `build/impact-arena-evidence/`（忽略的验证产物，不提交）。

源码门禁另发现并修复了 glTF 转换器的 macOS `/var` → `/private/var` 目录别名比较问题：
在结果验证入口规范化根目录，绝对路径、解析路径、相对路径均验证通过，`../` 越界仍被拒绝。
相关转换器 11 个测试通过。

最终源码门禁：`make check CI_BASE=434ae72e4c5715e84c8a8623311e76a459a00f5b`
退出码 0，`source-quality gate passed`，仓库脚本测试 180/180 通过。
