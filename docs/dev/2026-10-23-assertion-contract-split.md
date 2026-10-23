# 全局断言失败语义的独立工作

本工作从碰撞 PR #625 拆出，目标分支为 dev。不依赖 #625 的物理实现或测试。

包含 CHECK 失败升级、比较参数单次求值、MSVC 宏兼容、runner 非零退出码、
CTest 逐例与 bundle 日志保护、资源测试汇总，以及真实断言 probe 和发现器契约测试。

这些全局改动曾使既有告警成为真正的 CI 失败。已记录的 334 个不同失败用例见
[失败清单](2026-10-22-pr625-ci-failure-inventory.md)。这些用例尚未逐项修复；本工作应保持
Draft，待用例或实现按实际契约修复并重新验证后再合并。不通过放宽断言、跳过或新增失败基线清零。
清单来自原分支的 CI 快照；本分支使用更新的 dev，实际仍失败数量以重新执行为准，
不能把历史 334 项视为当前基线的完整测量。

本分支首次 macOS CI（[run 39880052373](https://github.com/EVEngine/EVEngine/actions/runs/39880052373/job/120176786263)，
提交 e50a4572e83a50c95e9d7c5be088eb50e922ca50）编译通过，断言退出码 probe 通过，
但 `LastTestsFailed.log` 记录了 334 项失败。这是本分支本轮测量，并不表示与历史清单逐项相同。
其中 `common.zeroerr.preservesAssertionCounters` 原来通过故意执行 `CHECK(2 == 3)`
检查警告计数；新契约会使它抛出并输出失败日志。现在直接构造警告级 `AssertionData`
并验证上下文计数，避免把测试数据当作真实失败提交给 runner。独立编译该文件的 zeroerr
诊断测试组后，10 项测试、67 条断言通过；退出码 probe 也通过。其余失败仍待逐项诊断，
不以本次局部验证代替全量 CI 复跑。

后续 [run 39889319408](https://github.com/EVEngine/EVEngine/actions/runs/39889319408)
（提交 4fe3c7fc19369e7b8027348111e92e6cadc8f150）的 Windows/macOS 编译通过，
计数器自测与退出码 probe 均通过；测试失败产物分别记录 331/332 项失败。
Linux 与 ASan 则在编译计数器自测时停止：GCC 不接受向 `AssertionData` 直接传入
该位域聚合类型的花括号参数。改为先构造显式 `assert_info` 局部变量，再传给构造函数。
本机 GCC 13 对修复前文件复现同一错误，修复后的同一测试文件通过语法检查；
macOS GCC 检查仅将 zeroerr 的平台专用 `__builtin_debugtrap` 映射为 `__builtin_trap`，
以避开 GCC 在 Apple 平台不支持该内建函数的问题，该检查参数未加入产品构建。
Linux/ASan 在本轮未进入完整测试阶段，不能据此判断其运行期测试或内存检查结果。

#625 撤回本工作中的全局策略，保留独立碰撞修复；爆炸测试在断言前保存操作结果，
因此不需要依赖全局比较宏的单次求值改动。
