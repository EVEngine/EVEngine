# EVEngine 开发者文档

本目录保存引擎的架构、模块设计、测试策略和实现计划，面向 EVEngine 本身的维护者与贡献者。

> **如果你是游戏开发者而不是引擎开发者**：请从[官网发布页](https://github.com/EVEngine/EVEngine/releases)
> 下载预编译 SDK 直接使用，**不需要编译引擎**；用户文档见[用户指南](../usr/README.md)与
> [模块使用手册](../usr/MODULES.md)。本目录及下文“从源码构建”只对需要修改引擎的人有意义。

## 怎么读这个目录

| 层级 | 含义 | 入口 |
|------|------|------|
| **规范** | 改公共 API / ECS / 持久化前必读；CI 会门禁 | 下文「架构与工程约定」 |
| **产品规划** | 当前版本叙事与收口清单 | `2026-10-08-*` |
| **模块设计** | 引擎内部设计与进度；脚本用法见 `docs/usr/modules/` | 下文「功能设计」 |
| **Schema CURRENT** | 资源格式现行版本；历史 vN 仅供迁移对照 | 下文「资源 Schema」 |
| **阶段性计划** | `superpowers/` 实施稿；完成后应删除或并入活文档，勿当入口 | [`superpowers/`](superpowers/) |

未列入本 README 的文件**不是导航入口**（多为历史切片、导入日记或调研草稿）；新增文档时请同步更新本索引，否则默认视为临时材料。

## 架构与工程约定

- [发布流程](发布流程.md)（`main` / `dev` / `vX.X.X`，Pre-release 发版）
- [0.6 / 0.7 发布路线图](2026-10-08-release-roadmap-0.6-0.7.md)（竖切收口 → 存档/作者工具/保真度）
- [0.6 收口清单](2026-10-08-0.6-closeout-checklist.md)（Must/Should 验收与波次）
- [整体架构](整体架构.md)
- [模块设计与实现进度](模块设计.md)
- [模块编排与裁剪架构](模块编排与裁剪架构.md)（实测依赖分层、协作接缝、按需裁剪；用户侧用法见[按需裁剪模块](../usr/TRIMMING.md)）
- [去中心化冲突热点设计](superpowers/specs/2026-10-10-decentralize-conflict-hotspots.md)（按模块拆频繁修改面，保留组合后的单一依赖契约）
- [模块接口契约与机制选型规范](模块接口契约与机制选型规范.md)（模块对外六个面、ECS/事件/provider/Link 选型判据、API 成本可见性）
- [模块边界审查清单](模块边界审查清单.md)（对单个模块执行上述规范的逐项判据与取证方式）
- [模块边界审查台账（首轮全量）](2026-09-15-模块边界审查台账.md)（194 个模块的逐条结论与实测数据）
- [重构代码质量与系统完整性规范](重构代码质量与系统完整性规范.md)
- [领域短根继承与跨域组合架构](领域短根继承与跨域组合架构.md)
- [Result 检查与不得丢弃返回值规范](Result检查与不得丢弃返回值规范.md)
- [架构巩固清单](2026-08-26-architecture-consolidation-checklist.md)（`ARCHITECTURE_BASE=HEAD make check/architecture-contracts`）
- [物理系统分层与后端契约](物理系统分层与后端契约.md)
- [场景对象模型使用指南](场景对象模型使用指南.md)（Node / SceneEntity / Link cookbook）
- [EveScript 语言设计](EveScript语言设计.md)（统一 `.nut` 前端、脚本模块、类型、持久变量与异步 lowering）
- [Squirrel 绑定风格](Squirrel绑定风格.md)（`BindContext` / `projectResult` / Contracts 刮取约定；不拆 `editing_script`）
- [依赖项](依赖项.md)
- [命令行设计](命令行设计.md)
- [测试覆盖](测试覆盖.md)
- [AI 与 MCP 支持](AI与MCP支持.md)
- [AI 知识补偿](AI知识补偿.md)（推理时知识包、Binding Contract 目录、少写脚本）
- [AI 场景导演](AI场景导演.md)（剧情 → 自动搭台 → 质检迭代 → 交付）
- [游戏开发系统与工具差距分析](游戏开发系统与工具差距分析.md)

## 功能设计

引擎设计文档；脚本侧手册见对应 [`docs/usr/modules/`](../usr/modules/) 页面。

- [2D 渲染 API](2D渲染API设计.md)
- [3D 渲染管线](3D渲染管线.md)（初始化 / VKBuilder / 异步帧、buffers、数据流、函数与 shader）
- [体积光模块](体积光模块设计.md)
- [抗锯齿模块](抗锯齿模块设计.md)
- [风格化渲染模块](风格化渲染模块设计.md)（对象模型摘要见 [stylize-architecture.md](stylize-architecture.md)）
- [实时雾系统](realtime-fog-system.md)（`graphics_fog`）
- [环境光遮蔽模块](环境光遮蔽模块设计.md)
- [GUI 框架](GUI框架设计.md)
- [Tilemap](Tilemap设计.md)
- [寻路系统](寻路系统设计.md)
- [动态视野系统](动态视野系统设计.md)
- [程序化生成模块](程序化生成模块设计.md)
- [贴花渲染模块](贴花渲染模块设计.md) → [usr/decal](../usr/modules/decal.md)
- [群体行为与流场](群体行为与流场模块设计.md) → [usr/crowd](../usr/modules/crowd.md)
- [RPG 系统](RPG系统设计.md)
- [RPG 任务系统](RPG任务系统设计.md)
- [背包系统](背包系统设计.md)
- [建筑放置系统](建筑放置系统设计.md)
- [空间索引模块](空间索引模块设计.md)
- [dnut 序列语言与解释器](dnut序列语言与解释器.md)

## 资源 Schema（CURRENT）

历史版本文件（如 `canonical-material-v3`…`v14`）仅描述迁移输入，**现行写入版本**如下：

- [Canonical material v15](canonical-material-v15.md)
- [Canonical mesh v3](canonical-mesh-v3.md)
- [Canonical terrain material v3](canonical-terrain-material-v3.md)
- [Canonical vegetation scene v1](canonical-vegetation-scene-v1.md)
- [Canonical vegetation conversion preset v1](canonical-vegetation-conversion-preset-v1.md)
- [Canonical volume texture v1](canonical-volume-texture-v1.md)

## 实施记录

阶段性设计稿与实施计划位于 [`superpowers/`](superpowers/)。文档使用的图示位于 [`img/`](img/)。

规则：功能合入且活文档（本 README 索引或 `docs/usr/`）已覆盖后，对应 `superpowers/plans|specs`
应删除或把独特内容并入活文档，避免再堆「未实施」假状态。

## 构建与文档

- 生成 C++ API 文档（Doxygen）：`make docs`（或 `cmake --build <build-dir> --target docs`），
  产物位于 `docs/api/html/`（已加入 `.gitignore`）。需要先安装 doxygen：
  Ubuntu/WSL `sudo apt install doxygen`、macOS `brew install doxygen`、Windows `choco install doxygen`。
  文档配置见 [`docs/Doxyfile.in`](../Doxyfile.in) 与 [`docs/CMakeLists.txt`](../CMakeLists.txt)，入口 `src/`、`Readme.md` 与 `docs/usr/`。
  在线版由 GitHub Pages 持续发布：<https://evengine.github.io/EVEngine/>。

- 运行时断言：引擎统一通过 zeroerr 的 `ASSERT` 系列宏做函数参数校验与内部不变量检查，
  入口见 [`src/engine/common/Assert.h`](../../src/engine/common/Assert.h)（`EV_PARAM_CHECK` / `EV_ASSERT`）。
  - Debug 构建默认启用；
  - Release 等非 Debug 构建默认通过 `ZEROERR_NO_ASSERT` 编译剔除，零运行时开销；
  - 需要手动开启时：`make build/linux CMAKE_EXTRA_ARGS=-DEVENGINE_ENABLE_ASSERTS=ON`
    （或 CMake 配置时加 `-DEVENGINE_ENABLE_ASSERTS=ON`）。

如果你是使用 EVEngine 制作游戏，而不是修改引擎，请从[用户指南](../usr/README.md)开始。
