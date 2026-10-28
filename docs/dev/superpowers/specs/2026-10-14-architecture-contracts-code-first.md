# 架构契约门禁：代码为真源（已落地）

> 状态：**已实施**（2026-10-14，一次落地，未分期）  
> 前置：`docs/dev/模块接口契约与机制选型规范.md`  
> 实现：`scripts/check_architecture_contracts.py`

---

## 做了什么

1. **删除** `scripts/architecture_contracts.json` 与 `scripts/architecture_contracts/*.json`（约 8k 行双写登记表）。
2. **重写** `check_architecture_contracts.py`：只做改动行代码 lint，不再加载 / 校验 catalogue。
3. **保留**：api-shape、裸指针 `@ownership`/`@lifetime`、G-3（源码或目录声明热路径）、G-4、G-5。
4. **废除**：G-1/G-2 对 JSON `provides`/`requires` 的交叉登记；新 Link/System 的 catalogue coverage。
5. `sort_architecture_contracts.py` 改为退役 stub（exit 2）。
6. `module_boundary_audit.load_catalogue()` 返回空列表（报告工具，不 gate）。

## 日常怎么用

```sh
ARCHITECTURE_BASE=<pr-base> make check/architecture-contracts
# 或
python3 scripts/check_architecture_contracts.py --base origin/dev
```

标记新热路径：在源码前导注释写 `// eve-hot-path`；整目录 CPP 可用本地 `.eve-hot-path`，内容为 `eve.hot-path-directory/v1:*.cpp`（不要维护中央列表）。

## 不再要求

- 为过 CI 补 `module-interface` / `link` / `ecs-system` JSON 条目。
- 维护合成 snapshot 与分片同步。

## 历史动机（摘要）

旧门禁把可从源码扫描的事实（`cap::`、System/Link 名）抄进 JSON，再用改动行去对表；散文字段只查「键在不在」。这是第二真相源，不是语义证明。详见对话审计与本文件初稿意图。
