# ui_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `getFloat(path: string)` → `dynamic`；[绑定实现 L117](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L117)。
- `getState()` → `dynamic`；[绑定实现 L121](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L121)。
- `getThemeActive(index: int)` → `dynamic`；[绑定实现 L102](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L102)。
- `getThemeId(index: int)` → `dynamic`；[绑定实现 L93](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L93)。
- `getThemeName(index: int)` → `dynamic`；[绑定实现 L96](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L96)。
- `getThemeSelected(index: int)` → `dynamic`；[绑定实现 L99](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L99)。
- `resetSelectedToBase()` → `dynamic`；[绑定实现 L59](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L59)。
- `selectTheme(id: string)` → `dynamic`；[绑定实现 L40](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L40)。
- `setActiveSelected()` → `dynamic`；[绑定实现 L56](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L56)。
- `setFloat(path: string, value: float)` → `dynamic`；[绑定实现 L71](../../../../src/modules/ui/editor/UiThemeEditorScriptBindings.cpp#L71)。

- `getColorA`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `getColorB`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `getColorG`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `getColorR`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `setColor`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。

重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
