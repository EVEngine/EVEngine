# transaction：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## TransactionEvent

- `getDetail()` → `dynamic`；[绑定实现 L789](../../../../src/modules/transaction/Transaction.cpp#L789)。
- `getOperationId()` → `dynamic`；[绑定实现 L787](../../../../src/modules/transaction/Transaction.cpp#L787)。
- `getSequence()` → `dynamic`；[绑定实现 L785](../../../../src/modules/transaction/Transaction.cpp#L785)。
- `getTransactionId()` → `dynamic`；[绑定实现 L786](../../../../src/modules/transaction/Transaction.cpp#L786)。
- `getType()` → `dynamic`；[绑定实现 L788](../../../../src/modules/transaction/Transaction.cpp#L788)。

## TransactionLedger

- `count()` → `int`；[绑定实现 L882](../../../../src/modules/transaction/Transaction.cpp#L882)。

## TransactionOperation

- `getError()` → `dynamic`；[绑定实现 L782](../../../../src/modules/transaction/Transaction.cpp#L782)。
- `getId()` → `dynamic`；[绑定实现 L776](../../../../src/modules/transaction/Transaction.cpp#L776)。
- `getPayload()` → `dynamic`；[绑定实现 L779](../../../../src/modules/transaction/Transaction.cpp#L779)。
- `getTarget()` → `dynamic`；[绑定实现 L778](../../../../src/modules/transaction/Transaction.cpp#L778)。
- `isChecked()` → `dynamic`；[绑定实现 L780](../../../../src/modules/transaction/Transaction.cpp#L780)。
- `isValid()` → `dynamic`；[绑定实现 L781](../../../../src/modules/transaction/Transaction.cpp#L781)。

## TransactionPlan

- `eventAt(i: int)` → `Event`；[绑定实现 L849](../../../../src/modules/transaction/Transaction.cpp#L849)。
- `eventCount()` → `int`；[绑定实现 L848](../../../../src/modules/transaction/Transaction.cpp#L848)。
- `fail(error: string)` → `dynamic`；[绑定实现 L834](../../../../src/modules/transaction/Transaction.cpp#L834)。
- `findOperation(id: string)` → `Operation`；[绑定实现 L845](../../../../src/modules/transaction/Transaction.cpp#L845)。
- `getCausation()` → `string`；[绑定实现 L795](../../../../src/modules/transaction/Transaction.cpp#L795)。
- `getCorrelation()` → `string`；[绑定实现 L794](../../../../src/modules/transaction/Transaction.cpp#L794)。
- `getError()` → `string`；[绑定实现 L841](../../../../src/modules/transaction/Transaction.cpp#L841)。
- `getId()` → `string`；[绑定实现 L792](../../../../src/modules/transaction/Transaction.cpp#L792)。
- `markInvalid(id: string, error: string)` → `dynamic`；[绑定实现 L806](../../../../src/modules/transaction/Transaction.cpp#L806)。
- `operationCount()` → `int`；[绑定实现 L842](../../../../src/modules/transaction/Transaction.cpp#L842)。

## 接收者见绑定源码

- `getName()` → `string`；[绑定实现 L922](../../../../src/modules/transaction/Transaction.cpp#L922)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
