# voxel_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `deleteSelectedModel()` → `dynamic`；[绑定实现 L74](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L74)。
- `getJoinPartnerCount()` → `dynamic`；[绑定实现 L145](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L145)。
- `getJoinPartnerId(index: int)` → `dynamic`；[绑定实现 L148](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L148)。
- `getModelCount()` → `dynamic`；[绑定实现 L88](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L88)。
- `getModelFill(index: int)` → `dynamic`；[绑定实现 L97](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L97)。
- `getModelId(index: int)` → `dynamic`；[绑定实现 L91](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L91)。
- `getModelName(index: int)` → `dynamic`；[绑定实现 L94](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L94)。
- `getModelSelected(index: int)` → `dynamic`；[绑定实现 L100](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L100)。
- `getModelSizeX()` → `dynamic`；[绑定实现 L115](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L115)。
- `getModelSizeY()` → `dynamic`；[绑定实现 L118](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L118)。
- `getModelSizeZ()` → `dynamic`；[绑定实现 L121](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L121)。
- `getScreenVoxelCount()` → `dynamic`；[绑定实现 L124](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L124)。
- `getScreenVoxelH(index: int)` → `dynamic`；[绑定实现 L136](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L136)。
- `getScreenVoxelW(index: int)` → `dynamic`；[绑定实现 L133](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L133)。
- `getScreenVoxelX(index: int)` → `dynamic`；[绑定实现 L127](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L127)。
- `getScreenVoxelY(index: int)` → `dynamic`；[绑定实现 L130](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L130)。
- `getSelectedFace()` → `dynamic`；[绑定实现 L85](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L85)。
- `getSelectedId()` → `dynamic`；[绑定实现 L79](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L79)。
- `getSelectedSocketKind()` → `dynamic`；[绑定实现 L142](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L142)。
- `getSelectedSocketTag()` → `dynamic`；[绑定实现 L139](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L139)。
- `getTool()` → `dynamic`；[绑定实现 L82](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L82)。
- `getVoxelCount()` → `dynamic`；[绑定实现 L103](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L103)。
- `getVoxelX(index: int)` → `dynamic`；[绑定实现 L106](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L106)。
- `getVoxelY(index: int)` → `dynamic`；[绑定实现 L109](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L109)。
- `getVoxelZ(index: int)` → `dynamic`；[绑定实现 L112](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L112)。
- `orbit(yaw: float, pitch: float)` → `dynamic`；[绑定实现 L46](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L46)。
- `pointerDown(x: float, y: float)` → `dynamic`；[绑定实现 L49](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L49)。
- `pointerWorldRay(ox: float, oy: float, oz: float, dx: float, dy: float, dz: float)` → `dynamic`；[绑定实现 L52](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L52)。
- `selectFace(face: int)` → `dynamic`；[绑定实现 L66](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L66)。
- `selectModel(id: string)` → `dynamic`；[绑定实现 L36](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L36)。
- `setSelectedSocket(tag: string, kind: string)` → `dynamic`；[绑定实现 L61](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L61)。
- `setTool(tool: string)` → `dynamic`；[绑定实现 L39](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L39)。
- `setViewport(width: float, height: float)` → `dynamic`；[绑定实现 L42](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L42)。
- `setVoxel(x: int, y: int, z: int, occupied: bool)` → `dynamic`；[绑定实现 L57](../../../../src/modules/voxel/editor/VoxelCatalogEditorScriptBindings.cpp#L57)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
