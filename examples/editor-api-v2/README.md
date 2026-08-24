# Editor API V2：游戏内 Runtime Builder

这个示例把编辑器直接作为玩法：玩家从资产和材质面板选择内容，点击场景网格完成建造。它重点演示的不是某个固定面板，而是游戏与开发编辑器共享的命令协议。

运行：

```powershell
make run/win32-debug GAME=examples/editor-api-v2
```

## 演示链路

1. 游戏用 `editor.registerScriptCommand(...)` 向共享 `EditorCommandService` 注入 `park.scene.place-asset`；
2. `EditorSession.getCommand*()` 按 HostProfile 发现命令；
3. UI 只组装包含资产、材质、位置和费用的 payload；
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
