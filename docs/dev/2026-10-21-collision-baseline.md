# 碰撞可靠性与性能基线

## 第一阶段范围

先验证 Box3D 经 EVEngine 封装后的真实行为，再决定生产代码修改。
本阶段不改变公开 API、持久化格式或模块边界。

常规回归位于 `test/physics_collision_regression.cpp`，由现有 CMake 自动发现：

- 120 m/s、半径 0.1 m 的球撞 0.02 m 静态薄墙，60 Hz、单 substep；逐 tick 检查球心未穿到墙另一侧。
- 单个 Bullet 与普通动态球迎面运动，60 Hz、四 substeps；逐 tick 检查有限坐标和相对次序。
- 八层立方体固定步运行十秒，检查地面支撑、最终位置与垂直速度；数值容差明确写入断言。

这不是任意高速场景的保证。Box3D `b3BodyDef::isBullet` 的上游契约排除 Bullet/Bullet，
并说明非 Bullet 先移动后产生的初始重叠可能漏检。动态迎面测试刻意避开该初始重叠，
不能据此扩展为通用动态 CCD 保证。高速旋转细长体仍需独立验证。

## 执行

使用根 Makefile 的主机 lane；macOS 示例：

```sh
make test/macosx-debug FILTER=physics.collision.
make test/macosx-debug FILTER=physics.benchmark.
```

性能对比使用 Release，并明确指定基准筛选以显示成功测试的输出：

```sh
make build/macosx JOBS=4
ctest --test-dir build/macosx -R '^physics[.]benchmark[.]' -V
```

Linux/Windows 使用对应平台构建目录和 Makefile lane；完整主机测试仍遵守根 AGENTS.md
的运行环境要求。基准标记为 `benchmark`，普通 `make test/*` 默认排除，显式 FILTER
或 `INCLUDE_BENCHMARKS=1` 才纳入。

## 固定性能负载

`test/physics_collision_benchmark.cpp` 创建 100 列、每列 10 层的动态盒子和一个地面，
禁用睡眠，固定 60 Hz、4 substeps。初始化与 120 tick 预热不计时；记录随后 300 tick
的墙钟耗时 p50、p95、最大值，再单独计时 10000 次射线查询。每条射线必须命中，
同时输出接触数、清醒物体数和后端分配字节数。后端字节数不是进程总内存。

不设置跨硬件的绝对耗时门禁。比较时保存完整输出，并记录提交 SHA、CPU、OS、编译器、
Debug/Release、验证/诊断开关；相同环境至少独立运行三次。这一负载不能代表网格地形、
CCD 密集场景或流送世界，也不能直接用于宣称与其他引擎的性能比值。

## 后续验收顺序

1. 增加高速旋转、薄网格与接缝、动态初始重叠的最小复现，明确支持边界。
2. 核验 climbing 层已有的台阶和贴地流程，补组合场景与移动平台、推挤刚体的证据，再按失败实现。
3. 增加网格/地形与查询密集负载；建立同机历史记录后制定回归预算。
4. 依据实际卡点完善碰撞资产烘焙与碰撞录制调试。

## 本机验证记录（2026-10-21）

完整 macOS Debug 构建（`eve`、`unit_test`）及运行验收已通过。
`TMPDIR=/private/tmp make check` 已通过（使用临时虚拟环境中的检查工具），其中仓库
脚本测试全集为 180 项；指定规范化临时目录是为了避开现有转换器测试对 macOS
`/var` 与 `/private/var` 路径别名的处理差异。新增 C++ 文件通过 clang-format 18 检查。

初次复用本机只读 `third-party-binary/macosx-debug` 时，现有 `ScriptModule.cpp`、
`ScriptCompiler.cpp` 报告缺少 `sq_setmodulehandlers`、`sq_setnamedargresolver` 和
`sq_setannotationresolver` 声明，说明该缓存的 Squirrel 头文件不满足当前引擎要求。
已通过根 Makefile 在本工作树重新构建固定版本依赖并安装至
`build/third-party-binary/macosx-debug`，未修改共享缓存或生产代码。

```sh
make build/macosx-debug JOBS=4 CMAKE_EXTRA_ARGS='-DEVENGINE_THIRD_PARTY_BINARY_DIR= -DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF -DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF -DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF'
make test/macosx-debug JOBS=4 CTEST_JOBS=4 FILTER=physics.collision. CTEST_REPEAT= CMAKE_EXTRA_ARGS='-DEVENGINE_THIRD_PARTY_BINARY_DIR= -DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF -DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF -DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF'
make test/macosx-debug JOBS=4 CTEST_JOBS=4 FILTER=box3d. CTEST_REPEAT= CMAKE_EXTRA_ARGS='-DEVENGINE_THIRD_PARTY_BINARY_DIR= -DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF -DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF -DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF'
ctest --test-dir build/macosx-debug -R '^physics[.]benchmark[.]' --repeat until-fail:3 -V
```

- 新增碰撞回归：3/3 通过；现有 Box3D：76/76 通过，均未使用失败重试。
- 千刚体基准：三次独立进程均通过，10000 条射线全部命中。
- 本次没有运行全仓 C++ 测试全集。

计时环境：Apple M4（10 核），macOS 26.6.2，AppleClang 21.0.0，arm64。
引擎为 Debug，第三方依赖按仓库 macOS 约定为 Release；未启用 sanitizer，场景不创建
Vulkan 窗口。源码基于 `434ae72e4c5715e84c8a8623311e76a459a00f5b` 加当前未提交的测试改动，
第三方固定提交为 `54725d0ae7c4a3092dc16841e0b0064f03365a45`。

| 独立运行 | Step p50（ms） | Step p95（ms） | Step max（ms） | 10000 rays（ms） |
|---|---:|---:|---:|---:|
| 1 | 0.604292 | 0.612125 | 0.629208 | 6.574583 |
| 2 | 0.603625 | 0.614125 | 0.654792 | 6.677125 |
| 3 | 0.604667 | 0.615709 | 0.633833 | 6.536333 |

三次的末帧接触数均为 1000，清醒动态体均为 1000，后端分配均为 2469776 字节。
这些是固定场景的 Debug 冒烟计时，不能替代 Release 性能基线或推导其他引擎的相对性能。

## 第二阶段：角色移动支撑状态

进一步核对发现，`ClimbingMotionSystem` 已在生产路径组合实现低台阶跨越和贴地，
`test/climbing_ecs.cpp` 也有跨步测试。先前仅从 World3D 底层接口判断整个引擎缺少这些
能力是不完整的；本阶段复用现有组合流程，不增加第二套角色控制器。

最小复现确认了三个底层缺陷：从有限平台水平走出后仍着地、从地面起跳后仍着地、
纯水平移动时着地但返回零法线。根因是把路径中曾访问过的接触平面作为最终支撑，
而代表性法线只记录反向阻挡移动的平面。修复在最终位置做一次 `b3World_CollideMover`，
只用终点支撑判断 grounded；没有阻挡法线时返回支撑法线，保留既有滑墙法线语义。
该查询沿用同一 Layer/Mask、自身排除和 sensor 过滤。

先将 mover 段原样拆至 `World3DMover.cpp`，再修改算法；显式的 physics-core-only 源列表
同步纳入新 TU。公开签名、类布局、bindings、存档格式和模块边界不变。新增六项底层
支撑测试与三项生产 ECS 组合测试（低台阶、高台阶、低天花板）。移动平台仅验证支撑
移走后立即失效，未实现自动平台跟随、旋转平台或推挤动态刚体。

验证中发现 zeroerr `CHECK` 失败被记为 warning，默认 CTest 仍可返回成功。本次新增碰撞、
基准与支撑测试均改为 `REQUIRE`；修复前用强断言实际得到三项失败，不能将之前的 CTest
绿色输出单独作为无断言失败的证据。旧 Box3D 回归还需检查 verbose 输出中的 warning。

### CCD 复核边界

直接链接当前固定版本 Box3D 的独立探针复现了同样行为，排除了引擎封装：

- 静态薄墙单子步：首 tick 球心 x=-0.105，第二 tick x=-0.001343，仍在墙前一侧，
  但允许短暂深重叠。原先要求每 tick x<=-0.08 的测试把“防穿越”误写成了“几乎无重叠”，
  已纠正为球心 x<0。没有修改求解器、放宽全局容差或隐藏这项限制。
- 动态双高速、单子步：第二 tick 左球 x=-0.579648、右球 x=-0.615352，顺序翻转。
  四子步同一 tick 则为 -0.669451 和 -0.525549，测试只保证该明确配置的固定场景。
  单子步动态穿越仍是已复现的未解决限制，不能宣称通用高速动态 CCD 已完成。

独立探针与日志保留在忽略的 `build/collision-validation/ccd-probe.cpp` 及
`ccd-*-substep*.log`；构建方式是以 C++20 链接本工作树依赖安装中的 `libbox3d.a`。

### 本轮运行验收

- 最终 `TMPDIR=/private/tmp make check` 通过，脚本测试全集 181 项；新 C++ 文件经 clang-format 18 检查。
- Debug 完整构建通过；六项底层支撑测试、三项角色组合测试、三项校正后的碰撞基线，
  合计 12 项强断言测试通过，未使用失败重试。
- `physics-core-only` 的 `eve_profile_smoke` 通过，包含新 TU 的真实独立编译。
- 千刚体基准改用强断言后通过，万次射线全命中；本轮计时与并行回归重叠，不作为可比较数据。
- 扩大到 Box3D、climbing 和本轮功能用例的 182 项进程隔离回归时，CTest 返回 0，
  但下表十项出现 assertion warning，因此不能声称这 182 项语义上全部通过。

| 现有测试 | verbose 日志中的未满足断言 |
|---|---|
| `box3d.body.dampingGravitySleepMassAndCenters` | 线速度未衰减、重力速度未增加；该测试注入 0.25 s，超过后端 step 的 0.05 s 契约 |
| `box3d.body.perAxisLocksAndAdvancedForces` | Y 位移未达到预期 |
| `box3d.body.kinematicTargetTransform` | 目标 XYZ 未达到；测试注入 0.5 s，大于 step 上限 |
| `box3d.joint.wheelCombinesSuspensionSpinAndSteering` | spin torque 未达到期望下限 |
| `box3d.world.explosionUsesGeometryFalloffMaskAndReportsVelocityDelta` | 报告的 delta velocity 与当前 velocity 不一致 |
| `box3d.shape.convexHullSupportsLocalTransformAndRigidCollision` | 球体最终高度不符 |
| `box3d.query.rayAndShapeCastExposeMeshAndHeightFieldTriangleIndex` | primitive 命中的 triangle index 为 0，断言期望 -1 |
| `box3d.sdf.moverStopsAtTwoPlaneCorner` | 两个轴的移动量未达到约束期望 |
| `box3d.sdf.moverStepOffsetClimbsOnlyLowLedges` | 高台阶场景 Y 位移高于预期 |
| `climbing.ledgeGrabTransitionsToHangAndDrop` | queryCount 为 1，断言期望至少 4 |

上述是实测发现清单，没有通过修改旧断言、扩大容差或加入跳过来清零。它们需要独立复现与
修复，不属于本轮 grounded/Sensor 修复的通过证据；旧有“76/76 通过”仅反映当时 CTest 状态，
现更正为不能证明全部断言成立。日志位于 `build/collision-validation/mover-regression-full.log`。

### 适用架构契约

- World3D 是查询结果的唯一 owner；owning Result 不保留后端指针，也不引入跨帧支撑缓存。
- `R-MECH-1`：已知的单 World3D、逐 tick 同步调用、无跨模块原子写入，继续直接调用；
  `R-MECH-2`：`World3DMover.cpp` 登记为 hot_path，无逐帧 capability 或字符串查询。
- 本次不新增 ECS System、Link、持久字段、optional provider 或昂贵创建 API；其相关迁移与
  销毁策略不变。浮点仍按已有容差级契约验证，使用调用方输入的位移，不读取时间或 RNG。
- `physics-module-interface` 登记六个面、owner、线程与成本；门禁新增该声明类型的字段检查，
  不据此声称已经完成全仓能力交叉校验。
- `R-BOUND-3`：physics-core-only 只验证 L1 独立编译边界，不宣称 L2/L3/L4 裁剪。
- 无批准例外、无新增跳过、降级或债务白名单。

后续十项修复和断言失败语义的验证见 [2026-10-22 碰撞断言修复](2026-10-22-collision-assertion-repairs.md)。本页保留发现时的历史结果。
