# audio_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `getBool(path: string)` → `dynamic`；[绑定实现 L128](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L128)。
- `getBucketCount()` → `dynamic`；[绑定实现 L105](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L105)。
- `getBucketMax(index: int)` → `dynamic`；[绑定实现 L113](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L113)。
- `getBucketMin(index: int)` → `dynamic`；[绑定实现 L108](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L108)。
- `getDuration()` → `dynamic`；[绑定实现 L87](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L87)。
- `getFloat(path: string)` → `dynamic`；[绑定实现 L121](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L121)。
- `getLayoutWidth()` → `dynamic`；[绑定实现 L93](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L93)。
- `getLoopEndX()` → `dynamic`；[绑定实现 L102](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L102)。
- `getLoopStartX()` → `dynamic`；[绑定实现 L99](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L99)。
- `getPlayhead()` → `dynamic`；[绑定实现 L90](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L90)。
- `getPlayheadX()` → `dynamic`；[绑定实现 L96](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L96)。
- `getString(path: string)` → `dynamic`；[绑定实现 L134](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L134)。
- `getTransportState()` → `dynamic`；[绑定实现 L118](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L118)。
- `isPlaying()` → `dynamic`；[绑定实现 L86](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L86)。
- `seekSeconds(seconds: float)` → `dynamic`；[绑定实现 L53](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L53)。
- `seekX(x: float)` → `dynamic`；[绑定实现 L50](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L50)。
- `setBool(path: string, value: bool)` → `dynamic`；[绑定实现 L62](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L62)。
- `setFloat(path: string, value: float)` → `dynamic`；[绑定实现 L57](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L57)。
- `setViewportWidth(width: float)` → `dynamic`；[绑定实现 L46](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L46)。
- `update(deltaSeconds: float)` → `dynamic`；[绑定实现 L77](../../../../src/modules/audio/editor/AudioSourceEditorScriptBindings.cpp#L77)。

- `pause`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `play`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。
- `stop`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。

重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
