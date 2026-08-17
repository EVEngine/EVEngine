# story-scene-agent · 剧情 → 合格舞台

把「玩家 / 导演的一句话剧情」变成 **可交付、质量过关的舞台**：
自动摆道具、调光照、架摄像机、多视角截图、质检迭代修复，最终输出报告 + 渲染截图。

```
故事 ──► stage brief ──► creative-brain 生成计划 ──► scene_director 搭台
   └──► scene-qc 多视角质检 + 自动修复（ReAct，硬上限）──► 报告 + 截图
```

## 打通了哪些 AI 能力

| 组件 | 角色 | 位置 |
|------|------|------|
| `scene_director.nut` | 引擎脚本侧搭台 kit（摆道具 / 光照 / 摄像机 / 信息） | `src/scripts/` |
| 引擎 MCP 新工具 | `eve_scene_modify` / `eve_scene_info` / `eve_camera_generate` / `eve_scene_reset` | `src/engine/devtools/McpServer.cpp` |
| creative-brain | 意图解析 → 资产匹配 → 布局 → 生成计划 | `tools/creative-brain/` |
| scene-qc-agent | 机位 → 截图 → 本地小模型筛查 → 高危 VLM 复核 → 融合 → 修复 | `tools/scene-qc-agent/` |
| vision-prefilter | 本地轻量视觉预筛选（可选，可作 `local_screen` 后端） | `tools/vision-prefilter/` |
| RenderVision | 引擎内置渲染描述（`eve_render_describe`） | `src/engine/devtools/RenderVision.*` |

## 快速开始

```bash
# 1) 起引擎空舞台 + MCP
eve run --debug --mcp-port=7529 examples/ai-stage

# 2) 干跑（无引擎 / 无模型，验证闭环）
cd tools/story-scene-agent
python -m story_scene_agent.cli "月光下骑士与巨龙在古堡前对峙" --dry-run

# 3) 连真引擎搭台 + 质检（在 tools/story-scene-agent 目录下）
python -m story_scene_agent.cli \
    "月光下骑士与巨龙在古堡前对峙" --port 7529 --out-dir stage_duel
```

产物：`<out-dir>/story_scene_report.json`（含 stage / plan / build / qc / final）+ 截图。

## 参数

| 参数 | 说明 |
|------|------|
| `story` | 一句话剧情（必填） |
| `--config` | 配置文件（默认内置） |
| `--scene-id` | 场景标识 |
| `--port` | 引擎 MCP 端口 |
| `--max-rounds` | 质检迭代硬上限 |
| `--dry-run` | 不连引擎/模型，仿真闭环 |
| `--trace` | 打印 ReAct 轨迹 |
| `--out-dir` | 输出目录 |

> 以模块方式运行（`python -m story_scene_agent.cli`），以便解析相对导入与复用
> `creative-brain` / `scene-qc-agent` 同仓库代码。若直接 `python cli.py` 需自行处理
> 包路径。

## 测试

```bash
python -m pytest tools/story-scene-agent/tests -q
```

## 依赖

- 运行：Python 3.10+，**标准库即可**（复用 `creative-brain/brain` 与
  `scene-qc-agent/scene_qc_agent` 同仓库代码，无需 pip 安装第三方）。
- 质检如需远端 VLM：在 `../scene-qc-agent/config.example.json` 配
  `models.vlm_review.base_url/api_key/model`；本地小模型可配 Ollama 或
  `tools/vision-prefilter` 网关。