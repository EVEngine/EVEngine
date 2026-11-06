# 多尺度 CI 组合接线

状态：CI 配置已补齐，Windows 本机对应 61 项原生测试通过；尚无远程 CI 运行结果。

## 问题与修改

Linux 图形 job 原先使用默认构建，随后 smoke 所有 runnable examples。
多尺度模块默认关闭，而 multiscale-spheres 需要 graphics_multiscale，导致这个组合缺少提供者。
现仅在 .github/workflows/ci.yml 的 Linux Debug 图形 job 显式开启 spatial_multiscale、
physics_field、physics_field_coupling、procgen_recursive、procgen_recursive_graphics、graphics_multiscale。
六个模块在各自 manifest 中仍默认 OFF；没有改变默认构建、模块声明、启动行为或旧 API。

新增 Verify opt-in multiscale composition 步骤，在六个功能族上分别运行 CTest，使用
--no-tests=error、固定 -j 1、Vulkan validation，并独立于 change-scope label filter。
这防止“其中一个模块没有测试被发现，但其余用例通过”造成假绿。原完整测试与全示例 smoke 保留。
它不测量百万对象性能，不能用软件 Vulkan 或短测试结果宣称 60 FPS。

## 验证

工作流 YAML 已解析成功。scripts/tests/test_multiscale_ci.py 从真实工作流提取 run 块，
使用真实 CTest 和临时 CTestTestfile.cmake 执行成功、六种缺族及首族失败场景；仅显示服务器 wrapper
由无窗口 fixture 替代。3 项测试通过，日志 build/multiscale-ci-selection-tests.log。
这证明筛选/错误传播逻辑，不代表 Linux 引擎或 Xvfb 已在本机运行。

本机 build/win32-debug 对相同筛选发现 61 项且所有可执行文件存在：
坐标/参考系 17、场 15、点场耦合 3、递归模型 14、递归图形 3、多尺度图形 9。
开启 Vulkan validation、逐用例进程隔离、-j 1 运行 61/61 通过，约 12.91 秒。
发现与执行记录为 build/multiscale-ci-host-discovery.json、multiscale-ci-host-tests.log。
这不是全仓 Windows CTest，也不是 Linux/其他平台终态证据。

本轮只修改 CI 接线、测试与记录，没有生产 API/ECS/持久化/生命周期变化。
保持单一模块 manifest、默认可选性与失败不可静默忽略；没有架构规则例外。
完整 make check（base=origin/dev）退出 0：532 项脚本测试、5 项既有平台跳过，未新增跳过；
日志为 build/multiscale-ci-composition-source-check.log。新增测试 ruff 与 git diff --check 通过。
远程 CI、provider-absent 组合与整帧性能仍未完成。
