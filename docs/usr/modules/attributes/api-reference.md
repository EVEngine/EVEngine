# attributes：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## AttributeSet

- `clearModifiers()` → `void`；[绑定实现 L42](../../../../src/modules/attributes/Attributes.cpp#L42)。
- `getModifierAttribute(index: int)` → `dynamic`；[绑定实现 L48](../../../../src/modules/attributes/Attributes.cpp#L48)。
- `getModifierCount()` → `int`；[绑定实现 L43](../../../../src/modules/attributes/Attributes.cpp#L43)。
- `getModifierOperation(index: int)` → `dynamic`；[绑定实现 L56](../../../../src/modules/attributes/Attributes.cpp#L56)。
- `getModifierPriority(index: int)` → `dynamic`；[绑定实现 L62](../../../../src/modules/attributes/Attributes.cpp#L62)。
- `getModifierSequence(index: int)` → `dynamic`；[绑定实现 L66](../../../../src/modules/attributes/Attributes.cpp#L66)。
- `getModifierSource(index: int)` → `dynamic`；[绑定实现 L52](../../../../src/modules/attributes/Attributes.cpp#L52)。
- `getSubject()` → `dynamic`；[绑定实现 L17](../../../../src/modules/attributes/Attributes.cpp#L17)。
- `has(attribute: AttributeId)` → `bool`；[绑定实现 L24](../../../../src/modules/attributes/Attributes.cpp#L24)。
- `modifyBase(key: string, value: float)` → `dynamic`；[绑定实现 L21](../../../../src/modules/attributes/Attributes.cpp#L21)。

## 接收者见绑定源码

- `getName()` → `string`；[绑定实现 L73](../../../../src/modules/attributes/Attributes.cpp#L73)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
