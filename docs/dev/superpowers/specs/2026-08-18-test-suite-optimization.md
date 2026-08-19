# 单元测试全景分析与开销优化

> 状态：已实施；日期：2026-08-18
> 目标：系统梳理全部单元测试的目的与成本，降低测试开销、加速测试、减少内存/显存占用。
> 涉及改动：`cmake/ZeroErrDiscoverTestsImpl.cmake`、`Makefile`、`external/zeroerr`（子模块增强）、本文档。

## 1. 现状与规模

- 测试框架：zeroerr（`external/zeroerr` 子模块），C++ 用例宏为 `TEST_CASE("Suite.Case")`。
- 产物：单一体可执行 `unit_test`（115 个测试 .cpp 全部编入同一个二进制，链接全部引擎模块）。
- 用例规模：**115 个文件、1438 个用例**；其中 **52 个文件会创建窗口 / Vulkan Graphics 设备**。
- 执行方式：构建后由 `cmake/ZeroErrDiscoverTestsImpl.cmake` 跑 `unit_test --list-test-cases`，
  为**每一个用例**注册一条 CTest 测试；`make test/*` 用 `ctest -j 4` 执行。

### 开销来源（按影响排序）

| 来源 | 说明 | 量级 |
| --- | --- | --- |
| 进程启动 ×1438 | 每个用例单独起一个 `unit_test` 进程，都要加载完整引擎动态库、跑全局静态初始化 | 每次 ~50–150 ms，串行累计可达分钟级 |
| ClassicScenes 视图停留 | 17 个飞越用例默认每相 4 s × 5 相，还要按 30 Hz 睡眠节流 | 单场景 ~20 s，全套串行 ~340 s |
| ClassicScenes 基准帧数 | `perf.maxFps` 每场景 5 配置 ×（20 预热 + 120 计时）帧 @ 960×540 | 单场景 ~700 帧 |
| 每用例新建窗口/设备 | RenderImageAudit 等 38 个用例各开一个 480×360 窗口 + Vulkan 设备/交换链 | 显存、启动时间随用例数线性增长 |
| 大窗口渲染 | procgen 900×700 / 768×768、ClassicScenes 960×540、grass 960、hex 960 等 | 显存与每帧成本最高 |
| 构建 | 115 个测试文件 + 全引擎模块单目标链接 | 增量编译受公共头文件影响 |

> 说明：以上为静态分析量级；本机（Windows worktree，子模块未初始化、无编译链）无法实际构建
> 计时，CI 上全套用例“约几分钟”与上表进程 + 视图停留的估算一致。

## 2. 测试分类总表（目的清单）

分类标记：`CPU`＝纯逻辑/无窗口；`SCRIPT`＝Squirrel 脚本；`GPU`＝开窗/Vulkan；
`NET`＝网络；`AUDIO`＝音频；`SIM`＝仿真（CPU 重）。

### 2.1 基础工具

| 文件（用例数） | 目的 |
| --- | --- |
| `math.cpp` (11) | 标量/向量/矩阵、随机、噪声、贝塞尔、procgen 数学 |
| `data.cpp` (22) | JSON/XML、ByteData、LZ4、hex/base64、MD5 |
| `event.cpp` (13) | 事件队列、Variant、跨线程推送 |
| `rx.cpp` (26) | 响应式：Subject、操作符、脚本绑定 |
| `async.cpp` (8) | Promise、setTimeout、async/await 语义 |
| `thread.cpp` (18) | 线程池、Channel、生命周期安全 |
| `timer.cpp`/`timer_cpp.cpp` (4) | 定时器 C++/脚本接口 |
| `system.cpp` (6) | 系统抽象（sleep 等） |
| `i18n.cpp`/`i18n_script.cpp` (10) | 多语言加载、复数规则 |
| `filesystem.cpp`/`filesystem_cpp.cpp` (15) | 读写、挂载、watch |
| `database.cpp` (1) | SQLite CRUD/ORM/ECS 导出 |
| `spatial.cpp` (10) | 四叉树/八叉树/BSP/哈希查询 |
| `tensor.cpp` (11) | 张量运算（含 GPU 编译执行） |
| `ECS.cpp`/`ScriptECS.cpp` (20) | ECS 核心与脚本绑定 |
| `runtime.cpp` (3) | 脚本运行时栈/反射 |
| `editor.cpp` (11) | gizmo、brush、撤销历史 |
| `ik.cpp` (16) | IK 求解（2D/3D） |

### 2.2 输入与窗口

| 文件（用例数） | 目的 |
| --- | --- |
| `mouse.cpp`/`mouse_cpp.cpp` (7) | 鼠标位置/可见性/系统光标 |
| `keyboard_cpp.cpp` (5) | 键盘状态 |
| `joystick_cpp.cpp` (4) | 手柄 |
| `touch.cpp` (2) | 触摸 |
| `window.cpp`/`window_cpp.cpp` (18) | `GPU` 窗口设置、尺寸、交换链（320×240） |

### 2.3 图形与渲染

| 文件（用例数） | 目的 |
| --- | --- |
| `RenderSystem.cpp` (8) | `GPU` 2D 批处理（Batcher/可见性） |
| `RenderSystem3D.cpp` (13) | `GPU` 3D 渲染、相机、HUD |
| `RenderImageAudit.cpp` (38) | `GPU` 渲染管线逐功能图像审计（每用例 480×360 开窗） |
| `RenderSceneEffects.cpp` (21) | `GPU` 光照/阴影/IBL/朝向等效果 |
| `ClassicScenes.cpp` (18) | `GPU` 经典场景飞越 + 性能基准（960×540，耗时大头） |
| `IBL.cpp` (10)、`ClusteredLighting.cpp` (2)、`Volumetric.cpp` (15)、`AmbientOcclusion.cpp` (6)、`AntiAliasing.cpp` (6)、`Outline.cpp` (4)、`Shadow3D.cpp` (3)、`GlobalIllumination.cpp` (1)、`MaterialRenderControl.cpp` (4)、`Lighting2D.cpp` (3)、`ParallaxMap.cpp` (3) | `GPU` 各光照/后处理模块参数与像素级验证 |
| `Quad.cpp` (2)、`TextureCellBomb.cpp` (4)、`TextureSampler.cpp` (4)、`shader.cpp` (3) | `GPU` 图元/纹理/着色器 |
| `gpgpu.cpp` (6) | `GPU` 计算着色器分发 |
| `virtualgeometry_builder.cpp`/`virtualgeometry_gpu.cpp` (14) | `GPU` 虚拟几何体构建与渲染 |
| `voxel_render.cpp` (77) | `GPU` 体素渲染（切块/光照/大气） |
| `spritestack.cpp` (12) | `GPU` 精灵堆叠渲染 |
| `stylize.cpp` (7) | `GPU` 风格化后处理 |
| `grass.cpp` (13)、`hair.cpp` (4)、`Xray.cpp` (2) | `GPU` 草/毛发/X 射线着色器 |
| `graphics_font.cpp` (4) | `GPU` 字体图集与打印 |
| `model3d.cpp` (10) | `GPU` 模型加载（obj/fbx）与渲染 |
| `scene.cpp`/`sceneloader.cpp` (51) | `GPU` 场景图/加载/序列化（含 glTF） |
| `housegen_render.cpp` (2)、`demo.cpp` (4) | `GPU` 房屋生成预览、demo 音效/纹理 |

### 2.4 2D/模拟/游戏模块

| 文件（用例数） | 目的 |
| --- | --- |
| `map.cpp`/`map_path.cpp`/`map_fov.cpp` (70) | 地图分层/投影/寻路/FOV（含 GPU mask） |
| `hex_level_data.cpp`/`hex_level_simulation.cpp` (63) | 六边形关卡数据与仿真流水线 |
| `voxel.cpp` (134) | 体素数据/生成（纯 CPU，用例数最多） |
| `particles.cpp` + attach 系列 (109) | 粒子发射/附着骨骼/皮肤 |
| `box2d.cpp`/`box3d.cpp` (22) | 2D/3D 物理（含渲染预览） |
| `softbody.cpp` (5) | 布料/流体软体 |
| `rpg.cpp`/`rpg_simulation.cpp` (29) | 属性/状态/技能/结算 |
| `inventory.cpp` (10) | 物品/背包/装备 |
| `building.cpp` (9) | 建筑放置/邻接 |
| `avatar.cpp` (8) | 头像图层/Live2D/VRoid |
| `dialogue.cpp` (7) | 对话/打字机/选项 |
| `daynight.cpp` (9)、`weather.cpp` (7) | 昼夜/天气 |
| `housegen.cpp` (8)、`procgen.cpp`/`procgen_simulation.cpp` (78) | 程序化生成（网格/纹理/WFC/roguelike） |
| `roguelike_generator.cpp` (8) | roguelike 生成器 |

### 2.5 动画

| 文件（用例数） | 目的 |
| --- | --- |
| `animation.cpp` (40) | 补间/状态机/运动匹配/轨迹 |
| `animation_mixamo.cpp` (11) | Mixamo 资产导入与重定向 |
| `animation_skinned.cpp` (7) | 蒙皮（CesiumMan） |
| `animation_sprite_spine.cpp` (6) | Sprite/Spine 动画 |

### 2.6 音频 / 网络 / 热重载

| 文件（用例数） | 目的 |
| --- | --- |
| `audio.cpp` (10)、`sound.cpp` (8) | `AUDIO` 音频设备/解码/MIDI |
| `network.cpp` (11) | `NET` TCP/UDP/HTTP/Poco 会话 |
| `hotreload.cpp` (4) | 资源/脚本热重载 |

### 2.7 开发工具（桌面）

| 文件（用例数） | 目的 |
| --- | --- |
| `callgraph.cpp` (26)、`renderflow.cpp` (28) | `CPU` 调用图/渲染流追踪 |
| `debugger.cpp` (16)、`debugger_audit.cpp` (6)、`dap.cpp` (9)、`mcp.cpp` (5)、`console.cpp` (3) | `CPU` 调试器/DAP/MCP/控制台 |

### 2.8 脚本绑定（Squirrel）

`ScriptTest` fixture（`test/ScriptTest.h`）负责 `expose + compile + run`；`filesystem.nut`、
`graphic.nut`、`i18n.nut`、`model.nut`、`mouse.nut`、`window.nut`、`timer.nut`、
`simplesquirrel.nut` 为脚本侧用例（部分文件如 `graphic.cpp`、`window.cpp` 内含窗口用例）。

## 3. 已实施的优化

### 3.1 CTest 按文件分捆（bundle）执行 —— 进程数可降 1438 → ~115（opt-in）

`cmake/ZeroErrDiscoverTestsImpl.cmake` 重写：

- 每个用例仍注册 `add_test("<Suite.Case>")`（精确 `--testcase=^...$`），保留 `ctest -R` 按名定位；
- 另按源文件注册 `add_test("bundle/<文件>")`，用 `--quiet --file=.*<basename>` 在**一个进程内**
  跑完该文件全部用例，并打上 `LABELS bundle`；
- 优先使用 zeroerr 新增的机器可读列表 `--list-test-cases --list-format=plain`，
  解析 `name\tfile:line`；老版本 zeroerr 自动回退到解析 `TEST CASE [file:line] name` 输出。

`Makefile`：

- **默认仍为逐用例执行**（`-E '^bundle/'` 排除 bundle）：实测发现 GPU/窗口用例在 bundle
  进程内共享状态时会被拖慢约 70 倍（Linux CI/Lavapipe 上同一用例 5s → 370s），因此 bundle
  不作为默认路径；逐用例进程隔离才是 CI 上的快速路径（main 1551 用例 2–11 分钟）；
- bundle 保留为显式 opt-in：`make test FILTER=bundle/<文件>.cpp`（`-L bundle`）适合本地
  按文件快速跑 CPU 用例；
- `FILTER=<Suite.Case>` 保持逐用例语义。

> 权衡：逐用例模式每个用例独立进程，某个用例崩溃（段错误/abort）只影响它自己；bundle 模式
> 同文件用例共享一个进程，崩溃会让该文件剩余用例一并失败。定位时用
> `make test FILTER=<Suite.Case>`（或 `--fail-fast`）逐用例复跑即可。

### 3.2 ClassicScenes 视图/基准参数接入默认测试命令

`ClassicScenes` 已内置 `EVENGINE_VIEW_SECONDS`（默认 4 s/相）与 `EVENGINE_PERF_FRAMES`
（默认 120 帧）环境变量，但 CI 一直未设置。`make test/*` 现在默认注入：

```make
VIEW_SECONDS ?= 0.3
PERF_FRAMES  ?= 30
```

效果：17 个飞越用例从 ~20 s/场景降到 ~1.5 s/场景；`perf.maxFps` 每场景从 ~700 帧降到
~170 帧。交互式查看时可用 `make test VIEW_SECONDS=4 PERF_FRAMES=120` 恢复。

### 3.3 zeroerr 增强（子模块 `evengine-test-opt` 分支）

- `--list-format=plain`：配合 `--list-test-cases` 输出机器可读的
  `<用例名>\t<文件名>:<行号>`，无颜色、无需正则匹配 ANSI/emoji，使 CTest 发现脚本简单可靠；
- `--fail-fast` / `-F`：遇到第一个失败用例立即停止（调试 bundle 时不必等整包跑完）；
- 增强已提交在子模块本地分支 `evengine-test-opt`（`52bf74f`），并在 WSL（GCC 15）下编译验证
  通过、新参数行为正确；**父仓库 gitlink 暂未指向该提交**，保证任何环境克隆后
  `git submodule update --init` 都能成功、引擎照常编译。
- 启用方式（推送到 zeroerr 远端后）：
  ```bash
  git -C external/zeroerr push origin evengine-test-opt
  git -C external/zeroerr checkout evengine-test-opt
  git add external/zeroerr   # 在 EVEngine 侧更新 gitlink
  ```
  推送前，发现脚本会自动回退到解析旧版控制台列表，测试不受影响。

### 3.4 逐用例能力保持不变

```bash
make test FILTER=math.procgen.hashFbmVoronoi   # 单个用例（逐进程）
make test FILTER=bundle/ClassicScenes.cpp      # 单个文件 bundle
make test FILTER=graphics.print                # 前缀过滤（逐用例）
ctest --test-dir build/linux-debug -L bundle -R '^bundle/particles'  # 只看某文件包
```

## 4. 后续可选优化（未实施，附风险）

| 方案 | 收益 | 风险 / 成本 |
| --- | --- | --- |
| 为图形测试增加窗口尺寸环境变量（如 `EVENGINE_TEST_VIEW_W`/`H`），CI 用 480×270 跑 ClassicScenes/procgen | 显存与每帧成本再降 40–70% | 需逐文件改 `openGfxWindow` 默认值；像素采样阈值可能需重校准 |
| 拆分 `unit_test_core` / `unit_test_graphics` 两个二进制 | 纯逻辑/脚本测试可脱离 Vulkan/显示环境，内存占用更低 | 链接成本翻倍；`make test` 需跑两个 ctest 集合 |
| 进程内并行 runner（zeroerr `--jobs`） | 免多进程、省内存 | SDL/Vulkan/音频全局状态非线程安全，引擎测试不可用 |
| CTest `RESOURCE_LOCK` 限制并发 GPU 进程 | 峰值显存可控 | 与 `-j` 冲突，墙钟时间回退；建议仅低内存 CI 使用 |
| 将 `perf.maxFps`、视图类用例单独打 label（如 `slow`）并从默认套件排除 | 日常回归更快 | 需维护“快/慢”两套预期 |

## 5. 验证

- CMake 发现脚本：用模拟输出桩分别验证了 `plain` 与 legacy 两条解析路径，生成的 CTest 文件
  结构正确（bundle 条目 + 逐用例条目 + label）；
- Makefile：`make -n test/linux-debug`（含/不含 FILTER）与 `make -n test/<前缀>` 干跑输出正确；
- 本机无 C++ 编译器与初始化子模块，无法编译运行整套测试；zeroerr 改动经过代码审阅，
  建议在 CI/开发机先 `make test/linux-debug` 冒烟，确认各 bundle 通过后再合入。

> 注：zeroerr 增强保存在本地分支 `evengine-test-opt`，父仓库 gitlink 未改动，因此任意环境
> 都能正常编译；启用新接口只需推送该分支并按 3.3 更新 gitlink。
