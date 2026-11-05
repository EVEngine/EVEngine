# 工厂物流（Logistics）

**这篇解决什么**

- 在 2D 格点上放传送带、机器、源/汇，驱动离散物品流动与缓冲合成。
- 需要大规模物品可视化时，把 `packItemSimFloats` 交给 `gpgpu` compute，而不是每件货一个 ECS 实体。
- **不要**用物理刚体模拟每件货；**不要**把传送带权威状态写进 graphics。

**脚本入口：** `eve.Logistics()`（根槽位 `logistics`）

Logistics 拥有格点拓扑、物品 SoA、机器输入/输出缓冲与内置合成计时。
`machineOwnerId(i)` 返回稳定字符串（如 `machine:3`），可投影到 `production` WorkQueue
的 owner，由游戏脚本决定何时改用 Production 结算。

`configure(width, height, layers, cellSize, originX, originY)` 的 `layers` 为多层预留；
当前垂直切片只步进 layer 0。

## 最小流水线

```squirrel
persist factory = null

eve_init <- function() {
    if (factory == null) factory = eve.Logistics()
    local cfg = factory.configure(24, 10, 1, 24.0, 40.0, 80.0)
    if (!cfg.ok) throw cfg.status.summary
    eve.result.ignore(factory.registerRecipe({
        id = "ingot", inputType = 1, inputCount = 1,
        outputType = 2, outputCount = 1, durationSec = 0.8
    }), "recipe")
    eve.result.ignore(factory.placeSource(0, 4, 1, 2.0, 0), "source")
    for (local x = 1; x <= 6; x += 1)
        eve.result.ignore(factory.placeBelt(x, 4, 0, 0), "belt")
    eve.result.ignore(factory.placeMachine(7, 4, 2, 2, "ingot", 0), "machine")
    for (local x = 9; x <= 14; x += 1)
        eve.result.ignore(factory.placeBelt(x, 4, 0, 0), "belt")
    eve.result.ignore(factory.placeSink(15, 4, 0), "sink")
}

eve_update <- function(dt) {
    local step = factory.advance(dt)
    if (!step.ok) throw step.status.summary
}
```

方向约定（屏幕 Y 向下）：`0=东(+X)`、`1=南(+Y)`、`2=西(-X)`、`3=北(-Y)`。
机器默认从西侧邻格拉入、向东侧邻格推出。

完整可运行切片见 `examples/factory/`（含 GPU 位置解算）。

## 查询快照

`getItem(i)` 返回 `LogisticsItemState`：`x` / `y`（世界坐标）、`type`、`cellX` / `cellY`、
`progress`（格内 0..1）、`dir`。

`getMachine(i)` 返回 `LogisticsMachineState`：`id`、`originX` / `originY`、`width` / `height`、
`recipeIndex`、`inputStored` / `outputStored`、`craftProgress`、`crafting`、`layer`。

`packItemSimFloats()` 输出 `[cellX, cellY, progress, dir, type, layer] * N`，供 compute 解算世界坐标。

## 与现有模块的边界

| 职责 | 模块 |
|---|---|
| 格点占用 / 建造会话 | `building` + `grid`（放置 UX）；Logistics 自管运行时拓扑 |
| 定时生产 / 结算收据 | `production`（可选投影） |
| 背包堆叠事务 | `inventory`（工厂缓冲是机器本地计数，不是 Bag） |
| GPU 位置 / 实例绘制 | `gpgpu` + `graphics` |

## 成本

- `advance`：`@cost` O(items + machines + sources)，每模拟 tick 一次。
- `packItemSimFloats` / `packItemWorldFloats`：`@cost` O(items)，在上传或回读边界摊销。
