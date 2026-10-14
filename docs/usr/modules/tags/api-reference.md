# tags：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `capabilityAt(subject: string, index: int)` → `string`；[绑定实现 L127](../../../../src/modules/tags/Tags.cpp#L127)。
- `capabilityCount(subject: string)` → `int`；[绑定实现 L126](../../../../src/modules/tags/Tags.cpp#L126)。
- `count(subject: string)` → `int`；[绑定实现 L117](../../../../src/modules/tags/Tags.cpp#L117)。
- `eventAction(index: int)` → `string`；[绑定实现 L134](../../../../src/modules/tags/Tags.cpp#L134)。
- `eventSequence(index: int)` → `int`；[绑定实现 L133](../../../../src/modules/tags/Tags.cpp#L133)。
- `eventSubject(index: int)` → `string`；[绑定实现 L135](../../../../src/modules/tags/Tags.cpp#L135)。
- `eventValue(index: int)` → `string`；[绑定实现 L136](../../../../src/modules/tags/Tags.cpp#L136)。
- `getName()` → `string`；[绑定实现 L113](../../../../src/modules/tags/Tags.cpp#L113)。
- `removeCapability(subject: string, capability: string)` → `bool`；[绑定实现 L124](../../../../src/modules/tags/Tags.cpp#L124)。
- `requireAnyCapability(subject: string, requested: Array<dynamic>)` → `bool`；[绑定实现 L129](../../../../src/modules/tags/Tags.cpp#L129)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
