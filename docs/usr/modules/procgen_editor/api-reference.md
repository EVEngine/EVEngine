# procgen_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `getActiveTool()` → `dynamic`；[绑定实现 L60](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L60)。
- `getBool(key: string)` → `dynamic`；[绑定实现 L170](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L170)。
- `getDisplayName()` → `dynamic`；[绑定实现 L125](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L125)。
- `getFloat(key: string)` → `dynamic`；[绑定实现 L167](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L167)。
- `getInt(key: string)` → `dynamic`；[绑定实现 L164](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L164)。
- `getKind()` → `dynamic`；[绑定实现 L128](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L128)。
- `getModuleId()` → `dynamic`；[绑定实现 L119](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L119)。
- `getModuleUri()` → `dynamic`；[绑定实现 L122](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L122)。
- `getParamChoice(paramIndex: int, choiceIndex: int)` → `dynamic`；[绑定实现 L161](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L161)。
- `getParamChoiceCount(index: int)` → `dynamic`；[绑定实现 L158](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L158)。
- `getParamCount()` → `dynamic`；[绑定实现 L137](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L137)。
- `getParamKey(index: int)` → `dynamic`；[绑定实现 L140](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L140)。
- `getParamKind(index: int)` → `dynamic`；[绑定实现 L146](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L146)。
- `getParamLabel(index: int)` → `dynamic`；[绑定实现 L143](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L143)。
- `getParamMaximum(index: int)` → `dynamic`；[绑定实现 L152](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L152)。
- `getParamMinimum(index: int)` → `dynamic`；[绑定实现 L149](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L149)。
- `getParamStep(index: int)` → `dynamic`；[绑定实现 L155](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L155)。
- `getPointCount()` → `dynamic`；[绑定实现 L182](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L182)。
- `getPointSeed(index: int)` → `dynamic`；[绑定实现 L191](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L191)。
- `getPointX(index: int)` → `dynamic`；[绑定实现 L185](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L185)。
- `getPointZ(index: int)` → `dynamic`；[绑定实现 L188](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L188)。
- `getPreviewFailureSummary()` → `dynamic`；[绑定实现 L134](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L134)。
- `getPreviewRevision()` → `dynamic`；[绑定实现 L116](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L116)。
- `getSelectedStage()` → `dynamic`；[绑定实现 L131](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L131)。
- `getStageCount()` → `dynamic`；[绑定实现 L176](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L176)。
- `getStageName(index: int)` → `dynamic`；[绑定实现 L179](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L179)。
- `getString(key: string)` → `dynamic`；[绑定实现 L173](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L173)。
- `getTargetId()` → `dynamic`；[绑定实现 L59](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L59)。
- `isDirty()` → `dynamic`；[绑定实现 L112](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L112)。
- `isLive()` → `dynamic`；[绑定实现 L113](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L113)。
- `observeRevision(document: string, revision: int)` → `dynamic`；[绑定实现 L55](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L55)。
- `publishPreview(points: PointSet, stage: string, expectedRevision: int)` → `dynamic`；[绑定实现 L84](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L84)。
- `publishStage(points: PointSet, stage: string)` → `dynamic`；[绑定实现 L90](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L90)。
- `selectStage(stage: string)` → `dynamic`；[绑定实现 L101](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L101)。
- `setBool(key: string, value: bool)` → `dynamic`；[绑定实现 L75](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L75)。
- `setFloat(key: string, value: float)` → `dynamic`；[绑定实现 L71](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L71)。
- `setInt(key: string, value: int)` → `dynamic`；[绑定实现 L68](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L68)。
- `setLive(enabled: bool)` → `dynamic`；[绑定实现 L108](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L108)。
- `setPointBudget(budget: int)` → `dynamic`；[绑定实现 L104](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L104)。
- `setString(key: string, value: string)` → `dynamic`；[绑定实现 L79](../../../../src/modules/procgen/editor/ProcgenScriptEditorScriptBindings.cpp#L79)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
