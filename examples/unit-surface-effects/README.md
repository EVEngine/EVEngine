# 单位表面特效：同模型参考与逐项验收

26 种参数化效果，使用可复现的装甲测试模型、统一单位空间和独立的材质/发光/
附加几何层。完整默认值、生命周期和限制见 [内置资源说明](../../assets/styles/unit-surface/README.md)。

- [同模型概念参考](../../assets/styles/unit-surface/reference/same-mesh-26.png)：AI 生成的目标，不是引擎画面。
- [当前逐项实渲与参考对照](comparison-current/reference-vs-render.png)
- [当前 26 种实渲图册](comparison-current/rendered-26.png)
- [本轮四项：参考 / 改前 / 改后](comparison-current/alignment-before-after.png)
- [最新过热双臂与轻微燃烧对照](comparison-current/overheat-arms-before-after.png)
- [每项完整时间序列](comparison-current/sequences/)

运行：仓库根目录 `make run/linux-debug GAME=examples/unit-surface-effects`；
Windows 用 `make run/win32-debug GAME=examples/unit-surface-effects`。
也可在此目录执行 `../../build/linux-debug/src/engine/eve run --mcp-port=17529`。

## 操作

- 1–5 / 左右键：分页，每页最多六种。
- G：26 种远景导航。
- V：单效果固定近景；左右键逐项切换，H 隐藏 HUD。近景隐藏底座和地面。
- Space：暂停；N 推进 0.25 秒；B 推进 1/60 秒，检查受击/电击短时变化。
- O：切换完整效果和中性材质，包括材质层与透明队列恢复。
- M：开关单位位移；上下键：光效强度；R：时钟归零。

自动热重载关闭。修改后重新启动进程，避免将引擎热重启问题混入视觉验收。
冷启动会准备所有效果的 GPU 管线；软件 Vulkan 上需较长时间，等待 READY 和首帧。

## 逐项采集

启动新的示例进程，等待首次呈现后，在有 Pillow 的 Python 环境中从仓库根目录运行：

```sh
python3 examples/unit-surface-effects/capture_reference.py
```

采集器只使用普通按键事件与引擎 `eve_screenshot`，逐个播放再采样，每种保留
12 个时刻。受击采前几十毫秒及消退，电击覆盖闪断，时间减速覆盖较长周期。
原始 PNG、时间、SHA-256 和统一裁切框均在 `comparison-current/manifest.json`。
拼图只裁切、缩放和加标签，不修色、不补光，不使用 AI 修改实际画面。
`--compose-only` 可从既有原始帧重排图册，旧 manifest 的裁切框仍会被保留。

从新进程选择性重采已完成图册中的效果：

```sh
python3 examples/unit-surface-effects/capture_reference.py --only metallize bark
```

它会保留其他项的原始帧并重排对照，要求输出目录已有完整 manifest。

前后对照可由 `python3 examples/unit-surface-effects/compare_alignment.py` 重建；
它读取改前、改后的 manifest 与原始帧，只做相同裁切和排版。

五种代表效果可以单独采集：

```sh
python3 examples/unit-surface-effects/capture_pilot.py --output examples/unit-surface-effects/pilot-new
```

同样需要新启动的进程；先播放再按每项特点取四帧，生成与同模型参考的对照。

## 资源与验证

`mannequin.nut` 由 `generate_mannequin.py` 生成，是本例原创的 CC0 几何。它检查
法线、三角形绕序和数据有限性。所有顶点共享单位原点；CPU 材质标识使用 UV.x，
板片边缘距离使用 UV.y。传送碎块为同坐标 0.125 网格上批处理的 347 个小立方体。
新增附加几何还包括三条环管和十条 UV 定义宽度/长度的曲带，分别用于治疗/时间减速光环与暗影烟丝。
字体授权见 `assets/fonts/NOTICE.txt`。

`verify.nut` 在实际创建管线后检查 32-float 布局、参数拒绝/失败不修改状态、
实例隔离、单位框架原子更新、材质阶段和透明/实体队列分类。必须同时配置 CPU
Material 的队列与 Shader 的 GPU 混合状态，不能把混合状态当作渲染排序声明。

```sh
python3 examples/unit-surface-effects/generate_mannequin.py
bash assets/styles/unit-surface/build.sh
python3 scripts/check_examples.py
MIN_RUN_SECONDS=2 RUN_SECONDS=180 xvfb-run -a bash scripts/smoke_examples.sh unit-surface-effects
```

本机运行借用现有 `1782` 工作树引擎构建，使用 Lavapipe 软件 Vulkan；Xvfb 只提供
SDL display。不是当前工作树的全量引擎编译，也不是独显性能或 WebGPU/骨骼角色验证。
PR 基于最新 dev 整理后，完整 `make check` 通过（463 项脚本测试）；
直接相关的资源/脚本检查也通过。详细证据见 [VERIFICATION.md](VERIFICATION.md)。

PR 只收录最终证据与本轮对照；早期试验图保留在作者本地。
