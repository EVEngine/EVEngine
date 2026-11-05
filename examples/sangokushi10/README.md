# Sangokushi 10 Slice

回合制《三国志 10》风格竖切：用引擎中性积木**自行定义**国家、战区、城池、武将、军团、
内政任务与人物关系，而不是依赖现成战略模块。

概念注册走 `Schema.registerFromClass`（见 `concepts.nut`），内容进 `Definitions`，
运行时归属/任命/邻接/驻军用 `Social`，五维与城池开发度用 `Attributes`，内政队列用
`Production`，武将排行与快照同步用 `eve.Database()` 的事务 + `upsertJson`。

## 运行

```bash
# Linux (Cloud VM / CI)
make debug JOBS=4
cd examples/sangokushi10
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
ALSOFT_DRIVERS=null \
XDG_RUNTIME_DIR=/tmp/xdg-runtime \
xvfb-run -a ../../build/linux-debug/src/engine/eve run .
```

Windows：`build\win32-debug\src\engine\eve.exe run examples\sangokushi10`

## 玩法

你操控 **魏**。地图上有 8 座城、3 个国家（魏/蜀/吴）、3 个战区、12 名武将与 6 支军团。

- 左键点选城池；`Tab` 在城内武将间切换。
- `G` / `F` / `M` / `C`：任命太守 / 开垦 / 商业 / 征兵职务。
- `1`–`4`：在当前城开始对应内政（需要匹配职务的武将）。
- `Space`：结束本月——结算收入、内政、忠诚；蜀吴 AI 发展，隔月可能攻城。
- `A`：从当前魏城出征，攻打一座相邻敌城（比较军团兵力与将领统率/武力 + 城防）。
- 占领 6 座城获胜；洛阳失守失败。
- `R`：重置。

右侧面板显示国库、城池开发、驻军、魏势力统率排行（来自 SQLite `Query`）。

## 文件

| 文件 | 作用 |
|------|------|
| `concepts.nut` | Nation/Region/City/Officer/Army/Task 的 Schema 类 |
| `world.nut` | 世界装配、任命、内政、攻城、AI、Database 目录同步 |
| `main.nut` | 地图绘制、UI、输入 |
| `config.nut` | 窗口与模块列表 |

## 引擎侧补齐（本示例用到）

- `DatabaseConnection.begin/commit/rollback/inTransaction`
- `DatabaseConnection.upsertJson`（SQLite `INSERT OR REPLACE`）
