# rpg：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## RPGActor

- `getBuffCount()` → `int`；[绑定实现 L653](../../../../src/modules/rpg/RPG.cpp#L653)。
- `getBuffEffectId(index: int)` → `string`；[绑定实现 L654](../../../../src/modules/rpg/RPG.cpp#L654)。
- `getBuffInstanceId(index: int)` → `int`；[绑定实现 L657](../../../../src/modules/rpg/RPG.cpp#L657)。
- `getBuffProp(instanceId: int, key: string, fallback: string)` → `string`；[绑定实现 L659](../../../../src/modules/rpg/RPG.cpp#L659)。
- `getBuffRemaining(index: int)` → `float`；[绑定实现 L656](../../../../src/modules/rpg/RPG.cpp#L656)。
- `getBuffSource(index: int)` → `string`；[绑定实现 L658](../../../../src/modules/rpg/RPG.cpp#L658)。
- `getBuffStacks(index: int)` → `int`；[绑定实现 L655](../../../../src/modules/rpg/RPG.cpp#L655)。
- `getStatusProp(instanceId: int, key: string, fallback: string)` → `string`；[绑定实现 L643](../../../../src/modules/rpg/RPG.cpp#L643)。
- `getStatusSource(index: int)` → `string`；[绑定实现 L642](../../../../src/modules/rpg/RPG.cpp#L642)。
- `hasBuffTag(tag: string)` → `bool`；[绑定实现 L652](../../../../src/modules/rpg/RPG.cpp#L652)。
- `hasStatusTag(tag: string)` → `bool`；[绑定实现 L636](../../../../src/modules/rpg/RPG.cpp#L636)。
- `removeBuffByEffect(effectId: string)` → `int`；[绑定实现 L648](../../../../src/modules/rpg/RPG.cpp#L648)。
- `removeBuffBySource(source: string)` → `int`；[绑定实现 L649](../../../../src/modules/rpg/RPG.cpp#L649)。
- `removeBuffByTag(tag: string)` → `int`；[绑定实现 L650](../../../../src/modules/rpg/RPG.cpp#L650)。
- `setStatusProp(instanceId: int, key: string, value: string)` → `bool`；[绑定实现 L644](../../../../src/modules/rpg/RPG.cpp#L644)。

## 接收者见绑定源码

- `getStatusChangeEventAction(index: int)` → `string`；[绑定实现 L1476](../../../../src/modules/rpg/RPG.cpp#L1476)。
- `getStatusChangeEventActor(index: int)` → `RPGActor`；[绑定实现 L1472](../../../../src/modules/rpg/RPG.cpp#L1472)。
- `getStatusChangeEventCount()` → `int`；[绑定实现 L1471](../../../../src/modules/rpg/RPG.cpp#L1471)。
- `getStatusChangeEventEffectId(index: int)` → `string`；[绑定实现 L1474](../../../../src/modules/rpg/RPG.cpp#L1474)。
- `getStatusChangeEventInstanceId(index: int)` → `int`；[绑定实现 L1473](../../../../src/modules/rpg/RPG.cpp#L1473)。
- `getStatusChangeEventReason(index: int)` → `string`；[绑定实现 L1478](../../../../src/modules/rpg/RPG.cpp#L1478)。
- `getStatusChangeEventSource(index: int)` → `string`；[绑定实现 L1475](../../../../src/modules/rpg/RPG.cpp#L1475)。
- `getStatusChangeEventStacks(index: int)` → `int`；[绑定实现 L1477](../../../../src/modules/rpg/RPG.cpp#L1477)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
