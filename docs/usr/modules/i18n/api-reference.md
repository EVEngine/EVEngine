# i18n：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `clear()` → `void`；[绑定实现 L716](../../../../src/modules/i18n/I18n.cpp#L716)。
- `getDefaultLanguage()` → `string`；[绑定实现 L727](../../../../src/modules/i18n/I18n.cpp#L727)。
- `getLanguage()` → `string`；[绑定实现 L725](../../../../src/modules/i18n/I18n.cpp#L725)。
- `getLanguageAt(index: int)` → `string`；[绑定实现 L729](../../../../src/modules/i18n/I18n.cpp#L729)。
- `getLanguageCount()` → `int`；[绑定实现 L728](../../../../src/modules/i18n/I18n.cpp#L728)。
- `getName()` → `string`；[绑定实现 L686](../../../../src/modules/i18n/I18n.cpp#L686)。
- `selectLanguage()` → `dynamic`；[绑定实现 L718](../../../../src/modules/i18n/I18n.cpp#L718)。
- `unload(lang: string)` → `void`；[绑定实现 L715](../../../../src/modules/i18n/I18n.cpp#L715)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
