# Destruction Clusters — budget / cluster / snapshot

P3 烟雾：双岛锚定柱（`newClusterPillarFixture`）。岛内边较强、岛间焊边较弱；
步进预算限制每 tick 断边数；跨岛断边记入 `clusterBreakEventCount`；可用快照
保存/恢复运行时状态。

## 运行

```sh
make run/<platform>-debug GAME=examples/destruction-clusters
```

Linux headless：

```sh
cd examples/destruction-clusters && \
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ALSOFT_DRIVERS=null \
  XDG_RUNTIME_DIR=/tmp/xdg-runtime \
  xvfb-run -a ../../build/linux-debug/src/engine/eve run
```

## 操作

- 自动：启动后施加 Strain，每步最多断 1 条边（`setStepBudget(1, 1)`）。
- `R`：重置场景。
- `S`：捕获 Instance 快照 JSON（内存）。
- `L`：从上次快照恢复。
- `I`：对已 Detached 碎块施加 Impulse。
- `Z`：Sleep 场合批已沉降碎块。

## 相关

- 设计：`docs/dev/2026-09-29-chaos-destruction-geometry-collection设计.md`（P3）
- 用户文档：`docs/usr/modules/physics_destruction.md`
- 基础例：[`destruction-basic`](../destruction-basic/README.md)
