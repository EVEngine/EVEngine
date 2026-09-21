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
| `os.cpp` (6) | 操作系统抽象（sleep 等） |
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
**§7.5 补充规则（2026-09-20，反例驱动的教训）**：任何批量标注器——包括临时自造的小工具——**必须把类/结构体的查找范围限定在"该符号的定义模块"内**，并输出每个命中的"定义模块 vs 引用模块"供人工核对；把模块集合传空（或在整棵树里取第一个同名类）会命中**同名类**，把宏标到错误组的类上。实测反例：给 `physics` 域批量插宏时未限定模块，`eve::orders::Orders` 被误标，导致 EVBackends 的 `QueueInspector.cpp.obj` 变成对裸数据符号的引用，`EVBackends.dll` 直接 `LNK1120: 1 个无法解析的外部命令`（`?name@Orders@orders@eve@@2PEBDEB`）。该批 26 个文件已全部回退。既有工具里 `b2_dup_decls.py`（同名/同实体重复声明）与 `b3_free_fn_defs.py`（自由函数的定义 TU 是否看得见声明头）正是这个核对步骤，不要跳过。

### 7.16 SHARED 测试面标注：physics（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E "^bundle/" --timeout 120 -j 4`） |
| --- | --- | --- | --- |
| physics | 256 → 0 | 16.26 MiB | **271/277 通过**（6 失败 0 超时；OBJECT 单链接单元对照 277/277） |

静态 `ninja -C C:\evs eve` exit 0（`eve.exe` 232,814,592 B）。标注 **254 个点 / 56 个唯一站点**（36 个类 + 20 个自由函数；`_INLINE` 0），30 个头文件、`Export.h` include 22 处、组宏 62 行（DOMAINS 41 / WORLD 8 / PLATFORM 8 / BACKENDS 3 / EDITORS 1 / ORCHESTRATION 1）；清**两个**对象根各 20 模块 / 311 个对象（共 622）。exe 从同源 OBJECT 的 230.4 MiB 降到 16.26 MiB（14.2×），`dumpbin /dependents` 直接导入全部 7 个组 DLL。

本轮 6 个失败**全部由 DLL 拆分造成**，分两族：box3d 的 `b3_worlds` 新子族 5 个 + SDL video `_this` 已知族 1 个（`softbody.volume.renderPreview`）。ECS `default_table()` / ImGui `GImGui` 本域未触发。

**新坑 1（并入 §7.5）：批量标注器要按"定义模块 + 作用域链"消歧，不能按文件名 glob 找类。**
`??0/?1Cloth@physics@eve@@…` 的正主是 `src/modules/physics/cloth/Cloth.h:27` 的 `eve::physics::Cloth`（5 参构造在 :36），**不是** `cloth/ClothModule.h:17` 的 `eve::cloth::Cloth`（Module 工厂，没有 5 参构造）。两者同属模块 `physics_cloth`（→ `EVDomains` → `EVENGINE_API_DOMAINS`），所以"谁定义"只能由**反修饰限定名与站点外层作用域链逐段比对**得出。只在 `src/modules/physics/*.h` 里找 `class Cloth` 的工具**永远找不到**正主——它在 `cloth/` 子目录下——会把该符号报成 AMBIGUOUS-SITE 并整体跳过，于是链到最后只剩这 2 个（256 → 2 → 0 的最后一跳）。`b6e_place.py` 已按作用域链实现。

**新坑 2：`b2_dup_decls.py` 只扫缩进行 → 顶格声明是盲区。**
`procgen/GtsTerrainLod.h:104` 的 friend 带了宏，同一实体的命名空间级声明 `:208`（顶格）没有 → 2 条 `warning C4273`；规则仍是 §7.8 的"同一实体的每条声明都要带宏"，但工具必须**同时扫顶格与缩进行**。

**新坑 3（影响整个 PR 的可评审性与 CI 门禁）：标注器会把整文件行尾改写成 CRLF，制造全文件 diff，并把"旧债"变成"新违规"。**
`b1_place.py` / `b1_includes.py` 用 `Path.write_text()` 回写，在 Windows 上把每个 `\n` 变成 `\r\n`。对本来就是 LF（或 LF 混少量 CRLF）的文件，`git diff <base>` 会把**每一行**都算成改动：
- PR 变大：`src/modules/network/Network.h` 真实改动 3 行，diff 却报 138/136；
- `check/architecture-contracts` 的口径是 **changed-line**，于是它去 lint 那些从未被改过的行，把该文件既有的 raw-pointer / `bool` 返回 / Link 目录缺失等**旧债报成新违规**（Network.h 一处 14 条），门禁直接失败。

两个必须做对的细节：
1. `git show <rev>:<path>` / `git cat-file -p` **会走 checkout 的 eol 转换**（本机 `core.autocrlf=true`），拿它当"原始字节"读基线会把基线也读成 CRLF，从而把要修的东西又写回去；读原始字节要用 `git cat-file blob <rev>:<path>`。
2. 门禁的 base 必须是**该 PR 的 merge-base**（本轮 `4b3cef55c`），不是构建 worktree 里停在陈旧提交上的 `HEAD`——`C:\evt` 的 `HEAD` 是 `3b9b24c4d`，拿它当 base 会把整段历史差异都算成 changed-line。

修法（`C:\evb2\eol_restore.py`，幂等）：以基线字节为底，用 difflib 把目标文本（工作树或某个 rev）的语义差量重放上去——未改动的行保留**基线原本的行尾**，插入行沿用前一行的行尾，写入前校验"去掉 CR 后与目标逐字相等"。本轮对 physics 批 30 个文件 + `Network.h`/`Thread.h` 共 32 个文件归一化后：`Network.h` 的 PR diff 从 138/136 回到 **3/1**，`check/architecture-contracts --base 4b3cef55c` 恢复 **OK**，重链仍 0 unresolved、ctest 仍是 271/277。

**跨 DLL 静态状态：新子族 `b3_worlds`（第三方静态库里的文件作用域状态）。**
4 个 `box3d.*` 用例以 `0x80000003` 断点停在 `third-party/box3d/src/body.c`（`B3_ASSERT(b3Body_IsValid(bodyId))`，body.c:30），另 1 个 `physics.core.world3dFailedStepPreservesPreviousContactEvents` 段错误（`World3D.cpp:743` → box3d）。根因是 box3d 以**静态库** `box3dd.lib` 链进每个链接单元，而 `b3_worlds`（`physics_world.c:36` 的文件作用域全局）因此每个链接单元一份。探针 `C:\evb2\b6e_box3d_state.py`（走 `B3_ASSERT` 的 `#condition`/`__FILE__` 字面量）实测：`EVWorld.dll` 与 `unit_test_physics.exe` **恰好各一份**，其余 6 个组 DLL 与 29 个测试 exe 全 0。注意 `b3_worlds` **不在**构建树的 2438 个 `.obj` 里（它来自预编译静态库），所以 §7.14 那种 obj 级探针对它无效——**第三方静态库里的文件作用域状态是 §7.3 清单之外的第四族**。

**并发教训**：physics 一度有 3 个代理在同一棵树、同一个构建目录上跑同一个任务。症状：`EVBackends.dll` 因陈旧对象报 `LNK2001 ?name@Orders@orders@eve@@2PEBDEB`（Ninja unscanned 规则在源码回退后不重编），以及 `b4a_clean --apply` 撞 `PermissionError [WinError 32]`（对象正被另一个构建占用）。仲裁为单一执行者后数字才自洽。**同一域一次只允许一个写者**，清对象前先确认没有并发的 cl/ninja/link。

**刻意例外**：`SoftBody3DWorldBridge.cpp:56,62` 的 2 条 `warning C4273` 未修——`softbody/SoftBody3D.h` 相对基线未改动且已写 `class EVENGINE_API_BACKENDS SoftBody3D`，而这两个成员的定义落在 `EVPhysics`（EVWorld 组），即"类归 BACKENDS、成员定义在 WORLD"的跨组错配，属**基线遗留**；两个符号都不在这 256 个未解析集里、无人导入，OBJECT 模式 0 条。归 §7.3 范畴。

进度：30 个域中 **23 个已链通**（20 个全绿）；余 `procgen` / `rpg` / `graphics`（各自需重新取权威基线，`procgen` 的旧日志 899 已过期）。§7.15 记的 256 个残余已在本节清零。

### 7.17 SHARED 测试面标注：rpg（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E "^bundle/" --timeout 120 -j 4`） |
| --- | --- | --- | --- |
| rpg | 461 → 0 | 14.69 MiB（OBJECT 同源码 231.8 MiB，15.8×） | **301/301 通过**（label 360，59 个 bundle 项排除；0 失败 0 超时） |

静态 `ninja -C C:\evs eve` exit 0（`eve.exe` 233,097,216 B）；OBJECT 单链接单元对照 **301/301**。基线 `LNK1120 = 461`（`LNK2019 450 + LNK2001 215 = 665 行`，`error C` 0，7 个组 DLL 全链通）——§7.6 记的 "rpg 565" 是 29 域一起链的汇总口径，逐域实测 461。标注 **66 个唯一站点 / 53 个文件**（类 59 + 自由函数 7），组宏 `PLATFORM 38 / FOUNDATION 18 / BACKENDS 6 / WORLD 2 / DOMAINS 2`，`Export.h` 新增 **47** 处，`_INLINE` 0，C2280 四件套 0，friend/兄弟声明补宏 **0**（`b2_dup_decls.py` 修掉顶格盲区后无遗漏 → 重链 **0 条 C4273**）；清两个对象根各 17 模块 / 167 个 `.obj`（共 334）。`dumpbin /dependents` 直接导入全部 7 个组 DLL。

本域 **301 个用例真实全绿**：五个已知失败族结构上都存在（探针 `b6c_dll_state.py` / `b6e_box3d_state.py`），但一个都没触发——SDL `_this` 在 exe 里有一份却不建 `SDL_WINDOW_VULKAN`；ECS `default_table()` 在 `unit_test_rpg.exe` 里 **0 份**（24 份在 EVRPG 内，同属 EVPlatform.dll，故只有一份有效表）；ImGui/box3d 本域不建 context/世界。`b10-ctest.log` 里 `Video subsystem…` / `No current context` / `B3_ASSERT` / `0x80000003` / `SegFault` 全 0。

**新坑（并入 §7.5 流水线规则）：批量标注器自己必须被验证，"能跑出计划"不等于"计划对了"。** 本轮在工具里挖出 4 个缺陷，前两个会直接污染源码：
1. **apply 必须按"站点"去重，不能按"符号"**：461 个符号落在 66 个站点上，而 `b6e_place.py` 的 ALREADY 判定用的是未修改的索引行，于是同一行被插了 N 遍（45 个文件 395 个重复宏，`rpg/RPG.h:54` 一次 50 个）。回修用 `b10_repair.py`：**按字节**把连续的 `EVENGINE_API_*` 串收敛为第一个，不解码/重编码（行尾字节零改动）。检查手段很便宜：对 diff 扫 `^\+.*EVENGINE_API_[A-Z_]+.*EVENGINE_API_[A-Z_]+` 必须为 0（physics 提交与 rpg 批现在都是 0）。
2. **`raw_lines()` 保留 `split` 的尾空元素** → 任何以换行结尾的文件都会先 `ABORT: line-count mismatch (N vs N-1)` 而一行不写：表现为"工具说成功、源码没变"。
3. **假验证器**：`b6e_verify_sites.py` 拿补丁计划的**整行**当 mangled 符号去查字典，必然全落空（66 个站点全报 `NAMESPACE-MISMATCH`）。正确做法是 `b6e_place --map`（sym→site）+ 归属证据（所选模块目录下确有 `.cpp` 定义 `Class::`），即 `b10_verify.py`：461 符号 / 66 站点 / 0 问题。
4. **`b10_includes.py` 的 include 正则漏了 `re.M`**（`^` 只匹配文首）→ 每个头文件的 `findall` 都返回空，于是给 6 个**本来已有** `Export.h` 的文件又插了一行；征兆是工具打印 `already reach Export.h: 0`。修正则 + 新增 `b10_fix_includes.py`（自校验：删掉插入块后仍至少有 1 个 include 才删）。

**环境教训（两条，都会伪装成"代理卡死"）**：
- 本机 `pwsh` 是 **Windows PowerShell 5.1**，`>` 重定向写 **UTF-16**；一切日志/中间产物改走 `cmd /c "python … > file"`。
- 前台工具调用有 **120 s 上限**，超时会**孤儿化** python 进程并锁住输出文件；长工具一律 `run_in_background` + 轮询进程。

`eol_restore.py` 本轮再次证明幂等：第一遍 `eol-churned=53`，修掉冗余 include 后再跑 `eol-churned=0`（收敛）。验收口径：这 53 个文件相对 merge-base 合计 **189 insertions**，**无任何整文件 diff**（最大 `game_event/GameEvent.h` 13/2）。

进度：30 个域中 **24 个已链通**（21 个全绿——rpg 全绿）；余 `procgen` / `graphics`。

### 7.18 SHARED 测试面标注：procgen（2026-09-20）

| 域 | LNK1120 前→后 | exe | ctest（`-E "^bundle/" --timeout 120 -j 4`） |
| --- | --- | --- | --- |
| procgen | 868 → 0 | 30.07 MiB（OBJECT 同源码 247.05 MiB，8.2×） | **729/738 通过**（9 失败 0 超时，全部 SDL video `_this` 族；OBJECT 单链接单元对照 **738/738**） |

基线 `LNK1120 = 868`（`LNK2019 862 + LNK2001 173 = 1035 行`，`error C` 0，7 个组 DLL 全链通）——§7.6 记的 "procgen 925" 同样是 29 域汇总口径，逐域实测 868。静态 `ninja -C C:\evs eve` exit 0（`eve.exe` 233,654,784 B）。标注 **301 个唯一站点 / 129 个文件**（类 92 + 自由函数 209）；**本批新增带宏行 319**（类 92 + 自由函数 215 + friend 12）/ 133 个文件，组宏直方图 `DOMAINS 284 / ORCHESTRATION 15 / BACKENDS 8 / WORLD 5 / EDITORS 4 / PLATFORM 3`，`Export.h` 新增 41 处（133 个文件全部可见宏：48 处字面 include + 85 处经自身 include 闭包），`_INLINE` 0，C2280/C2027 0；清两个对象根各 10 模块 / 375 个对象（共 750）。exe 8.2×（这个域 SHARED 收益最小，因为它本来就几乎全在一个组里）。

**新坑：friend/同名声明有两种，`b2_dup_decls.py` 只覆盖同文件那种。**
第一轮重链暴露 **`error C2375`（redefinition; different linkage）7 个 `EVProcgen` 对象失败**：`TerrainMultiTile.h:89` 的 friend 声明带宏、而**另一个头文件**里的同名命名空间级声明（`TerrainDetailLayer.h:75`）不带 → `.cpp` 里连带 `C3861` 找不到标识符。修法是给跨文件的每条同实体声明也补宏。本批同文件 11 条 + 跨文件 1 条 = 12 条。**新增工具 `b11_xdecl.py`（跨文件同实体声明扫描）应并入 §7.5 流水线**——`b2_dup_decls.py` 只看单个文件，`b3_free_fn_defs.py` 只看定义侧，跨文件声明是第三条路径。

**测试侧跨 link unit 符号（§7.8 家族的第三次出现）**：未解析集里有 3 个符号任何引擎宏都无效——`auditImage`（声明 `test/RenderImageAudit.h:73`，定义 `test/RenderImageAudit.cpp:463`）、`createStylizedWaterScene`（`test/water_scene_fixture.h:26` / `.cpp:40`）、`WaterSceneFixture::update`。它们必须走 `scripts/test_domains.py` 的 `SHARED_SOURCES` 机制（把定义 TU 编进每个需要的域），不能靠标注。**注意牵连面**：`procgen` 这一批把 `test/RenderImageAudit.cpp` 拉进了自己的链接单元，所以 `graphics` 域批要检查同一组 fixture 是否也落在它的未解析集里。

**EOL 归一**：第一遍 `eol-churned=133`，第二遍 `eol-churned=0`（收敛）；工具内建校验从未触发 `FAIL`。5 个 `SKIP … not in base revision`（本 PR 新增文件）改用手工取证：`RenderImageAuditIo.cpp` / `SceneCameraProjection.h` 全 LF、`test_domains.py` / `RenderImageAudit.cpp` 全 CRLF，各自内部一致、无混行尾。验收：**无任何整文件 diff**（最大真实比例 `TerrainDerivedMap.h` 26/26，256 行里 26 个站点；`test/RenderImageAudit.cpp` 是 9/570 的纯移动）。

**9 个失败用例全部出自同一个 TU `test/procgen.cpp`**，断言都是 `SDL_Vulkan_GetInstanceExtensions failed: Video subsystem has not been initialized`——窗口初始化在 EVPlatform 组、Vulkan 入口在 EVBackends 组各自的 SDL 副本里：`procgen.render.{hexplanetPng,cloudShadowsDarkenGround,skyscraperPng,castlePng}`、`graphics.waterfall.{paramsRoundTrip,render.flowAndFoam}`、`graphics.water.{paramsRoundTrip,render.dynamicRipplesAndReflection}`、`graphics.render3d.toCanvas`。探针：该域 link unit 里 ECS 表 **0 份**、ImGui 0、box3d 0，SDL 副本 8 份（7 个组 DLL + exe）→ 只有 SDL 族被触发。

进度：30 个域中 **25 个已链通**（21 个全绿）；余 `graphics`。

### 7.19 SHARED 测试面标注：graphics（2026-09-20）—— 30/30 全部链通

| 域 | LNK1120 前→后 | exe | ctest（`-E "^bundle/" --timeout 120 -j 4`） |
| --- | --- | --- | --- |
| graphics | 675 → 0 | 51.56 MiB（OBJECT 同源码 250.01 MiB，4.85×） | **1023/1116 通过**（93 失败 0 超时：92 个 SDL video `_this` 族 + 1 个**新族** box2d `s_initialized`；OBJECT 单链接单元对照 **1116/1116**） |

基线 `LNK1120 = 675`（`LNK2019 669 + LNK2001 107 = 776 行`，`error C` 0，7 个组 DLL 全链通）——§7.6 记的 "graphics 1138" 同样是 29 域汇总口径，逐域实测 675。静态 `ninja -C C:\evs eve` exit 0（`eve.exe` 234,105,344 B）；脚本测试 170 OK。标注 **210 个唯一站点 / 129 个文件**（类 107 + 自由函数 103）；**本批新增带宏行 217**（210 站点 + 6 手工行 + 1 friend）/ 130 个文件，组宏 `BACKENDS 97 / WORLD 51 / DOMAINS 39 / ORCHESTRATION 8 / FOUNDATION 7 / PLATFORM 4 / EDITORS 4`，`Export.h` 新增 42 处（130 个文件里 129 个可见宏），`_INLINE` 0；清两个对象根各 35 模块 / 615 个对象（共 1230，另加 C2036 修复后各 17 个 EVStylize 对象）。

**到本批为止：30 个测试域在 `EVENGINE_MODULE_LINKAGE=SHARED` 下全部链通**（7 个组 DLL + 30 个薄 exe；最后一个域的 `LNK1120` 从 675 清零）。失败用例不再是"链接问题"，而是下文与 §7.3 列出的**跨 DLL 静态状态族**。

**新坑 1：类级 dllexport 还会触发 `C2036: unknown size`（C2280/C2027 家族的新子类）。**
`MeshParticleEmitter` 持 `std::vector<Particle>`，而 `Particle` 只在**该类内部**前向声明（`struct Particle;` 是它的私有嵌套声明）→ 类级 dllexport 强制实例化隐式拷贝构造，编译器要算 `vector<Particle>` 的元素大小 → `error C2036`，4 个 EVStylize 对象失败。修法与 C2280 相同：**只删拷贝**（`MeshParticleEmitter(const MeshParticleEmitter&) = delete;` 等），不声明移动。判别口径也要更新：`b6c_member_scan.py` 现在把"成员容器元素类型在该类内才前向声明"也算命中——**只看"是否 unique_ptr"是不够的**。

**新坑 2：规划器的解析器缺陷会伪装成 AMBIGUOUS/MANUAL。** 本批先修工具、不手工特判（`C:\evb2\b12_names.py`）：
1. `ns_chain` 用 `re.match` 一行只见第一个作用域开启 —— `camera/PcgFreeCamera.h:3` 一行塞了三个 `namespace` 声明，于是 `class PcgFreeCamera{` 被算成 `ssq::PcgFreeCamera`，规划器对三个成员报**假 NS-MISMATCH** 并跳过。改为整行从左到右按深度入栈后自动恢复（新旧计划 668 行映射逐字节相同，只多这 3 行）。
2. `head.split()[-1]` 在调用约定（`__cdecl`）之后退化成裸名 → `jobSystemPassExecutor` 报 NO-QUALIFIED-NAME。新增 `qualified_name_from_demangle()` 回退路径。
3. `insert_class_macro` 锚定 `^\s*(?:class|struct)\s+`，漏掉**同行 doc 注释开头的类声明**（`/** @brief … */ class PcgFreeCamera{`）→ 规划说"可标注"、`re.sub` 实际不匹配。

**手工 3 个实体（6 行，全部 `map` → `EVENGINE_API_WORLD`）**：`loadMapFile`（`map/TileConfig.h:57,58` 重载集）、`resolveDualGrid`（`map/DualGrid.h:167,171` 重载集）、`importRpgMakerMap`（`map/TileConfig.h:70` 头声明 + `map/RpgMakerTileImporter.inl:116` 定义 —— **`.inl` 也算声明点**）。反证：`dumpbin /exports EVWorld.dll` 里两个 `loadMapFile`、两个 `resolveDualGrid`、`importRpgMakerMap`、`applyConfigText` 全部在导出表内。

**数据符号判据的坑**：675 个符号按 MSVC kind 编码分类是 **函数 675 / 数据 0**。不要用"出现 `@@2`/`@@3` 即数据"来扫——模板实参里也有 `@23@`、`@@@3@`，那样会误判 57 个。正确做法是解析第一个 `@@` 之后的调用约定/kind 字母。

**新增第四/第五族**：`graphics` 触发了 1 个 `box2d` 的 `s_initialized`（box2d 也是**第三方静态库**，与 §7.16 的 box3d `b3_worlds` 同源）——第三方静态库的文件作用域状态现在是 `box3d`、`box2d` 两个实例，任何 obj 级探针都看不见它们。

进度：30 个域中 **30 个已链通**（21 个全绿；余下 9 个域共 130 个失败用例，全部属于跨 DLL 静态状态族，其中 `graphics` 一个域就占 93 个）。

### 7.20 SHARED 成为开发默认值 + 130 个已知失败的分族清单（2026-09-20）

30/30 链通后（§7.19），`EVENGINE_MODULE_LINKAGE` 的默认值由 `OBJECT` 翻成 **`SHARED`**（`CMakeLists.txt`）。翻转的判据与配套改动：

1. **谁必须显式传 OBJECT**：`Makefile` 的 release 配置（`WIN32_CMAKE_ARGS`）本来就钉了 `OBJECT`，`make build/win32`、`make sdk/win32` 因此不变——发布仍是"单个 exe、旁边没有引擎 DLL"。`CMakeLists.txt` 的注释把这条写成硬要求：**任何要出产物的配置都必须显式传 `-DEVENGINE_MODULE_LINKAGE=OBJECT`**。
2. **非 Windows 的运行时发现必须跟着改**：`cmake/ZeroErrDiscoverTestsImpl.cmake` 原来只写 `ENVIRONMENT "PATH=..."`（Win32 专用）。现在按平台选变量与分隔符——Win32 `PATH`（分隔 `;`，且在生成文本里必须转义成 `\;`）、ELF `LD_LIBRARY_PATH`、Mach-O `DYLD_LIBRARY_PATH`（都按 `:` 分隔），继承值在**生成期**展开。构建期的 listing 步骤同样设置该变量。`Makefile` 的 `run|debug/{linux,macosx}*` 六个目标用新的 `eve-dll-path-so`（探测 `libEVFoundation.so`/`.dylib`）前缀 `LD_LIBRARY_PATH`/`DYLD_LIBRARY_PATH`；静态配置下探测为空、命令与翻转前逐字节一致。
3. **验证**：(a) 不传任何 linkage 参数 configure 一个干净目录 → `EVENGINE_MODULE_LINKAGE:STRING=SHARED`、7 行 `Link group ... -> one shared library`、`ninja -n EVFoundation` 的末步是 `Linking CXX shared library EVFoundation.dll`；(b) 用**真的** `ZeroErrDiscoverTestsImpl.cmake` 生成一次注册表：Windows 得到 `ENVIRONMENT "PATH=C:/…\;C:\\…"`，WSL 里的 Linux cmake 得到 `ENVIRONMENT "LD_LIBRARY_PATH=/build/…:/pre/existing"`（继承值保留、分隔符正确）。
4. `AGENTS.md` 的 "Module linkage policy" 段同步改写：默认 SHARED、出产物必须显式 OBJECT、并指向本节的分族清单。
5. **文档同步的方向坑（本轮踩到）**：交付 worktree 与 `C:\evt` 是两份内容相同的检出。我在交付 worktree 里写完本节后，又用 `C:\evt → 交付 worktree` 的批量复制覆盖了它（本节第一次因此丢失，只能重写）。**规则：在交付 worktree 里编辑过的文件，必须立刻反向复制回 `C:\evt`；批量同步只能是 `C:\evt → 交付 worktree`，并且要排除刚在交付侧编辑过的文件。**

**整套 SHARED 测试的权威读数**（`ctest -E "^bundle/" --timeout 120 -j 4`，一次跑全部 30 个域）：

| 项 | 数 |
| --- | --- |
| 用例总数 / 失败 / 超时 | **5274 / 129 / 0**（130 个里 1 个已随下面那处**构建修复**转绿） |
| 分族 | SDL video `_this` **111**、ECS `default_table()` **6**、ImGui `GImGui` **6（挂起→超时）**、box3d `b3_worlds` **5**、box2d `s_initialized` **1**；插件夹具族已修好（0） |
| 分域 | particles 9、procgen 9、graphics 93（92 SDL + 1 box2d）、physics 6（5 box3d + 1 SDL）、ui 6、building 4、scene 1、combat 1；**其余 22 个域 0 失败**（`core` 507/507 全绿） |

**翻转默认值后，整套构建暴露出一处"构建级"断裂（已修，不属于"记录不修"的那一类）**：SHARED 下 `ninja`（默认目标）失败于 `test/native_test_plugin.dll` → `LNK1104: 无法打开文件 "test\unit_test_core.lib"`——SHARED 里宿主 exe 没有导出表、**根本不存在**这个 `.lib`，插件夹具因而链不上。这与"某个用例在 SHARED 下失败"性质不同：它让默认构建直接红（开发者 `make build/win32-debug`、CI 都会停在链接阶段），所以尽管决策是"记录不修"，这一处仍按 §7.13 早已写明的方向修掉了：SHARED 下插件改链 `${EVE_LINK_TARGETS}`（组 DLL 的导入库），这也正是真实插件的发布模型——从引擎 DLL 解析符号，而不是从宿主 exe。修后：`ninja` 全目标 **0 FAILED**，`plugins.*` **5/5 通过**，`unit_test_core` **507/507 全绿**，已知失败 130 → **129**、全绿域 21 → **22**。

**交叉核对**：9+9+93+6+6+4+1+1 = **129**（130 减去已修好的 core 插件用例），与整套跑出来的失败数逐一吻合，说明整套跑没有暴露任何逐域没记录过的新失败。分族明细与每族取证方式见 §7.3；这里补充两条**新族**：`box2d` 的 `s_initialized`（与 §7.16 的 box3d `b3_worlds` 同源：第三方静态库的文件作用域状态，`EVBackends.dll` 与 `EVWorld.dll` 各一份、`unit_test_graphics.exe` 里 0 份）与 `graphics` 域特有的 92 个 SDL 用例（窗口初始化在 EVPlatform 组、Vulkan 入口在 EVBackends 组各自的 SDL 副本里）。

**剩下的 129 个是"记录"而不是"待修"**（项目所有者 2026-09-20 决策）：本仓库内还能修 6 个（把 ImGui 编成 SHARED 目标，救 `ui` 的 6 个挂起用例），其余需要把 SDL2/box2d/box3d 改成动态库（新增一份第三方动态预编译树 + 部署/打包改动），ECS 的 6 个还需要改 `external/ECS.hpp` **子模块**（另一个仓库）。全部保留为已知失败并在此存档。

**运维陷阱（本轮踩到）**：`ctest` 的 `Test #NNN` 编号在本树上**不稳定**——同一个二进制两次全量跑之间，`graphics.imageAudit.pipelineConfigs` 从 #658 变 #748、`window.zeroSizeUsesDesktopDimensions` 从 #1686 变 #1914。因此 **`ctest --rerun-failed` 会跑错用例**（它一度声称"93 个里只有 7 个失败"，那 7 个其实是别的用例）。所有 ctest 结论都必须按**用例名集合**而不是编号。

**未验证项（如实记录）**：
- 本机没有 Linux 构建树，翻转后的 Linux SHARED 路线只做到"生成文本正确"这一层（上面 3(b) 与 WSL 里的脚本级验证）。在 WSL 里尝试过完整 Linux 构建，卡在**共享** `third-party/` 检出上：该检出已被 Windows git 打过补丁（工作树 CRLF），而 `cmake/patch_third_party.cmake` 故意不做空白松弛，于是 Linux `git apply --check` 报 `SDL2/CMakeLists.txt: patch does not apply`。这是**两台机器共用一份检出的产物**，不是 CI 的问题（CI 在 Linux 上全新 clone 后按顺序打补丁）。
- `check-format.sh`（CI 用 clang-format-18）本机没有该二进制，无法本地实证；`.clang-format` 是 `MaxEmptyLinesToKeep: 2` + `IncludeBlocks: Preserve`，"include + 两空行 + 原 include 块"符合规则，但这是规则推断而非实测。

### 7.21 全新（Linux）构建暴露的两处"只在冷构建里出现"的回归（2026-09-20）

上面那次 WSL 全新构建虽然没走到最后，却抓到了两处**在 Windows 增量构建里被隐藏**的回归——这是本轮最有价值的一次发现，值得单列：

**回归 1（CI 会红）：严格 EveScript 绑定契约生成失败。** 该目标由 `scripts/generate_binding_contracts.py` 驱动，它的 `class_blocks()` 用 `\b(?:class|struct)\s+([A-Za-z_]\w*)[^;{]*\{` 取类名。标注批次给类声明加了组宏之后，**类名被解析成了宏名**（`class EVENGINE_API_DOMAINS Widget` → 类名 `EVENGINE_API_DOMAINS`），于是这些类的成员绑定全部变成"未解析"：实测分支 `10313 contracts / 297 unresolved / 22 placeholder`（exit 1），而 `dev` 基线是 `10621 / 0 / 0`（exit 0）。Windows 侧之所以没暴露，是因为 `BindingContracts.generated.cpp` 是**陈旧产物**（2026-09-19 生成，早于本次改动），ninja 的 CUSTOM_COMMAND 依赖里没有模块源码，所以从不重跑；**任何冷构建（CI、新 build 目录）都会撞上**。修法：模式允许类键与类名之间出现 `EVENGINE_API\w*`；修后 `10610 / 0 / 0`（与 dev 的 10621 差额是分支基线较早，属正常）。新增两条回归测试（`class_blocks` 与 `SignatureIndex.member` 都要能穿过宏）。

**回归 2（诊断过程中发现的门禁盲区）：`check/architecture-contracts` 的 Link/System 规则被宏"遮蔽"。** 同理，`\b(?:class|struct)\s+[A-Za-z_]\w*Link\b` 之类在宏前缀下**静默失配**——任何新加的 `class EVENGINE_API_X FooSystem` 都不再被门禁看见（覆盖率损失）。修法是让模式穿过宏；但只改模式会立刻在**既有**声明上报 6 条 `missing-contract-entry`（`network/UdpLink.h`、`particles/ParticleSystem.h`、`procgen/algorithms/LSystem.h`、`scene/TransformSystem.h`、`ui/UISystem.h`、`weapon/WeaponSystem.h`）——这些类型本来就在，只是"行被我们改过"。所以同时把规则**按它自己的描述实现成"只针对新声明"**：`lint_contract_coverage` 新增 `base` 参数，若该类型名在 base 版本的同一文件里已存在，则不算新面。修后门禁 **OK**、契约测试 **10/10**，并且新增一条测试（既有 `TransformSystem` 不报、base 里没有的 `BrandNewSystem` 照报）。

**教训**：`ninja` 增量构建 + 陈旧生成物会把"冷构建才跑"的检查藏起来。**验收一条与生成物相关的改动，必须至少跑一次冷构建（或强制重跑该生成目标）**；本轮的判据是"Linux 全新 clone 的构建"。

### 7.22 ELF 侧：SHARED 把第三方与测试框架链进 .so，因此需要 PIC（2026-09-20）

修掉 §7.21 的两处之后，Linux 冷构建继续推进到**第一个组库链接**，暴露第二条 Windows 上根本不存在的问题类：

```
libEVFoundation.so: external/zeroerr/src/libzeroerr.a(color.cpp.o): relocation
R_X86_64_PC32 against symbol `_ZN7zeroerr5ResetE' can not be used when making
a shared object; recompile with -fPIC
```

ELF 不允许把非 PIC 的目标文件链进共享对象；OBJECT（release/SDK）只产出可执行文件，永远不需要 PIC，所以这条只在"SHARED + Linux"组合下出现。逐个定位后是两处，且**恰好两处**：

1. **`zeroerr`**：它来自 `external/zeroerr` 子模块，自己不开 PIC → `cmake/link_groups.cmake` 在 SHARED 时给它设 `POSITION_INDEPENDENT_CODE ON`（MSVC 无视该属性）。
2. **`SDL2-static`**：第三方聚合的 `third-party/CMakeLists.txt:9` 本来就 `set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)`，其余子项目（Poco / box3d / assimp / mpg123 / OpenAL）都用它；**只有 SDL2 例外**——它按 `SDL_STATIC_PIC`（默认 `OFF`）逐 target 覆盖该属性（`third-party/SDL2/CMakeLists.txt:2639`）。因此 SHARED 时给第三方聚合加 `-DSDL_STATIC_PIC=ON`。

**取证方法值得记下**：不要在每次链接失败后盲目重编第三方（一轮 20-40 分钟）。先做静态排查——在第三方源码树里 grep `POSITION_INDEPENDENT_CODE|_STATIC_PIC`（注意 `third-party/` **不在 git 里**，`git grep` 搜不到，要用文件系统级搜索），就能一次看清谁覆盖了全局设置：本次 16 处命中里只有 SDL2 是覆盖型的，其余都是"跟随全局"。加上"聚合 CMakeLists 已经 FORCE 全局 PIC"这一条，结论就是**只有 SDL2 需要显式开关**，不用逐个试错。

`CMAKE_POSITION_INDEPENDENT_CODE` 那条全局参数因此被撤掉（聚合已经强制它），只保留 SDL 的开关——第三方 configure 参数越少越好，每个参数变化都会让整棵第三方树重编。

**Linux 侧现状**：这两处修好后需要在 WSL 里强制重编第三方（ExternalProject 的 stamp 不认为参数变了，必须删 `third-party-prefix` 与第三方安装树）才能验证；`libEV*.so` 是否真正产出、`LD_LIBRARY_PATH` 运行期发现是否成立，以那次冷构建的实测为准（本节的结论只覆盖到"可以开始链接组库"）。

### 7.23 Linux SHARED 路线实测通过（2026-09-20）

§7.21/§7.22 的三处修复之后，WSL（Ubuntu 2、16 核、cmake 4.2.3、g++ 15.2、全新 clone 的 `codex/test-domain-split`）里**跑通**了整条 ELF 动态路线：

1. **不传任何 linkage 参数** configure → `EVENGINE_MODULE_LINKAGE:STRING=SHARED`、7 行 `Link group ... -> one shared library`；
2. `deps` → 7 个组库全部产出：`libEVFoundation.so`、`libEVPlatform.so`、`libEVBackends.so`、`libEVWorld.so`、`libEVDomains.so`、`libEVOrchestration.so`、`libEVEditors.so`；
3. `unit_test_platform` 链接成功，`ldd` 显示 7 个组库全部由 `LD_LIBRARY_PATH` 解析到；
4. `env -u LD_LIBRARY_PATH ctest -L unit_test_platform -E "^bundle/" --timeout 120 -j 4` → **100% tests passed, 0 tests failed out of 27**（0.4 s）。**环境变量被清空仍然全绿**，说明 §7.20 那条平台感知的 `ENVIRONMENT "LD_LIBRARY_PATH=..."`（CMake 生成期写入）确实生效——这正是翻转默认值在 Linux CI 上能否成立的关键。

**过程中又抓到第三个 ELF 专属问题（已修）：`-Bsymbolic`。** 组库与可执行文件都链了同一批第三方**静态归档**（Poco/SDL2/box3d/…）。ELF 会把符号解析成进程内唯一的一份（首个定义胜出），但**每个归档副本仍然注册自己的静态析构器**，于是 `Poco::DateTimeFormat::SORTABLE_FORMAT`（Poco 头里的全局 `std::string`）被析构两次：可执行文件在 `main` 之前就 `double free or corruption (!prev)` 中止（gdb 栈：`__cxa_finalize → __do_global_dtors_aux@libEVDomains.so → ~basic_string`），zeroerr 的 discovery 步骤因此报 `list-test-cases failed (Subprocess aborted)`。修法是在**非 Windows** 的组库上加 `-Wl,-Bsymbolic`：每个组库把自己内部的引用绑定到自己的定义，per-link-unit 副本互不干扰——这正是 Windows 构建本来就有的行为（也正是 §7.3 那些"每个链接单元一份状态"族在 Linux 上的等价物）。

**顺带确认的两条**：(a) 仅靠 `LD_LIBRARY_PATH` 就够，CMake 没有为组库写 rpath（`build/linux-debug` 的 `make run` 现在也由 `eve-dll-path-so` 前缀同一条路径）；(b) `ctest` 在 Linux 上不需要额外 PATH 处理，`ZeroErrDiscoverTestsImpl.cmake` 的平台分支选对了变量。

**仍未验证**：Linux 上的 `graphics` / `ui` 这类需要 Vulkan 与窗口的域（WSL 无显示与 GPU，属于环境限制，不是路线问题）；macOS（没有机器）。Windows 侧的整套读数是 §7.20 的那张表（5274 用例 / 129 失败，全部属于已记录的跨链接单元状态族）。

### 7.24 合并 dev 之后：为新增测试面补标注 + 新错误类 C2487（2026-09-20）

把 `dev` 合进本分支时，它已经在 56 个提交 / 436 个文件 / 207 个新文件上走远了，而那些代码是在"默认仍是 OBJECT"的时期写的。翻转默认值之后，dev 新增的**测试/宿主面**立刻变成硬错误：SHARED 全量构建有 **127 个未解析符号 / 7 个测试 exe**（ui 7、editor 5、tactics_rts 27、asset 49、core 1、npc_ai 4、graphics 40），而 7 个组 DLL 已经全部链通。补标注：**71 个规划站点 / 32 个文件 + 4 条手工行**（`asset/import/VegetationPreset.h` 的两个重载集）= **75 行带宏**；直方图 `WORLD 22 / ORCHESTRATION 14 / PLATFORM 14 / DOMAINS 13 / BACKENDS 10 / FOUNDATION 2`；`Export.h` +1；`_INLINE` 0；数据符号 0；friend/兄弟声明 0；C2280/C2027/C2036 0。

**新错误类 `C2487`（应并入 §7.5 的类级 dllexport 陷阱清单）**：**dll-interface 类的成员不得重复该类的 `EVENGINE_API_*`**。它有两个容易误判的特点：(1) 报错发生在**每个包含该头的 TU**，看起来像"某个 cpp 的问题"；(2) **OBJECT 模式同样报**，所以"静态路上也炸"不代表标注本身错。实践规则：**类级宏必须"取代"而不是"叠加"先前给单个成员加的宏**——给成员加宏是权宜（当时类还没标注），一旦类也标注了，成员那一份就要删掉（类级宏导出/导入的是同一批成员，语义不变）。本轮实测 3 处：`sensing/LineOfSightRouter.h:53 addProvider`、`graphics/VegetationField.h:95 snapshotElements`、`:107 replace`。新增树级预扫 `C:\evb2\b16_membermacro.py`：修完仍有 **26 处**"成员与类同名宏"的形状，全部**合法**——嵌套类自带自己的宏、以及规范明确要求的 `friend` 自由函数声明——**不要顺手"修掉"它们**。

**合并后的失败口径（新的权威读数）**：`ctest -E "^bundle/" --timeout 120 -j 4` → **5531 用例 / 131 失败 / 0 超时**，三次运行**用例名集合逐字节一致**。族别：SDL video `_this` **113**、ECS `default_table()` 6、ImGui `GImGui` 6、box3d `b3_worlds` 5、box2d `s_initialized` 1。**没有新族**：§7.20 的 SDL 111 一个不少，多出的 2 个是 dev 新增的窗口用例（`graphics.vegetation.render_mesh_update_and_season`、`asset.procgen.terrainDetailSidecarPublishesDecodableRuntimeResource`，来自 dev `7088c1dd8`），命中的仍是同一个 SDL 族。探针复核：ECS 表 27 targets / 197 objs、`GImGui` 4 个二进制、SDL 静态状态在 7 个组 DLL + 30 个 exe、box3d 2 份 PE 副本、box2d `s_initialized` 只在 EVBackends 与 EVWorld。

**其余验证**：SHARED 全量构建 EXIT 0（0 FAILED / 0 `error C` / 0 `warning C` / 0 未解析），EOL 归一之后**强制全量重编**（清 13 个模块对象、567 步）仍 EXIT 0、ctest 失败名集合不变；静态 `ninja -C C:\evs eve` EXIT 0（`eve.exe` 239,112,704 B）；`check/architecture-contracts --base 6a49a982` → OK；脚本测试 **178 OK**；EOL 收敛（41 → 0，无整文件 diff）。2 条基线遗留的 `C4273`（`SoftBody3DWorldBridge.cpp:56,62`）保持不动。

**Linux 侧合并后的复测**：merge 之后在 WSL 里重建并跑通 `platform` 27/27、`rpg` 301/301、`core` 515/515（插件的 1 个失败是"我只构建了指定目标、没构建 `native_test_plugin`"，补建后即通过）、`ui` 106/112（6 个挂起族）、`asset` 210/211（唯一失败 `asset.procgen.terrainDetailSidecar...` 在 Windows 上同样失败，属同一 SDL 族，不是 Linux 特有）。

### 7.25 五个跨链接单元状态族清零：SHARED 路线首次全绿（2026-09-20）

§7.24 记录的 131 个失败（SDL 113 / ECS 6 / ImGui 6 / box3d 5 / box2d 1）**全部**属于"进程级状态被每个链接单元复制一份"这一族——没有一个是新缺陷。本节记录逐族修法、过程中新抓到的四类陷阱，以及"两条路线共用一棵第三方安装树"的设计。

**各族的所有者与修法**

| 族 | 之前失败 | 进程内唯一所有者 | 修法 |
| --- | --- | --- | --- |
| SDL video `_this` | 113 | `SDL2d.dll` | 第三方聚合加 `-DSDL_SHARED=ON -DSDL_STATIC=ON`（§7.20 落地）；动态路线链导入库，归档路线链 `SDL2-staticd` |
| ImGui `GImGui` | 6 | `eve_imgui.dll` | imgui 源码编成一个独立 DLL（`IMGUI_API` 经 `SHELL:/D"…"` 传入、`/WX-` 例外，并用 `eve_append_system_libraries` 链上 SDL 与 Win32 系统库） |
| box3d `b3_worlds` | 5 | `box3d-dynamic.dll` | 第 13 个补丁 `cmake/patches/box3d-shared-library.patch` |
| box2d `s_initialized` | 2 | `Box2D-dynamic.dll` | 第 14 个补丁 `cmake/patches/box2d-shared-library.patch` |
| ECS `default_table()` | 6 | `EVFoundation.dll` | `external/ECS.hpp` 子模块补丁 + `src/engine/common/EcsDefaultTable.cpp` |

**ECS 那 6 个失败的表象值得单记**：用例名是 scene 1（`editor.automation_publishes_material_transactions_to_live_renderable`）、building 4（`buildingfx.*`）、combat 1（`actionPrefabRuntime.spawnsRealRenderablePoolsAndAppliesThreeLifecycles`），另有 1 个看起来与 ECS 毫无关系的 `RenderScenes.mesh.assimpNodeTransformBaked`——它比较"烘焙了节点变换"与"未烘焙"两帧的中心亮度，实体表分裂时前一阶段的场景残留到控制组，亮度比较就反了。**判据**：`default_table()` 修好之后这 7 个用例同时变绿，单跑各自通过。

**设计要点：双形态（归档 + dynamic twin），而不是"库类型随开关变化"。** 最初按 `EVE_BOX3D_SHARED` / `EVE_BOX2D_SHARED` 让库类型跟随 linkage，构建通过、测试全绿，但有一个只在**切换配置**时才现形的洞：第三方安装树只有一份（`build/third-party-binary/<platform>[-debug]`），内容却取决于最后一次配置的是哪条路线；先跑 SHARED 再跑 OBJECT（`make build/win32` / `make sdk/win32`）时，OBJECT 会链到导入库，产物悄悄带上 `Box2D-dynamic.dll`，**"单个 exe"的 release 保证就没了，而且不报错**。改成：**canonical target 永远是归档**（`Box2Dmdd.lib` / `box3dd.lib`，名字与今天逐字节一致），补丁再加一个 `Box2D_dynamic` / `box3d_dynamic` 共享 twin（`OUTPUT_NAME Box2D-dynamic` / `box3d-dynamic`，清空四种配置 postfix），`thirdparty_libs.cmake` 按 linkage 选名字。两条路线于是共用一棵安装树、互不污染、与先后顺序无关；SDL 本来就是双形态（`SDL_SHARED` + `SDL_STATIC`），box3d/Box2D 现在与它同构。

twin 的两个 CMake 细节：(a) `DEFINE_SYMBOL box3d_EXPORTS` / `Box2D_EXPORTS` —— box3d 的 `B3_API` 与 Box2D 的数据符号宏都按 `<target>_EXPORTS` 判断"我是导出侧"，换个 target 名就会丢掉这个宏；(b) 清空 `DEBUG/RELEASE/MINSIZEREL/RELWITHDEBINFO_POSTFIX`，让导入库只有一个可预测的名字。twin 只在桌面平台构建（与 `SDL_SHARED` 同一守卫）：Emscripten 没有共享库概念，移动端不需要它。

**陷阱 1：MSVC 不允许"先普通声明、再 dllexport 定义"（C2375）。** Box2D 的公共数据符号 `b2Vec2_zero`（`test/physics_core_boundary.cpp` 的 `b2World rawWorld(b2Vec2_zero)`）在 DLL 化之后 LNK2001：`WINDOWS_EXPORT_ALL_SYMBOLS` 只导出**函数**，数据必须显式标注。头文件写 `extern B2_DATA_API const b2Vec2 b2Vec2_zero;`、`.cpp` 写 `B2_DATA_API const b2Vec2 b2Vec2_zero(...)` —— **两处都必须带宏**：最小复现 `inline int &f();` + `__declspec(dllexport) int &f() { … }` → C2375"重定义；不同的链接"（§7.24 的 C2375 是同一错误码的另一种成因：跨文件 friend 声明缺符号）。`B2_DATA_API` 的判据与 box3d 对齐：`Box2D_EXPORTS`（twin 自带）→ dllexport，引擎消费侧 `BOX2D_DLL`（`link_groups.cmake` 中与 `BOX3D_DLL` 并列）→ dllimport，两者都没有时展开为空（归档路线不受影响）。

**陷阱 2：`file(GLOB)` 在 configure 期求值，冷构建会漏。** `eve_third_party_runtime`（把 `SDL2*.dll` / `box3d*.dll` 拷到 exe 旁）原来在 configure 期 glob：冷构建时 ninja 先跑 `deps`，此刻第三方安装树还是空的 → 列表为空 → `box3dd.dll` 从未被拷贝 → **30 个测试 exe 的 zeroerr discovery 步骤全部失败**，而 `extract_unresolved.py` 报 0 个未解析符号，极易误判成链接问题。修法：`cmake/copy_third_party_runtime.cmake` 用 `cmake -P` 在**构建期** glob（该目标 `DEPENDS third-party`，执行时安装树已经是新的），对时序免疫。

**陷阱 3：ExternalProject 的 stamp 不跟踪补丁文件内容。** 只改 `cmake/patches/*.patch` 的内容（命令行不变）不会重跑 patch/configure/build/install，改装过的 Box2D 头不会被重新安装，消费方仍看到旧声明（表现为"明明改了却还是同一个 LNK2001"）。配方：删 `build/<tree>/third-party-prefix/src/third-party-stamp/third-party-{build,install,done}` 再 `ninja`。CI 不受影响：第三方缓存的 key 里包含 `cmake/patches/**`。

**陷阱 4：改广泛包含的头之后必须清对象重编。** ECS.hpp 的内联函数改了语义，但 ninja 的 unscanned 依赖不追头文件；只重编"看着相关"的那批 TU 会得到"6 个失败修好 3 个"的假象。本轮做法：清空 `build/<tree>/**/*.obj` 后全量重编（2794 个对象）。

**陷阱 5：为 SHARED 加的标注会在 OBJECT 模式变成 dllexport，从而强制实例化成员。** `src/modules/network/NetRpc.h` 里 `class EVENGINE_API_PLATFORM NetRpc` 有一个 `std::map<uint16_t, ssq::Object>` 成员，而该头只前置声明了 `ssq::Object`。SHARED 下消费方看到的是 dllimport，不强制实例化，测试 TU 编译得过；OBJECT 下 `EVENGINE_API_PLATFORM` 退化为 `EVENGINE_API` = **dllexport**，MSVC 于是实例化该类的成员 → `test/network.cpp` 报 C2079（`std::pair<…,ssq::Object>::second` 使用未定义的 class），而同一次 `ninja -C C:\evs eve` 不会碰到（不编测试）。修法是让头文件**包含它按值持有的类型**（`#include <simplesquirrel/object.hpp>`）——这也是"类级 dllexport 会让成员实例化"这条（§7.5 的 C2280/C2027 家族）第一次落在**测试面**上：只构建 `eve` 的静态路线验收会漏掉它，必须真的编一次 OBJECT 测试。

**陷阱 6：Windows 上必需的"SDL 共享化"在 Linux 上暴露了第三个上游兼容问题。** 这个 pinned 的 SDL 2.0.16 以 `WAYLAND_SHARED=ON`（默认）动态加载 Wayland（dlopen + 函数指针表），因此**从不链接** `libwayland-client`。但 wayland 头文件从 1.20 起把 `wl_proxy_marshal()` 导向 `wl_proxy_marshal_flags()`，于是 SDL 编出来的目标文件里留下了直接引用：静态归档不会因此报错（引擎侧 `cmake/system_libraries.cmake` 本来就链了 `wayland-client wayland-cursor wayland-egl xkbcommon`），而**共享** SDL 会以 `undefined reference to wl_proxy_marshal_flags` 收场。第 15 个补丁给它补上 `wayland-client`。这条只在"Linux + 共享 SDL"同时成立时出现，Windows 侧的验证完全看不到它。

**陷阱 6b：按"选项开关"加的系统库补链，会踩到另一个平台上同样为 ON 的开关。** 上面那条补丁的第一版守卫是 `VIDEO_WAYLAND && WAYLAND_SHARED && UNIX && !APPLE && !EMSCRIPTEN`——Android 也满足（`ANDROID` 属于 `UNIX`，而 SDL 给 Android 同样开了 `VIDEO_WAYLAND`/`WAYLAND_SHARED`），于是 Android 构建直接 `ld.lld: error: unable to find library -lwayland-client`（SDL 在 Android 上本来就会产出 `libSDL2.so`，和我们的 `SDL_SHARED` 开关无关）。最终守卫生效两层：显式排除 `ANDROID`（含 `CMAKE_SYSTEM_NAME STREQUAL "Android"`）+ `find_library(EVE_WAYLAND_CLIENT_LIBRARY wayland-client)` 存在才链。判据：**只要某条补链是"按选项开关"加的，就必须假设它在另一个平台上也可能为 ON**；能用"库是否真的存在"判据就别用开关。

**陷阱 7：补丁文件的换行必须与目标文件逐字节一致。** `git apply` 比的是字节，而 `external/ECS.hpp` 的 blob 是 CRLF、补丁文件在 Linux 上是 LF：Windows（工作树默认 CRLF）能过，Linux 报 `error: src/ECS.hpp: patch does not apply`。两条看起来可行的替代路都不通——`--ignore-cr-at-eol` 在 Git for Windows 的这个版本里根本不存在（`error: unknown option`），而 `cmake -P` 里"先把目标文件归一化成 LF 再打补丁"会静默失效，因为 CMake 的 `file(READ)` 在所有平台上都会吃掉 CR（实测探针：`string(FIND "${content}" "\r")` = -1，读 27986 字节 vs 磁盘 29079 字节）。最终做法是 `.gitattributes` 里给这一个补丁文件钉 `text eol=crlf`（blob 仍按 LF 存，checkout 时统一成 CRLF），并在 WSL 脚本里先删再 checkout（同 blob 的 checkout 不会刷新工作树文件）。

**陷阱 8：新增的"链接第三方安装树"的目标必须自己声明等待第三方。** `eve_imgui` 本地怎么构建都过，CI 的 windows job 却报 `LNK1104: cannot open file 'SDL2d.lib'`：它按名字链第三方安装树里的导入库，却没有任何依赖把它排在 `third-party` 步骤之后（`create_module()` 给每个模块目标、`link_groups.cmake` 给每个组库都加了 `add_dependencies(... third-party)`，只有这个新目标漏了）。本地第三方树是热的（文件早就在），而 CI 那次因为补丁集变化导致缓存 key 变化而**冷构建**：日志里第 183 步在链 `eve_imgui.dll`，第 186 步才开始 configure 第三方。判据：**新目标只要链第三方安装树里的东西，就必须自己声明这条依赖**——热缓存的本地构建永远看不出来。

**陷阱 9：自定义命令的输出被多个目标消费时，Unix Makefiles 生成器每个目标一条规则。** 把 `nut_scripts.cpp`（由 `cmake/integrate.cmake` 生成）列为 30 个域目标各自的源文件，在 Ninja 下没问题（Ninja 对同一输出去重），`make -j` 下每个目标的 `build.make` 都会带一条生成规则、彼此不去重：一个目标在重新生成（并短暂删除）该文件时另一个正在编译它，CI 的 Linux job 于是报 `integrate.cmake:8 (configure_file): No such file or directory` 与 `*** Deleting file 'test/nut_scripts.cpp'`。**本地用 Ninja 永远看不出来**（WSL 的 Linux 验证也全是 Ninja）。修法：把生成的 fixture TU 编成唯一的 `eve_nut_fixtures` 静态库，各域目标改为链这个归档；WSL 侧用 `-G "Unix Makefiles"` 只做 configure，`grep -rl '^test/nut_scripts.cpp:'` 结果为 1（修改前是每个域一条）。

**陷阱 10（平台默认值的边界）：把 SHARED 设为桌面默认值会连带 macOS。** macOS 的平台支持库（`platform/macosx` → `EVPlatformMacOSX`）只链进宿主，因此 `libEVFoundation.dylib` 无法解析 `eve::macosx::getExecutablePath()`（`eve::filesystem::Filesystem::getExecutablePath()` 调用它），CI 的 macOS job 在 `ld` 阶段失败。这条路线在 macOS 上从未验证过（§7.23 已注明"没有机器"），所以先把 macOS 放回归档路线（`CMAKE_SYSTEM_NAME STREQUAL "Darwin"` / `BUILD_PLATFORM STREQUAL "macosx"`），并在根 `CMakeLists.txt` 的守卫注释里写明缺的是哪一个符号、要往哪个方向修（把该库链进消费它的组，或把符号挪到接口后面）。**判据**：改默认值时要把"所有桌面平台"列出来逐个确认，而不是假设"桌面"等同于"Windows + Linux"。

**陷阱 11：把第三方做成"双形态"之后，`-lSDL2` 在 ELF/Mach-O 上会被共享库抢走。** 为让 SHARED 路线共用一份 SDL 状态，第三方聚合加了 `SDL_SHARED=ON`（同时保留 `SDL_STATIC=ON`），安装树里于是同时有 `libSDL2.a` 与 `libSDL2.so`（SDL 给自己的静态目标也起名 `SDL2`）。Windows 两种形态名字天然不同（`SDL2d` vs `SDL2-staticd`），但 ELF/Mach-O 上 `-lSDL2` 一定解析到 `.so` —— **归档路线（OBJECT/release）因此被悄悄变成动态依赖**：WSL 里 `ldd build-object/test/unit_test_platform | grep -i sdl` 出现 `libSDL2-2.0.so.0`，"单个产物"在 Linux 上就破了。修法：归档路线按绝对路径取归档（`third_party_build.cmake` 把安装 lib 目录记入 `EVENGINE_TP_LIB_DIR`，built 与 prebuilt 两个注册点都设置；`thirdparty_libs.cmake` 非 SHARED 时链 `${EVENGINE_TP_LIB_DIR}/libSDL2.a`）。复测：OBJECT 路线 `ldd` 不再有 `libEV*`/`box*`/`SDL*`，SHARED 路线仍链 `libSDL2-2.0.so.0` + 7 个组库 + `libeve_imgui.so`，两条路线的 `unit_test_platform` 都是 27/27。**判据**：第三方拆成双形态后，必须在**两条路线、每个平台**上各自 `ldd`/`dumpbin` 取证——只在 Windows 上验证会漏掉 ELF 的 `-l` 解析规则。

### 7.26 分域拆分改为可选，默认跟随 linkage（2026-09-20）

§7.7 起把 `unit_test` 拆成"每域一个可执行文件"。这个拆分是**动态路线的开发便利**：SHARED 下每个测试 exe 只是 7 个组库的薄消费者，改一个域只重链一个小二进制。但把它无条件带进**归档路线**是错的——OBJECT 下每个 exe 都要**静态**装进整个引擎：实测 30 个域 exe 各约 0.3–0.5 GB，加上每个约 2.5 GB 的增量链接状态 `.ilk`，整棵树 93 GB（§7.25 的 OBJECT 验证口径就是在跟它搏斗：`LNK1116 无法增大 ilk 文件`、串行链接、删 `.ilk`）。

现在由 `EVENGINE_TEST_DOMAIN_SPLIT`（`test/CMakeLists.txt`）决定形状，**默认跟随 linkage**：SHARED → `ON`（30 个域 exe），OBJECT → `OFF`（单个 `unit_test`）。想强制另一种形状就显式传参。

| | 拆分（SHARED 默认） | 单体（OBJECT 默认） |
| --- | --- | --- |
| 目标 | `unit_test_<domain>` × 30 | `unit_test` |
| ctest 标签 | `unit_test_<domain>` | `unit_test` |
| Windows 实测体积 | 30 × (exe 0.3–0.5 GB + `.ilk` ~2.5 GB) ≈ 93 GB 树 | **exe 0.37 GB + pdb 0.43 GB + `.ilk` 3.16 GB** |
| 全量用例 | 5546 / 0 失败 | 5544 / 0 失败（`-L unit_test`，与 SHARED 的差异来自 profile 过滤） |

配套改动：生成脚本 fixture 的 `eve_nut_fixtures` 两种形状共用（§7.25 陷阱 9）；`native_test_plugin` 的宿主在拆分时是 `unit_test_core`（`plugins.cpp` 所在域），单体时是 `unit_test` 本身，`ENABLE_EXPORTS` / `--export-dynamic` 跟着宿主走；资源预取钩子（经典场景、Spine、蒙皮角色）挂在"消费该资源的 exe"上——拆分时是 `unit_test_graphics`，单体时是 `unit_test`。`Makefile` 的 `DOMAIN=` 与 `unit-test/<plat>` 只对拆分形状有意义，现在会先探测标签是否存在并给出可执行的提示，而不是让 cmake 报 "unknown target"。

**验证（单体形状）**：Windows OBJECT `ninja -C C:\evs unit_test` + `ctest -L unit_test -E "^bundle/" -j 16` → **5544 / 0 失败**；Linux OBJECT（WSL，默认即单体）`test/unit_test` 1.65 GB、无 `libEV*.so`、`ldd` 无引擎/box/SDL 共享依赖，`rpg.*` 143/143、`box2d.*` 14/14。**踩到的坑**：只 `--target unit_test` 构建时 `native_test_plugin` 不会被重链，它仍从旧的 `unit_test_core.exe` 导入引擎符号，插件用例于是 `LoadLibrary ... err=126`（`dumpbin /dependents` 一眼看出宿主名不对）——切形状后要么构建 `all`，要么单独重链该插件。

### 7.27 第二次合并 dev：合并树才是 CI 的输入 + 两个 ELF 专属陷阱（2026-09-20）

**前提修正（最重要的一条）**：`pull_request` 事件下 GitHub 检出的不是分支头，而是 `refs/pull/<n>/merge`——**分支与当前 dev 的合并树**。只验证分支本身会系统性地漏掉 dev 的新面：这轮 dev 又前进 **28 个提交 / 44 个文件**，新增 `test/hexmap_module.cpp` 等，于是 SHARED 全量构建出现 **10 个未解析符号**（`eve::hexmap::HexMapModule` 的构造/析构/`newGrid`/`newSphere`/`rebuildDirtyChunks`/`rebuildSphere`/`chunkMeshAt`/`releaseMeshes`/`releaseSphereMeshes` + 自由函数 `buildSphereWaterMesh`）——它们写在"默认还是 OBJECT"的时期，类/函数都没标注，组库里根本不导出。修法是两行标注（`HexMapModule.h` 的类、`HexSphereMesh.h` 的自由函数，均为 `EVENGINE_API_WORLD`），合并树上 Windows SHARED 全量 **5553 / 0 失败**。**流程结论**：验收 SHARED 路线要在合并树上做（`git merge origin/dev` 后构建），否则 CI 会替你发现。

**陷阱 12：ELF 的 copy relocation 会复制"惰性初始化"的数据符号。** CI Linux 的 42 个 GPU 用例 SEGFAULT（tensor/gpgpu/virtualgeometry/UI/softbody-gpu），本地用 lavapipe + Xvfb 复现出完全一样的栈：`vk::Device::getQueue<vk::DispatchLoaderDynamic>` 跳到 **0x0**。原因不是 vkbuilder 没初始化，而是 **exe 对 `vk::defaultDispatchLoaderDynamic` 生成了 `R_X86_64_COPY` 重定位**（`readelf -rW` 里 65 条 COPY，其中就有它）：加载时把库里的*初始*字节抄进 exe 的 BSS，之后所有模块都通过 exe 的那份解析这个符号——而 dispatcher 是引擎创建设备时才赋值的，exe 的副本永远保持全零（且 `-Bsymbolic` 让拥有者那一组的赋值只写自己的副本，两份永远不会汇合）。修法：给可执行文件加 `-Wl,-z,nocopyreloc`（`eve_engine_includes` INTERFACE，仅 ELF），数据引用改走 GOT。取证：COPY 数 65 → **0**；`nm -D libEVBackends.so` 仍是 dispatcher 的唯一定义者、exe 只剩 `U`；`gpgpu.dispatch.scaleFloats`、`tensor.gpu.reduceLargeTensor`、`softbody.gpu.clothBoundsAndInteract`、`virtualgeometry.gpu.buildIcosphere` 从 SEGFAULT 变通过。

**陷阱 12b：`-z nocopyreloc` 需要 `-fPIC`，`-fPIE` 不够。** 只加 nocopyreloc 会把问题从"读到零值"变成"加载器拒绝启动"：`Symbol '_ZSt4cerr' / '_ZN7zeroerr5FgRedE' / '_ZTISt13runtime_error' causes overflow in R_X86_64_PC32 relocation`，随后整套标签大面积失败（platform 2/27、physics 37/277…）。根因是 `-fPIE` 语义：PIE 代码**允许**编译器为它认为会被 copy 进可执行文件的数据发 PC32 重定位，一旦禁用 copy，这些重定位就够不到共享库里的定义。把 `POSITION_INDEPENDENT_CODE ON` 换成显式的 `-fPIC`（仅 SHARED、非 MSVC）之后，整套又全绿：platform 27/27、physics 277/277、scene 58/58、building 96/96、combat 151/151、rpg 301/301、scripts 68/68，四个 GPU 计算用例也通过。**判据**：ELF 上"禁用 copy relocation"与"必须 -fPIC"是一对，不能只做一半。

**同一条实验里确认：`-Bsymbolic` 不能退成 `-Bsymbolic-functions`。** 把组库的 `-Bsymbolic` 放松成 `-Bsymbolic-functions`（想让 RTTI/数据恢复 interposition）会立刻招回另一类问题：zeroerr 的 `FgRed` 等数据符号被 interpose 到别处后，PIE 里的 `R_X86_64_PC32` 直接溢出，discovery 步骤 `Subprocess aborted`。所以三者必须**同时**存在：组库 `-Bsymbolic`（每个组自持第三方状态、避免重复析构）+ 可执行文件 `-z nocopyreloc`（跨镜像共享的数据符号必须读到拥有者的活跃值）+ 全局 `-fPIC`（让禁 copy 之后的重定位可达）。

**陷阱 13：补丁仓库的换行也要在测试夹具里对齐。** `cmake.third_party_patch_idempotence` 在 Linux CI 失败（本地 Windows 通过）：夹具用 Python 文本模式写文件，Linux 下得到 LF，而补丁文件因 `.gitattributes` 是 CRLF，`git apply` 逐字节比较于是失败。修法：`scripts/tests/test_patch_third_party.py` 把夹具与"应用时那份补丁副本"都规范成 LF（真实换行匹配由真实补丁步骤保证，测试只负责幂等与漂移检测）。

**本地复现的边界**：WSL 里 lavapipe + Xvfb **能**可信复现 GPU *计算*族（tensor/gpgpu/softbody-gpu/virtualgeometry）的崩溃与修复，但渲染类用例（IBL/ClusteredLighting/outline/UI 图形）在本地会大面积失败，属于软件渲染环境的限制，不能当作 CI 的读数。

**Linux 侧复核**（WSL、16 核、全新 clone 的 `codex/test-domain-split`）：不传任何 linkage 参数 configure → `EVENGINE_MODULE_LINKAGE:STRING=SHARED` + 7 行 link group；第三方同时产出 `libBox2D.a` / `libBox2D-dynamic.so` 与 `libbox3d.a` / `libbox3d-dynamic.so`（双形态在 ELF 上同样成立，动态路线链 `-lBox2D-dynamic` / `-lbox3d-dynamic`）；`env -u LD_LIBRARY_PATH ctest -E "^bundle/" --timeout 120` → **platform 27/27、physics 277/277、scene 58/58、building 96/96、combat 151/151、rpg 301/301**（含全部 ECS 族与 box2d 用例）。ECS 补丁在 configure 期落地的直接证据：`external/ECS.hpp:178,184` 出现 `EVE_ECS_DEFAULT_TABLE_API Table &engine_default_table();` 与 `return engine_default_table();`。

**OBJECT 侧验证口径（磁盘）**：OBJECT 路由每个域测试 exe 带完整调试信息约 2 GB、增量链接状态 `.ilk` 各约 2.5 GB；30 个一起链接会把 80 GB 空闲空间吃光（`LNK1116 无法增大 ilk 文件` / `LNK1180 没有足够的磁盘空间完成链接`）。做法：删掉 `build/<tree>/**/*.ilk`，再用 `-j 1` 串行链接要验收的目标（`eve.exe` 本身只有 228 MB）。**取证**：`dumpbin /dependents C:\evs\src\engine\eve.exe` → 只有系统 DLL（vulkan-1 / MSVCP140D / ucrtbased / …），**没有** `Box2D-dynamic.dll`、`box3d-dynamic.dll`、`SDL2d.dll`，也没有任何 `EV*.dll` —— 双形态设计下 OBJECT 路线仍然只链归档，单文件产物成立。Linux 侧的 OBJECT 复测（WSL，`-DEVENGINE_MODULE_LINKAGE=OBJECT`）：不产出任何 `libEV*.so`，`unit_test_platform` 27/27，`ldd` 里没有 `libEV*` / `box*` / `SDL*`（陷阱 11 修好之前这里会漏出 `libSDL2-2.0.so.0`）。

**子模块补丁的落点**：`external/ECS.hpp` 的 `default_table()` 是"内联函数 + 函数局部静态"，MSVC 与 ELF 都不跨 image 合并，于是宿主与 7 个组各持一张表（§7.24 探针：27 targets / 197 objs）。补丁把它转发给 `ecs::engine_default_table()`，实现在 FOUNDATION 组（`src/engine/common/EcsDefaultTable.cpp`），**进程内唯一所有者**——与"每个可变事实只有一个权威所有者"这条架构规范同向。补丁在 **configure 期**用现成的 `cmake/patch_third_party.cmake` 应用（模块 TU 直接编译该头，没有任何 per-module 依赖能排在它们之前），子模块缺失时静默跳过；OBJECT 路线的预处理输出不变（整块包在 `#if defined(EVENGINE_MODULE_DLL)` 里）。`scripts/tests/test_patch_third_party.py` 增加了幂等覆盖：box3d / box2d / ECS 三个补丁各连打两次。

**读数**：Windows SHARED `ctest -E "^bundle/" --timeout 120 -j 16` → **5546 用例 / 0 失败 / 0 超时**（连续两次构建口径一致：先按"库类型随开关"实现时全绿，改成双形态之后重编重跑仍全绿）；其中 `unit_test_physics` 277/277、`box2d.*` 14/14，`unit_test_ui` 112/112、`unit_test_procgen` 643/643 为逐域复核。静态 OBJECT 路线 → `eve.exe` 链接成功（单文件，依赖表见上）+ `unit_test_{physics,scene,network,building,combat}` **608/608**。源码门禁 `make check/quality check/module-layers check/bindings check/examples check/test-manifest` 全绿；`scripts/tests/test_patch_third_party.py` 覆盖三个新补丁的幂等性。
