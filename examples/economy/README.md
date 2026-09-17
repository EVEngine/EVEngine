# Economy — 采集循环与满仓浪费的 economy 模块演示

`economy_demo.nut` 是一个**无窗口**的脚本演示，用来快速验证 `eve.Economy` 的资源账本、
`eve.GatherNode` 矿点与 `eve.Collector` 采集者的最小闭环：不做渲染，纯逻辑跑完整条
「接近 → 采集 → 满载 → 返回 → 卸货入账」链路并打印逐步事件。

## 运行

该目录没有项目级 `main.nut` / `config.nut`，用 `-r` 直接运行根脚本（在仓库根目录执行）：

```bash
# Windows
build/win32-debug/src/engine/eve.exe run -r examples/economy/economy_demo.nut

# Linux / macOS
build/linux-debug/src/engine/eve run -r examples/economy/economy_demo.nut
```

## 演示内容

- **场景 1 · SC2 式矿物采集**：注册 `minerals` 资源（储量 100、有限），创建一个双槽位
  `eve.GatherNode`（储量 30）与两个 `eve.Collector`（A：载荷 5 / 速率 1 / 单程 2 tick，
  B：载荷 3 / 速率 2 / 单程 3 tick），`assign()` 占槽后按 tick 推进到双方 `isIdle()`，
  打印每个 `gather / deposit` 事件、往返次数、累计入账与矿点余量。
- **场景 2 · 满仓浪费**：玩家持有上限 10、节点储量 25、载荷 5，第三次往返起超出上限的部分
  计入 `waste` 事件，用于展示 `eco.getWasted()` 与 `eco.getCap()` 的语义。

## 输出

脚本是确定性的：同样参数下 `doneTick`、往返次数与余额完全一致。结尾会打印两个场景的
余额 / 上限 / 浪费统计，例如：

```
  玩家1 矿物余额: ... / 上限 100, 浪费 0
  矿点剩余: 0
```

## 相关文件

| 文件 | 说明 |
|---|---|
| `economy_demo.nut` | 演示脚本本体（唯一的入口，用 `eve run -r` 运行） |
| `README.md` | 本文档 |
