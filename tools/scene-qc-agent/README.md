# scene-qc-agent · 场景质检大脑

ReAct 范式的自动化巡检 / 迭代微调中枢（openCode / CLI 可用）。对 EVEngine 场景做多视角自检，
发现缺陷自动修正、循环迭代，直至达标或触及硬上限。

## 定位

自动化审核、迭代微调中枢：全自动完成场景多视角自检，融合三类信息综合判定，不达标则自动生成
修改方案并重新执行全套巡检。

## 能力

1. **自主调用链路**：`生成机位 → 获取快照 → 本地轻量模型筛查 → 高危画面送远端高精度 VLM 复核`。
2. **三源融合**：`3D 几何真值` + `本地小模型评估` + `云端 VLM 精细评审` 加权综合判定。
3. **自动修复**：判定不达标则生成修改方案，调用 MCP 调整场景，并重跑完整巡检管线。
4. **硬上限**：最多 `max_rounds`（默认 3）轮优化，避免无限循环。

## 架构

```
                    +------------------------ SceneQCBrain (ReAct) ------------------------+
                    |  Thought -> ACTION{tool} -> Observation(重检)  ，rounds <= max_rounds  |
                    +-----+--------------------------+--------------------------+----------+
                         | inspect_scene             | apply_fix                | finalize
                         v                          v                          v
               +-----------------+        +-----------------------+      终止
               |  QcPipeline      |        |  Fixer (planner 模型) |
               | 机位->快照->筛查  |        | 生成修改计划 -> 应用   |
               | ->VLM->融合      |        +-----------------------+
               +-----------------+                    | scene_modify / run_script
                     |   camera_generate / screenshot / scene_info / scene_modify  (Engine MCP)
                     +---------------------->  ToolRegistry  ->  EvMcpClient (eve-mcp bridge / TCP)

   模型分级调度（models.py / pipeline.py）：
      全帧   -> 本地轻量小模型筛查 (local_screen)       低成本
      高危   -> 远端高精度 VLM 复核 (vlm_review)        高精度，仅高危画面
      修复   -> 修复规划模型 (fix_planner)              生成结构化修改计划
```

## 目录

```
tools/scene-qc-agent/
├── config.example.json        # 模型端点 / 阈值 / 工具名 / 迭代上限
├── scene_qc_agent/
│   ├── cli.py                 # 命令行入口
│   ├── config.py              # 配置加载（与默认深合并）
│   ├── mcp_client.py          # EVEngine MCP JSON-RPC 客户端（bridge/TCP 两种传输）
│   ├── tools.py               # 逻辑能力 -> 引擎工具映射（含 dry-run 仿真）
│   ├── models.py              # OpenAI 兼容 / opencode-go 客户端（文本 + 视觉）
│   ├── pipeline.py            # 机位->快照->本地筛查->VLM 复核 完整管线
│   ├── fusion.py              # 三源信息加权融合 + 判定
│   ├── fixer.py               # 修复计划生成与落地
│   ├── react.py               # ReAct 调度大脑（含硬上限）
│   └── report.py              # 数据模型
└── tests/                     # 单元测试（融合 / ReAct / 工具）
```

## 用法

### 干跑（无需引擎 / 模型，演示闭环）

```bash
python -m scene_qc_agent --dry-run --trace --targets hero enemy
```

输出示例：基线 FAIL(0.40) → apply_fix → 重检 PASS(1.00)，`rounds_used:1`。

### 连接真实引擎

先起引擎 MCP：

```bash
eve run --debug --mcp-port=7529 examples/basic
```

再运行（默认走 `tools/eve-mcp/server.js` stdio 桥）：

```bash
python -m scene_qc_agent --config config.example.json --trace --targets hero enemy
```

### 参数

| 参数 | 说明 |
|------|------|
| `--config` | 配置文件路径（默认 `config.example.json`） |
| `--scene-id` | 场景标识 |
| `--targets` | 待巡检目标（默认 `hero`） |
| `--max-rounds` | 覆盖迭代硬上限 |
| `--dry-run` | 不连引擎/模型，使用仿真闭环 |
| `--trace` | 打印 ReAct 思维与动作轨迹 |
| `--report` | 报告输出 JSON 路径 |

### 测试

```bash
python -m unittest discover -s tests -p "test_*.py"
```

## 配置要点

- `models.*.base_url` 可指向任意 OpenAI 兼容端点（OpenAI / Ollama / vLLM / opencode-go 网关）。
  `vision:true` 时向该端点传 `image_url` 内容块（本地小模型、远端 VLM 各按需开启）。
- `fusion`：`weights` 三源权重；`screen_escalate`（本地得分低于此送 VLM）；
  `pass_threshold`（融合得分低于此判 FAIL）。
- `iteration.max_rounds`：优化硬上限，默认 3。
- `mcp_tools`：逻辑能力 → 引擎 MCP 工具名的映射。机位 / 截图 / 场景能力由引擎侧其他
  agent 落地，拿到实际工具名后在此对齐即可（本包只做通用转发，无需改代码）。

## 大小模型分级调度逻辑

1. 生成 `N` 个机位，逐帧 `screenshot` + `scene_info`。
2. 全部画面过**本地轻量模型**筛查（低成本）。
3. 满足任一高危条件则送**远端高精度 VLM** 复核：本地得分 < `screen_escalate`，
   或几何真值含 `critical` 缺陷。
4. 融合 `geometry` / `local` / `vlm` 三源加权得分，产出每帧 `PASS/ESCALATE/FAIL`，
   汇总为场景级判定。
5. 判定 FAIL → 修复规划模型生成修改计划 → `scene_modify`/`run_script` 应用 → 重检，
   至多 `max_rounds` 轮。

## 与 openCode 集成

仓库提供 `.opencode/agent/scene-qc-brain.md`，把该工具封装为 openCode 子代理；也可直接用
命令行调用。修改 openCode 配置后需**退出并重启 openCode** 生效。
