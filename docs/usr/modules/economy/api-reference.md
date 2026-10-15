# economy：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## Collector

- `destroy()` → `void`；[绑定实现 L129](../../../../src/modules/economy/Economy.cpp#L129)。
- `isIdle()` → `bool`；[绑定实现 L133](../../../../src/modules/economy/Economy.cpp#L133)。
- `resourceType()` → `string`；[绑定实现 L137](../../../../src/modules/economy/Economy.cpp#L137)。
- `totalGathered()` → `int`；[绑定实现 L138](../../../../src/modules/economy/Economy.cpp#L138)。

## GatherNode

- `amount()` → `int`；[绑定实现 L118](../../../../src/modules/economy/Economy.cpp#L118)。
- `depleted()` → `bool`；[绑定实现 L120](../../../../src/modules/economy/Economy.cpp#L120)。
- `destroy()` → `void`；[绑定实现 L116](../../../../src/modules/economy/Economy.cpp#L116)。
- `freeSlots()` → `int`；[绑定实现 L121](../../../../src/modules/economy/Economy.cpp#L121)。
- `regenTick()` → `void`；[绑定实现 L122](../../../../src/modules/economy/Economy.cpp#L122)。
- `resourceType()` → `string`；[绑定实现 L117](../../../../src/modules/economy/Economy.cpp#L117)。

## 接收者见绑定源码

- `clearGameplayControls()` → `void`；[绑定实现 L193](../../../../src/modules/economy/Economy.cpp#L193)。
- `clearTypes()` → `dynamic`；[绑定实现 L149](../../../../src/modules/economy/Economy.cpp#L149)。
- `getCap(player: int, type: string)` → `int`；[绑定实现 L156](../../../../src/modules/economy/Economy.cpp#L156)。
- `getEventPlayer(index: int)` → `dynamic`；[绑定实现 L163](../../../../src/modules/economy/Economy.cpp#L163)。
- `getEventType(index: int)` → `dynamic`；[绑定实现 L164](../../../../src/modules/economy/Economy.cpp#L164)。
- `getGameplayControlCount()` → `int`；[绑定实现 L194](../../../../src/modules/economy/Economy.cpp#L194)。
- `getName()` → `string`；[绑定实现 L143](../../../../src/modules/economy/Economy.cpp#L143)。
- `getStockMax(id: string)` → `dynamic`；[绑定实现 L152](../../../../src/modules/economy/Economy.cpp#L152)。
- `getTypeId(index: int)` → `string`；[绑定实现 L166](../../../../src/modules/economy/Economy.cpp#L166)。
- `hasType(id: string)` → `dynamic`；[绑定实现 L151](../../../../src/modules/economy/Economy.cpp#L151)。
- `typeCount()` → `dynamic`；[绑定实现 L150](../../../../src/modules/economy/Economy.cpp#L150)。
- `unpublishGameplay()` → `dynamic`；[绑定实现 L183](../../../../src/modules/economy/Economy.cpp#L183)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
