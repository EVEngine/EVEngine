# definitions：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## Definition

- `getGeneration()` → `dynamic`；[绑定实现 L649](../../../../src/modules/definitions/Definitions.cpp#L649)。
- `getId()` → `dynamic`；[绑定实现 L645](../../../../src/modules/definitions/Definitions.cpp#L645)。
- `getType()` → `dynamic`；[绑定实现 L644](../../../../src/modules/definitions/Definitions.cpp#L644)。
- `getVersion()` → `dynamic`；[绑定实现 L646](../../../../src/modules/definitions/Definitions.cpp#L646)。

## DefinitionEvent

- `getGeneration()` → `dynamic`；[绑定实现 L665](../../../../src/modules/definitions/Definitions.cpp#L665)。
- `getId()` → `dynamic`；[绑定实现 L661](../../../../src/modules/definitions/Definitions.cpp#L661)。
- `getName()` → `dynamic`；[绑定实现 L659](../../../../src/modules/definitions/Definitions.cpp#L659)。
- `getSequence()` → `dynamic`；[绑定实现 L656](../../../../src/modules/definitions/Definitions.cpp#L656)。
- `getType()` → `dynamic`；[绑定实现 L660](../../../../src/modules/definitions/Definitions.cpp#L660)。
- `getVersion()` → `dynamic`；[绑定实现 L662](../../../../src/modules/definitions/Definitions.cpp#L662)。

## DefinitionRegistry

- `clearEvents()` → `void`；[绑定实现 L762](../../../../src/modules/definitions/Definitions.cpp#L762)。
- `eventCount()` → `int`；[绑定实现 L758](../../../../src/modules/definitions/Definitions.cpp#L758)。
- `generationOf(type: string, id: string)` → `dynamic`；[绑定实现 L783](../../../../src/modules/definitions/Definitions.cpp#L783)。
- `handle(type: string, id: string)` → `dynamic`；[绑定实现 L772](../../../../src/modules/definitions/Definitions.cpp#L772)。
- `remove(type: string, id: string)` → `dynamic`；[绑定实现 L696](../../../../src/modules/definitions/Definitions.cpp#L696)。
- `size()` → `int`；[绑定实现 L750](../../../../src/modules/definitions/Definitions.cpp#L750)。

## 接收者见绑定源码

- `getName()` → `string`；[绑定实现 L801](../../../../src/modules/definitions/Definitions.cpp#L801)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
