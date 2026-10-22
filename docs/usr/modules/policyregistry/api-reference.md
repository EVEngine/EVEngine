# policyregistry：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## PolicyDescriptor

- `getDomain()` → `dynamic`；[绑定实现 L657](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L657)。
- `getGeneration()` → `dynamic`；[绑定实现 L668](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L668)。
- `getKind()` → `dynamic`；[绑定实现 L664](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L664)。
- `getMetadataJson()` → `dynamic`；[绑定实现 L667](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L667)。
- `getPriority()` → `dynamic`；[绑定实现 L662](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L662)。
- `getSchemaId()` → `dynamic`；[绑定实现 L666](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L666)。
- `getVersion()` → `dynamic`；[绑定实现 L659](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L659)。
- `isEnabled()` → `dynamic`；[绑定实现 L663](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L663)。

## PolicyEvent

- `getDomain()` → `dynamic`；[绑定实现 L677](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L677)。
- `getGeneration()` → `dynamic`；[绑定实现 L681](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L681)。
- `getPolicyName()` → `dynamic`；[绑定实现 L678](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L678)。
- `getSequence()` → `dynamic`；[绑定实现 L674](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L674)。
- `getVersion()` → `dynamic`；[绑定实现 L679](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L679)。
- `isEnabled()` → `dynamic`；[绑定实现 L684](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L684)。

## PolicyRegistry

- `atDomain(d: string, i: int)` → `PolicyDescriptor`；[绑定实现 L796](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L796)。
- `clearEvents()` → `void`；[绑定实现 L803](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L803)。
- `countDomain(domain: string)` → `int`；[绑定实现 L792](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L792)。
- `enable(domain: string, policyName: string, enabled: bool)` → `dynamic`；[绑定实现 L727](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L727)。
- `eventAt(i: int)` → `PolicyEvent`；[绑定实现 L800](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L800)。
- `eventCount()` → `int`；[绑定实现 L799](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L799)。
- `generationOf(domain: string, policyName: string)` → `dynamic`；[绑定实现 L813](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L813)。
- `remove(domain: string, policyName: string)` → `dynamic`；[绑定实现 L716](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L716)。
- `size()` → `int`；[绑定实现 L791](../../../../src/modules/policyregistry/PolicyRegistry.cpp#L791)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
