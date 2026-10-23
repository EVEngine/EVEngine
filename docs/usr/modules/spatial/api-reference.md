# spatial：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## BSPTree2D

- `getMaxY()` → `float`；[绑定实现 L138](../../../../src/modules/spatial/Spatial.cpp#L138)。
- `getMinY()` → `float`；[绑定实现 L136](../../../../src/modules/spatial/Spatial.cpp#L136)。

## BSPTree3D

- `getMaxY()` → `float`；[绑定实现 L160](../../../../src/modules/spatial/Spatial.cpp#L160)。
- `getMaxZ()` → `float`；[绑定实现 L161](../../../../src/modules/spatial/Spatial.cpp#L161)。
- `getMinY()` → `float`；[绑定实现 L157](../../../../src/modules/spatial/Spatial.cpp#L157)。
- `getMinZ()` → `float`；[绑定实现 L158](../../../../src/modules/spatial/Spatial.cpp#L158)。

## Octree

- `getMaxY()` → `float`；[绑定实现 L82](../../../../src/modules/spatial/Spatial.cpp#L82)。
- `getMaxZ()` → `float`；[绑定实现 L83](../../../../src/modules/spatial/Spatial.cpp#L83)。
- `getMinY()` → `float`；[绑定实现 L79](../../../../src/modules/spatial/Spatial.cpp#L79)。
- `getMinZ()` → `float`；[绑定实现 L80](../../../../src/modules/spatial/Spatial.cpp#L80)。

## QuadTree

- `getMaxY()` → `float`；[绑定实现 L61](../../../../src/modules/spatial/Spatial.cpp#L61)。
- `getMinY()` → `float`；[绑定实现 L59](../../../../src/modules/spatial/Spatial.cpp#L59)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
