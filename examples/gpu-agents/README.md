# GPU Agents — 鱼群 / 生命网格 / 花瓣

演示 `eve.GpuAgents()`：在 `GpuAgentWorld` 中注册鱼群、生命网格与花瓣后端，
共享 SDF 障碍与环境场，每帧 `stepAll`。

```sh
# Linux headless smoke（见 AGENTS.md）
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export ALSOFT_DRIVERS=null
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
cd examples/gpu-agents
xvfb-run -a timeout 8 ../../build/linux-debug/src/engine/eve run
```

设计文档：`docs/dev/2026-10-05-gpu-agents-simulation-framework.md`
