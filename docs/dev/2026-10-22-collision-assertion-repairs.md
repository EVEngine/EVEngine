# 碰撞回归断言修复（2026-10-22）

## 范围

修复前一轮记录的十项断言告警，并使真正的断言失败在独立 runner、CTest 逐例和
bundle 中都返回失败。没有通过跳过、白名单或放宽原有误差阈值来清零。

## 断言基础设施

- 仓库的 zeroerr shadow header 将 CHECK 系列改为 ERROR 级断言；失败立即终止该用例，
  不再只记 warning。CHECK/REQUIRE/ASSERT 的比较宏先缓存左右参数，各求值一次，
  避免 `CHECK_EQ(world.explode(...), 2)` 在比较和生成诊断时各执行一次爆炸。
- 固定的 zeroerr 源码保持不动。CMake 生成 runner 副本，捕获辅助函数中无 TestContext 的
  AssertionData 时补记失败，并将未处理的 assertion warning 纳入进程退出码。
  上游源码不匹配时配置直接拒绝，不静默使用旧行为。
- CTest 的所有 zeroerr 逐例和 bundle 入口增加 WARN/ERROR assertion 日志保护。
  预期由负向测试捕获的引擎 FATAL invariant 异常仍允许通过；未捕获的异常由 runner 的
  非零退出码判失败。资源测试汇总器同步拒绝 warning assertion。
- 独立 probe 验证参数只求值一次、预期捕获 invariant、CHECK、比较 CHECK、REQUIRE、
  ASSERT 和辅助函数失败。Python 契约测试实际运行 CTest，证明退出码为零但输出
  WARN/ERROR Assertion Failed 的假 runner 在逐例与 bundle 中都不能通过。

## 十项对应处理

| 用例 | 原因与修复 |
|---|---|
| dampingGravitySleepMassAndCenters | 0.25 s 超过 step 0.05 s 上限；改为相同总时长的 15 个固定步 |
| perAxisLocksAndAdvancedForces | 同上，保持原位移和锁轴断言 |
| kinematicTargetTransform | 0.5 s 超限；改为 30 个固定步到达相同目标 |
| wheelCombinesSuspensionSpinAndSteering | 空载达到匀速后力矩趋零是正确行为；用单子步采样首次加速力矩，同时保留最终转速与转向角验证 |
| explosionUsesGeometryFalloffMaskAndReportsVelocityDelta | 比较宏把爆炸执行两次；修复宏，不改爆炸公式或断言 |
| convexHullSupportsLocalTransformAndRigidCollision | 自由球会沿斜面滚落，且原高度门槛不对应斜面；锁住水平自由度，按实际斜面 y=-z/4 的解析支撑高度验证，误差仍为 0.02 m |
| rayAndShapeCastExposeMeshAndHeightFieldTriangleIndex | 把后端凸包面编号误当源三角形编号；普通形状统一返回 -1，mesh/height-field 保留真实源索引，覆盖 owning ray 与缓存 ray/cast |
| sdf.moverStopsAtTwoPlaneCorner | 0.5 m 网格的 min 距离场插值把角点圆化，无法匹配 0.01 m 容差；网格细化到 0.02 m，保留原厘米级位置断言 |
| sdf.moverStepOffsetClimbsOnlyLowLedges | 胶囊轴线上等距接触的首个混合法线会产生虚假上升；在等距采样中选择最阻挡运动的真实接触法线，同时将网格细化到 0.02 m 以解析 0.01 m skin；原高/低台阶位置断言不变 |
| climbing.ledgeGrabTransitionsToHangAndDrop | 计数按 tick 重置，不是累计探测次数；分别验证起始候选探测至少四次、稳定悬挂每 tick 一次 clearance 查询 |

## 架构与验收口径

查询实现按原边界移至 `World3DQueries.cpp`，共享内部 ABI/验证辅助函数在
`World3DQueryInternal.h`；physics-core-only 显式源列表与 hot_path 声明同步更新。
不新增模块、ECS System、Link、provider、公开签名、类布局或持久字段。
World3D 继续唯一拥有查询缓存，owning Result 与线程亲和性不变；仿真输入使用固定步和
已有容差等级，无 wall clock/RNG 依赖。独立核心构建只作为 L1 编译证据，不宣称 L2/L3/L4。

最终验收要求：Debug 干净构建、十项原用例及完整 Box3D/climbing 相关回归、真实断言退出码
probe、源码门禁与核心独立编译。未以 mock 或 CTest 绿色状态替代断言语义验证。


## 实际验收结果

- macOS Debug 引擎、单元测试与断言 probe 编译成功。shadow header 首次引入后已清理并完整重编译，最后的参数生命周期修复再次触发所有受影响 TU 的重编译。
- `make test/macosx-debug`，筛选 `box3d.|climbing.|physics.collision.|physics.mover.|test.assertionExitContract`，关闭重试：**185/185 通过**，约 11.85 秒。十项原告警全部通过，CTest 详细日志没有 WARN/ERROR assertion failure。此处不代表全仓 C++ 用例均已运行。
- 实际 `assertion_contract_probe`：单次求值（含临时参数）、预期捕获 invariant 通过；CHECK、比较 CHECK、REQUIRE、ASSERT、无 TestContext helper 的失败全部返回非零。
- `make check`：**182 项 Python/契约测试通过**，源码质量门禁通过（base `origin/dev`）。包含实际 CTest 的零退出码假 runner 逐例/bundle 失败验证。
- `physics-core-only` 的 `eve_profile_smoke`：L1 独立编译通过；不扩展为运行或链接裁剪能力声明。
- `git diff --check` 通过。

证据保存在忽略目录 `build/collision-validation/assertion-repairs-*.log`。
本轮适用的模块边界、单一 owner、Result、固定仿真时间、公共生命周期与裁剪取证规则均保持；无架构规则例外。

## PR #625 与最新 dev 的合并适配

基线更新为 `5151e1480cfe3909982c12ca1ce43ba594e3fca0`。保留 dev 的后端接口和
World3D 转发层，将最终位置支撑、Sensor 排除和 primitive 三角形索引修复迁入
`box3d/Box3DWorldMover.cpp` 与 `box3d/Box3DWorldQueries.cpp`。不恢复旧实现或已移除的
JSON 架构 catalogue；hot_path 按 dev 的代码式规则声明。

十项旧测试修正迁入现有 `test/unit/physics/physics_tests.cpp` 和
`test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp`；新增碰撞、
接地、climbing 与性能用例也归入 physics-simulation 目录。测试等级元数据显式登记，
`test/MODULE_TEST_COVERAGE.md` 由 `scripts/test_module_coverage.py --write` 更新。
CTest 的新增失败保护保留 dev 的精确过滤、等级、前缀、环境和 resourceFormats 既有强约束。
zeroerr 的 runner 修复与 dev 的 compiled diagnostics 同时启用。

本次合并后的针对性验证使用固定依赖 `477333b3e4045a2c09a9ef8cf761acb94e9eb6f6`：

- physics-core-only / eve_profile_smoke 编译通过（Box3D 配置，Jolt 关闭）。
- 直接链接该核心对象的真实回归通过 22 个 CCD、查询、mover 与 SDF 用例，19,446 项断言无失败或告警。
- 在 compiled diagnostics 模式重新编译的断言 probe 通过；CHECK/REQUIRE/ASSERT/helper 失败均返回非零。
- 发现器及资源汇总的 23 项 Python 测试通过，包括零退出码断言日志被 CTest 判失败的验证。
- `make check` 对最新 dev 差异通过，461 项脚本/契约测试全部通过。
- 正常宿主 CMake 入口编译 `assertion_contract_probe` 并通过 CTest 的 `test.assertionExitContract`，等级标签从测试元数据派生为 unit/no-render。

此前 185/185 是合并前的历史验收，本次没有将其作为新基线上的全量重跑结果。
