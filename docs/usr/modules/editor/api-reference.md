# editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## EditorDock

- `getRegionSize(region: string)` → `float`；[绑定实现 L492](../../../../src/modules/editor/Editor.cpp#L492)。

## EditorInspector

- `addBool(id: string, label: string, value: bool)` → `void`；[绑定实现 L461](../../../../src/modules/editor/Editor.cpp#L461)。
- `addString(id: string, label: string, value: string)` → `void`；[绑定实现 L462](../../../../src/modules/editor/Editor.cpp#L462)。
- `clearAllDirty()` → `void`；[绑定实现 L486](../../../../src/modules/editor/Editor.cpp#L486)。
- `getBool(id: string)` → `bool`；[绑定实现 L477](../../../../src/modules/editor/Editor.cpp#L477)。
- `getChoice(id: string)` → `string`；[绑定实现 L481](../../../../src/modules/editor/Editor.cpp#L481)。
- `getChoicesCsv(id: string)` → `string`；[绑定实现 L483](../../../../src/modules/editor/Editor.cpp#L483)。
- `getFieldCount()` → `int`；[绑定实现 L464](../../../../src/modules/editor/Editor.cpp#L464)。
- `getFieldId(index: int)` → `string`；[绑定实现 L466](../../../../src/modules/editor/Editor.cpp#L466)。
- `getFieldKind(index: int)` → `string`；[绑定实现 L465](../../../../src/modules/editor/Editor.cpp#L465)。
- `getFieldLabel(index: int)` → `string`；[绑定实现 L467](../../../../src/modules/editor/Editor.cpp#L467)。
- `getFloat3X(id: string)` → `float`；[绑定实现 L473](../../../../src/modules/editor/Editor.cpp#L473)。
- `getFloat3Y(id: string)` → `float`；[绑定实现 L474](../../../../src/modules/editor/Editor.cpp#L474)。
- `getFloat3Z(id: string)` → `float`；[绑定实现 L475](../../../../src/modules/editor/Editor.cpp#L475)。
- `getFloatMax(id: string)` → `float`；[绑定实现 L471](../../../../src/modules/editor/Editor.cpp#L471)。
- `getFloatMin(id: string)` → `float`；[绑定实现 L470](../../../../src/modules/editor/Editor.cpp#L470)。
- `getFloatStep(id: string)` → `float`；[绑定实现 L472](../../../../src/modules/editor/Editor.cpp#L472)。
- `getString(id: string)` → `string`；[绑定实现 L479](../../../../src/modules/editor/Editor.cpp#L479)。
- `isDirty(id: string)` → `bool`；[绑定实现 L484](../../../../src/modules/editor/Editor.cpp#L484)。
- `pollChangedId()` → `string`；[绑定实现 L487](../../../../src/modules/editor/Editor.cpp#L487)。
- `setChoice(id: string, value: string)` → `void`；[绑定实现 L482](../../../../src/modules/editor/Editor.cpp#L482)。
- `setFloat3(id: string, x: float, y: float, z: float)` → `void`；[绑定实现 L476](../../../../src/modules/editor/Editor.cpp#L476)。

## GizmoManager

- `getBoundEnabled()` → `bool`；[绑定实现 L430](../../../../src/modules/editor/Editor.cpp#L430)。
- `getPositionEnabled()` → `bool`；[绑定实现 L427](../../../../src/modules/editor/Editor.cpp#L427)。
- `getRotationEnabled()` → `bool`；[绑定实现 L428](../../../../src/modules/editor/Editor.cpp#L428)。
- `getScaleEnabled()` → `bool`；[绑定实现 L429](../../../../src/modules/editor/Editor.cpp#L429)。
- `isAttached()` → `bool`；[绑定实现 L433](../../../../src/modules/editor/Editor.cpp#L433)。
- `isHovered()` → `bool`；[绑定实现 L439](../../../../src/modules/editor/Editor.cpp#L439)。
- `setBoundEnabled(enabled: bool)` → `void`；[绑定实现 L426](../../../../src/modules/editor/Editor.cpp#L426)。

## TransformGizmo

- `getActiveAxis()` → `string`；[绑定实现 L401](../../../../src/modules/editor/Editor.cpp#L401)。
- `getBoundsMaxX()` → `float`；[绑定实现 L383](../../../../src/modules/editor/Editor.cpp#L383)。
- `getBoundsMaxY()` → `float`；[绑定实现 L384](../../../../src/modules/editor/Editor.cpp#L384)。
- `getBoundsMaxZ()` → `float`；[绑定实现 L385](../../../../src/modules/editor/Editor.cpp#L385)。
- `getBoundsMinX()` → `float`；[绑定实现 L380](../../../../src/modules/editor/Editor.cpp#L380)。
- `getBoundsMinY()` → `float`；[绑定实现 L381](../../../../src/modules/editor/Editor.cpp#L381)。
- `getBoundsMinZ()` → `float`；[绑定实现 L382](../../../../src/modules/editor/Editor.cpp#L382)。
- `getHoverAxis()` → `string`；[绑定实现 L402](../../../../src/modules/editor/Editor.cpp#L402)。
- `getMatrix(index: int)` → `float`；[绑定实现 L394](../../../../src/modules/editor/Editor.cpp#L394)。
- `getPartAxis(index: int)` → `string`；[绑定实现 L406](../../../../src/modules/editor/Editor.cpp#L406)。
- `getPartColorA(index: int)` → `float`；[绑定实现 L410](../../../../src/modules/editor/Editor.cpp#L410)。
- `getPartColorB(index: int)` → `float`；[绑定实现 L409](../../../../src/modules/editor/Editor.cpp#L409)。
- `getPartColorG(index: int)` → `float`；[绑定实现 L408](../../../../src/modules/editor/Editor.cpp#L408)。
- `getPartColorR(index: int)` → `float`；[绑定实现 L407](../../../../src/modules/editor/Editor.cpp#L407)。
- `getPartDirX(index: int)` → `float`；[绑定实现 L414](../../../../src/modules/editor/Editor.cpp#L414)。
- `getPartDirY(index: int)` → `float`；[绑定实现 L415](../../../../src/modules/editor/Editor.cpp#L415)。
- `getPartDirZ(index: int)` → `float`；[绑定实现 L416](../../../../src/modules/editor/Editor.cpp#L416)。
- `getPartLength(index: int)` → `float`；[绑定实现 L417](../../../../src/modules/editor/Editor.cpp#L417)。
- `getPartOriginX(index: int)` → `float`；[绑定实现 L411](../../../../src/modules/editor/Editor.cpp#L411)。
- `getPartOriginY(index: int)` → `float`；[绑定实现 L412](../../../../src/modules/editor/Editor.cpp#L412)。
- `getPartOriginZ(index: int)` → `float`；[绑定实现 L413](../../../../src/modules/editor/Editor.cpp#L413)。
- `getPartRadius(index: int)` → `float`；[绑定实现 L418](../../../../src/modules/editor/Editor.cpp#L418)。
- `getPositionX()` → `float`；[绑定实现 L368](../../../../src/modules/editor/Editor.cpp#L368)。
- `getPositionY()` → `float`；[绑定实现 L369](../../../../src/modules/editor/Editor.cpp#L369)。
- `getPositionZ()` → `float`；[绑定实现 L370](../../../../src/modules/editor/Editor.cpp#L370)。
- `getRotationX()` → `float`；[绑定实现 L372](../../../../src/modules/editor/Editor.cpp#L372)。
- `getRotationY()` → `float`；[绑定实现 L373](../../../../src/modules/editor/Editor.cpp#L373)。
- `getRotationZ()` → `float`；[绑定实现 L374](../../../../src/modules/editor/Editor.cpp#L374)。
- `getScaleX()` → `float`；[绑定实现 L376](../../../../src/modules/editor/Editor.cpp#L376)。
- `getScaleY()` → `float`；[绑定实现 L377](../../../../src/modules/editor/Editor.cpp#L377)。
- `getScaleZ()` → `float`；[绑定实现 L378](../../../../src/modules/editor/Editor.cpp#L378)。
- `getSize()` → `float`；[绑定实现 L366](../../../../src/modules/editor/Editor.cpp#L366)。
- `getSnapRotate()` → `float`；[绑定实现 L392](../../../../src/modules/editor/Editor.cpp#L392)。
- `getSnapScale()` → `float`；[绑定实现 L393](../../../../src/modules/editor/Editor.cpp#L393)。
- `getSnapTranslateX()` → `float`；[绑定实现 L389](../../../../src/modules/editor/Editor.cpp#L389)。
- `getSnapTranslateY()` → `float`；[绑定实现 L390](../../../../src/modules/editor/Editor.cpp#L390)。
- `getSnapTranslateZ()` → `float`；[绑定实现 L391](../../../../src/modules/editor/Editor.cpp#L391)。
- `getSpace()` → `string`；[绑定实现 L364](../../../../src/modules/editor/Editor.cpp#L364)。
- `isHovered()` → `bool`；[绑定实现 L400](../../../../src/modules/editor/Editor.cpp#L400)。
- `setSnapRotate(degrees: float)` → `void`；[绑定实现 L387](../../../../src/modules/editor/Editor.cpp#L387)。
- `setSnapScale(s: float)` → `void`；[绑定实现 L388](../../../../src/modules/editor/Editor.cpp#L388)。
- `setSnapTranslate(x: float, y: float, z: float)` → `void`；[绑定实现 L386](../../../../src/modules/editor/Editor.cpp#L386)。

## 接收者见绑定源码

- `getName()` → `string`；[绑定实现 L797](../../../../src/modules/editor/Editor.cpp#L797)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
