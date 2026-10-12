# map：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## Fov

- `getBlockEmpty()` → `bool`；[绑定实现 L516](../../../../src/modules/map/Map.cpp#L516)。
- `getCliffBlock()` → `float`；[绑定实现 L525](../../../../src/modules/map/Map.cpp#L525)。
- `getCornerPeek()` → `bool`；[绑定实现 L511](../../../../src/modules/map/Map.cpp#L511)。
- `getDetectionMargin()` → `float`；[绑定实现 L546](../../../../src/modules/map/Map.cpp#L546)。
- `getEyeOffset()` → `float`；[绑定实现 L527](../../../../src/modules/map/Map.cpp#L527)。
- `getPerceptionRadiusScale()` → `float`；[绑定实现 L544](../../../../src/modules/map/Map.cpp#L544)。
- `getVerticalRange()` → `int`；[绑定实现 L529](../../../../src/modules/map/Map.cpp#L529)。

## Pathfinder

- `getBlockEmpty()` → `bool`；[绑定实现 L485](../../../../src/modules/map/Map.cpp#L485)。
- `getDiagonal()` → `bool`；[绑定实现 L480](../../../../src/modules/map/Map.cpp#L480)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
