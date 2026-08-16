# Creative Brain — 架构

EVEngine 的“场景创作主入口”：把用户自然语言需求，解析为结构化场景配置，
按标签匹配引擎资产，生成自然布局策略，输出标准化生成计划，并通过 MCP
批量生成场景。运行时主要是一个 **Python CLI**（`creative_brain.py`），
用 OpenAI 做意图解析与风格选择，用引擎自带 MCP 驱动生成。

```
用户自然语言
   │
   ▼
┌────────────────────────────┐   OpenAI (OPENAI_API_KEY)
│ intent.parse_intent        │  ── 风格/地貌/光照/道路/植被/探索
│  (LLM or keyword fallback) │  ──> SceneConfig
└────────────────────────────┘
   │ SceneConfig
   ▼
┌────────────────────────────┐  catalog JSON
│ assets.match_to_config     │  ──> 白名单(whitelist)
│  (tag / style 匹配)         │      + missing_assets
└────────────────────────────┘
   │ whitelist + missing
   ▼
┌────────────────────────────┐
│ layout.apply_layout        │  ──> 布局规则 + 具体放置坐标
│  road_buffer/layer/scatter/│
│  corner                     │
└────────────────────────────┘
   │
   ▼
┌────────────────────────────┐   missing? 
│ plan.GenerationPlan        │  ───────────────▶ ResourceBroker
│  config+whitelist+rules+    │  (docs/RESOURCE_AGENT_PROTOCOL.md)
│  steps                      │
└────────────────────────────┘
   │
   ▼
┌────────────────────────────┐  eve-mcp 桥 / 引擎 TCP MCP
│ mcp.run_batch              │  eve_run_script 推送 plan + 逐 step 生成
└────────────────────────────┘
```

## 模块

| 模块 | 职责 |
|------|------|
| `brain/schema.py` | 数据模型 + 场景配置 JSON Schema（LLM 结构化输出契约） |
| `brain/llm.py` | OpenAI 客户端封装 + 确定性回退 |
| `brain/intent.py` | 意图解析 -> SceneConfig |
| `brain/assets.py` | 资产目录加载 + 标签/风格匹配 -> 白名单 |
| `brain/layout.py` | 布局规则生成（道路缓冲/分层/错落/拐角）+ 放置坐标 |
| `brain/plan.py` | 组装并序列化标准生成计划 |
| `brain/resource_proto.py` | 与【PR6 资源 Agent】异步通信的客户端桩 |
| `brain/mcp.py` | 引擎 TCP MCP 客户端 + 批量生成驱动 |
| `creative_brain.py` | CLI 入口 |

## 复用引擎能力

- 生成算法（`Procgen` 模块）：`dungeon.bsp` / `cave.cellular` /
  `cave.drunkard` / `maze.backtrack` / `noise.terrain` /
  `terrain.heightmap` / `wfc.simple` / `level.roguelike`。
- 材质 / 纹理：`pbr.*`、`tex.*`；网格：`mesh.marchingcubes`、`mesh.hexplanet`。
- MCP：`eve_run_script`（在活 VM 上跑 Squirrel 片段），由 `brain/mcp.py`
  直接以 TCP 调用（无需 Node 桥），与 `tools/eve-mcp` 同协议。

## 标准生成计划（GenerationPlan）

```json
{
  "config": { "...": "SceneConfig" },
  "whitelist": [ { "id": "tree", "category": "plant", "tags": [...] } ],
  "missing_assets": ["model.ruin_arch"],
  "layout_rules": [
    {"kind": "road_buffer", "params": {"buffer": 1, "network": "grid"}},
    {"kind": "layer", "params": {"layers": ["foreground","midground","background"]}},
    {"kind": "scatter", "params": {"jitter": 1.0, "asymmetry": true}},
    {"kind": "corner", "params": {"corner_weight": 2.0}}
  ],
  "steps": [
    {"action": "terrain", "target": "noise.terrain", "params": {...}},
    {"action": "set_lighting", "target": "pbr.soil", "params": {...}},
    {"action": "place", "target": "tree", "params": {"x": 3, "y": 5, "layer": "foreground"}}
  ]
}
```

## 运行

见 `README.md`。
