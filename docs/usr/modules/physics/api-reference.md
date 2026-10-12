# physics：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## Body

- `getLinearSpeed()` → `float`；[绑定实现 L498](../../../../src/modules/physics/Physics.cpp#L498)。
- `getWorldCenterY()` → `float`；[绑定实现 L501](../../../../src/modules/physics/Physics.cpp#L501)。
- `newRectangleFixtureAt(width: float, height: float, offsetX: float, offsetY: float, density: float, friction: float, restitution: float)` → `Fixture`；[绑定实现 L519](../../../../src/modules/physics/Physics.cpp#L519)。

## Body3D

- `getWorldCenterY()` → `float`；[绑定实现 L657](../../../../src/modules/physics/Physics.cpp#L657)。

## Fixture

- `getBodyId()` → `int`；[绑定实现 L845](../../../../src/modules/physics/Physics.cpp#L845)。
- `getCategoryBits()` → `int`；[绑定实现 L840](../../../../src/modules/physics/Physics.cpp#L840)。
- `getGroupIndex()` → `int`；[绑定实现 L844](../../../../src/modules/physics/Physics.cpp#L844)。
- `getMaskBits()` → `int`；[绑定实现 L842](../../../../src/modules/physics/Physics.cpp#L842)。
- `getTag()` → `string`；[绑定实现 L838](../../../../src/modules/physics/Physics.cpp#L838)。

## Shape3D

- `getCategoryBits()` → `dynamic`；[绑定实现 L985](../../../../src/modules/physics/Physics.cpp#L985)。
- `getGroupIndex()` → `int`；[绑定实现 L995](../../../../src/modules/physics/Physics.cpp#L995)。
- `getMaskBits()` → `dynamic`；[绑定实现 L991](../../../../src/modules/physics/Physics.cpp#L991)。
- `getTag()` → `int`；[绑定实现 L854](../../../../src/modules/physics/Physics.cpp#L854)。

## World

- `getBeginContactBodyAId(index: int)` → `int`；[绑定实现 L127](../../../../src/modules/physics/Physics.cpp#L127)。
- `getBeginContactBodyBId(index: int)` → `int`；[绑定实现 L128](../../../../src/modules/physics/Physics.cpp#L128)。
- `getBeginContactFixtureATag(index: int)` → `string`；[绑定实现 L129](../../../../src/modules/physics/Physics.cpp#L129)。
- `getBeginContactFixtureBTag(index: int)` → `string`；[绑定实现 L130](../../../../src/modules/physics/Physics.cpp#L130)。
- `getEndContactBodyAId(index: int)` → `int`；[绑定实现 L132](../../../../src/modules/physics/Physics.cpp#L132)。
- `getEndContactBodyBId(index: int)` → `int`；[绑定实现 L133](../../../../src/modules/physics/Physics.cpp#L133)。
- `getEndContactFixtureATag(index: int)` → `string`；[绑定实现 L134](../../../../src/modules/physics/Physics.cpp#L134)。
- `getEndContactFixtureBTag(index: int)` → `string`；[绑定实现 L135](../../../../src/modules/physics/Physics.cpp#L135)。
- `getImpactBodyAId(index: int)` → `int`；[绑定实现 L137](../../../../src/modules/physics/Physics.cpp#L137)。
- `getImpactBodyBId(index: int)` → `int`；[绑定实现 L138](../../../../src/modules/physics/Physics.cpp#L138)。
- `getImpactCount()` → `int`；[绑定实现 L136](../../../../src/modules/physics/Physics.cpp#L136)。
- `getImpactFixtureATag(index: int)` → `string`；[绑定实现 L139](../../../../src/modules/physics/Physics.cpp#L139)。
- `getImpactFixtureBTag(index: int)` → `string`；[绑定实现 L140](../../../../src/modules/physics/Physics.cpp#L140)。
- `getImpactNormalImpulse(index: int)` → `float`；[绑定实现 L146](../../../../src/modules/physics/Physics.cpp#L146)。
- `getImpactNormalX(index: int)` → `float`；[绑定实现 L143](../../../../src/modules/physics/Physics.cpp#L143)。
- `getImpactNormalY(index: int)` → `float`；[绑定实现 L144](../../../../src/modules/physics/Physics.cpp#L144)。
- `getImpactPointX(index: int)` → `float`；[绑定实现 L141](../../../../src/modules/physics/Physics.cpp#L141)。
- `getImpactPointY(index: int)` → `float`；[绑定实现 L142](../../../../src/modules/physics/Physics.cpp#L142)。
- `getImpactRelativeNormalSpeed(index: int)` → `float`；[绑定实现 L145](../../../../src/modules/physics/Physics.cpp#L145)。
- `getImpactTangentImpulse(index: int)` → `float`；[绑定实现 L147](../../../../src/modules/physics/Physics.cpp#L147)。

## World3D

- `getBeginContactBodyAId(index: int)` → `int`；[绑定实现 L327](../../../../src/modules/physics/Physics.cpp#L327)。
- `getBeginContactBodyBId(index: int)` → `int`；[绑定实现 L328](../../../../src/modules/physics/Physics.cpp#L328)。
- `getEndContactBodyAId(index: int)` → `int`；[绑定实现 L334](../../../../src/modules/physics/Physics.cpp#L334)。
- `getEndContactBodyBId(index: int)` → `int`；[绑定实现 L335](../../../../src/modules/physics/Physics.cpp#L335)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
