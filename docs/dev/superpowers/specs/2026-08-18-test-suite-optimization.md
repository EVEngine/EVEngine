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

## 6. 分域链接的实测结论（2026-09-19，实施后回填）

§1 的规模数字已过时（当时 115 个测试 `.cpp` / 1438 用例；实测时已到 **789 个文件 / 5188 用例**，
注册 CTest 条目 6058 条）。据此把 `unit_test` 拆成 `unit_test_<domain>`（见
`test/test_domains.cmake`、`scripts/test_domains.py`），并在 Windows/worktree 上做了完整配置 + 编译 + 链接的实测。

### 6.1 实测数据（`unit_test_climbing`，该域 4 个测试文件）

| 产物 | 单体（789 测试） | `unit_test_climbing` + `/INCREMENTAL` | `unit_test_climbing` + `/INCREMENTAL:NO` |
| --- | --- | --- | --- |
| `.exe` | 362.5 MB | 218.4 MB | **176.3 MB** |
| `.ilk` | 2309.5 MB | 1819.4 MB | **不生成（0）** |
| `.pdb` | 1831.2 MB | 1216.7 MB | 1217.1 MB |
| 测试侧 `.obj` | 792 个 / 2.36 GB | **6 个 / 18.3 MB** | — |
| 全量链接耗时 | — | — | **50.2 s** |

> 口径说明：单体的数字取自主仓构建（`EVENGINE_COMPILER_CACHE=OFF`，编译用 `/Zi`），
> 分域构建启用 sccache（`/Z7` Embedded），因此 `.exe`/`.pdb` 的横向差值含口径差异。
> 但结论对口径不敏感：**测试代码只占 `.exe` 的 40%，引擎闭包占 60%** —— 只有 4 个测试的域，
> `.exe` 仍是单体的 60%，`.ilk` 仍有 1.8 GB。

### 6.2 结论：拆分不是省磁盘的手段，`/INCREMENTAL:NO` 才是；两者必须配对

- **只拆分**：每个域各留一份 `.ilk`/`.pdb`。若 30 个域都建出来并保留，估算 `.exe` ≈ 5.2 GB、
  `.pdb` ≈ 35.7 GB、`.ilk` ≈ 53.3 GB —— **比单体（约 4.5 GB）差一个数量级**。测试文件数量
  对链接产物大小几乎没有影响，因为决定映像大小的是引擎闭包。
- **只 `/INCREMENTAL:NO`**：`.ilk` 全盘归零（省 ~45 GB），代价是每次链接都变成全量链接，
  对单体（0.35 GB 映像）是分钟级。
- **拆分 + `/INCREMENTAL:NO`**：既没有 `.ilk`，每次全量链接又只有 **50 秒**。这才是正解 ——
  拆分的真正价值是**让关掉增量链接变得可承受**，而不是自己省磁盘。

### 6.3 由此确定的工作流与后续

- 正确用法是「**建一个域 → 跑它 → 删掉**」：`make unit-test/win32-debug DOMAIN=<域>` +
  `ctest -L unit_test_<域>`，**不要**把 30 个域都建出来留着。
- 下一个瓶颈是 `.pdb`（1.2 GB/域，几乎全是引擎闭包的调试信息）。要压它需要
  `/DEBUG:FASTLINK`，或让 per-domain 测试构建不带引擎调试信息 —— 这是新的待办，不是本方案能覆盖的。
- **实测发现的坑（已修）**：`TEST_INCLUDE_FILES` 登记了全部 30 个 `<target>_zeroerr_tests.cmake`，
  而每个文件由该目标的 POST_BUILD 写出；只建一个域时另外 29 个文件不存在，`ctest` 会直接
  以 include 错误中止，使「只建一个域再跑它的测试」完全失效。修法是在配置期先写空 stub，
  目标链接后由 POST_BUILD 覆盖。**这个问题只有真编译一个域才会暴露。**
- 另需注意：CMake 的对象路径上限为 250 字符。把 worktree 放在深目录（如
  `...\EVEngine--worktrees\EVEngine--<长名>--worktree`）会让 `third-party` 的 mpg123 对象
  路径达到 260 字符而编译失败；worktree 与构建目录应使用短路径。

## 7. layer 组 DLL：入口符号与导出面的实测（2026-09-19）

§6 的分域拆分解决了「按域重建」的开销，但每个域的 `.pdb` 仍要复制一份引擎闭包的调试信息
（1.2 GB/域）。按 layer 把模块聚合成少量 DLL 就是为了把这份调试信息收进少数几个 PDB：
`EVE_LINK_GROUP_TABLE`（`cmake/module_manifest.cmake`）把启用的模块分成 7 组 ——
`EVFoundation`(-1,0)、`EVPlatform`(1)、`EVBackends`(2,3)、`EVWorld`(4)、`EVDomains`(5)、
`EVOrchestration`(6)、`EVEditors`(7)；模块本身仍是 OBJECT 库，DLL 只是链接聚合
（`cmake/link_groups.cmake`）；导出面仍然只有 `EVENGINE_API`（`src/engine/common/Export.h`），
不开 `WINDOWS_EXPORT_ALL_SYMBOLS`（实测 EVFoundation 全量导出 75,885 个符号，超过 MSVC 的
65,535 上限，LNK1189）。

### 7.1 组 DLL 链接失败的真实原因：静态库里定义了 DLL 入口符号

**现象**：`EVFoundation.dll`（组内模块 OBJECT 库 + 55 个第三方/系统库）链接失败，202 个
`LNK2019`，全部出自 `MSVCRTD.lib(utility.obj)` / `init.obj` / `error.obj` / `pdblkup.obj`，
符号是 `__acrt_initialize`、`__vcrt_initialize`、`_CrtDbgReport`、`strcpy_s`、
`__stdio_common_vsprintf_s`、`__vcrt_GetModuleFileNameW` 等 UCRT/VCRuntime 符号；
`/VERBOSE:LIB` 显示 `ucrtd.lib` / `vcruntimed.lib` **从未被搜索**（对照：`cl /LD /MDd` 的
空 DLL 会依次搜索 `MSVCRTD.lib` → `OLDNAMES.lib` → `vcruntimed.lib` → `ucrtd.lib`）。

**排除（均为实测，非推断）**：
- 不是 `/ENTRY:DllMain`：全树（含 `third-party/`、`external/`）的 CMake 与所有 `.obj`/`.lib`
  都没有 `/ENTRY`，`build.ninja` 的 `LINK_FLAGS` 只有 `/machine:x64 /debug /INCREMENTAL`，
  失败链接的输出里也没有任何 `DllMain` 相关条目。
- 不是「对象当源码传入」：`link_group.cpp.obj` + 全部库能链通，再加上模块对象就失败；两种写法
  都只是把 `.obj` 放到链接命令行上。
- 不是 Box3D 的静态 CRT：该项已按上游改为动态 CRT（`cmake/patches/box3d-dynamic-crt.patch`），
  改后失败点不变。
- 最小复现：`cl /LD /MDd` 一个只调用 `SDL_Init` 的 DLL 并链接 `SDL2d.lib`，复现同一批错误；
  进一步地，**手工写一个空的 `_DllMainCRTStartup`**（不引入任何第三方库）也能复现 ——
  只要入口符号由别处提供，链接器就不再拉 CRT 的启动对象，而 `ucrtd.lib` / `vcruntimed.lib`
  的 `/DEFAULTLIB` 请求正写在 CRT 启动对象里，于是整个 UCRT 未解析。

**根因**：SDL2 静态库在 `HAVE_LIBC` 未定义时（MSVC 下 CMake 的 `LIBC` 默认 OFF）会在
`src/SDL.c` 编出一个无 CRT 的 `_DllMainCRTStartup`（SDL bug 4034 为此提供了守卫宏
`SDL_STATIC_LIB`）。命令行上的库先于默认库被搜索，链接器于是把它当成我们 DLL 的入口。

**修法**：新增 `cmake/patches/sdl2-static-library-crt-entry.patch`（第 12 个 third-party 补丁，
经 `cmake/third_party_build.cmake` 在构建期打入）：给 `SDL2-static` 定义 `SDL_STATIC_LIB`，
并删掉那三行 `STATIC_LIBRARY_FLAGS /NODEFAULTLIB`（上游 commit 26a56a4 在同一处做的改动）。
选用窄守卫宏而不是上游后来的 `PUBLIC HAVE_LIBC=1`，是因为引擎**直接链接 `SDL2d.lib`**、不走
SDL 导出的 CMake target，`PUBLIC` 定义传不到我们的 TU，会让 SDL 自己的 TU 与引擎看到不同的
SDL 头：实测 `#include <SDL.h>` 的预处理输出在 `-DHAVE_LIBC=1` 下多出 8,752 行差异
（拉进 `sys/types.h`、`vadefs.h` 等），而 `-DSDL_STATIC_LIB` 的输出**逐行相同（0 行差异）**，
且全树只有 `src/SDL.c` 引用它。

**验证**：重建并安装 SDL2 后 `SDL2d.lib` 不再含 `_DllMainCRTStartup`（全量扫描：其余第三方库
本来也没有）；`EVFoundation.dll` 链接通过（44.8 MB，PDB 231.3 MB），`dumpbin /dependents` 显示
`ucrtbased.dll`、`VCRUNTIME140D.dll`、`MSVCP140D.dll`（CRT 初始化确实接上了），入口点是
`_DllMainCRTStartup` —— 这次来自 CRT。

### 7.2 跨组符号面（已枚举，2026-09-19）

方法：从 `build.ninja` 的 7 条组 DLL 链接语句取出每组自己的 `.obj`（分别 206 / 152 / 195 / 254 / 388 /
123 / 84 个），用 `dumpbin /symbols` 汇总每组的 `External` 定义集与未定义集，再按切口分类：

- **需要导出**（本组未定义、下组已定义）：`EVPlatform` 230、`EVBackends` 170、`EVWorld` 387、
  `EVDomains` 761、`EVOrchestration` 419、`EVEditors` 460，去重后 **1,848 个符号 / 349 个 owner 类型**。
- **只能靠改分层**（只在上层定义、下层拿不到）：**3 个**，全在 `EVPlatform` 切口，都是
  `eve::scene::Scene` 的 `pickScreenAt` / `collectFrustumIdsAt` / `applyPcgTerrainCullingAt`：
  声明在 `src/modules/scene/Scene.h`（scene，L1），实现却在 `src/modules/graphics/ScenePicking.cpp`
  （graphics，L4）。这是真实的上行引用，导出救不了 —— 已按「低层经接口/能力调用高层」改成 capability：
  新增 `ISceneCameraProjection`（由 scene 声明），graphics/ScenePicking.cpp 注册，三个入口留在
  scene 并显式处理 provider 缺失（空 id / 空表 / `DiagnosticCode::Unsupported`），详见提交 `0365d7552`。

校准：`EVPlatform` 这一刀有 ground truth（那次失败链接报出的 153 条未解析符号）。上表对这 153 个的
**召回率 1.000**；反方向 230 个里有 77 个是假阳 —— 它们只出现在被链接器丢弃的 COMDAT（未使用的内联
函数体）里，标注同一批 owner 时会被一并覆盖，无害。

结论：既定路线（7 组命名 DLL + `EVENGINE_API`）的代价是**约 1,850 处跨组导出标注**（不是数万）
加 3 个上行引用的重构；磁盘收益不变（引擎调试信息只存一份）。标注按 owner 类型做（349 个），
所以实际改动是数百个头文件里的类/函数声明。完整清单用同样方法可复现（枚举脚本落在工作目录，未入库）。

### 7.3 运行时发现与待办

组 DLL 输出到 `${CMAKE_BINARY_DIR}`（`cmake/link_groups.cmake`），而 `unit_test_<域>.exe` 在
`${CMAKE_BINARY_DIR}/test/`、`eve.exe` 在 `${CMAKE_BINARY_DIR}/src/engine/`，CTest 的
`WORKING_DIRECTORY` 是仓库根（`cmake/ZeroErrDiscoverTests.cmake`）——三者都不同目录，Windows 的
DLL 搜索不会去找 `${CMAKE_BINARY_DIR}`，所以组 DLL 在**运行时**还找不到。这需要在「运行时产物放
同一目录」与「给 `add_test` 的 `ENVIRONMENT` 加 PATH」之间定一个做法，然后才能真正跑起来。

其余待办：EVThirdParty（把第三方库也收进一个共享库）；按域裁剪链接闭包（否则每个域仍要拉 7 个
组 DLL 的全部导出）；`DEPS` 与真实调用对齐后重定 `LAYER`（`rpg` 可放宽到 [1..99]，`npc_ai` ≤1）。

**链接策略（2026-09-19 定，已落进 `Makefile` / `CMakeLists.txt` / `AGENTS.md`）**：开发用动态
（`EVENGINE_MODULE_LINKAGE=SHARED`，改一个域只重链测试可执行文件），release / SDK 用静态 ——
`Makefile` 的 `WIN32_CMAKE_ARGS` 显式传 `-DEVENGINE_MODULE_LINKAGE=OBJECT`，所以 `make build/win32`
与 `make sdk/win32` 的产物一定是单个 exe、旁边不带引擎 DLL。CMake 默认值仍是 `OBJECT`，等 7 个组
DLL 真正链通（= §7.2 的跨组 `EVENGINE_API` 标注完成）后再把它翻成 `SHARED`；在那之前可用
`CMAKE_EXTRA_ARGS="-DEVENGINE_MODULE_LINKAGE=SHARED"` 自行启用，但要知道：SHARED 下每个测试目标对
`EVE_LINK_TARGETS` 有 order-only 依赖，组 DLL 没链通前**任何测试都编不出来**。

debug SDK（`make sdk/win32-debug`）**沿用开发配置，即动态**：它从 `build/win32-debug` 安装，会连 7 个
组 DLL 一起发布，所以「运行时 DLL 发现」必须先解决（§7.3）。只有 release SDK 承诺单个自包含 exe。

### 7.5 逐组迁移的固定流水线与两条坑（2026-09-19，实施中记录）

每刀（cut N = 让第 N 个组 DLL 链通）必须走完这五步，少一步都会在更高层复发：

1. `python annotate_group.py <GROUP> --apply` —— 按 `cross-group-symbols.txt` 标注该切口需要的 owner 类型；
2. `python add_export_includes.py --apply` —— 给首次用宏的头文件补 `#include "common/Export.h"`；
3. `python rewrite_group_api.py` —— 把裸 `EVENGINE_API` 换成该文件所属组的宏。**漏掉这步，被标注的类在消费方仍是 dllexport**（模块对象始终定义 `EVENGINE_ENGINE_EXPORTS`），会重演数据符号 LNK2001；
4. `python apply_inline_variant.py` —— 没有类外成员定义的头文件类型改用 `_INLINE`，否则消费方 dllimport 必然 LNK2019（vtable / 隐式特殊成员 / `static constexpr` 都是 vague linkage，拥有者组无物可导出）；
5. `ninja <GROUP>.dll` 反复修到零未解析，再跑 `C:\evs` 的 `ninja eve` 做 OBJECT/静态回归。

两条必记的坑：

- **数据符号**（`Module_REG` 的 `<Module>::name`、`capabilityName` 这类静态数据）：导入库里数据只有 `__imp_<data>`，函数才有 thunk。所以这类声明绝不能以 dllexport 出现在消费方 TU 里，必须归到拥有者组的宏。
- **类级 `dllexport` 会实例化全部成员函数**：持有 `vector<unique_ptr<T>>` / `deque<unique_ptr<T>>` 的类会因此撞 C2280（`vector::operator=` 声明不受约束、函数体才失败，隐式拷贝赋值没有被 deleted，于是强制实例化即硬错）。修法是显式写出四个特殊成员 —— 删拷贝、default 移动，语义不变；含裸 `unique_ptr`/`mutex`/`atomic` 成员的类不受影响（隐式拷贝赋值本来就是 deleted，无函数体可实例化）。

进度（2026-09-19）：EVFoundation / EVPlatform / EVBackends 三个组 DLL 已链通（导出 471→461，依赖含 `ucrtbased.dll` / `VCRUNTIME140D.dll` / `MSVCP140D.dll`），OBJECT 模式 `eve.exe` 每刀后都回归通过；其余四组按上面流水线推进。
### 7.6 运行时 DLL 发现与 SHARED 消费者实测（2026-09-20）

实现（都不改现有产物布局）：`cmake/ZeroErrDiscoverTests.cmake` + `Impl` 在 SHARED 时给每条生成的 CTest 项加 `ENVIRONMENT "PATH=<build root>;$ENV{PATH}"` —— 生成文件会被 CMake 二次解析，所以生成期必须两步转义（`\`→`\\`，否则 `C:\Users` 的 `\U` 让整个 CTestTestfile 加载失败；`;`→`\;`，否则 ENVIRONMENT 列表被拆散），并且 POST_BUILD 的 discovery 自身要 `set(ENV{PATH} ...)`，否则 ninja 在 POST_BUILD 阶段就以 `0xC0000135` 失败、根本生成不出测试清单。Makefile 新增 `eve-dll-path`（用 `$(wildcard …/EVFoundation.dll)` 探测，静态路线展开为空）给 `run/win32*` 与 `debug/win32` 前插 PATH（Git bash 下必须用 `$$(cd … && pwd)` 得到 `/c/…` 形式，`C:/…` 会被 MSYS 当相对段而失效）。`cmake/link_groups.cmake` 在 SHARED 时加 `install(TARGETS <group> RUNTIME bin ARCHIVE lib)`，让"动态 debug SDK"自带组 DLL；release SDK 走 OBJECT，文件开头的 `if(NOT SHARED) return()` 保证不生成任何组安装规则。

实测（`unit_test_platform`）：exe **4.71 MiB**（同域静态产物曾 218 MiB）；`dumpbin /dependents` 直接导入 EVEditors / EVBackends / EVPlatform / EVFoundation.dll（其余是间接依赖，进程启动即 7 个组 DLL 全部加载）；`ctest -L unit_test_platform -E '^bundle/'` **27/27 通过**（含 bundle 33/35，2 个失败是 bundle 同进程路径的 SEGFAULT，`make test` 默认排除，未定位）；反向对照：PATH 去掉 build 根 → `0xC0000135 STATUS_DLL_NOT_FOUND`，加上即通过；静态 `C:\evs ninja eve` 仍 exit 0 且无组安装规则。

**动态路线尚未覆盖的面（后续工作量的硬数字）**：30 个域里只有 platform 能链，29 个失败 —— LNK1120 合计 5806、去重后 **5263 个未导出符号**，分布在 144 个命名空间（graphics 1138 / procgen 925 / rpg 565 / physics 343 / animation 313 / core 303 / editor 247 / tactics_rts 240 / ui 210 / map 192 / combat 171 / building 162 / card 133 / scene 128 / fluids 126 / dialogue 125 …）。仍有 **22 个模块目录零 `EVENGINE_API` 标注**（authority, card, climbing, database, demo, dialogue, dnut_interpreter, font, hexmap, math, npc_ai, plugins, policyregistry, rpg, rts, rx, snow, statepatch, steering, system, tactics, vehicle）。注意按"零标注模块"计数会**低估**：已部分标注的 graphics/procgen/editor 等仍漏了大量脚本绑定入口，工作量以 LNK1120 为准。补齐这一面之后，"开发默认动态（`EVENGINE_MODULE_LINKAGE=SHARED`）"才真正可用；release/SDK 侧不受影响（`WIN32_CMAKE_ARGS` 已钉 `OBJECT`）。
### 7.7 SHARED 测试面标注：第一批六个域（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E '^bundle/'`） |
| --- | --- | --- | --- |
| agent | 8 → 0 | 4.73 MiB | 13/13 通过 |
| climbing | 11 → 0 | 4.76 MiB | 14/14 通过 |
| devtools | 12 → 0 | 9.31 MiB | 145/145 通过 |
| scripts | 12 → 0 | 5.92 MiB | 72/72 通过 |
| particles | 16 → 0 | 5.61 MiB | **25/34（9 失败）** |
| audio | 33 → 0 | 5.80 MiB | 51/51 通过 |

改动 34 个头文件 / 50 行组宏 / 29 处 `Export.h` include（21 个类、29 个自由函数；`_INLINE` 0、特殊成员 0）。`unit_test_platform` 重链后仍 27/27；`C:\evs` 的 OBJECT `ninja eve` exit 0。

流水线补丁（§7.5 的第 2、3 步需要按此修正）：
- `add_export_includes.py` 只扫 `.h`，**`.hpp`/`.inl`/`.ipp` 会漏**（devtools 因此编出 `class EVENGINE_API_FOUNDATION AgentDevelopmentSession`，C2079/C2270 一片）；改用覆盖这些后缀的版本。
- 按符号名找声明点**会命中注释**（`/** One live decal instance … */` 被当成成员 `instance`）：placer 必须先屏蔽注释、字符串与 raw string 再建索引。
- 同名重载要判 AMBIGUOUS 并交给人工（`ParticleEmitter.h` 的 `advanceEmitterSim` 两行），不要乱挑一个。

**particles 的 9 个失败是 DLL 拆分本身造成的（不是标注问题）**：全部报 `SDL_Vulkan_GetInstanceExtensions failed: Video subsystem has not been initialized`。根因是第三方静态状态被复制——`build.ninja` 里 7 条组 DLL 链接语句都链了 SDL2 静态库，实测 7 个组 DLL 与测试 exe 每个都各含一份 SDL，而 SDL 的 `_this` 是 `SDL_video.c` 的静态全局：`SDL_InitSubSystem(SDL_INIT_VIDEO)` 发生在 window 模块（EVPlatform 组 DLL 的副本），`SDL_Vulkan_GetInstanceExtensions` 却跑在 graphics 模块（EVBackends 组 DLL 的副本）里。平台域之所以 27/27 是因为它没有 Vulkan 窗口用例。**结论：窗口/Vulkan 类域要真正跑通，必须先做 §7.3 的第三方共享化（EVThirdParty 收进一个共享库）或把 Vulkan 实例创建与 SDL video 初始化收进同一组 DLL**；这一步不能靠补标注解决。
### 7.8 SHARED 测试面标注：第二批六个域（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E '^bundle/'`） |
| --- | --- | --- | --- |
| tensor | 34 → 0 | 4.83 MiB | 18/18 通过 |
| pixelworld | 39 → 0 | 5.95 MiB | 59/59 通过 |
| asset | 52 → 0 | 26.94 MiB | 122/122 通过 |
| npc_ai | 67 → 0 | 6.13 MiB | 53/53 通过 |
| network | 77 → 0 | 4.96 MiB | 26/26 通过 |
| voxel | 33 → **1** | 未生成 | 未运行（见下） |

合计 302 → 1，已链通的五个域 ctest **278/278 通过、0 失败**。改动 91 个站点 / 70 个头文件（类 55 + 自由函数 36）、63 处 `Export.h` include、`_INLINE` 0、手工 3 处 C2280 四特殊成员（`TcpSocket`、`TargetingPipeline`、`ReliablePixelChunkReceiver`）、2 处 friend 补宏。`C:\evs` 的 OBJECT `ninja eve` exit 0。

两条新坑（§7.5 的第 2/3 步再次修正）：
- **同一实体的每条声明都要带宏**：类内 `friend` 前置声明与命名空间声明各出现一次时只给后者加宏会 `C2375 重定义；不同的链接`（asset 2 处）。跨文件同名扫描有大半是不同命名空间的同名成员函数，不能自动套用。
- **`_INLINE` 判据不能用字符窗口**：按"声明后 300 字符内出现 `(`…`)`"判断会把长参数表的 `EvpackGraphicsLoader::loadMesh` 误判成 header-only，给出 `_INLINE` 后消费方不导入 —— 正是 `_INLINE` 要避免的 LNK2019。权威判据是"该类型在某个 .cpp 里出现过 `Name::` 定义"。

**测试侧跨 link unit 的依赖（新面）**：`voxel` 只差 `?saveImagePng@@YA_N…` —— 声明在 `test/RenderImageAudit.h:88`、定义在 `test/RenderImageAudit.cpp:572`，该 .cpp 归 **graphics 域**，而 `voxel_render_scenes.cpp`（经 `VoxelRenderFixtures.h`）在 **voxel 域**，跨 link unit，加任何引擎宏都无效。同形状风险还有 `test/water_scene_fixture.h::createStylizedWaterScene`（graphics+procgen）、`GraphicsParitySupport.h`（graphics+weather）、`ScriptTest.h`（12 个域）。修法二选一：给每个域都编一份共享测试 TU（照 `test/main.cpp` / `nut_scripts.cpp` 的 `SHARED_RUNNER` 机制，需同步改 `test/CMakeLists.txt` 与 `scripts/test_domains.py` 的共享文件表），或把这族辅助改成 header-inline。
### 7.9 SHARED 测试面标注：第三批四个域 + 测试侧共享 TU（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E '^bundle/'`） |
| --- | --- | --- | --- |
| weather | 84 → 0 | 5.45 MiB | 47/47 通过 |
| fluids | 126 → 0 | 12.02 MiB | 239/239 通过 |
| scene | 120 → 0 | 7.29 MiB | 69/70（1 失败，见下） |
| card | 133 → 0 | 6.10 MiB | 30/30 通过 |
| voxel（结构性修复后） | 1 → 0 | 7.73 MiB | 185/185 通过 |

标注 69 个站点 / 66 个头文件、64 处 `Export.h`；手工 4 类：重载歧义（`VolumeFluidEmitter.h` 四条声明全标）、placer 漏点（`PcgGrowth.h:19`，行首被 doc 注释推成缩进）、C2280 四特殊成员（`Fluids`、`Card`）、以及下面第 2 条新坑。静态 `ninja -C C:\evs eve` exit 0。

**两条必须加进 §7.5 流水线的新坑**：
1. **声明上的宏只有在「定义所在 TU」看得见声明时才生效**。`graphics/GraphicsCapabilities.cpp` 定义 `registerGraphicsCapabilities()` 却不 include 自己的头，于是声明虽带 `EVENGINE_API_BACKENDS`，EVBackends.dll 仍不导出（dumpbin 无符号、.obj 无 `/EXPORT`）。类级宏天然免疫（成员定义必须 include 类头），**自由函数不免疫**；应固化一条「用 include 传递闭包复查被标注的自由函数」的检查。
2. **Ninja 的 unscanned 规则让改头文件不触发重编**（模块 OBJECT 库没有 depfile）：标注完只重链会拿到陈旧 .obj，表现为"明明标了还 LNK"。本轮因此删掉 23 个受影响模块的 499 个 .obj（脚本 `b3_clean_modules.py`）才拿到真实结果。**每一刀标注后都应清理受影响模块对象再链接**，并把它写进流水线第 5 步。

**测试侧跨 link unit 的结构性修复（已实施）**：`scripts/test_domains.py` 新增 `SHARED_SOURCES`（共享源不参与分区，输出 `EVE_TEST_DOMAIN_<domain>_SHARED_SOURCES`）+ `test/CMakeLists.txt` 消费 + `CMAKE_CONFIGURE_DEPENDS`（否则改规则表不会重新生成分区）；`saveImagePng` 从含大量 TEST_CASE 的 `RenderImageAudit.cpp` 拆到无 TEST_CASE 的新 TU `test/RenderImageAuditIo.cpp`，仅 graphics / procgen / voxel 三个消费域编译它；`check_test_manifest.py` 增加契约校验（16 个单测通过）。选共享 TU 而非 header-inline（避免把引擎头塞进 3 域 8 文件；同形状的 `test/water_scene_fixture.h` 只需一行表项）；shared test library 方案不可行（会把 graphics 用例带进每个域）。

**修正 §7.8 的一处说法**：`test/ScriptTest.h` 不是跨 link-unit 问题 —— 该头全内联，唯一类外依赖 `ModuleManager::expose` 已是 `EVENGINE_API_FOUNDATION`；三个使用域（platform 27/27、scripts 72/72、card 30/30）都 0 未解析。

**scene 的那 1 个失败属于跨 DLL 静态状态复制（与 §7.3/§7.7 同族）**：`editor.automation_publishes_material_transactions_to_live_renderable` 返回 `Renderable3D handle is missing or stale`。根因是 `external/ECS.hpp` 的 `inline Table &default_table(){ static Table t; … }` —— 头内联的函数局部静态单例，每个 link unit 一份：实体由 graphics（EVBackends）写进它那份 table，material_editor（EVEditors）读自己那份，于是判定 stale。引擎宏无解，必须等 §7.3 的第三方共享化或把默认 ECS table 收归引擎自持。窗口/Vulkan 类域还会叠加 SDL 的同类问题。
### 7.10 SHARED 测试面标注：dialogue / building（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E "^bundle/"`） |
| --- | --- | --- | --- |
| dialogue | 119 → 0 | 5.61 MiB | 39/39 通过（含 bundle 55/55） |
| building | 160 → 0 | 9.18 MiB | 92/96（4 失败，见下；含 bundle 101/107） |

40 个类站点 + 13 个自由函数站点、34 处 `Export.h`、1 处手工特殊成员（`BuildingPlacementTarget`）、`_INLINE` 0、未定位 0。`C:\evs` 的 OBJECT `ninja eve` exit 0。

**building 的 4 个失败 = 跨 DLL 单例复制，已用对照实验定性**：失败点都是测试 TU 自己的 `ecs::View<Renderable3D,…>` 数不到实体（同用例在 DLL 侧操作全部成功）。证据链：`external/ECS.hpp:160` 的 `inline Table &default_table()` 是函数局部 static，每 link unit 一份（.obj 级实测：259 个目标中 27 个各持一份，`unit_test_building` 2 份、`EVBuilding` 1 份、`EVGraphics` 44 份）；`Renderable3D` 是 `EVENGINE_API_BACKENDS`，实体建在 EVBackends.dll 那份表里。**决定性对照**：同一份源码在 OBJECT 单链接单元下 `ctest -R "^buildingfx\."` = **18/18 全通过**。结论与 §7.9 的 scene 那条同族，引擎宏无解，必须等 §7.3 第三方共享化或把默认 ECS 表收归引擎自持。

**三条新坑（并入 §7.5 流水线）**：
1. **OBJECT 模式下类级标注的第二变体**：类里持有 `unique_ptr<前向声明类型>` 时，dllexport 会在每个 include 该头的 TU 里实例化析构 → `C2027/C2338 can't delete an incomplete type`（实测 `building/editing/BuildingTarget.h::BuildingPlacementTarget` 的 `unique_ptr<placement::PlacementWorld>`）。修法（语义不变）：头里声明析构与移动、删拷贝，在已 include 完整类型的 `.cpp` 里 `= default` 定义。
2. **清理陈旧对象的脚本要同时覆盖两个对象根**：`src/engine/CMakeFiles/<LIB>.dir` 与 `src/modules/CMakeFiles/<LIB>.dir`（`b3_clean_modules.py` 只扫后者，`common/Container.h` 曾因此漏清、留下陈旧 `.obj`）。
3. **`ctest -E '^bundle/'` 在 cmd 下单引号会静默失效**（bundle 被一起跑），必须写双引号 `-E "^bundle/"`。因此此前各批报告里"已排除 bundle"的口径要按此重读：本刀 dialogue 不排除是 55（含 16 个 bundle），building 是 107（6 失败 = 4 个用例 + 2 个 bundle 复跑）。
### 7.11 SHARED 测试面标注：combat / map（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E "^bundle/"`） |
| --- | --- | --- | --- |
| combat | 163 → 0 | 10.86 MiB | 150/151（1 失败，见下） |
| map | 165 → 0 | 9.42 MiB | 109/109 通过 |

327 个符号收敛到 77 个不同声明点 / 56 个文件：类/结构体组宏 52、自由函数 25、friend 补宏 1（`ResourceAccount.h` 的 `allocateAccountNonce`，否则 C2375）、四特殊成员 1（`ActionTimelineDocumentWorkspace`，`vector<Tab>` + `unique_ptr` → C2280）、`Export.h` include 53。口径修正：§7.6 表里 combat/map 的 171/192 是"29 个域一起链"的汇总口径，逐域实测为 163/165。

**combat 的唯一失败与 §7.9/§7.10 同族**：`actionPrefabRuntime.spawnsRealRenderablePoolsAndAppliesThreeLifecycles` 断言 `renderableCount(true) == 1` 得到 0（Vulkan 初始化正常，不是 SDL 族）。`external/ECS.hpp:160` 的 `inline default_table()` 每 link unit 一份（SHARED 实测 **28 个 link unit / 198 个 .obj** 各持一份），而 `Renderable3D::create()` 定义在 EVGraphics（`Renderable3D.cpp.obj` plain=8、`__imp_`=0），测试 TU 读自己那份表 → 数不到实体。**决定性对照**：OBJECT 单链接单元 `unit_test_combat` **151/151 全通过**。引擎宏无解。

**回归**：`ninja -C C:\evs eve` exit 0；OBJECT 的 `unit_test_combat` / `unit_test_map` 均链接成功且 map 109/109；已迁移域重链后复跑计数与 §7.7/§7.9/§7.10 记录逐项一致（platform 27/27、particles 25/34、scene 69/70、building 92/96）→ 无回归。`unit_test_rpg` 仍 481 未解析（该域未迁移，非回归）。
### 7.12 SHARED 测试面标注：tactics_rts / ui（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E "^bundle/"`） |
| --- | --- | --- | --- |
| tactics_rts | 116 → 0 | 13.45 MiB（OBJECT 同一源码 228.38 MiB） | **145/145 通过**（另有 21 个 bundle 未跑） |
| ui | 194 → 0 | 10.80 MiB（OBJECT 同一源码 225.05 MiB） | **100/106 通过，6 个用例挂起**（另有 31 个 bundle 未跑） |

改动：303 个未解析符号收敛到 **111 个声明点 / 34 个头文件**（`EVENGINE_API_WORLD` 49、`EVENGINE_API_DOMAINS` 61、`EVENGINE_API_EDITORS` 1）、`Export.h` include 29、`_INLINE` 0、四特殊成员 0、friend 补宏 0；7 处 AMBIGUOUS（`applyThemeToImGui` 两个重载、`Faction`、`RTSProjectileSystem` 等）由类级宏或逐条标注覆盖（`Theme.h` 两个重载都带宏）。链接前用 `b4a/b4b_clean.py --apply` 清掉两棵构建树两个对象根的 6 模块 93 个 `.obj`，再 `ninja -k 0 unit_test_ui unit_test_tactics_rts` → exit 0，LNK1120/LNK2019/LNK2001 全 0。

**`ui` 的 6 个挂起**（`--timeout 120`，同一断言，都不是"慢"）：`UI.editorKit.desktopCompositionRenders`、`UI.layout.measureNestedFlexAndWindowContent`、`UI.p1.statsAfterHeadlessRender`、`UI.p1.scrollListHeadlessRenderLarge`、`UISystem.render.headlessImGuiWalk`、`UI.overlay.transparentHostHasNoChromeAndRestoresStyle`；断言为 `GImGui != 0 && "No current context…"`（`third-party/imgui/imgui.cpp:2425` 的 `GetStyle()` 与 `:3441` 的 `GetIO()`）。机制与 §7.9/§7.10/§7.11 同族（第三方静态状态每个 link unit 一份）：用例在测试 TU 里 `ImGui::CreateContext()/SetCurrentContext()`（走 exe 自带的那份 imgui —— 链接行里有 `src\modules\eve_imgui.lib`），随后调用的 ui 模块函数在 **EVWorld.dll** 里读自己那份 `GImGui`。取证 `b5a2-imgui-copies.log`：该断言字符串在 `imgui.cpp` 里共 3 处，而只有 `EVWorld.dll` 与 `unit_test_ui.exe` 各命中 3 处（= 各含 1 份 imgui），其余 29 个二进制 0 处；`?GImGui@@` 定义在 `eve_imgui`(3 obj)/`EVUI`(1 obj)。**挂起而非退出**：单跑 `UI.editorKit.desktopCompositionRenders` 45 s 内 CPU 仅 0.09 s，主线程 `Wait/UserRequest`、另 3 线程 `EventPairLow`，stderr 打出断言后进程不结束 → ctest 只能靠超时杀掉。**决定性对照**：OBJECT 单链接单元 `unit_test_ui` **106/106 全通过**（7.4 s，这 6 个用例 0.26–0.51 s）、`unit_test_tactics_rts` **145/145**。

**流水线新增两条**：
1. 跑 ctest **必须带 `--timeout`**：挂死用例在默认 1500 s 下会把"日志很久没动"伪装成"代理卡死"（本批前后两个代理都误判过，§7.12 初稿也因此只记了 2 个挂起）。判断依据是"文件写入时间 + 是否有 cl/ninja/ctest 进程在跑"，不是"多久没输出"。
2. 统计 failed 之前先确认日志覆盖了全部用例：截断的日志会漏报挂起用例（本轮 `ui` 4 → 6）。

**回归对账（本批标注未破坏已迁移域）**：platform 27/27、particles 25/34（9 个仍是 SDL `_this` 族）、scene 69/70（ECS 族）、building 92/96（ECS 族）——与 §7.7/§7.9/§7.10 记录**逐项一致**。静态 `ninja -C C:\evs eve` exit 0（`eve.exe` 221.25 MiB）。ECS 单例对象级取证：2437 个 `.obj` / 259 个目标中 173 个 `.obj` / 27 个目标各持一份（`unit_test_ui` 1、`unit_test_tactics_rts` 1、`EVUI` 15、`EVRts` 11、`EVTactics` 1、`EVGraphics` 44…）；SDL 取证：7 个组 DLL + 全部 30 个测试 exe 各含一份 SDL video 静态状态。本批日志前缀 `b5a2-`。
### 7.13 SHARED 测试面标注：editor / core（2026-09-20）

| 域 | ctest（`-E "^bundle/" --timeout 120`） |
| --- | --- |
| editor | **181/181 通过** |
| core | **506/507** |

两域在第一次构建时都先在编译期撞上同一个 **C2280**（`eve::level_editing::LevelLayer::operator=`，类级 dllexport 强制实例化隐式拷贝赋值 —— §7.5/§7.10 已记录的陷阱），按四件套（删拷贝 + default 移动）修好后链通；`editor` 曾出现过 `b5b-ecs-copies-editor.log` 的 ECS 单例取证。

**`core` 的唯一失败不是标注问题、也不是静态状态复制族**：`plugins.load.nativeLibraryAndInstantiateCppModule` → `LoadLibrary failed for 'C:/evt/build/win32-debug/test/native_test_plugin.dll' (err=126 = ERROR_MOD_NOT_FOUND)`。这是**插件的运行时依赖发现**问题（插件用完整路径加载时，其依赖只按"应用目录 + PATH"搜索，不按插件所在目录；或插件本身是陈旧产物），属于可修的一类，与 SDL/ECS/ImGui 的每二进制副本不同。待定性。

**回归对账**：platform 27/27、particles 25/34、scene 69/70、building 92/96 —— 与记录逐项一致，本批标注无回归；静态 OBJECT `ninja -C C:\evs eve` 正常。

进度：30 个域中 **21 个已链通**（19 个全绿）；仍待迁移 `animation` / `physics` / `rpg` / `procgen` / `graphics`。
**补充（batch 5b 最终报告，2026-09-20）**：`editor` LNK1120 **220 → 0**（exe 13.18 MiB）、`core` **253 → 0**（exe 32.99 MiB），两域未解析符号互不相交；标注落在 **94 个站点 / 66 个文件**（类 82、自由函数 12），C2280 四特殊成员 **4 类**（`LevelDocument`（`vector<LevelLayer>` → `unique_ptr<TileBuffer>` 的两级形态）、`LevelFormatRegistry`、`Ledger`、`Transaction`），`Export.h` include 54 处，`_INLINE` 0。

`core` 那条失败**已定性，不是跨 DLL 复制状态**：SHARED 打断了"插件夹具从宿主 exe 导入引擎符号"的契约 —— SHARED 下 `unit_test_core.exe` 没有导出表、`unit_test_core.lib` 与 `native_test_plugin.dll` 均不存在（构建插件目标报 LNK1104），于是 `LoadLibrary` err=126。**决定性 OBJECT 对照**（`C:\evs`）：宿主为单链接单元时导出引擎符号并生成 `unit_test_core.lib`(9.42 MiB)、`native_test_plugin.dll` 构建成功（73,216 B）、同一用例 1/1 通过（OBJECT 下 editor 181/181、core 507/507）。修法方向：SHARED 下让插件夹具改链组 DLL 的导入库，或在该配置下跳过该用例。
### 7.14 SHARED 测试面标注：animation（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E "^bundle/" --timeout 120`） |
| --- | --- | --- | --- |
| animation | 269 → 0 | 11.85 MiB | **301/301 通过**（0 失败 0 超时） |

静态 `ninja -C C:\evs eve` exit 0。标注 **270 站 / 38 头文件**（自动 266 + 手工 4 处同名重载：`beginClimbingAnimation`×2、`applyClimbingPose`×2）、`Export.h` include 33 处、`_INLINE` 0；清 11 模块 / 148 个对象。

**口径修正**：§7.13 之后流传的"animation 545 个符号"是 animation+physics 的**并集**——两域共用的提取器在两个域都回退了 animation 日志。按 `Linking CXX executable unit_test_<域>.exe` 切分后：animation 269、physics 287（都与各自 LNK1120 吻合）。切片器 `C:\evb2\b6c_harvest.py`。

**两条新坑（并入 §7.5）**：
1. **头内 `ClassName() = default;` 会触发 C2027**：类级 dllexport 需要计算默认构造函数的异常规格，而它依赖成员容器的析构（`unordered_map<string, unique_ptr<SpriteSheet>>`），`SpriteSheet` 在该处只有前向声明 → `can't delete an incomplete type`。修法：头里只**声明**构造函数，在 `.cpp` 里 `= default`。
2. **四件套不是万能**：当类的成员本身既不可拷贝也不可移动（`Animation` 的 `MotionRuntime motions_`）时，`Class(Class&&) = default` 会失败。此时正确做法是**只删拷贝、不声明移动**，而不是硬套四行。

**跨 DLL 静态状态取证**（新脚本 `C:\evb2\b6c_dll_state.py`，扫 2438 个 `.obj` / 260 个 link unit）：ECS `external/ECS.hpp` 的 `default_table()` 27 unit / 173 obj 各一份；ImGui `GImGui` 2 unit / 4 obj；SDL video 静态状态 7 个组 DLL + 若干测试 exe 各一份。animation 的 301 个用例不建 `SDL_WINDOW_VULKAN`、不建 ImGui context，且它唯一持 ECS 表的 link unit 就是测试 exe 本身（EVWorld.dll 为 0）→ 三族**潜伏未触发**：该域 0 失败 0 挂起，且没有任何"既不属于三族"的遗留失败。

进度：30 个域中 **22 个已链通**（20 个全绿）；`physics` 仍有 256 个未解析（本轮顺带减掉 31 个 animation 侧符号）。
### 7.15 进行中状态：physics（2026-09-20，交接记录）

`physics` 域（SHARED）基线 **LNK1120 = 287**（从 `C:\evt\b6a-animation-link-base.log` 按 `Linking CXX executable unit_test_physics.exe` 切片，工具 `C:\evb2\b6c_harvest.py`），animation 那批标注已顺带消掉 31 个，**实时残余约 256**。符号清单已落盘 `C:\evt\b6d-physics-syms.txt`；该批次的进度日志 `C:\evt\b6d-progress.log` 停在 `[step1b]`（用 `with-msvc.cmd` 跑权威基线重链）。**未产生任何标注、未提交。**

接手时的固定流程（§7.5-§7.14 的规则全部适用，另有两条本轮新增的存活判断经验）：
1. 逐域建基线链接（必须 `C:\evt\cmake\with-msvc.cmd cmd /c "ninja.exe -C C:\evt\build\win32-debug -k 0 unit_test_<域>"`，裸 `ninja.exe` 会因找不到 `cl.exe` 失败）→ 按 exe 目标切片未解析符号；
2. 标注（组宏按定义模块的 `-DEVENGINE_EXPORTS_<GROUP>`；重载/friend 全声明带宏；`Export.h` 覆盖 `.h/.hpp/.inl/.ipp`；raw string 禁标；数据符号归拥有者组；C2280 四件套**仅在成员可移动时**适用，否则只删拷贝；头内 `ClassName() = default;` 遇前向声明容器会触发 C2027 → 头里声明、`.cpp` 里 `= default`）；
3. 清**两个**对象根（`src/engine/CMakeFiles/<LIB>.dir`、`src/modules/CMakeFiles/<LIB>.dir`）后重链；
4. `ctest --test-dir C:\evt\build\win32-debug -L unit_test_<域> -E "^bundle/" --timeout 120 --output-on-failure -j 4`（双引号 + 超时都不可省）；
5. 静态回归 `ninja -C C:\evs eve` 必须 exit 0；失败按"跨 DLL 静态状态复制（SDL/Vulkan/ImGui/ECS `default_table()`）或插件契约"分类，并用 OBJECT 单链接单元对照定性。

**存活判断经验（本轮踩过）**：不要只凭"进程表无 cl/ninja"或"日志几分钟没动"就判定代理卡死——这类单域批次是"长构建 → 长分析 → 一次性写标注"，`git diff` 会长时间为 0 然后突然跳变；判活应看**该代理自己的进度日志是否跨轮次增长**。`animation` 那轮就因此被我误判为停滞并被中断过一次（后被续跑代理完成，269→0、301/301）。