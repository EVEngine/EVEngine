# EVEngine Agent 生产力基准

衡量 Agent 在固定起点、工具配置和预算下，独立完成**可运行、可验证的游戏开发任务**的能力。
被测对象是「模型 + CLI/Agent + 工具 + 知识上下文」整体，不把差异全部归因于模型。
首版只依赖 Python 3.10+ 标准库；真实玩法验收需要本机可运行的 EVEngine。

多 Agent 合作开发同一个项目的基准见 [TEAM.md](TEAM.md)，覆盖任务拆分、Agent 创建隔离仓库、并行开发、真实 Git 冲突和语义集成。该内容集成轨道独立报告，不替代真引擎验收。

## 设计原则

1. 先冻结任务、输入、验收器和预算，再运行被测 Agent。
2. Agent 的完成声明、自己提交的 evidence 和退出码都不能证明任务成功。
3. 修改完成后停止 Agent，检查写入范围，复制产物，由独立进程验收。
4. 已知坏样例必须失败，已知正确修复必须通过，才允许比较真实成绩。
5. 全部尝试进入报告，失败、超时、缺失数据不能删除或记成零成本。
6. 保留逐任务结果；不把质量、时间、成本随意加权成一个总分。

这是一套公开的开发基准，不是防恶意代码的安全沙箱。独立进程和目录防止普通误写与自报分数，
POSIX 会清理进程组；Windows 的 taskkill 后代清理是尽力而为，严格隔离需外部 Job Object/容器。
不能阻止同用户进程逃出目录、读取公开验收器、派生新 session 或改宿主。正式实验应在独立容器/VM
运行每次试验，只挂载候选工作区、固定知识包和需要的工具；验收器与 API 凭证不挂进候选容器。
不要把整个引擎仓库开放给参赛 Agent 再宣称是「首次接触」。原始 CLI 日志也应在发布前检查。

## 固定任务 v1

起点来自 `examples/ai-game` 的三个文件。每个任务精确注入一个缺陷或待调整参数；源码与知识包以归一化 LF 文本 hash 固定，输入漂移时
拒绝运行，不能悄悄用新的任务替代旧版本。全部任务都获得同一份 `EVESCRIPT.md` 知识包。

| ID | 能力 | 初始状态 | 独立验收 |
| --- | --- | --- | --- |
| contract-observation | 合同理解 | 观察路径指向不存在的状态 | JSON 与正确合同语义相等，其余字段不变 |
| combat-damage | 玩法修复 | 玩家攻击给敌人加血 | 真引擎 observe 验证伤害、玩家存活、重置与检查点 |
| combat-balance | 约束调参 | 攻击力太低 | 敌人在 3–4.5 仿真秒内死亡，玩家存活，其余回归检查通过 |
| restart-parameters | 参数集成 | 重启忽略 enemyHp | 多组 seed/生命参数生效，hp/maxHp 一致，重置后仍一致 |
| checkpoint-recovery | 状态生命周期 | 注册了错误的状态根 | 推进后恢复检查点，全部被观察字段精确还原 |

这是**小型脚本与玩法合同基线**，不是完整游戏生产力排行榜。合同任务的静态验收不能证明运行时
观察有效；另外四项只使用真实引擎验收。首版不衡量美术质量、CPU/GPU 帧预算、跨关卡旅程、
人机协作或 Agent 自身崩溃后的续作；`checkpoint-recovery` 测的是游戏状态恢复，不能代表 Agent 续作。
所有任务是公开白盒任务，适合开发回归；泛化结论需要另外冻结并隔离的留出任务。

## 快速校准：无需模型、无需引擎

在仓库根目录执行。输出目录必须不存在，避免覆盖历史实验：

```sh
python3 benchmarks/agent/run.py run \
  --adapter benchmarks/agent/adapters/noop.json \
  --task contract-observation --seeds 17 --repeats 1 --calibration \
  --output build/agent-benchmark/noop

python3 benchmarks/agent/run.py run \
  --adapter benchmarks/agent/adapters/reference.json \
  --task contract-observation --seeds 17 --repeats 1 --calibration \
  --output build/agent-benchmark/reference
```

第一条预期退出 1（验收失败），第二条预期退出 0。控制适配器必须带 `--calibration`，其结果不能排名。
执行全任务时去掉 `--task`，并指定 `--engine /absolute/path/to/eve`。未配置引擎的运行时任务记为
`blocked`，不会使用模拟结果填补。

## Codex CLI 与 DeepSeek CLI

Codex 的命令、JSONL 用量解析和版本捕获已经实现：

```sh
python3 benchmarks/agent/run.py run \
  --adapter benchmarks/agent/adapters/codex.json \
  --task contract-observation --seeds 17 --repeats 3 \
  --environment-label workstation-a \
  --output build/agent-benchmark/codex-smoke
```

示例使用 CLI 默认模型，适合连接检查。正式实验应复制 JSON 配置，写明具体模型版本，并在 `command`
追加 `--model MODEL_ID`，同时更新配置的 `model` 描述。配置文件及实际 CLI 版本随结果保留。
Codex 使用新会话、`--ignore-user-config`、`--ephemeral`、`workspace-write` 和禁止交互审批；
临时 CODEX_HOME 只复制现有认证文件，不复制历史会话和用户配置，外层 runner 在超时后也会清理。
凭证不写入实验产物。显式 HTTP CONNECT 403 会记录为 provider_policy_denied/blocked 并停止无效重连。平台账号自动注入的能力仍取决于 CLI；正式实验需要核对原始事件和工具面。
需要配置 MCP 或 provider 时，在适配器命令追加重复的 `--codex-config KEY=TOML`；不要把密钥写进 JSON。
默认 Codex 配置评估文件/命令行工具，**没有自动配置 EVEngine MCP**，不能宣称是 MCP 模式的成绩。

DeepSeek 的同名 CLI 实现不统一，因此 `adapters/deepseek.json` 是明确标记的模板。
把 `--cli-command-json` 的值替换为已安装实现的**非交互 argv 数组**，固定模型和 provider；
适配器通过 stdin 提交同一任务提示，保留原始输出和版本信息。支持 `--version` 且能从 stdin
接收提示的实现可直接接入，其他实现可实现下节协议。尚未实现对应 CLI 用量解析时，成本字段保持 null。
不能仅凭 DeepSeek 服务有 API 就假定存在一个通用的 `deepseek --prompt` 命令。

## 通用 Agent 适配协议

配置格式：

```json
{
  "schema": "eve.agent-benchmark-adapter/v1",
  "id": "my-agent-tools-v1",
  "model": "exact-model-version",
  "tools": "pinned CLI and MCP configuration",
  "command": ["/absolute/path/to/adapter", "{request}", "{result}"]
}
```

命令为 argv 数组，不经过 shell。仅替换 `{request}`、`{result}`、`{workspace}`、`{python}`、
`{benchmark}` 五个字面占位符，参数里的其他 JSON 花括号保持原样。工作目录是隔离候选目录。
每次请求为 `eve.agent-benchmark-request/v1`，包含任务描述、工作区、允许文件、秒数预算、case seed、
重复编号、产物目录和可选引擎路径。工具和模型的随机采样参数必须在适配器中显式固定/记录；
case seed 只控制游戏输入，不能声称固定了模型采样。

修改完成后正常退出。可选输出：

```json
{
  "schema": "eve.agent-benchmark-adapter-result/v1",
  "usage": {
    "inputTokens": 1200,
    "outputTokens": 300,
    "costUsd": null,
    "toolCalls": 6,
    "humanInterventions": 0
  }
}
```

缺失用量为 unknown。负数、布尔值、非数值、NaN/Infinity 会被拒绝。用量来自适配器自报，
不等于独立审计；Codex 的 token 为 `turn.completed` 用量之和，toolCalls 计已完成的 shell、MCP、
web-search 事件，不跨 CLI 假定完全等价。价格不从 token 数量猜测，缓存 token 计价也不自行推算。
首版只硬性限制墙钟时间；token/美元是事后统计，没有声称实现硬性 token/花费限额。

## 真实引擎验收

`--engine` 必须是绝对路径或可解析为绝对路径的本机二进制。每次候选使用新引擎进程、独立项目副本和
独立 TCP 端口；验收进程拥有自己的超时。使用 `eve_restart`、`eve_play clock/act/step/observe/checkpoint`，
不使用 `eve_eval` 或 `eve_run_script` 代替玩法。验收器仅在冻结的评估副本中增加观察字段，原候选保留。
JSON-RPC error、MCP isError 和业务 ok=false 都作为错误处理。RPC 请求/响应保存在 `grader-rpc.json`。

当前 Play Host 只提供 host frame 推进，**没有保证固定仿真 dt**。验收按 observe 的仿真时间判定，
最多 4096 host frames，并保留原始时间数据；不能拿 step 数量当秒数。暂停期间的 tick/时间也保留在记录中。
首版数值窗口是宽容差任务，跨平台 bit-exact 重放仍需引擎提供固定 dt 能力。

Linux 应使用引擎既有的 Vulkan/X11/音频设置。没有图形设备时，在已安装 Xvfb 和 Mesa 的机器上例如：

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
ALSOFT_DRIVERS=null xvfb-run -a \
python3 benchmarks/agent/run.py run \
  --adapter benchmarks/agent/adapters/codex-pinned.json \
  --engine /absolute/path/to/eve --repeats 3 \
  --environment-label linux-lavapipe-a \
  --output build/agent-benchmark/codex-full
```

`codex-pinned.json` 是你复制并固定模型/工具后的配置，不是仓库自带文件。
截图任务只接受 engine-owned capture；本套首版没有视觉分数。

## 实验与比较

默认 5 任务 × 3 case seeds × 3 次独立重复 = 45 次。每次从干净起点开始，不把多个独立尝试拼成一次成功；
因此报告是独立尝试成功率，不是 pass@k，也没有在失败后偷偷更换模型。默认串行，避免资源竞争。
执行顺序由 `--order-seed` 打乱且完整保留；正式 A/B 建议轮换 A/B 顺序，固定机器、CPU/内存配额、
驱动、缓存冷暖、网络与并发限制，在 environment-label 指向的实验记录中记录这些条件。

每次实验记录 suite、fixture/知识包/验收器 SHA-256、引擎二进制 SHA-256、平台/Python、Git revision、
预算、工具配置及执行顺序。动态链接引擎还需要在实验记录另存依赖库/SDK build manifest；
二进制 hash 不能证明所有共享库一致。环境标签是人工声明，不能替代机器配置审计。

主指标是逐任务成功率与任务宏平均；同时报告失败原因分布、全部尝试的 Agent 墙钟 p50/p95、
每成功一次消耗的总 Agent 时间、用量字段及其覆盖率。验收耗时单独记录，不混入 Agent 墙钟。
只有所有尝试均提供某项用量时才计算该项总成本及每成功一次的美元成本；失败尝试也计入成本。零成功时每成功耗时/成本为 null。
`wilson95` 是固定任务上尝试成功比例的描述性区间；重复 seed 并非独立任务，不用于泛化显著性宣称。

对相同任务、seed、重复次数、预算、输入、验收器和环境身份的两个**完整**实验：

```sh
python3 benchmarks/agent/run.py compare \
  build/agent-benchmark/agent-a/report.json \
  build/agent-benchmark/agent-b/report.json \
  --positive-control build/agent-benchmark/reference-full/report.json \
  --negative-control build/agent-benchmark/noop-full/report.json \
  --output build/agent-benchmark/comparison.json
```

默认模型未解析或工具面未声明时，比较命令会拒绝排名。
两个 controls 必须具有相同输入/环境/验收器身份，覆盖每个 task/seed，且正控制全部 pass、负控制全部 fail。
控制组可以只运行一次重复。比较输出配对胜/负次数、成功率差和固定随机种子的 paired bootstrap 区间。
小样本区间只描述这些公开任务上的重复结果，不代表新任务总体上的统计显著性。
`blocked`、`grader_error`、`adapter_error` 的实验不排名，不能简单当成模型能力差；
它们仍保留在原始分母与原因分布中。`fail`、`timeout`、`scope_violation` 属于任务未完成。

## 结果与退出码

每次尝试保留 request、初始/最终文件 hash、候选文件、独立评估副本、Agent 日志、可选用量、
逐项检查结果、原始引擎 RPC 与引擎日志。`report.json` 在每次尝试后原子更新，只有完整调度完成才有
`complete=true`；中途中断不允许拿部分结果排名。输出目录必须新建，禁止覆盖上次实验。

- 0：全部任务通过；比较命令则表示比较生成成功。
- 1：至少一项失败、超时、阻塞或执行错误；详情看逐次状态。
- 2：配置/输入错误，例如不支持的版本、输入漂移或不等价实验比较。

## 扩展任务与研究路线

新增任务需要一起提交：固定 fixture/缺陷、公开目标与修改边界、独立验收、正负控制、失败注入测试、
预算依据，以及至少一次真引擎校准。现有 registry 在 `suite.json`；新的验收类型在 `grade.py` 实现，
不能让候选任意指定一个「总是 exit 0」的验收命令。修改任务或验收语义需提升 suite version，旧报告不覆盖。

按下列顺序扩展，每条路线单独报告，避免窄任务的高分掩盖其他能力：

| 路线 | 典型任务 | 验收证据 |
| --- | --- | --- |
| API 发现 | 未读仓库，仅查询合同实现新交互 | API 查询轨迹、真实行为、首次运行成功率 |
| 内容创作 | recipe 创建可达、有视线约束的小关卡 | 权威文档 revision、导航/视线检查、保存重载 |
| 跨系统玩法 | 接任务→战斗→掉落→交任务 | 独立玩家旅程、库存/任务终态、失败分支 |
| 视觉与性能 | 在帧预算内匹配构图 | 固定机位引擎截图、几何检查、固定评审版本、硬件指标 |
| Agent 续作 | 执行中断、工具超时、重新连接 | 已提交 revision 不重复，进度恢复，预算含恢复成本 |
| 项目集成 | 独立关卡候选组合 | 单关卡、关卡连接、完整旅程和峰值资源检查 |

视觉评审分数应单独报告并用人工标注集校准，不作为所有任务唯一成败标准。
后续工具消融每次只改变一个因素，例如文件工具 vs MCP、全量工具表 vs task profile、无知识包 vs 查询合同；
固定模型版本和预算，不同时换模型、提示词与工具后归因于某个工具。

## 验证评测器

```sh
make check/agent-benchmark
# 或
python3 -m unittest discover -s benchmarks/agent/tests -v
```

该测试目标加入 source-quality 清单，CI 不发起模型调用、不需要图形环境。它测试真假成功区分、越界修改、
输入漂移、RPC 错误、未知成本、进程超时/后代清理、隔离起点和比较约束。
本目录只增加 benchmark 与构建检查入口，不改变引擎公共 API、ECS、持久格式或运行时模块边界。
