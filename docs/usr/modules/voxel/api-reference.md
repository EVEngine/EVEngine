# voxel：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## VoxelWorld

- `getCubeTypeName(wx: int, wy: int, wz: int)` → `string`；[绑定实现 L156](../../../../src/modules/voxel/Voxel.cpp#L156)。
- `getCubeTypeTex(wx: int, wy: int, wz: int, faceDir: string)` → `int`；[绑定实现 L157](../../../../src/modules/voxel/Voxel.cpp#L157)。
- `getDirtyCount()` → `int`；[绑定实现 L138](../../../../src/modules/voxel/Voxel.cpp#L138)。
- `getRaycastPrevY()` → `int`；[绑定实现 L170](../../../../src/modules/voxel/Voxel.cpp#L170)。
- `getRaycastPrevZ()` → `int`；[绑定实现 L171](../../../../src/modules/voxel/Voxel.cpp#L171)。
- `getVisibleBatchCount()` → `int`；[绑定实现 L175](../../../../src/modules/voxel/Voxel.cpp#L175)。
- `getVisibleChunkCount()` → `int`；[绑定实现 L176](../../../../src/modules/voxel/Voxel.cpp#L176)。
- `terrainHeightAt(wx: int, wz: int)` → `int`；[绑定实现 L110](../../../../src/modules/voxel/Voxel.cpp#L110)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
