# whitebox-playtest · MCP 快速白盒 + 自动游玩测试

面向 Agent 的**快速白盒**工具：一条 recipe 批量搭景 / 调景，再自动注入输入并做断言。
可走引擎内置 MCP 工具，也可作为 Cursor / Claude 的独立 stdio MCP 服务。

## 能力

1. **批量搭景 / 调景**：`eve_scene_batch` / `wb_apply_scene`（一次 round-trip 多个 `scene_director.modify`）。
2. **一键游玩测试**：`eve_whitebox_playtest` / `wb_run`（reset → scene → inputs → poll → play → assert）。
3. **干跑**：`--dry-run` / `EVE_WB_DRY_RUN=1`，无需引擎即可验证 recipe 与 MCP 工具面。

## 引擎 MCP（已附着的游戏）

```bash
eve run --debug --mcp-port=7529 examples/ai-stage
```

| 工具 | 作用 |
|------|------|
| `eve_scene_batch` | `{actions:[{action,target?,params?}], stopOnError?}` |
| `eve_whitebox_playtest` | `{reset?, scene[], inputs[], waitFrames?, capture?, play[]}` |

`inputs` 若带 wait/capture，响应 `status=pending`；继续 `eve_input_poll` 直到 `ready`
（本仓库 Python runner 会自动轮询）。

## Python CLI

```bash
# 干跑（默认 corridor recipe）
python -m whitebox_playtest --dry-run --trace

# 连真引擎
python -m whitebox_playtest recipes/corridor.json --port 7529 --report build/wb_report.json
```

在 `tools/whitebox-playtest` 目录下运行，或把该目录加入 `PYTHONPATH`。

## Cursor / Claude stdio MCP

```json
{
  "mcpServers": {
    "eve-whitebox": {
      "command": "python3",
      "args": ["-m", "whitebox_playtest", "--mcp-stdio"],
      "env": {
        "PYTHONPATH": "tools/whitebox-playtest",
        "EVE_MCP_HOST": "127.0.0.1",
        "EVE_MCP_PORT": "7529"
      },
      "cwd": "/absolute/path/to/EVEngine"
    }
  }
}
```

暴露的工具：`wb_run`、`wb_apply_scene`、`wb_playtest`、`wb_default_recipe`。
干跑调试可加 `"EVE_WB_DRY_RUN": "1"`。

## Recipe 形状

```json
{
  "schemaId": "eve.whitebox.playtest-recipe",
  "schemaVersion": 1,
  "id": "blockout-corridor",
  "reset": true,
  "scene": [
    {"action": "add_object", "target": "wall_0",
     "params": {"id": "wall_0", "kind": "box", "pos": [0,1,0], "scale": [4,2,0.4]}}
  ],
  "inputs": [
    {"op": "key", "key": "W", "down": true},
    {"op": "wait", "frames": 3},
    {"op": "key", "key": "W", "down": false}
  ],
  "waitFrames": 1,
  "play": [{"op": "status"}],
  "assert": [
    {"path": "scene.applied", "op": "gte", "value": 1},
    {"path": "info.count", "op": "gte", "value": 1}
  ]
}
```

`assert` 支持 `eq|ne|gt|gte|lt|lte|contains|truthy|falsy`，路径用点号访问报告字段。

## 测试

```bash
cd tools/whitebox-playtest
python -m unittest discover -s tests -p "test_*.py"
```

## 与现有组件的关系

| 组件 | 差别 |
|------|------|
| `story-scene-agent` | 剧情 → 质检舞台（视觉 QC），不是输入游玩 |
| `scene-qc-agent` | 多视角截图质检 / VLM |
| `eve_input_batch` | 底层输入注入；本工具在其之上编排 |
| `examples/level-designer` | 人类交互白盒编辑器；本工具是 Agent MCP 路径 |

推荐舞台：`examples/ai-stage`（已装 `scene_director`）。
