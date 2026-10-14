# voxelworld_target：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## VoxelWorldTarget

- `getDirtyMaxX()` → `dynamic`；[绑定实现 L53](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L53)。
- `getDirtyMaxY()` → `dynamic`；[绑定实现 L56](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L56)。
- `getDirtyMaxZ()` → `dynamic`；[绑定实现 L59](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L59)。
- `getDirtyMinX()` → `dynamic`；[绑定实现 L44](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L44)。
- `getDirtyMinY()` → `dynamic`；[绑定实现 L47](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L47)。
- `getDirtyMinZ()` → `dynamic`；[绑定实现 L50](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L50)。
- `getRevision()` → `dynamic`；[绑定实现 L36](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L36)。
- `getTargetId()` → `dynamic`；[绑定实现 L33](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L33)。

## 接收者见绑定源码

- `bind(session: EditorSession, target: VoxelWorldTarget)` → `void`；[绑定实现 L72](../../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp#L72)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
