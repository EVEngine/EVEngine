# graphics_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## ReflectionProbeVisualizer

- `getCenterX()` → `float`；[绑定实现 L31](../../../../src/modules/graphics/editor/GraphicsEditorModule.cpp#L31)。
- `getCenterY()` → `float`；[绑定实现 L32](../../../../src/modules/graphics/editor/GraphicsEditorModule.cpp#L32)。
- `getCenterZ()` → `float`；[绑定实现 L33](../../../../src/modules/graphics/editor/GraphicsEditorModule.cpp#L33)。
- `getColorB()` → `float`；[绑定实现 L30](../../../../src/modules/graphics/editor/GraphicsEditorModule.cpp#L30)。
- `getColorG()` → `float`；[绑定实现 L29](../../../../src/modules/graphics/editor/GraphicsEditorModule.cpp#L29)。
- `getColorR()` → `float`；[绑定实现 L28](../../../../src/modules/graphics/editor/GraphicsEditorModule.cpp#L28)。
- `setExtents(x: float, y: float, z: float)` → `void`；[绑定实现 L24](../../../../src/modules/graphics/editor/GraphicsEditorModule.cpp#L24)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
