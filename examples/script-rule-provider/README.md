# Script Rule Provider（用脚本接管原生扩展点）

演示**只写脚本、不写 C++** 就能给引擎加游戏规则。

载具的伤害管线在扣血前会遍历一个原生"插座"：

```cpp
// src/modules/vehicle/Vehicle.cpp（引擎侧，本示例没有改它）
eve::cap::forEach<IVehicleDamageModifier>(
    [&](IVehicleDamageModifier* m) { amount = m->modifyDamage(*v, amount, zone, sourceId); });
```

这个插座过去没有任何提供方（`cap::listenerCount<IVehicleDamageModifier>()` 恒为 0）。
现在脚本可以回答它：

```squirrel
class FrontResistRule {
    function modifyDamage(vehicleId, defId, amount, zone, sourceId) {
        if (zone == "front") return amount * 0.5;
        return amount;
    }
}

// 顺序很重要：接缝由 Vehicle 模块声明，所以先拿到模块实例，再发布规则。
local vehicle = eve.Vehicle();

local result = eve.provider.publishFor("IVehicleDamageModifier", "front-resist", FrontResistRule());
if (!result.ok) print(result.code + ": " + result.status.summary + "\n");
```

## 演示内容

- **三条脚本规则**：`front-resist`（正面减伤 50%）、`rear-crit`（背面暴击 ×2）、
  `armor-plating`（再减固定 5 点）。它们按发布顺序依次调用，**装甲区倍率在全部规则之后应用**。
- **启动自检**：`eve_init` 里打印每一步的算术并断言结果，通过时输出
  `SCRIPT_RULE_PASS`。它同时验证"没有规则时行为不变"与"撤销规则后立即失效"。
- **结构化结果**：`eve.provider` 的每个调用都返回 `eve.result` 的
  `{ ok, code, status, diagnostics, value }`；重复撤销是 `ok=false / code=rejected`，
  不是静默成功。
- **故意写错的规则**（按 `4` 开关）：规则抛错时**按原值放行**（这一击不会消失），
  引擎在 `eve.log` 里报一次，而不是每帧刷屏。
- **可发现性**：HUD 列出 `eve.provider.capabilities()` 与 `eve.provider.published()`，
  脚本能自己发现有哪些扩展点、当前有哪些 provider 及其 generation。

## 运行

```bash
make run/win32-debug GAME=examples/script-rule-provider     # 或对应平台
```

操作：

```
1 / 2 / 3 = 开关 front-resist / rear-crit / armor-plating
4         = 开关"故意写错的规则"（演示按原值放行）
R         = 把目标血量恢复满
```

目标每 0.7 秒挨一枪（正面 / 背面轮流），HUD 显示实际扣血量；被击毁后 1 秒重生。

## 判定依据

本示例是方案文档里判据 **J-1**（"用户能在脚本中注册一个游戏规则型扩展点并生效，
且不改任何 C++ 文件"）的取证：

- `git log --oneline -- src/modules/vehicle/Vehicle.cpp` 显示接缝随阶段 4 落地，
  之后本示例只新增 `examples/script-rule-provider/`；
- `SCRIPT_RULE_PASS` 由脚本自己打印，来自 `applyDamage` 之后真实读取的血量，
  不是"看起来对"；
- 提供方缺席时的行为（socket 未声明 → `publishFor` 返回 `not_found`）由
  `test/vehicle.cpp` 的 `vehicle.script.damageRule.absent` 覆盖。

约束与边界：

- 脚本拿不到 `VehicleEntity*`，只拿到 `vehicleId` / `defId` / `amount` / `zone` / `sourceId`；
  想读引擎内部布局的规则属于原生代码。
- 伤害事件不是"每帧每实体"路径；需要那个频率的逻辑应写原生组件。
- 脚本规则注册在默认优先级；原生提供方仍可传更小的优先级以先运行。

相关文档：

- 方案：[docs/dev/2026-09-19-脚本定义组件与引擎扩展性方案.md](../../docs/dev/2026-09-19-脚本定义组件与引擎扩展性方案.md)
- 载具系统：[docs/dev/通用载具系统设计.md](../../docs/dev/通用载具系统设计.md)
- 机制选型：[docs/dev/模块接口契约与机制选型规范.md](../../docs/dev/模块接口契约与机制选型规范.md)
