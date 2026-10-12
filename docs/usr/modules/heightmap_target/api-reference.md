# heightmap_target：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## HeightmapTarget

- `clearDirtyRegion()` → `void`；[绑定实现 L98](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L98)。
- `getHeight()` → `int`；[绑定实现 L94](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L94)。
- `getTargetId()` → `dynamic`；[绑定实现 L87](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L87)。
- `getWidth()` → `int`；[绑定实现 L93](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L93)。
- `sampleScalar(x: float, y: float)` → `float`；[绑定实现 L97](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L97)。

## 接收者见绑定源码

- `applyBrush(heightmap: Heightmap, centerX: float, centerY: float, radius: float, strength: float)` → `int`；[绑定实现 L109](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L109)。
- `bind(session: EditorSession, target: HeightmapTarget)` → `void`；[绑定实现 L108](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L108)。
- `decodeDocument()` → `dynamic`；[绑定实现 L125](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L125)。
- `encodeDocument()` → `dynamic`；[绑定实现 L114](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L114)。
- `newMesh(heightmap: Heightmap, cellSize: float, heightScale: float)` → `Mesh`；[绑定实现 L110](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L110)。
- `newSmoothMesh(heightmap: Heightmap, cellSize: float, heightScale: float)` → `Mesh`；[绑定实现 L112](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L112)。
- `updateMesh(mesh: Mesh, graphics: Graphics, heightmap: Heightmap, cellSize: float, heightScale: float)` → `bool`；[绑定实现 L111](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L111)。
- `updateSmoothMesh(mesh: Mesh, graphics: Graphics, heightmap: Heightmap, cellSize: float, heightScale: float)` → `bool`；[绑定实现 L113](../../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp#L113)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
