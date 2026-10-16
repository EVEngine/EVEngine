# biome_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `addExclusion(spatialAsset: string)` → `dynamic`；[绑定实现 L72](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L72)。
- `createAsset(id: string, asset: string)` → `dynamic`；[绑定实现 L63](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L63)。
- `createLayer(id: string, name: string)` → `dynamic`；[绑定实现 L54](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L54)。
- `deleteSelectedAsset()` → `dynamic`；[绑定实现 L68](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L68)。
- `deleteSelectedLayer()` → `dynamic`；[绑定实现 L59](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L59)。
- `getAssetCount()` → `dynamic`；[绑定实现 L121](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L121)。
- `getAssetId(index: int)` → `dynamic`；[绑定实现 L124](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L124)。
- `getAssetRef(index: int)` → `dynamic`；[绑定实现 L127](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L127)。
- `getAssetSelected(index: int)` → `dynamic`；[绑定实现 L133](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L133)。
- `getAssetWeight(index: int)` → `dynamic`；[绑定实现 L130](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L130)。
- `getExclusionAsset(index: int)` → `dynamic`；[绑定实现 L139](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L139)。
- `getExclusionCount()` → `dynamic`；[绑定实现 L136](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L136)。
- `getLayerCount()` → `dynamic`；[绑定实现 L103](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L103)。
- `getLayerDensity(index: int)` → `dynamic`；[绑定实现 L112](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L112)。
- `getLayerId(index: int)` → `dynamic`；[绑定实现 L106](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L106)。
- `getLayerName(index: int)` → `dynamic`；[绑定实现 L109](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L109)。
- `getLayerPriority(index: int)` → `dynamic`；[绑定实现 L115](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L115)。
- `getLayerSelected(index: int)` → `dynamic`；[绑定实现 L118](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L118)。
- `getPointAsset(index: int)` → `dynamic`；[绑定实现 L151](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L151)。
- `getSeed()` → `dynamic`；[绑定实现 L91](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L91)。
- `getSelectedId()` → `dynamic`；[绑定实现 L97](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L97)。
- `getSelectedType()` → `dynamic`；[绑定实现 L100](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L100)。
- `getSpacing()` → `dynamic`；[绑定实现 L94](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L94)。
- `removeExclusion(spatialAsset: string)` → `dynamic`；[绑定实现 L76](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L76)。
- `selectAsset(id: string)` → `dynamic`；[绑定实现 L39](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L39)。
- `selectLayer(id: string)` → `dynamic`；[绑定实现 L36](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L36)。
- `setAssetWeight(weight: float)` → `dynamic`；[绑定实现 L50](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L50)。
- `setLayerDensity(density: float)` → `dynamic`；[绑定实现 L42](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L42)。
- `setLayerPriority(priority: int)` → `dynamic`；[绑定实现 L46](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L46)。
- `setSeed(seed: int)` → `dynamic`；[绑定实现 L80](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L80)。
- `setSpacing(spacing: float)` → `dynamic`；[绑定实现 L84](../../../../src/modules/procgen/biome/editor/BiomeRulesEditorScriptBindings.cpp#L84)。

- `getPointCount`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `getPointX`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `getPointZ`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `getPreviewRevision`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。

重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
