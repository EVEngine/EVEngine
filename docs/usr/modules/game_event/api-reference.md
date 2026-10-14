# game_event：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## GameEventLog

- `querySequence(sequence: int)` → `dynamic`；[绑定实现 L703](../../../../src/modules/game_event/GameEvent.cpp#L703)。
- `querySource(source: string)` → `int`；[绑定实现 L701](../../../../src/modules/game_event/GameEvent.cpp#L701)。
- `querySubject(subject: string)` → `int`；[绑定实现 L702](../../../../src/modules/game_event/GameEvent.cpp#L702)。
- `reset()` → `void`；[绑定实现 L720](../../../../src/modules/game_event/GameEvent.cpp#L720)。

## GameEventRecord

- `getCausation()` → `dynamic`；[绑定实现 L613](../../../../src/modules/game_event/GameEvent.cpp#L613)。
- `getCausationKind()` → `dynamic`；[绑定实现 L614](../../../../src/modules/game_event/GameEvent.cpp#L614)。
- `getCorrelation()` → `dynamic`；[绑定实现 L617](../../../../src/modules/game_event/GameEvent.cpp#L617)。
- `getCorrelationKind()` → `dynamic`；[绑定实现 L618](../../../../src/modules/game_event/GameEvent.cpp#L618)。
- `getEventId()` → `dynamic`；[绑定实现 L607](../../../../src/modules/game_event/GameEvent.cpp#L607)。
- `getFlags()` → `dynamic`；[绑定实现 L625](../../../../src/modules/game_event/GameEvent.cpp#L625)。
- `getPayload()` → `dynamic`；[绑定实现 L626](../../../../src/modules/game_event/GameEvent.cpp#L626)。
- `getSchemaId()` → `dynamic`；[绑定实现 L621](../../../../src/modules/game_event/GameEvent.cpp#L621)。
- `getSchemaVersion()` → `dynamic`；[绑定实现 L622](../../../../src/modules/game_event/GameEvent.cpp#L622)。
- `getSequence()` → `dynamic`；[绑定实现 L608](../../../../src/modules/game_event/GameEvent.cpp#L608)。
- `getSource()` → `dynamic`；[绑定实现 L611](../../../../src/modules/game_event/GameEvent.cpp#L611)。

## 接收者见绑定源码

- `getName()` → `string`；[绑定实现 L735](../../../../src/modules/game_event/GameEvent.cpp#L735)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
