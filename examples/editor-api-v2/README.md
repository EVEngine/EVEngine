# Editor API V2：游戏内 Runtime Builder

这个示例把编辑器直接作为玩法：玩家可点击 `Add Tree / Add Bench / Add Ride`
立即建造到下一个空网格，也可先选材质、再点击场景网格精确放置。两种交互都走同一套
`planCommand / executePlan` 命令协议。

运行：

```powershell
make run/win32-debug GAME=examples/editor-api-v2
```

## 演示链路

1. 游戏用 `editor.registerScriptCommand(...)` 向共享 `EditorCommandService` 注入 `park.scene.place-asset`；
2. `EditorSession.getCommand*()` 按 HostProfile 发现命令；
3. UI 的快捷添加和精确放置只组装包含资产、材质、位置和费用的 payload；
4. `planCommand()` 生成无副作用计划和稳定 `planId`；
5. `executePlan()` 执行保留的计划，回传 transaction state、revision 和 authority receipt；
6. 游戏逻辑回调校验预算、占用和边界，然后修改真实玩法状态。

核心调用：

```squirrel
local plan = session.planCommand("park.scene.place-asset", {
    x = 3,
    y = 4,
    asset = "park.asset.ride",
    material = "park.material.sunset",
    cost = 120
});

if (plan.accepted) {
    local receipt = session.executePlan(plan.planId, {});
    print(receipt.transactionState + "\n");
}
```

这里的脚本注册入口适合游戏原型和项目专用编辑能力。需要服务器 Authority、可逆 Domain Operation、多人提交或持久化策略时，游戏模块应实现 `IGameEditorExtension`，但 UI 和 Session 调用保持不变。
