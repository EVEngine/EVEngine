# weather：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `getFogColorB()` → `float`；[绑定实现 L800](../../../../src/modules/weather/Weather.cpp#L800)。
- `getFogColorG()` → `float`；[绑定实现 L799](../../../../src/modules/weather/Weather.cpp#L799)。
- `getFogColorR()` → `float`；[绑定实现 L798](../../../../src/modules/weather/Weather.cpp#L798)。
- `getFogDensity()` → `float`；[绑定实现 L802](../../../../src/modules/weather/Weather.cpp#L802)。
- `getName()` → `string`；[绑定实现 L746](../../../../src/modules/weather/Weather.cpp#L746)。
- `getPreset()` → `string`；[绑定实现 L750](../../../../src/modules/weather/Weather.cpp#L750)。
- `getSkyColorB()` → `float`；[绑定实现 L794](../../../../src/modules/weather/Weather.cpp#L794)。
- `getSkyColorG()` → `float`；[绑定实现 L793](../../../../src/modules/weather/Weather.cpp#L793)。
- `getSkyColorR()` → `float`；[绑定实现 L792](../../../../src/modules/weather/Weather.cpp#L792)。
- `getSunIntensity()` → `float`；[绑定实现 L796](../../../../src/modules/weather/Weather.cpp#L796)。
- `getWindDirection()` → `float`；[绑定实现 L756](../../../../src/modules/weather/Weather.cpp#L756)。
- `init(gfx: Graphics)` → `void`；[绑定实现 L747](../../../../src/modules/weather/Weather.cpp#L747)。
- `isLightningEnabled()` → `bool`；[绑定实现 L788](../../../../src/modules/weather/Weather.cpp#L788)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
