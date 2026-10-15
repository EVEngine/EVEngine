# PCG Runtime Orchestration（运行时编排）

在**没有 GPU 场景**的情况下验证 Pcg 运行时三件套的数值契约：运行时盖章器
（`PcgRuntimeStamper`）、生成进度（`PcgSpawnProgress`）与任务队列（`PcgTaskQueue`）。
窗口只有 640×360、不可热重载，画面是纯 2D 矩形，因此这个示例基本等同于一个脚本级
单元测试，但走的是完整的 `eve run` 启动路径。

## 运行

```bash
cd examples/pcg-runtime-orchestration
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-runtime-orchestration
```

## 验证内容

**运行时盖章**：源高度图 3×3（全部高度 1.0）经 `loadStamp` 载入，`execute` 以
`(0, 0, 1.0, 1.0)` 盖到 5×5、初值 20.0 的目标高度图上；断言 `height(2, 2) == 6.0`
且 `getStatus() == 3`。

**生成进度**：`updateRule("Terrain", 10, 3, 4, 1)` 加 `updateRuleFraction(0.5)` 之后
`getProgress()` 必须等于 0.35（容差 1e-4）。

**任务队列**：`add(0.25)` → `tick(0.25)` → `getReadyTaskId()` 必须等于 `add` 返回的 id →
`resolveReady(true)` 收尾。

另外打印 OS 模块探测行 `PCG_HOST_OS limitFrame=... type=...`。示例保持正常帧循环，
以便 `scripts/smoke_examples.sh` 能验证它在启动后持续存活。

成功标记：`PCG_RUNTIME_ORCHESTRATION_PASS stamp=6 progress=0.35 queue=0`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 三项数值契约的构造、断言与成功标记；2D 矩形绘制 |
| `config.nut` | 引擎 `config`：640×360、`hotReload = false` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
