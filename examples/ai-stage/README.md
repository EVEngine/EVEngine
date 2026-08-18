# AI Stage

一个"空舞台"示例：启动时把 `scene_director` 搭台 kit 安装进虚拟机，并摆好默认地面 /
光照 / 摄像机。AI Agent（`tools/story-scene-agent`、`creative-brain`、
`scene-qc-agent`）通过引擎 MCP 直接往这个舞台上摆放道具、调整摄像机、截图质检。

## 运行

```bash
make build/win32-debug
# 或先起引擎 MCP：
build/win32-debug/src/engine/eve.exe run --debug --mcp-port=7529 examples/ai-stage
```

## Agent 接入

```bash
# 一句话剧情 → 搭台 → 质检迭代 → 报告 + 截图
cd tools/story-scene-agent
python -m story_scene_agent.cli "月光下骑士与巨龙在古堡前对峙" --port 7529 --out-dir stage_duel
# 干跑（无需引擎/模型）验证闭环
python -m story_scene_agent.cli "月光下骑士与巨龙在古堡前对峙" --dry-run
```

详见 [`docs/dev/AI场景导演.md`](../../docs/dev/AI场景导演.md)。