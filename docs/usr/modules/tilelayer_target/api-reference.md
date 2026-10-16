# tilelayer_target：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## TileLayerTarget

- `getHeight()` → `int`；[绑定实现 L36](../../../../src/modules/map/tilelayer_target/TileLayerTargetModule.cpp#L36)。
- `getTargetId()` → `dynamic`；[绑定实现 L30](../../../../src/modules/map/tilelayer_target/TileLayerTargetModule.cpp#L30)。
- `getWidth()` → `int`；[绑定实现 L35](../../../../src/modules/map/tilelayer_target/TileLayerTargetModule.cpp#L35)。

## 接收者见绑定源码

- `bind(session: EditorSession, target: TileLayerTarget)` → `void`；[绑定实现 L49](../../../../src/modules/map/tilelayer_target/TileLayerTargetModule.cpp#L49)。
- `create(id: string, layer: TileLayer)` → `dynamic`；[绑定实现 L46](../../../../src/modules/map/tilelayer_target/TileLayerTargetModule.cpp#L46)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
