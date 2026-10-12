# avatar_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `createExpression(id: string, name: string)` → `dynamic`；[绑定实现 L77](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L77)。
- `createLayer(id: string, name: string)` → `dynamic`；[绑定实现 L59](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L59)。
- `createParameter(id: string, name: string)` → `dynamic`；[绑定实现 L68](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L68)。
- `deleteSelectedExpression()` → `dynamic`；[绑定实现 L82](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L82)。
- `deleteSelectedLayer()` → `dynamic`；[绑定实现 L64](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L64)。
- `deleteSelectedParameter()` → `dynamic`；[绑定实现 L73](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L73)。
- `getExpressionChannelCount(index: int)` → `dynamic`；[绑定实现 L183](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L183)。
- `getExpressionChannelName(expression: int, channel: int)` → `dynamic`；[绑定实现 L186](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L186)。
- `getExpressionCount()` → `dynamic`；[绑定实现 L171](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L171)。
- `getExpressionId(index: int)` → `dynamic`；[绑定实现 L174](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L174)。
- `getExpressionName(index: int)` → `dynamic`；[绑定实现 L177](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L177)。
- `getExpressionSelected(index: int)` → `dynamic`；[绑定实现 L180](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L180)。
- `getKind()` → `dynamic`；[绑定实现 L90](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L90)。
- `getLayerCount()` → `dynamic`；[绑定实现 L99](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L99)。
- `getLayerId(index: int)` → `dynamic`；[绑定实现 L102](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L102)。
- `getLayerName(index: int)` → `dynamic`；[绑定实现 L105](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L105)。
- `getLayerSelected(index: int)` → `dynamic`；[绑定实现 L114](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L114)。
- `getLayerVisible(index: int)` → `dynamic`；[绑定实现 L108](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L108)。
- `getLayerZ(index: int)` → `dynamic`；[绑定实现 L111](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L111)。
- `getParameterCount()` → `dynamic`；[绑定实现 L150](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L150)。
- `getParameterId(index: int)` → `dynamic`；[绑定实现 L153](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L153)。
- `getParameterMaximum(index: int)` → `dynamic`；[绑定实现 L165](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L165)。
- `getParameterMinimum(index: int)` → `dynamic`；[绑定实现 L162](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L162)。
- `getParameterName(index: int)` → `dynamic`；[绑定实现 L156](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L156)。
- `getParameterSelected(index: int)` → `dynamic`；[绑定实现 L168](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L168)。
- `getParameterValue(index: int)` → `dynamic`；[绑定实现 L159](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L159)。
- `getPreviewA(index: int)` → `dynamic`；[绑定实现 L141](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L141)。
- `getPreviewB(index: int)` → `dynamic`；[绑定实现 L138](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L138)。
- `getPreviewG(index: int)` → `dynamic`；[绑定实现 L135](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L135)。
- `getPreviewH(index: int)` → `dynamic`；[绑定实现 L129](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L129)。
- `getPreviewLayerCount()` → `dynamic`；[绑定实现 L117](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L117)。
- `getPreviewName(index: int)` → `dynamic`；[绑定实现 L147](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L147)。
- `getPreviewR(index: int)` → `dynamic`；[绑定实现 L132](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L132)。
- `getPreviewRevision()` → `dynamic`；[绑定实现 L87](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L87)。
- `getPreviewSelected(index: int)` → `dynamic`；[绑定实现 L144](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L144)。
- `getPreviewW(index: int)` → `dynamic`；[绑定实现 L126](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L126)。
- `getPreviewX(index: int)` → `dynamic`；[绑定实现 L120](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L120)。
- `getPreviewY(index: int)` → `dynamic`；[绑定实现 L123](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L123)。
- `getSelectedId()` → `dynamic`；[绑定实现 L93](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L93)。
- `getSelectedType()` → `dynamic`；[绑定实现 L96](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L96)。
- `pointerDown(x: float, y: float)` → `dynamic`；[绑定实现 L45](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L45)。
- `selectExpression(id: string)` → `dynamic`；[绑定实现 L41](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L41)。
- `selectLayer(id: string)` → `dynamic`；[绑定实现 L35](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L35)。
- `selectParameter(id: string)` → `dynamic`；[绑定实现 L38](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L38)。
- `setLayerVisible(visible: bool)` → `dynamic`；[绑定实现 L48](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L48)。
- `setLayerZ(zIndex: int)` → `dynamic`；[绑定实现 L52](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L52)。
- `setParameterValue(value: float)` → `dynamic`；[绑定实现 L55](../../../../src/modules/avatar/editor/AvatarDocumentEditorScriptBindings.cpp#L55)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
