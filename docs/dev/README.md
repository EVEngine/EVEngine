# EVEngine 开发者文档

本目录保存引擎的架构、模块设计、测试策略和实现计划，面向 EVEngine 本身的维护者与贡献者。

## 架构与工程约定

- [整体架构](整体架构.md)
- [模块设计与实现进度](模块设计.md)
- [依赖项](依赖项.md)
- [命令行设计](命令行设计.md)
- [测试覆盖](测试覆盖.md)
- [AI 与 MCP 支持](AI与MCP支持.md)
- [AI 场景导演](AI场景导演.md)（剧情 → 自动搭台 → 质检迭代 → 交付）
- [游戏开发系统与工具差距分析](游戏开发系统与工具差距分析.md)

## 功能设计

- [2D 渲染 API](2D渲染API设计.md)
- [3D 渲染管线](3D渲染管线.md)（初始化 / VKBuilder / 异步帧、buffers、数据流、函数与 shader）
- [体积光模块](体积光模块设计.md)
- [抗锯齿模块](抗锯齿模块设计.md)
- [风格化渲染模块](风格化渲染模块设计.md)
- [GUI 框架](GUI框架设计.md)
- [Tilemap](Tilemap设计.md)
- [寻路系统](寻路系统设计.md)
- [动态视野系统](动态视野系统设计.md)
- [程序化生成模块](程序化生成模块设计.md)
- [体积光模块](体积光模块设计.md)
- [环境光遮蔽模块](环境光遮蔽模块设计.md)
- [风格化渲染模块](风格化渲染模块设计.md)
- [RPG 系统](RPG系统设计.md)
- [背包系统](背包系统设计.md)
- [建筑放置系统](建筑放置系统设计.md)
- [空间索引模块](空间索引模块设计.md)
- [游戏模型](游戏模型设计.md)
- [界面设计](界面设计.md)
- [修改思路](修改思路.md)

## 实施记录

阶段性设计稿与实施计划位于 [`superpowers/`](superpowers/)。文档使用的图示位于 [`img/`](img/)。

## 构建与文档

- 生成 C++ API 文档（Doxygen）：`make docs`（或 `cmake --build <build-dir> --target docs`），
  产物位于 `docs/api/html/`（已加入 `.gitignore`）。需要先安装 doxygen：
  Ubuntu/WSL `sudo apt install doxygen`、macOS `brew install doxygen`、Windows `choco install doxygen`。
  文档配置见根目录 [`Doxyfile`](../../Doxyfile)，入口 `src/` 与 `Readme.md`。

- 运行时断言：引擎统一通过 zeroerr 的 `ASSERT` 系列宏做函数参数校验与内部不变量检查，
  入口见 [`src/engine/common/Assert.h`](../../src/engine/common/Assert.h)（`EV_PARAM_CHECK` / `EV_ASSERT`）。
  - Debug 构建默认启用；
  - Release 等非 Debug 构建默认通过 `ZEROERR_NO_ASSERT` 编译剔除，零运行时开销；
  - 需要手动开启时：`make build/linux CMAKE_EXTRA_ARGS=-DEVENGINE_ENABLE_ASSERTS=ON`
    （或 CMake 配置时加 `-DEVENGINE_ENABLE_ASSERTS=ON`）。

如果你是使用 EVEngine 制作游戏，而不是修改引擎，请从[用户指南](../usr/README.md)开始。
