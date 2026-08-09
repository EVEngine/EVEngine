# GoogleTest → zeroerr 测试框架迁移设计

> 状态：已讨论确认；实现计划见 `docs/dev/superpowers/plans/2026-08-04-zeroerr-test-framework.md`。
> 范围：用 [zeroerr](https://github.com/sunxfancy/zeroerr) 完全替换 GoogleTest；CTest 支持按用例单独运行；同步清理 third-party。

## 1. 目标

- 用 zeroerr 完全替换 GoogleTest（构建链接 + 全部测试源码）
- zeroerr 以 git submodule 置于 `external/zeroerr`，经 `add_subdirectory` 编入工程，便于本地修 bug
- CTest 能按用例单独跑（借助 zeroerr 的 `--list-test-cases` / `--testcase=<regex>`）
- 同步清理 `EVEngine/third-party`：移除 zeroerr，并停止构建 googletest

## 2. 非目标

- 本轮不改 zeroerr 公共 API（除非 `add_subdirectory` 缺 PUBLIC include 等阻塞项，再做最小修补）
- 不把 fuzz / bench 接入 EVEngine CI
- 不改变现有测试业务语义（只换框架宏与 fixture 生命周期）
- 不把 ECS.hpp 自带的 `test/zeroerr.hpp` 并入本迁移

## 3. 依赖布局

| 位置 | 变化 |
|------|------|
| `external/zeroerr` | 新增 submodule → `https://github.com/sunxfancy/zeroerr` |
| `external/ECS.hpp` | 不变 |
| 根 `CMakeLists.txt` | include 从 `third-party/zeroerr` 改为 submodule；`BUILD_TESTING` 时 `add_subdirectory(external/zeroerr)` |
| `third-party` 仓库 | 删 zeroerr submodule 与 `install(FILES ... zeroerr.hpp)`；去掉 `add_subdirectory(googletest)` |

根工程接入时强制：

- `BUILD_TEST=OFF`
- `BUILD_EXAMPLES=OFF`
- `USE_MOLD=OFF`

避免子项目选项污染父工程。

## 4. 测试迁移约定

### 4.1 API 对照

| GoogleTest | zeroerr |
|---|---|
| `#include <gtest/gtest.h>` | `#include "zeroerr/assert.h"` + `#include "zeroerr/unittest.h"` |
| `TEST(Suite, Name)` | `TEST_CASE("Suite.Name")`（保留 Suite.Name 便于过滤） |
| `TEST_F(Fixture, Name)` | `TEST_CASE_FIXTURE(Fixture, "Fixture.Name")` |
| `EXPECT_*` | `CHECK` / `CHECK_EQ` / `CHECK_GE` … |
| `ASSERT_*` | `REQUIRE` / `REQUIRE_EQ` … |
| `EXPECT_FLOAT_EQ` / `EXPECT_NEAR` | `CHECK(std::abs(a - b) < eps)`（或等价写法） |
| `gtest_main` | 新增 `test/main.cpp`：`return zeroerr::UnitTest().parseArgs(argc, argv).run();` |

### 4.2 ScriptTest fixture

zeroerr 的 `TEST_CASE_FIXTURE` 只做「构造实例 → 调用测试方法」，**不会**调用 `SetUp` / `TearDown`。

因此：

- `ScriptTest` 去掉 `public ::testing::Test`
- 将原 `SetUp()`（`expose` + `compileSource` + `run`）移入构造函数，保持原有顺序
- `UnitSciptTest` 宏保留，派生类仍只传入 script content

### 4.3 涉及文件

- `test/ScriptTest.h`
- `test/main.cpp`（新增）
- `test/ECS.cpp`、`test/RenderSystem.cpp`、`test/filesystem.cpp`、`test/graphic.cpp`、`test/model.cpp`、`test/mouse.cpp`、`test/simplesquirrel.cpp`、`test/window.cpp`
- `test/CMakeLists.txt`
- `cmake/ZeroErrDiscoverTests.cmake`（新增）
- `docs/dev/依赖项.md`、`docs/dev/整体架构.md`

## 5. CMake / CTest

### 5.1 链接

- `unit_test` 链接 `zeroerr`，不再链接 `gtest` / `gtest_main`
- include 使用子目录导出的 `ZEROERR_INCLUDE_DIR`；若 `zeroerr` 目标缺 PUBLIC include，则补最小 `target_include_directories`，或先用该变量
- 去掉 `include(GoogleTest)` / `gtest_discover_tests`

### 5.2 按用例注册 CTest

新增 `cmake/ZeroErrDiscoverTests.cmake`：

1. 构建后执行 `unit_test --list-test-cases`（或 `-l`）得到用例名列表
2. 对每个名字：`add_test(NAME <name> COMMAND unit_test --testcase=^<escaped_name>$)`
3. 额外注册 `unit_test_all`：无 filter 跑全集，兼容现有 Makefile 一次性执行

用法：

```bash
ctest -R "Batcher.addRect"   # 单独跑
ctest                        # 跑全部已注册用例
build/.../unit_test.exe      # Makefile 路径仍可一次跑全
```

若 `--list-test-cases` 输出难以稳定解析，再考虑给 zeroerr 增加机器可读列表格式（仅阻塞时做）。

## 6. third-party 清理

在 `EVEngine/third-party` 仓库：

1. 移除 `.gitmodules` 中的 `zeroerr` 及目录
2. 移除 `CMakeLists.txt` 中 `install(FILES ... zeroerr/zeroerr.hpp ...)`
3. 移除 `add_subdirectory(googletest)` 及相关注释

提交顺序建议：先让 EVEngine 切到 `external/zeroerr` 并去掉对 gtest/third-party zeroerr 的依赖，再合 third-party 清理（或同批说明依赖顺序）。

## 7. 验收标准

- `BUILD_TESTING=ON` 可配置并编过 `unit_test`（不依赖 gtest 库）
- `unit_test` 与 `unit_test --list-test-cases` 正常
- `ctest -R <某个用例名>` 只跑该用例；无 filter / `unit_test_all` 可跑全集
- 现有 Makefile `test/*` 目标仍可用
- 工程中无残留 `#include <gtest/gtest.h>` 或 gtest 链接
- `third-party` 不再包含/安装 zeroerr，也不再构建 googletest

## 8. 风险

| 风险 | 处理 |
|------|------|
| zeroerr 子目录未设 PUBLIC include | 接入时补最小 include 设置，或使用 `ZEROERR_INCLUDE_DIR` |
| list 输出格式影响发现脚本 | 按实际输出解析；必要时上游加稳定格式 |
| fixture 迁到构造函数后行为变化 | 保持 expose / compile / run 顺序与原 `SetUp` 一致 |
| 两仓提交顺序 | EVEngine 先切 submodule，再清 third-party |
