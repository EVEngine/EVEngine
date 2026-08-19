# 分支模型与发布流水线设计

日期：2026-08-19
状态：已实施

## 背景

当前仓库只有 `main` 一条活跃线：日常 CI（`.github/workflows/ci.yml`）在 `main` 的 push/PR 上跑；
GitHub Pre-release 触发 `.github/workflows/sdk-release.yml` 打五平台 SDK，成功后把同一个
Release 升成正式版。版本号写在根目录 `CMakeLists.txt` 的
`EVENGINE_MAJOR_VERSION` / `MINOR` / `PATCH` / `DEV_VERSION`（现为 `0.1.0-dev`）。
没有隔离的开发分支，也没有统一的版本/分支脚本。

目标：`main` 始终是「克隆即可编译」的最新正式版；日常开发在 `dev`；一次发布用
`vX.X.X` 分支隔离。发布由 GitHub Pre-release 启动，`scripts/release.py` 负责所有
git/gh 变更，CI 只编排严测与 SDK。

## 非目标

- 不在本设计里改引擎代码或 SDK 打包内容（`make sdk/*`、`test-sdk.sh`、`zip-sdk.py` 保持原样）。
- 不自动按 semver 递增下一正式号；下一正式号以新的 Pre-release tag 为准。
- 不把 `gh` 做成可选依赖：不在 PATH 就报错退出。
- 不引入 GitHub App / 额外 bot 账号 / 管理员 PAT；使用 `GITHUB_TOKEN`（`contents: write` + `pull-requests: write` + `checks: write`）。
- 不在发布收尾时用推送再触发新的 workflow（依赖默认 token 不连锁触发，避免死循环）。
- **不直推 `main` / `dev`**：`finish` 只开 PR，由人点 Merge。`GITHUB_TOKEN` 开出的 PR 不会再触发 `pull_request` workflow，因此 `finish` 用 Checks API 写同名检查 `main-gate`，否则必过检查会一直停在 expected。

## 分支职责

| 分支 | 职责 | 版本号 | CI |
|---|---|---|---|
| `main` | 最新正式版；GitHub 默认分支；clone 即可用 | 正式号，如 `0.1.0` | 不跑日常构建。文档白名单 PR 走 `sync-docs.yml` |
| `dev` | 日常开发与功能 PR 的目标分支 | 上一正式号 + `-dev`，如 `0.1.0-dev` | push/PR 跑现有 Debug CI |
| `vX.X.X` | 某次发布的隔离线，由 `release.py start` 创建 | 先正式号，`finish` 后再写回同号 `-dev` | 仅由 `release.yml` 编排的严测 + SDK |
| `rebase/vX.X.X` | rebase `dev` 冲突时的临时分支 | 与 `vX.X.X` 收尾后一致 | 合入 `dev` 时走日常 CI |

发布期间 `dev` 继续接受合并。`vX.X.X` 完成后，把 `dev` 上多出来的提交 rebase 到带 `-dev` 回写的发布分支上。

### `main` 上的文档热修

GitHub 展示默认分支上的 README/文档。允许对 `main` 开**文档白名单** PR，不必走完整发布：

- 允许路径：`README.md`、`Readme.md`、`Readme.en.md`、`docs/**`、`Doxyfile`
- 混入白名单外文件则检查失败，改走 `dev`
- 合并后 `sync-docs.yml` 调用 `release.py sync-docs`：把 `main` 上尚未在 `dev` 的文档提交 cherry-pick 到 `dev`；冲突则开 PR，不强制改写 `dev`
- 发布更新 `main` 时：能快进到「去 `-dev`」的正式版 commit 就快进；否则 **merge** 该 commit，保留 `main` 上已有的文档提交

## 一次发布的时间线

维护者在 GitHub 上对当时的 `dev` HEAD 创建一个 Pre-release，tag 形如 `v0.1.0`。
此时树上仍是 `0.1.0-dev`。之后全部由 CI 完成：

```text
dev  ──●──●──●──────────●──●──────────►  日常开发，Debug CI
         \              ↑ rebase 成功，或 rebase/vX.X.X PR
          \             │
v0.1.0     ●──(去-dev)──●──严测──SDK──●──(+ -dev)
           ↑            ↑ 正式版 commit   ↑ 只留在 v0.1.0 / dev
           Pre-release  │
                        └── promote/v0.1.0 PR → main（人点 Merge）
```

1. `release.py start`：从 Pre-release 的 tag commit 建 `v0.1.0`，按 tag 写入正式版并提交，**把同名 tag 移到这次 commit**（`git checkout v0.1.0` 必须得到正式号）。
2. 在 `v0.1.0` 上跑严测（见「CI」），再跑现有五平台 SDK 打包与可用性测试，把 zip 挂到该 Release。
3. `release.py finish`：升正式 Release；推送 `promote/v0.1.0`（指向去 `-dev` 的正式版 commit）并开 PR 到 `main`；在 `v0.1.0` 上提交 `0.1.0-dev`；尝试 rebase `dev`，无论成功或冲突都开 `rebase/v0.1.0` → `dev` 的 PR（成功时若已无新提交则跳过）。**不** `git push origin main`，**不** force-push `dev`。

任一层严测或 SDK 失败：不执行 `finish`，不升正式版，不动 `main`/`dev`。`vX.X.X` 与已移动的 tag 保留，便于修复后对同一 Pre-release 重跑。

## `scripts/release.py`

所有 git/gh 变更的唯一入口。启动即检查 `gh`（`shutil.which("gh")`），缺失则非零退出并写明安装来源（GitHub CLI）。
通过 `GH_TOKEN` 或 `GITHUB_TOKEN` 调用 `gh`。`--dry-run` 只打印将执行的命令。

版本号的**来源**仍是根目录 `CMakeLists.txt` 这四行：

```cmake
set(EVENGINE_MAJOR_VERSION "...")
set(EVENGINE_MINOR_VERSION "...")
set(EVENGINE_PATCH_VERSION "...")
set(EVENGINE_DEV_VERSION "...")   # 正式版为空串；开发版为 "-dev"
```

`EVENGINE_VERSION` 由这四行拼接，不单独手改。

`start` 除改写这四行外，还会把官方 `MAJOR.MINOR.PATCH` 同步到对外暴露版本的
**required 触点**（Android `build.gradle.kts` 的 `versionName` / 推导 `versionCode`、
MCP `serverInfo.version`、三个 `root.nut`、`docs/CMakeLists.txt`），一并提交进
`vX.X.X`；`check-versions` 子命令校验这些触点与 CMake 是否一致，挂在日常 CI 的
layering 作业（无网络、无工具链依赖）。独立发布的工具版本只做 WARN。

### 版本规则

- Pre-release 的 tag 是本次 **MAJOR.MINOR.PATCH 的唯一来源**，必须匹配 `^v([0-9]+)\.([0-9]+)\.([0-9]+)$`。
- `start` 把这三个数字写入 CMake，并令 `EVENGINE_DEV_VERSION` 为空。
- 允许 tag 比树上现有数字新（例如树上 `0.1.0-dev`、tag `v0.1.1`），这是升高下一正式号的方式。
- 禁止降级：tag 的 `(major, minor, patch)` 字典序小于树上现有三元组则失败。
- `finish` 的 `-dev` 回写使用**刚刚发布的同一组数字**（`0.1.0` → `0.1.0-dev`），不自动 +1。
- 提交信息固定：`release: X.X.X` 与 `release: X.X.X-dev`。

### 子命令

**`start`**（发布 CI 在严测之前调用）

1. `gh release view` 读取触发本次运行的 Pre-release（CI 传入 `--tag`；本地则取最新的 `prerelease: true` 且未正式发布的 release）。
2. 校验 tag、禁止降级。
3. 从该 tag 当前指向的 commit 创建并检出 `vX.X.X`（已存在则检出，不重建）。
4. 写入正式版并提交；若工作区已是该正式号则不再提交（幂等）。
5. 将同名 tag 移到这次正式版 commit，并 `git push --force` 该 tag 与分支。

**`finish`**（SDK 作业全部成功之后）

1. `gh release edit <tag> --prerelease=false`。
2. 若正式版 commit 尚不在 `main`：建并推送 `promote/vX.X.X`（钉在去 `-dev` 的 commit 上，避免随后的 `-dev` 回写漏进 PR），开 PR 到 `main`，并用 Checks API 给该 SHA 打 `main-gate` = success。已有同 head 的 open PR 则复用。
3. 在 `vX.X.X` 上把 `EVENGINE_DEV_VERSION` 设回 `-dev` 并提交（已是该开发号则跳过）。
4. `git rebase` 将 `dev` 接到该 `-dev` commit 上。成功且有新提交：推送 `rebase/vX.X.X`，开 PR 到 `dev`。已与 `origin/dev` 一致则跳过。
5. rebase 冲突：中止 rebase，推送 `rebase/vX.X.X`（发布尖端 + 说明），开 PR 到 `dev`。PR 正文写明冲突与本地续做命令。
6. **不得** `git push origin main`，也不得 force-push 日常开发分支。

**`sync-docs`**（`main` 文档 PR 合并后）

1. 确认 `main` 相对 `dev` 多出来的提交只碰白名单路径，否则失败。
2. cherry-pick 这些提交到 `dev`；成功则推送 `dev`（普通推送，不需要 lease，除非非快进）。
3. 冲突则开 PR，不改写 `dev`。

**`check-versions`**（无网络、无 `gh` 依赖）

1. 读取 `CMakeLists.txt` 的版本三元组。
2. 逐个校验 required 触点（失配即失败），并报告 tracked 工具的偏差（仅 WARN）。
3. `start` 同步过的发布分支上必然通过；日常 dev 若有失配会在 CI 失败，提示先跑
   `start` 或手工同步。

脚本不编译、不跑测试、不打 SDK zip。

## CI

### `ci.yml`

- 日常触发改为 `dev` 的 `push` / `pull_request`（不再对 `main` 的日常推送跑构建）。
- 增加 `workflow_call`，入参 `build_type`：`debug`（默认）| `release` | `both`。
- `debug`：现有作业（分层检查 + Windows/Linux/macOS Debug 构建与单测 + Android/iOS Debug 构建 + WebGPU 构建验证）。
- `release`：桌面三端 Release 构建；对存在 `make test/win32`、`test/linux`、`test/macosx` 的平台再跑 Release 单测。Android / iOS / WebGPU 做 Release 构建（及现有产物检查），不新增不存在的测试目标。
- `both`：先 `debug` 全套，再 `release` 全套。
- 周常 cron 与 `workflow_dispatch` 保留，目标改为 `dev`。
- layering 作业增加 `scripts/release.py check-versions`（版本一致性，纯文件检查）。

### `sdk-release.yml`

- 去掉 `on.release.prereleased` 触发，改为 `workflow_call`（可保留 `workflow_dispatch` + tag 以便单独重打 SDK）。
- 五平台 SDK 作业与可用性测试、上传 artifact 保持不变。
- `test-sdk.sh` 接收期望版本（tag 去 `v`），精确校验 `share/eve/VERSION` 与 `eve -v`；
  publish 作业在上传前对每个 zip 的 `share/eve/VERSION` 与 tag 交叉校验。
- **不再** `gh release edit --prerelease=false`；升正式版只在 `release.py finish`。

### `release.yml`（新建）

触发：`release: types: [prereleased]`；以及 `workflow_dispatch`（必填 tag，用于重跑）。

```text
start (release.py start --tag)
  → strict-tests (ci.yml workflow_call, build_type=both)
    → sdk (sdk-release.yml workflow_call)
      → finish (release.py finish --tag)
```

权限：`contents: write`、`pull-requests: write`、`checks: write`。`start` 之后的作业必须 checkout 已推送的 `vX.X.X`（正式版 commit），不能再用 Pre-release 最初指向的 `-dev` commit。`finish` 只开 PR，由人点 Merge。

### `main-gate.yml`（新建）

- `pull_request` 目标为 `main`。
- 调用 `release.py check-main-pr`：文档白名单或 `promote/vX.X.X` 才通过。
- 检查名 `main-gate`。`GITHUB_TOKEN` 开的 promote PR 不会跑本 workflow，由 `finish` 用 Checks API 补打同名检查。

### `sync-docs.yml`（新建）

- `main` 的 `push`，且路径落在文档白名单。
- 跑 `release.py sync-docs`。
- 若 push 混有非文档文件（例如有人绕过保护直接推了代码），作业失败并注释说明应走 `dev`。

## 失败与重跑

| 阶段 | 失败时留下什么 | 怎么继续 |
|---|---|---|
| `start` | 可能已有 `vX.X.X` 或已挪 tag | 幂等重跑：已是正式号则不再改文件，只保证 tag 指对 commit |
| 严测 / SDK | 正式版 commit 与 tag 在，Release 仍为 pre | 在 `vX.X.X` 或 `dev` 修复后，对同一 tag 重跑 `release.yml` |
| `finish` 升正式版 / 开 PR | SDK zip 可能已挂上；可能已开 promote / rebase PR | `workflow_dispatch` 同一 tag 重跑；已正式的 release 与已开 PR 幂等 |
| rebase `dev` 冲突 | 不改写 `dev` | 自动开 `rebase/vX.X.X` PR，人修完再合 |

`start` 在以下情况立即失败、不建分支：tag 格式非法、对应 Release 不存在、版本降级、`gh` 缺失、工作区脏且不在 CI。
Release 已经是正式版时：若 `vX.X.X` 分支、CMake 正式号与 tag 已指向同一正式版 commit，则 `start` 视为成功（整条 `release.yml` 重跑时的幂等）；若树与已发布 tag 不一致，则失败，避免改写已发布版本。

## 测试

`scripts/tests/test_release.py` 不访问真 GitHub。注入假的 `gh` / `git`（或对纯函数单测）：

- 解析合法/非法 tag
- 写入与读回 `EVENGINE_*_VERSION`
- 禁止降级、允许升高
- `start` 在已是正式号时不再提交
- rebase 成功或冲突都走「开 PR」路径，不调用 force-push 日常开发分支
- `main_pr_allowed`：`promote/vX.X.X` 放行引擎文件；文档白名单放行；`dev` 合引擎进 `main` 拒绝
- `finish` 开 `promote/` → `main` 与 `rebase/` → 日常开发分支，不 `git push origin main`
- 文档白名单判定

这条 Python 测试挂在 `ci.yml` 的 layering 作业之后（或同 job），不依赖 Vulkan / 显示器。

## 一次性仓库设置

实现落地时由维护者执行（脚本可提供打印命令，不在 CI 里擅自改默认分支）：

1. 从当前 `main` 创建并推送 `dev`（`git branch dev main && git push -u origin dev`）。
2. GitHub 默认分支保持 `main`。
3. 建议保护规则（人工在 GitHub 设置，不由脚本静默修改）：`main` 必须走 PR，必过检查 `main-gate`，禁止 force-push / 删除，**不加 Bypass**。日常开发分支可要求 PR，但不要要求 status check（否则 bot 开的 rebase PR 可能一直转圈）。

## 文档改动（实现阶段）

- 本 spec；`docs/dev/` 增加一页面向维护者的发布说明（如何建 Pre-release、失败如何重跑、如何改 `main` 文档）。
- `ci.yml` / `sdk-release.yml` 文件头注释与触发说明。
- 用户指南与根 `Readme.md`：克隆默认是正式版 `main`；参与开发请基于 `dev`。

## 架构选择

采用「`release.py` 管所有 git/gh 变更，CI YAML 只编排测试」：版本、分支、tag、PR 逻辑可在本地 `--dry-run` 复查，避免散落在多份 bash 里。不用 `workflow_run` 串联（状态难查），也不把建分支写进 YAML。
