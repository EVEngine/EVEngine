# Profile 与质量门禁

本文说明裁剪 profile、独立检查目标，以及受控质量债务清单的本地和 CI 用法。

## Profile 矩阵

`cmake/module_manifest.cmake` 是模块和第三方依赖的唯一声明源。`scripts/profile_matrix.py`
从该 manifest 解析依赖闭包；它不会维护第二份链接列表。

| profile | 用途 | host |
| --- | --- | --- |
| `minimal` | 最小可运行客户端 | 有 |
| `2d` / `3d` | 对应客户端模块集合 | 有 |
| `web` | WebGPU/Emscripten 客户端集合 | 有，需工具链 |
| `procgen-core-only` | 只验证 procgen 核心边界 | 无 |
| `physics-core-only` | 只验证 physics 核心边界 | 无 |
| `headless` | 无渲染的基础运行集合 | 无 |
| `server` | 服务端/规则侧集合 | 无 |

core/headless profile 会关闭 native host、window、renderer 和 unit-test；
`eve_profile_smoke` 只依赖已选择的模块、独立 public-header translation unit，
以及 capability present/absent probe，不替代完整单元测试。

## 本地命令

先执行不需要编译器的门禁：

```sh
make check/quality
make check/profile-matrix
python3 -m unittest scripts.tests.test_quality_metadata -v
python3 -m unittest scripts.tests.test_architecture_contracts -v
```

## 全阶段架构门禁

`scripts/check_architecture_contracts.py` 是顶部十项架构规则的 source-only
门禁。它先校验 `scripts/architecture_contracts.json` 中每条 contract 的 owner、
issue、证据、测试、expiry 和规则专属字段，再对 pull request 的新增 C/C++ 行做
高信号检查：操作结果不得新增含混 `bool` 或 `lastError` 通道，公共裸指针必须在
Doxygen 中说明 ownership/lifetime，并且新增 Link/System 等契约面必须能匹配
contract catalogue。谓词型 `is/has/can/...` 的 `bool` 仍然允许。

默认只检查 git diff 新增行；旧代码不会因为接入门禁而突然阻断，新增债务预算为
零。CI 通过 checkout 的 base SHA，开发者可用 `ARCHITECTURE_BASE=HEAD make
check/architecture-contracts` 检查当前工作树。`--all` 可用于一次性审计全部
`src/` C/C++，但不替代增量门禁。

十项规则的机器可读 contract 记录在
`scripts/architecture_contracts.json`：

1. API 结果形状与关键返回值；
2. Link 创建、所有权、双向销毁、restore/hot-reload 与 stale；
3. 可变状态唯一 owner 与投影；
4. ECS System 的实体范围、View、读写集、结构变化、事件、服务、阶段；
5. 注入式仿真时间、命名 RNG、确定性与容差；
6. 公共 API 的线程、重入、所有权与生命周期；
7. schema/version/migration/unknown-field 与事务式 restore；
8. capability present/absent 与可观测 fallback；
9. provider/backend 共享 contract 与 failure injection；
10. TODO/fallback/soft-skip/allowlist 的债务元数据与零净增长。

fixture 位于 `scripts/tests/fixtures_architecture_contracts`，测试同时覆盖合法
API、违规 API、缺失 contract 字段和未登记 Link/System。

profile 阶段故意拆开，默认使用 `/tmp/evengine-profile-matrix`，不会触碰普通
`build/<platform>`：

```sh
make profile/configure PROFILE=physics-core-only
make profile/build PROFILE=physics-core-only
make profile/smoke PROFILE=physics-core-only
```

也可以显式指定独立目录、Vulkan SDK 和少量并行度：

```sh
export VULKAN_SDK=/path/to/1.4.357.1/x86_64
make profile/configure PROFILE=minimal \
  PROFILE_BUILD_ROOT=/tmp/evengine-profile-minimal PROFILE_JOBS=2
```

`profile/dry-run` 只打印全部 profile 的 configure/build/smoke 命令：

```sh
make profile/dry-run
```

Web profile 需要 Emscripten。工具链已激活时使用：

```sh
make profile/configure PROFILE=web PROFILE_PLATFORM=webgpu \
  PROFILE_CMAKE_COMMAND="emcmake cmake" \
  PROFILE_BUILD_ROOT=/tmp/evengine-profile-web
```

CI 默认不额外执行 Web profile；workflow dispatch 的 `profile_web=true`，或仓库
变量 `EVENGINE_PROFILE_WEBGPU=true` 时启用。已有 WebGPU 全量 job 与该独立 profile
检查互不替代。

## 质量债务 metadata

`scripts/check_quality_metadata.py` 只扫描受控目录中的四类显式库存：binding gap、
TODO/FIXME/HACK、soft-skip，以及注释中的 fallback。普通参数名不会被当成库存。

每个 `scripts/quality_debt_allowlist.json` entry 必须包含：

```json
{
  "id": "example",
  "kind": "todo",
  "scope": "src/*",
  "owner": "engine-maintainers",
  "issue": "EVENGINE-QUALITY-BASELINE",
  "reason": "reviewed legacy inventory",
  "expiry": "2027-12-31",
  "max_net_growth": 0
}
```

`scripts/quality_debt_baseline.json` 按 entry 记录既有数量。普通门禁要求：

1. 每条发现必须匹配且只能匹配一个 metadata entry；
2. owner、issue、reason、expiry 必须有效，expiry 不能已过期；
3. 当前数量超过基线时，增长不得超过 `max_net_growth`；默认预算为零；
4. 删除债务允许数量下降，但不能通过抬高基线隐藏新增债务。

基线更新是显式维护动作，只能在实际减少或经评审调整范围后执行：

```sh
python3 scripts/check_quality_metadata.py --write-baseline
git diff -- scripts/quality_debt_baseline.json
```

fixture 和自测位于 `scripts/tests/fixtures_quality_metadata` 与
`scripts/tests/test_quality_metadata.py`。CI 的 `quality-contracts` job 运行同一套
本地命令；失败时应修正文档/实现或补充经过评审的 metadata，而不是直接改基线。
