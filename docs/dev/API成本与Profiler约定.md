# API 成本与 Profiler 约定

日期：2026-11-05。状态：约定已落地（文档导读 + `@cost` + hotspot 读取面）。
相关：[`模块接口契约与机制选型规范.md`](模块接口契约与机制选型规范.md) §5、
[`AI知识补偿.md`](AI知识补偿.md)、[`../usr/modules/profiler.md`](../usr/modules/profiler.md)。

## 两个问题分开答

| 问题 | 手段 |
| --- | --- |
| 还有没有另一种写法？ | 模块文档开头 **这篇解决什么**（问题 / 入口 / 不要） |
| 哪个更快、这次是否不合理？ | 静态成本维（选型）+ Profiler 热点（规模化实测） |

不要用绝对毫秒当 API 合同真源（会过期）；合同写**相对量级与摊销维**（R-COST-5）。

## 静态成本（写代码前选型）

在 **这篇解决什么** 或 Doxygen `@cost` 里用这三项即可，不必另建 recipe 库：

| 字段 | 含义 | 例 |
| --- | --- | --- |
| `costClass` | `cheap` / `scales` / `expensive` | sensing query = `scales` |
| `scalesWith` | 成本跟什么涨 | `maxCount,radius`；全表扫描 = `worldSize` |
| `prefer` / `不要` | 同意图下的正确/错误入口 | prefer `SensingWorld.query`；不要扫全体单位数组 |

同意图多条路径时：选 **scalesWith 更小** 且文档标明入口的那条。  
昂贵公共 API 仍须满足 §5：`@cost` + 必要时两步/令牌形状。

## 动态 Profiler（跑起来证伪）

1. 热路径用稳定 zone 名（如 `sensing/SensingWorld::query`），便于归因。
2. **小场景不能当证据**；固定种子把单位/对象数放大后再采。
3. 读热点：
   - 脚本：`profiler.hotspots(limit, minSelfMs)`
   - MCP：`eve_profiler_hotspots`（或 `eve_profiler_frame` 的 `hotspots` 字段）
4. 不合理的信号：forbidden 全表路径出现在 top、或官方 query zone 远高于预算。

`cpuFrameMs` / zone selfMs 是本机构采样，只用于相对比较与回归，不写进 Binding Contract。
