# Creative Brain（创意大脑）— 场景生成意图解析 & 资产排布生成 Agent

EVEngine 的场景创作主入口。将自然语言场景需求解析为结构化场景配置、
按标签匹配引擎资产、生成自然布局策略，并输出标准生成计划，通过引擎 MCP
批量生成场景。

## 功能

1. **意图解析**（`intent`）：风格、地貌、光照氛围、道路规则、植被分布、
   探索道具规则 -> 结构化 `SceneConfig`（LLM 或关键词回退）。
2. **资产匹配**（`assets`）：查询资产标签库（`catalogs/assets.example.json`），
   按风格/标签筛选，构建资产白名单；报告缺失资产。
3. **布局策略**（`layout`）：道路禁放缓冲、远近分层、非对称错落、拐角重点装饰。
4. **异步资源拉取**（`request`/`status`）：缺失资产经 `ResourceBroker`
   异步请求【PR6 资源 Agent】（协议见 `docs/RESOURCE_AGENT_PROTOCOL.md`）。
5. **MCP 批量生成**（`mcp`）：输出标准 `GenerationPlan`，通过引擎 TCP MCP
   推送到活 VM 并逐 step 生成。

## 安装

```bash
pip install -r tools/creative-brain/requirements.txt
```

纯 `plan`/`parse`/`assets`/`request` 只需 Python 标准库；启用 LLM 需
`openai` 与 `OPENAI_API_KEY`（可选 `OPENAI_BASE_URL` / `OPENAI_MODEL`）。

## 用法

```bash
cd tools/creative-brain

# 1) 意图解析
python creative_brain.py parse "a moonlit dark cave dungeon with treasure corners"

# 2) 完整管线 -> 标准生成计划
python creative_brain.py plan "a lush fantasy forest at dusk with winding roads" --out plan.json

# 3) 查看资产目录
python creative_brain.py assets
python creative_brain.py assets --category prop

# 4) 异步请求缺失资产（与 PR6 资源 Agent 通信）
python creative_brain.py request model.ruin_arch --style "overgrown ruins"
python creative_brain.py status <request_id>

# 5) 推送到引擎批量生成（需已启动：eve run --debug --mcp-port=7529 .）
python creative_brain.py mcp "a snowy tundra with scattered rocks" --port 7529
```

打印场景配置 JSON Schema：

```bash
python creative_brain.py --schema
```

## 启用 LLM（OpenAI）

```bash
export OPENAI_API_KEY=sk-...            # Windows: $env:OPENAI_API_KEY="sk-..."
export OPENAI_MODEL=gpt-4o-mini        # 可选
python creative_brain.py plan "cyberpunk city at night with neon alleyways"
```

无 key 时自动回退到确定性关键词解析，管线照常工作（便于 CI / 测试）。

## MCP 接入引擎

引擎 MCP 在 `eve run --debug --mcp-port=7529` 下监听 TCP 新行 JSON-RPC。
`brain/mcp.py` 直接以 socket 调用 `eve_run_script`（与 `tools/eve-mcp`
同协议，无需 Node）。协议细节见 `src/engine/devtools/McpServer.cpp`。

## 结构

```
tools/creative-brain/
├── creative_brain.py       # CLI 入口
├── brain/                  # 核心包
│   ├── intent.py           # 意图解析
│   ├── assets.py           # 资产匹配
│   ├── layout.py           # 布局规则生成
│   ├── plan.py             # 标准生成计划组装
│   ├── resource_proto.py   # 资源 Agent 异步协议客户端
│   ├── mcp.py              # 引擎 MCP 客户端 + 批量驱动
│   └── schema.py           # 数据模型 + JSON Schema
├── catalogs/               # 资产标签库示例 + schema
├── docs/                   # 架构 + 资源协议规范
└── tests/                  # 单元测试
```

## 测试

```bash
python -m pytest tools/creative-brain/tests -q
```
