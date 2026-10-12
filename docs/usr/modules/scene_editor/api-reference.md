# scene_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## SceneEditorSession

- `cachePhysicsPlacementCompound(object: string, resourceKey: string, parts: Array<dynamic>)` → `dynamic`；[绑定实现 L434](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L434)。
- `cachePhysicsPlacementHull(object: string, vertices: Array<dynamic>, maxVertices: int)` → `dynamic`；[绑定实现 L426](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L426)。
- `execute(command: string, payload: dynamic)` → `dynamic`；[绑定实现 L404](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L404)。
- `getRevision()` → `Revision`；[绑定实现 L425](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L425)。
- `isPhysicsPlacementActive()` → `dynamic`；[绑定实现 L515](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L515)。
- `redo()` → `dynamic`；[绑定实现 L416](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L416)。
- `removePhysicsPlacementHull(object: string)` → `dynamic`；[绑定实现 L457](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L457)。
- `restoreJson(json: string)` → `dynamic`；[绑定实现 L418](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L418)。
- `restorePhysicsPlacementColliderCacheJson(json: string)` → `dynamic`；[绑定实现 L453](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L453)。
- `restrictCommands(commands: Array<dynamic>)` → `dynamic`；[绑定实现 L410](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L410)。
- `saveJson()` → `string`；[绑定实现 L417](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L417)。
- `savePhysicsPlacementColliderCacheJson()` → `string`；[绑定实现 L452](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L452)。
- `snapshot()` → `dynamic`；[绑定实现 L421](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L421)。
- `undo()` → `dynamic`；[绑定实现 L409](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L409)。
- `updatePhysicsPlacement(x: float, y: float, z: float, dt: float)` → `dynamic`；[绑定实现 L469](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L469)。
- `updatePhysicsPlacementPose(x: float, y: float, z: float, rotationX: float, rotationY: float, rotationZ: float, dt: float)` → `dynamic`；[绑定实现 L475](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L475)。
- `updatePhysicsPlacementTransform(position: Array<dynamic>, rotation: Array<dynamic>, scale: Array<dynamic>, dt: float)` → `dynamic`；[绑定实现 L482](../../../../src/modules/scene/editor/SceneEditorScriptBindings.cpp#L482)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
