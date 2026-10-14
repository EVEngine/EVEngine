# 架构契约门禁：代码为真源重构方案

> 状态：设计提案（未实施）  
> 日期：2026-10-14  
> 前置：`docs/dev/模块接口契约与机制选型规范.md`、`docs/dev/重构代码质量与系统完整性规范.md`、  
> `docs/dev/superpowers/specs/2026-10-10-decentralize-conflict-hotspots.md`、  
> `scripts/check_architecture_contracts.py`  
> 动机：当前 catalogue 要求人工维护与源码平行的 JSON，门禁却几乎不验证语义一致性；  
> G-2 / coverage 把「改代码还要改 JSON」当成正确性，制造第二真相源与无谓维护成本。

---

## 1. 问题陈述

### 1.1 门禁实际在查什么（现状）

`make check/architecture-contracts`（`scripts/check_architecture_contracts.py`）做两件事：

1. **Catalogue 格式校验**  
   每条 entry 的信封字段齐全、`evidence`/`tests` 路径存在、`expiry` 未过期、  
   每种 `rule` 至少一条；`link.symbols` / `ecs-system.systems` 在 scope 内做**符号字面存在**扫描。
2. **改动行正则 lint**  
   api-shape（含糊 `bool` / `lastError`）、裸指针缺 Doxygen、  
   新 `*Link`/`*System` 等是否已有 JSON 条目（coverage）、  
   `cap::` 用法是否登记在 `provides`/`requires`（G-2）、  
   `hot_path` 文件禁止 lookup 原语（G-3）、运行时禁止 `getModInst`（G-4）、昂贵 API 要 `@cost`（G-5）。

它**不编译、不跑测试、不证明** ownership / destroy 顺序 / read-write set / migration  
与实现一致。长文字段只检查「键在不在」。

### 1.2 为何不合理

| 现象 | 后果 |
|------|------|
| `provides`/`requires`/`systems`/`symbols`/`binds` 可从源码扫描得到，却手写进 JSON | 双写；漏登才挂 CI，语义对错不挂 |
| 99 条 entry 共用同一套信封样板（expiry/issue/evidence） | 维护税集中在格式，不在设计 |
| 约 12 条 `module-interface` 无 provides/requires/hot_path | 纯登记壳，G-2/G-3 从不读 |
| 合成 `architecture_contracts.json` 与分片 100% 重复且入库 | 冲突热点（见 2026-10-10 去中心化规格） |
| 规范已写「触碰补齐、不一次填满」 | catalogue 仍膨胀到近百条，意图被淹没 |

核心错位：**门禁把「登记表」当成契约，却把「可机械提取的事实」交给人抄。**

### 1.3 非目标（本方案明确不做）

- 不上完整 C++ AST / clang 插件作为 P0（可后续增强；P0 用现有正则 + 文件 scope）。
- 不取消 api-shape / G-4 / G-5 等**纯代码**坏味道检查。
- 不要求一次性删光所有历史 JSON 文案（迁移期可归档）。
- 不把 ownership 生命周期证明改成「静态完全可判定」（那仍需测试 + 短意图声明）。

---

## 2. 目标架构

### 2.1 原则

1. **代码是能力面 / 符号面 / 绑定面的唯一真源。**  
   CI 从源码扫描得到 facts；禁止再要求人手维护与 facts 平行的清单。
2. **意图（intent）与事实（facts）分离。**  
   只有静态推不出、且门禁要强制的意图，才写短 overlay。
3. **登记表不得成为第二真相源。**  
   Overlay 不得重复罗列 `cap::` 类型名、`*System`/`*Link` 符号、脚本方法表。
4. **触碰才补意图，不预填空壳。**  
   无 cap / Link / System / 热路径声明的模块，零文件。

### 2.2 两层模型

```
┌──────────────────────────────────────────────────────────┐
│  Gate (CI)                                               │
│  A. code facts scan  →  G-2/coverage 的「有没有表面」     │
│  B. intent overlay   →  hot_path / ownership / trim …    │
│  C. shape lints      →  api-shape / G-4 / G-5（不变）     │
└──────────────────────────▲───────────────────────────────┘
                           │
          ┌────────────────┴────────────────┐
          │                                 │
   scanned from C++/headers          optional intent files
   cap::provide/query                (YAML or Doxygen tags)
   class FooLink / FooSystem         only non-inferable fields
   SSQ_REG / expose                  default: absent = no extra
```

### 2.3 仍允许的「手写」内容（白名单）

| Intent 字段 | 为何不能只靠扫描 | 形态 |
|-------------|------------------|------|
| `hot_path` 文件 glob | 性能**意图**：哪些文件禁止 lookup | 模块 overlay 或文件头 `@hot_path` |
| Link 生命周期：`destroy_order` / `stale` / `restore` | 设计约束，声明句表达不了 | 短 overlay，**仅当存在该 Link**；或头文件 `@link_contract` 块 |
| `trim.absent_profile` / 缺席回退要点 | 裁剪后可观察行为 | 一句；可与 profile 实测门禁（G-7，后续）衔接 |
| 持久化 `unknown_fields` / migration 窗口 | 产品策略 | 仅真正有 schema 的格式 |
| 确定性等级 / 容差 | 产品策略 | 仅 replay/网络/backend 路径 |

**禁止再手写：** `provides`、`requires`、`systems`、`symbols`、`binds` 方法列表、  
空的 `emits`/`observes`/`protocol`、复制粘贴的 owner/issue/expiry 样板（系统填默认）。

### 2.4 Overlay 建议形态（二选一，实施时定）

**推荐 A — 旁路 YAML（少、可 grep）：**

```yaml
# src/modules/keyboard/architecture.intent.yaml
schema: eve.architecture-intent/v1
hot_path:
  - src/modules/keyboard/sdl/EventSink.cpp
trim:
  absent_profile: "profiles without keyboard"
links: []   # 仅当本模块有手写生命周期补充时
```

**备选 B — 头文件 Doxygen 标签**（与现有 `@ownership`/`@cost` 同族）：

```cpp
/** @hot_path */
/** @link_contract destroy=... stale=... restore=... */
```

P0 用 A（脚本简单、不依赖注释解析完整性）；P1 可把 B 作为同权输入。

---

## 3. 门禁行为重定义

### 3.1 保留（几乎不动）

| ID | 行为 |
|----|------|
| api-shape | 改动行：含糊操作 `bool`、`lastError`、error 指针通道 |
| api-lifetime | 公开裸指针缺 `@ownership`/`@lifetime` |
| G-4 | 运行时模块新增 `getModInst` / `ModuleManager::getInstance` |
| G-5 | 昂贵公开 API 缺 `@cost` |
| debt-metadata | 继续走 `check_quality_metadata.py`（不混进本 catalogue） |

### 3.2 改写

| 现行为 | 目标行为 |
|--------|----------|
| G-2：`cap::` 必须出现在 JSON `provides`/`requires` | **扫描 scope 内已有 `cap::` 事实即可**；新增 `cap::` 不要求 JSON。可选：若存在 intent 文件，校验其未**否定**该能力（一般不需要） |
| coverage：新 `*Link`/`*System` 必须有 JSON 条目 | **扫描到声明即通过「存在」检查**；仅当规则要求生命周期意图时，才要求同模块 intent 含对应 `links[]`/`systems[]` **补充字段**（不是再抄类名） |
| catalogue 校验 99 条大 JSON | **校验 intent schema + 可选生成的 facts 报告**；删除「每种 rule 必须有一条 policy 信封塞满散文」的膨胀激励 |
| `link.symbols` / `systems` 与源码交叉 | 改为 **facts 扫描输出**；intent 只引用已扫描到的名字做补充，不得登记扫描不到的符号 |

### 3.3 删除或降级

| 项 | 处理 |
|----|------|
| 合成 `scripts/architecture_contracts.json` | 停止入库；需要时由脚本生成 artifact，不进 git |
| 无 P/R/hot_path 的 module-interface 空壳 | 删除，不迁移 |
| 全局 `src/**` 政策长文（api-shape-policy 等） | 收敛进 checker 内建规则或单页 docs；不再每条复制 evidence/tests |
| `binds` 方法清单 | 删除；脚本面交给 `check_bindings.py` |
| 统一 `expiry: 2027-12-31` | 删除字段或仅对 debt/intent 例外使用 |

### 3.4 新输出（给人和 CI）

```
scripts/architecture_facts.py          # 扫描 → stdout / JSON artifact
  provides/requires by file
  Link / System declarations
  optional: SSQ_REG script classes

make check/architecture-contracts      # 编排：
  1) facts scan (always)
  2) shape lints on changed lines (always)
  3) intent overlay validate (only files that exist)
  4) hot_path primitive ban (from intent ∪ 默认空)
```

人工审查看 **facts diff**（PR 上能力面/符号面变化），而不是看 JSON 是否抄全。

---

## 4. 迁移阶段

### S0 — 文档与冻结（本规格）

- [ ] 合并本规格；AGENTS.md / 模块接口规范 §8 增加「代码为真源」指针。
- [ ] 冻结：**禁止新增**空壳 `module-interface`；禁止再扩大合成 JSON 手写面。
- [ ] 新 PR 若只为「补登记过 G-2」而加 JSON，改为等 S1 或临时 waive 并链本 issue。

### S1 — Facts 扫描器 + 倒转 G-2/coverage（行为翻转，最大杠杆）

- [ ] 新增 `scripts/architecture_facts.py`（复用 `check_architecture_contracts.py` 内已有正则）。
- [ ] G-2：以 facts 为准；**不再**因「JSON 未登记」失败。
- [ ] coverage：新 Link/System **不再**要求 JSON 条目存在；改为 optional intent 生命周期检查（默认关闭或仅 warn，见 S2）。
- [ ] 单测：fixture 源码 → facts；改动行 cap:: 不再需要平行 JSON。
- [ ] CI：`ARCHITECTURE_BASE` 语义不变（仍只 lint 改动行形状）。

**验收：** 删除任意一条仅服务 G-2 的 `provides`/`requires` 后，在无新 cap 的 PR 上门禁仍绿；  
故意在运行时模块新增未声明生命周期的…（S2 再收紧）。

### S2 — Intent overlay 最小集

- [ ] 引入 `architecture.intent.yaml` schema（或选定 Doxygen 标签）。
- [ ] 从现有 JSON **只迁移**非空 `hot_path` 与确有内容的 link 生命周期段落（机械提取脚本一次）。
- [ ] 启用：若模块声明了 `hot_path`，G-3 照旧；未声明则不假装有热路径围栏。
- [ ] Link：若扫描到 `*Link` 且模块选择 opt-in `require_link_intent: true`（或全局对新增 Link 强制），则 intent 必须含 destroy/stale/restore 短字段。

**验收：** 现有 G-3 热路径集合不回退；JSON 中 `hot_path` 源可删除。

### S3 — 删除重复与空壳

- [ ] 删除入库的 `scripts/architecture_contracts.json`；`sort`/`compose` 仅留本地/CI artifact 可选路径。
- [ ] 删除 12+ 空壳 module-interface 及不再被门禁读取的散文条目。
- [ ] 分片目录改为 `intent/` 或就地 `architecture.intent.yaml`；旧 `scripts/architecture_contracts/*.json` 清空或归档到 `docs/archive/architecture_contracts-2026-10/`。
- [ ] 更新所有文档指向（AGENTS.md、模块边界清单、质量门禁文）。

**验收：**  
`git grep architecture_contracts.json` 仅剩历史归档；  
`make check/architecture-contracts` 不再 `rglob` 校验上百条信封。

### S4 — 可选增强（非阻塞）

- [ ] PR bot / CI artifact：贴出本次 diff 的 facts 变更摘要（新增/删除的 cap 与 System）。
- [ ] 与 `module_depgraph` / 未来符号引用图（G-6）对齐：facts 中的 requires 可对照链接期证据。
- [ ] 裁剪矩阵（G-7）消费 `trim.absent_profile`，把「缺席回退」从句子升级为实测。

---

## 5. 对现有 99 条的处置表

| 类别 | 约数 | S1 | S2 | S3 |
|------|------|----|----|-----|
| `_global` 政策信封 | 9 | 内建到 checker / docs | — | 删 JSON |
| `module-interface` 有 provides/requires | ~27 | 停用为 G-2 真源 | 不迁移清单 | 删 |
| `module-interface` 仅 hot_path | ~8 | 保留到 S2 | 迁 intent | 删 JSON |
| `module-interface` 空壳 | ~12 | 可立即删 | — | 删 |
| `link` / `ecs-system` | 9+18 | 符号改扫描 | 生命周期迁 intent（按需） | 删抄写字段 |
| `persistence` / `backend` / `state-owner` / `time-rng` 具体条 | ~16 | coverage 不再强制条目 | 策略句迁 intent 或 docs | 删空壳 |
| 合成 JSON | 1 文件 | 可仍生成 | — | 不入库 |

---

## 6. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 倒转 G-2 后「忘了声明能力」无人拦 | Facts 是声明本身；真问题是缺席回退——交给 trim 实测（S4/G-7）与代码评审看 facts diff |
| 正则漏扫宏包装的 `cap::` / 跨行声明 | 单测夹具覆盖常见宏；漏扫时补正则，不上完整 parser 也可迭代 |
| 丢失历史 ownership 长文 | S3 归档到 `docs/archive/…`，不丢 git 历史 |
| Agent 仍按旧习惯补 JSON | AGENTS.md 明确禁止；checker 对旧路径给出「已废弃，见本规格」错误信息 |
| 与「触碰补齐」文化冲突 | 补齐对象改为 **intent（若需要）+ 代码形状**，不再补 provides 列表 |

---

## 7. 实施任务拆分（建议 PR）

| PR | 内容 | 门禁影响 |
|----|------|----------|
| PR-A | 本规格 + AGENTS/规范指针 + 冻结空壳新增 | 无行为变化 |
| PR-B | `architecture_facts.py` + 单测；G-2/coverage 倒转 | 行为变化（减轻 JSON 要求） |
| PR-C | intent schema + hot_path 迁移 + G-3 改读 intent | 热路径不回退 |
| PR-D | 删合成 JSON、空壳、旧分片；文档全量改链 | 仓库瘦身 |

每个 PR 必须：`ARCHITECTURE_BASE=… make check/architecture-contracts` 与  
`python3 -X utf8 -m unittest scripts.tests.test_architecture_contracts`（及 facts 新测）通过。

---

## 8. 成功标准

1. **日常改模块不再强制编辑 JSON catalogue** 才能过架构门禁。  
2. **G-2 与符号覆盖以源码扫描为准**；intent 文件行数远小于今日分片总和（目标：热路径模块各 <30 行）。  
3. **入库树中无合成 `architecture_contracts.json` 双份。**  
4. api-shape / G-4 / G-5 信号不减弱。  
5. 文档与 AGENTS 不再把「九十九条 catalogue」描述为架构正确性的充分条件。

---

## 9. 决策记录（待确认）

实施 PR-B 前需确认：

1. Overlay 用 **YAML 旁路（A）** 还是 **Doxygen 标签（B）**？推荐 A。  
2. 新增 `*Link` 是否 **默认强制** 生命周期 intent，还是先 warn 一个版本？推荐先 warn，下一版本 error。  
3. 旧 JSON 长文归档目录是否进入主 docs 导航，还是仅 git 可查？推荐 `docs/archive/` 不进默认导航。

---

## 10. 一句话

**契约检测应回答「这次改动的代码形状和能力面是什么、有没有踩坏味道」，**  
**而不是「作者有没有把同一事实再抄进 JSON」。**  
把事实交给扫描，把意图缩到最小 overlay，删除其余登记税。
