# card：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## CardData

- `getTintB()` → `float`；[绑定实现 L793](../../../../src/modules/card/Card.cpp#L793)。
- `getTintG()` → `float`；[绑定实现 L792](../../../../src/modules/card/Card.cpp#L792)。

## LayoutConfig

- `getArcHeight()` → `float`；[绑定实现 L699](../../../../src/modules/card/Card.cpp#L699)。
- `getCardH()` → `float`；[绑定实现 L691](../../../../src/modules/card/Card.cpp#L691)。
- `getCardW()` → `float`；[绑定实现 L689](../../../../src/modules/card/Card.cpp#L689)。
- `getDeckX()` → `float`；[绑定实现 L717](../../../../src/modules/card/Card.cpp#L717)。
- `getDeckY()` → `float`；[绑定实现 L719](../../../../src/modules/card/Card.cpp#L719)。
- `getDisabledAlpha()` → `float`；[绑定实现 L713](../../../../src/modules/card/Card.cpp#L713)。
- `getDragThreshold()` → `float`；[绑定实现 L721](../../../../src/modules/card/Card.cpp#L721)。
- `getHandX()` → `float`；[绑定实现 L695](../../../../src/modules/card/Card.cpp#L695)。
- `getHandY()` → `float`；[绑定实现 L697](../../../../src/modules/card/Card.cpp#L697)。
- `getHoverLift()` → `float`；[绑定实现 L707](../../../../src/modules/card/Card.cpp#L707)。
- `getHoverRotation()` → `bool`；[绑定实现 L703](../../../../src/modules/card/Card.cpp#L703)。
- `getHoverScale()` → `float`；[绑定实现 L705](../../../../src/modules/card/Card.cpp#L705)。
- `getHoverSpeed()` → `float`；[绑定实现 L709](../../../../src/modules/card/Card.cpp#L709)。
- `getMotionSpeed()` → `float`；[绑定实现 L711](../../../../src/modules/card/Card.cpp#L711)。
- `getRotationAngle()` → `float`；[绑定实现 L701](../../../../src/modules/card/Card.cpp#L701)。
- `getShowZones()` → `bool`；[绑定实现 L715](../../../../src/modules/card/Card.cpp#L715)。
- `getSpacing()` → `float`；[绑定实现 L693](../../../../src/modules/card/Card.cpp#L693)。

## Zone

- `getColorB()` → `float`；[绑定实现 L843](../../../../src/modules/card/Card.cpp#L843)。
- `getColorG()` → `float`；[绑定实现 L842](../../../../src/modules/card/Card.cpp#L842)。

## 接收者见绑定源码

- `getCardDefinitionAttack(id: string)` → `int`；[绑定实现 L914](../../../../src/modules/card/Card.cpp#L914)。
- `getCardDefinitionCost(id: string)` → `int`；[绑定实现 L913](../../../../src/modules/card/Card.cpp#L913)。
- `getCardDefinitionHealth(id: string)` → `int`；[绑定实现 L915](../../../../src/modules/card/Card.cpp#L915)。
- `getCardDefinitionKind(id: string)` → `string`；[绑定实现 L912](../../../../src/modules/card/Card.cpp#L912)。
- `getCardDefinitionTintB(id: string)` → `float`；[绑定实现 L918](../../../../src/modules/card/Card.cpp#L918)。
- `getCardDefinitionTintG(id: string)` → `float`；[绑定实现 L917](../../../../src/modules/card/Card.cpp#L917)。
- `getCardDefinitionTintR(id: string)` → `float`；[绑定实现 L916](../../../../src/modules/card/Card.cpp#L916)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
