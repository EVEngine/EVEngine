# Third-party patch 应用修复记录

日期：2026-08-26

## 根因

`third-party-squirrel-ssq-export.patch` 原来把唯一 hunk 放在聚合仓库的
`CMakeLists.txt`，并以 pinned commit `761fc836ebcc37820c779e0c8ee30a975bd0d148`
的 blob `a9711f3f...` 为前置上下文。该 blob 与当前 pinned `HEAD:CMakeLists.txt`
完全一致，因此不是 pin 漂移，也不是 Vulkan SDK 缺失。

构建代理和 profile 获取逻辑会在同一个已下载 checkout 中产生未提交修改；当前
聚合 `CMakeLists.txt` 已增加第三方分组逻辑，且其中已经有语义相近但条件不同的
导出代码。于是原补丁的正向上下文和完整反向上下文都不匹配，包装器把它报告为
“patch does not apply”。同时，`mpg123` 补丁的文件路径包含 `medialoader/`，但
调用时却把聚合仓库作为 `PATCH_DIR`，这是另一个真实的 submodule 根路径错误。

本地可见的 `HEAD`、`origin/main` 和其他 refs 均没有
`SQUIRREL_API=__declspec(dllexport)` 的 target 定义；上游只已有
SimpleSquirrel 共享 target 的 `SSQ_EXPORTS/SSQ_DLL` 定义。因此不能删除修复，
也没有证据支持更新 third-party pin。

## 修复

- 导出补丁现在修改 pinned 版本中稳定的
  `squirrel/squirrel/CMakeLists.txt`、`squirrel/sqstdlib/CMakeLists.txt` 和
  `simplesquirrel/CMakeLists.txt`，不再依赖易变的聚合文件上下文。
- `mpg123-signal-handler.patch` 改为以 `third-party/medialoader` 子模块为根，
  CMake 调用同步使用该根；`medialoader-smooth-normals.patch` 已采用同样规则。
- `patch_third_party.cmake` 严格匹配空白，先做反向检查实现幂等，再做正向检查和
  实际应用。Git checkout、路径错误、目标漂移和实际应用失败都会返回 fatal error，
  并保留 `git apply` 的诊断；不使用静默 fallback。
- 新增 `scripts/tests/test_patch_third_party.py`，覆盖干净 checkout、带无关污染的
  checkout、重复应用、两个 medialoader submodule patch 的根路径，以及真实目标漂移
  必须失败。测试只从本地已下载源码建立临时 clone，不访问网络、不修改共享 checkout。

## 验证命令

单独运行 patch 回归测试：

```sh
python3 scripts/tests/test_patch_third_party.py
```

目标 patch 的手工幂等验证：

```sh
cmake -DPATCH="$PWD/cmake/patches/third-party-squirrel-ssq-export.patch" \
      -DPATCH_DIR=/tmp/clean-third-party \
      -P "$PWD/cmake/patch_third_party.cmake"
```

完整引擎 configure 验证使用 Vulkan SDK：

```sh
export VULKAN_SDK=/home/sunxiaofan/Downloads/vulkansdk-linux-x86_64-1.4.357.1/1.4.357.1/x86_64
export PATH="$VULKAN_SDK/bin:$PATH"
cmake -S . -B /tmp/evengine-configure-1 -G Ninja \
  -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 \
  -DVULKAN_SDK="$VULKAN_SDK"
cmake -S . -B /tmp/evengine-configure-1 -G Ninja \
  -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 \
  -DVULKAN_SDK="$VULKAN_SDK"
```

两次 configure 必须成功，第二次不得重新获取或重复应用 patch；配置目录必须
使用 `/tmp`，不能使用共享 `build/linux-debug`。
