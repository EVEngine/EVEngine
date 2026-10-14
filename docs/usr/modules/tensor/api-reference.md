# tensor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## CompiledFunction

- `getPlaceholderCount()` → `int`；[绑定实现 L939](../../../../src/modules/tensor/TF.cpp#L939)。
- `run0()` → `Tensor`；[绑定实现 L932](../../../../src/modules/tensor/TF.cpp#L932)。
- `run2(in0: Tensor, in1: Tensor)` → `Tensor`；[绑定实现 L934](../../../../src/modules/tensor/TF.cpp#L934)。
- `run3(in0: Tensor, in1: Tensor, in2: Tensor)` → `Tensor`；[绑定实现 L935](../../../../src/modules/tensor/TF.cpp#L935)。
- `run4(in0: Tensor, in1: Tensor, in2: Tensor, in3: Tensor)` → `Tensor`；[绑定实现 L936](../../../../src/modules/tensor/TF.cpp#L936)。
- `run5(in0: Tensor, in1: Tensor, in2: Tensor, in3: Tensor, in4: Tensor)` → `Tensor`；[绑定实现 L937](../../../../src/modules/tensor/TF.cpp#L937)。
- `run6(in0: Tensor, in1: Tensor, in2: Tensor, in3: Tensor, in4: Tensor, in5: Tensor)` → `Tensor`；[绑定实现 L938](../../../../src/modules/tensor/TF.cpp#L938)。

## Func

- `input2(d0: int, d1: int)` → `Tensor`；[绑定实现 L921](../../../../src/modules/tensor/TF.cpp#L921)。
- `input3(d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L922](../../../../src/modules/tensor/TF.cpp#L922)。
- `input4(d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L923](../../../../src/modules/tensor/TF.cpp#L923)。
- `input5(d0: int, d1: int, d2: int, d3: int, d4: int)` → `Tensor`；[绑定实现 L924](../../../../src/modules/tensor/TF.cpp#L924)。
- `input6(d0: int, d1: int, d2: int, d3: int, d4: int, d5: int)` → `Tensor`；[绑定实现 L925](../../../../src/modules/tensor/TF.cpp#L925)。

## Tensor

- `abs()` → `Tensor`；[绑定实现 L880](../../../../src/modules/tensor/TF.cpp#L880)。
- `addInPlace(other: Tensor)` → `void`；[绑定实现 L896](../../../../src/modules/tensor/TF.cpp#L896)。
- `addScalarInPlace(s: float)` → `void`；[绑定实现 L898](../../../../src/modules/tensor/TF.cpp#L898)。
- `clone()` → `Tensor`；[绑定实现 L869](../../../../src/modules/tensor/TF.cpp#L869)。
- `copyFrom(other: Tensor)` → `void`；[绑定实现 L868](../../../../src/modules/tensor/TF.cpp#L868)。
- `cos()` → `Tensor`；[绑定实现 L885](../../../../src/modules/tensor/TF.cpp#L885)。
- `div(other: Tensor)` → `Tensor`；[绑定实现 L874](../../../../src/modules/tensor/TF.cpp#L874)。
- `divScalar(s: float)` → `Tensor`；[绑定实现 L878](../../../../src/modules/tensor/TF.cpp#L878)。
- `dot(other: Tensor)` → `float`；[绑定实现 L906](../../../../src/modules/tensor/TF.cpp#L906)。
- `flatten()` → `Tensor`；[绑定实现 L916](../../../../src/modules/tensor/TF.cpp#L916)。
- `get1(i0: int)` → `float`；[绑定实现 L854](../../../../src/modules/tensor/TF.cpp#L854)。
- `get3(i0: int, i1: int, i2: int)` → `float`；[绑定实现 L858](../../../../src/modules/tensor/TF.cpp#L858)。
- `get4(i0: int, i1: int, i2: int, i3: int)` → `float`；[绑定实现 L860](../../../../src/modules/tensor/TF.cpp#L860)。
- `get5(i0: int, i1: int, i2: int, i3: int, i4: int)` → `float`；[绑定实现 L862](../../../../src/modules/tensor/TF.cpp#L862)。
- `get6(i0: int, i1: int, i2: int, i3: int, i4: int, i5: int)` → `float`；[绑定实现 L864](../../../../src/modules/tensor/TF.cpp#L864)。
- `getDim(axis: int)` → `int`；[绑定实现 L841](../../../../src/modules/tensor/TF.cpp#L841)。
- `getDim0()` → `int`；[绑定实现 L842](../../../../src/modules/tensor/TF.cpp#L842)。
- `getDim1()` → `int`；[绑定实现 L843](../../../../src/modules/tensor/TF.cpp#L843)。
- `getDim2()` → `int`；[绑定实现 L844](../../../../src/modules/tensor/TF.cpp#L844)。
- `getDim3()` → `int`；[绑定实现 L845](../../../../src/modules/tensor/TF.cpp#L845)。
- `getDim4()` → `int`；[绑定实现 L846](../../../../src/modules/tensor/TF.cpp#L846)。
- `getDim5()` → `int`；[绑定实现 L847](../../../../src/modules/tensor/TF.cpp#L847)。
- `getDtype()` → `string`；[绑定实现 L849](../../../../src/modules/tensor/TF.cpp#L849)。
- `getRank()` → `int`；[绑定实现 L839](../../../../src/modules/tensor/TF.cpp#L839)。
- `getSize()` → `int`；[绑定实现 L840](../../../../src/modules/tensor/TF.cpp#L840)。
- `isEager()` → `bool`；[绑定实现 L838](../../../../src/modules/tensor/TF.cpp#L838)。
- `isSymbolic()` → `bool`；[绑定实现 L837](../../../../src/modules/tensor/TF.cpp#L837)。
- `maximumScalar(s: float)` → `Tensor`；[绑定实现 L893](../../../../src/modules/tensor/TF.cpp#L893)。
- `minimumScalar(s: float)` → `Tensor`；[绑定实现 L894](../../../../src/modules/tensor/TF.cpp#L894)。
- `mulScalarInPlace(s: float)` → `void`；[绑定实现 L899](../../../../src/modules/tensor/TF.cpp#L899)。
- `multiply(other: Tensor)` → `Tensor`；[绑定实现 L873](../../../../src/modules/tensor/TF.cpp#L873)。
- `multiplyInPlace(other: Tensor)` → `void`；[绑定实现 L897](../../../../src/modules/tensor/TF.cpp#L897)。
- `neg()` → `Tensor`；[绑定实现 L879](../../../../src/modules/tensor/TF.cpp#L879)。
- `powScalar(exp: float)` → `Tensor`；[绑定实现 L891](../../../../src/modules/tensor/TF.cpp#L891)。
- `reduceMax()` → `float`；[绑定实现 L905](../../../../src/modules/tensor/TF.cpp#L905)。
- `reduceMean()` → `float`；[绑定实现 L903](../../../../src/modules/tensor/TF.cpp#L903)。
- `reduceMin()` → `float`；[绑定实现 L904](../../../../src/modules/tensor/TF.cpp#L904)。
- `reluInPlace()` → `void`；[绑定实现 L900](../../../../src/modules/tensor/TF.cpp#L900)。
- `reshape1(d0: int)` → `Tensor`；[绑定实现 L910](../../../../src/modules/tensor/TF.cpp#L910)。
- `reshape3(d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L912](../../../../src/modules/tensor/TF.cpp#L912)。
- `reshape4(d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L913](../../../../src/modules/tensor/TF.cpp#L913)。
- `reshape5(d0: int, d1: int, d2: int, d3: int, d4: int)` → `Tensor`；[绑定实现 L914](../../../../src/modules/tensor/TF.cpp#L914)。
- `reshape6(d0: int, d1: int, d2: int, d3: int, d4: int, d5: int)` → `Tensor`；[绑定实现 L915](../../../../src/modules/tensor/TF.cpp#L915)。
- `set1(i0: int, value: float)` → `void`；[绑定实现 L855](../../../../src/modules/tensor/TF.cpp#L855)。
- `set2(i0: int, i1: int, value: float)` → `void`；[绑定实现 L857](../../../../src/modules/tensor/TF.cpp#L857)。
- `set3(i0: int, i1: int, i2: int, value: float)` → `void`；[绑定实现 L859](../../../../src/modules/tensor/TF.cpp#L859)。
- `set4(i0: int, i1: int, i2: int, i3: int, value: float)` → `void`；[绑定实现 L861](../../../../src/modules/tensor/TF.cpp#L861)。
- `set5(i0: int, i1: int, i2: int, i3: int, i4: int, value: float)` → `void`；[绑定实现 L863](../../../../src/modules/tensor/TF.cpp#L863)。
- `set6(i0: int, i1: int, i2: int, i3: int, i4: int, i5: int, value: float)` → `void`；[绑定实现 L865](../../../../src/modules/tensor/TF.cpp#L865)。
- `sigmoid()` → `Tensor`；[绑定实现 L888](../../../../src/modules/tensor/TF.cpp#L888)。
- `sin()` → `Tensor`；[绑定实现 L884](../../../../src/modules/tensor/TF.cpp#L884)。
- `sqrt()` → `Tensor`；[绑定实现 L881](../../../../src/modules/tensor/TF.cpp#L881)。
- `subScalar(s: float)` → `Tensor`；[绑定实现 L876](../../../../src/modules/tensor/TF.cpp#L876)。
- `tanh()` → `Tensor`；[绑定实现 L886](../../../../src/modules/tensor/TF.cpp#L886)。

## 接收者见绑定源码

- `abs(a: Tensor)` → `Tensor`；[绑定实现 L996](../../../../src/modules/tensor/TF.cpp#L996)。
- `arange(n: int)` → `Tensor`；[绑定实现 L965](../../../../src/modules/tensor/TF.cpp#L965)。
- `concat2(a: Tensor, b: Tensor, axis: int)` → `Tensor`；[绑定实现 L1044](../../../../src/modules/tensor/TF.cpp#L1044)。
- `concat3(a: Tensor, b: Tensor, c: Tensor, axis: int)` → `Tensor`；[绑定实现 L1045](../../../../src/modules/tensor/TF.cpp#L1045)。
- `concat4(a: Tensor, b: Tensor, c: Tensor, d: Tensor, axis: int)` → `Tensor`；[绑定实现 L1046](../../../../src/modules/tensor/TF.cpp#L1046)。
- `constantScalar(value: float)` → `Tensor`；[绑定实现 L964](../../../../src/modules/tensor/TF.cpp#L964)。
- `conv1dBias(x: Tensor, w: Tensor, bias: Tensor, stride: int, pad: int)` → `Tensor`；[绑定实现 L1038](../../../../src/modules/tensor/TF.cpp#L1038)。
- `conv2dBias(x: Tensor, w: Tensor, bias: Tensor, stride: int, pad: int)` → `Tensor`；[绑定实现 L1040](../../../../src/modules/tensor/TF.cpp#L1040)。
- `cos(a: Tensor)` → `Tensor`；[绑定实现 L1001](../../../../src/modules/tensor/TF.cpp#L1001)。
- `div(a: Tensor, b: Tensor)` → `Tensor`；[绑定实现 L990](../../../../src/modules/tensor/TF.cpp#L990)。
- `divScalar(a: Tensor, s: float)` → `Tensor`；[绑定实现 L994](../../../../src/modules/tensor/TF.cpp#L994)。
- `eye(n: int)` → `Tensor`；[绑定实现 L967](../../../../src/modules/tensor/TF.cpp#L967)。
- `fill1(d0: int, value: float)` → `Tensor`；[绑定实现 L960](../../../../src/modules/tensor/TF.cpp#L960)。
- `fill3(d0: int, d1: int, d2: int, value: float)` → `Tensor`；[绑定实现 L962](../../../../src/modules/tensor/TF.cpp#L962)。
- `fill4(d0: int, d1: int, d2: int, d3: int, value: float)` → `Tensor`；[绑定实现 L963](../../../../src/modules/tensor/TF.cpp#L963)。
- `flatten(a: Tensor)` → `Tensor`；[绑定实现 L1024](../../../../src/modules/tensor/TF.cpp#L1024)。
- `getName()` → `string`；[绑定实现 L944](../../../../src/modules/tensor/TF.cpp#L944)。
- `getRandomSeed()` → `int`；[绑定实现 L985](../../../../src/modules/tensor/TF.cpp#L985)。
- `linspace(start: float, end: float, n: int)` → `Tensor`；[绑定实现 L966](../../../../src/modules/tensor/TF.cpp#L966)。
- `logSoftmax(a: Tensor, axis: int)` → `Tensor`；[绑定实现 L1032](../../../../src/modules/tensor/TF.cpp#L1032)。
- `maximumScalar(a: Tensor, s: float)` → `Tensor`；[绑定实现 L1009](../../../../src/modules/tensor/TF.cpp#L1009)。
- `minimumScalar(a: Tensor, s: float)` → `Tensor`；[绑定实现 L1010](../../../../src/modules/tensor/TF.cpp#L1010)。
- `multiply(a: Tensor, b: Tensor)` → `Tensor`；[绑定实现 L989](../../../../src/modules/tensor/TF.cpp#L989)。
- `neg(a: Tensor)` → `Tensor`；[绑定实现 L995](../../../../src/modules/tensor/TF.cpp#L995)。
- `ones1(d0: int)` → `Tensor`；[绑定实现 L954](../../../../src/modules/tensor/TF.cpp#L954)。
- `ones3(d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L956](../../../../src/modules/tensor/TF.cpp#L956)。
- `ones4(d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L957](../../../../src/modules/tensor/TF.cpp#L957)。
- `ones5(d0: int, d1: int, d2: int, d3: int, d4: int)` → `Tensor`；[绑定实现 L958](../../../../src/modules/tensor/TF.cpp#L958)。
- `ones6(d0: int, d1: int, d2: int, d3: int, d4: int, d5: int)` → `Tensor`；[绑定实现 L959](../../../../src/modules/tensor/TF.cpp#L959)。
- `permute2(a: Tensor, a0: int, a1: int)` → `Tensor`；[绑定实现 L1013](../../../../src/modules/tensor/TF.cpp#L1013)。
- `permute3(a: Tensor, a0: int, a1: int, a2: int)` → `Tensor`；[绑定实现 L1014](../../../../src/modules/tensor/TF.cpp#L1014)。
- `permute4(a: Tensor, a0: int, a1: int, a2: int, a3: int)` → `Tensor`；[绑定实现 L1015](../../../../src/modules/tensor/TF.cpp#L1015)。
- `permute5(a: Tensor, a0: int, a1: int, a2: int, a3: int, a4: int)` → `Tensor`；[绑定实现 L1016](../../../../src/modules/tensor/TF.cpp#L1016)。
- `permute6(a: Tensor, a0: int, a1: int, a2: int, a3: int, a4: int, a5: int)` → `Tensor`；[绑定实现 L1017](../../../../src/modules/tensor/TF.cpp#L1017)。
- `powScalar(a: Tensor, exp: float)` → `Tensor`；[绑定实现 L1007](../../../../src/modules/tensor/TF.cpp#L1007)。
- `rand1(d0: int)` → `Tensor`；[绑定实现 L976](../../../../src/modules/tensor/TF.cpp#L976)。
- `rand2(d0: int, d1: int)` → `Tensor`；[绑定实现 L977](../../../../src/modules/tensor/TF.cpp#L977)。
- `rand3(d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L978](../../../../src/modules/tensor/TF.cpp#L978)。
- `rand4(d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L979](../../../../src/modules/tensor/TF.cpp#L979)。
- `randn1(d0: int)` → `Tensor`；[绑定实现 L980](../../../../src/modules/tensor/TF.cpp#L980)。
- `randn2(d0: int, d1: int)` → `Tensor`；[绑定实现 L981](../../../../src/modules/tensor/TF.cpp#L981)。
- `randn3(d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L982](../../../../src/modules/tensor/TF.cpp#L982)。
- `randn4(d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L983](../../../../src/modules/tensor/TF.cpp#L983)。
- `randomNormal1(d0: int)` → `Tensor`；[绑定实现 L972](../../../../src/modules/tensor/TF.cpp#L972)。
- `randomNormal2(d0: int, d1: int)` → `Tensor`；[绑定实现 L973](../../../../src/modules/tensor/TF.cpp#L973)。
- `randomNormal3(d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L974](../../../../src/modules/tensor/TF.cpp#L974)。
- `randomNormal4(d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L975](../../../../src/modules/tensor/TF.cpp#L975)。
- `randomUniform1(d0: int)` → `Tensor`；[绑定实现 L968](../../../../src/modules/tensor/TF.cpp#L968)。
- `randomUniform2(d0: int, d1: int)` → `Tensor`；[绑定实现 L969](../../../../src/modules/tensor/TF.cpp#L969)。
- `randomUniform3(d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L970](../../../../src/modules/tensor/TF.cpp#L970)。
- `randomUniform4(d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L971](../../../../src/modules/tensor/TF.cpp#L971)。
- `reduceMax(a: Tensor)` → `float`；[绑定实现 L1029](../../../../src/modules/tensor/TF.cpp#L1029)。
- `reduceMean(a: Tensor)` → `float`；[绑定实现 L1027](../../../../src/modules/tensor/TF.cpp#L1027)。
- `reduceMin(a: Tensor)` → `float`；[绑定实现 L1028](../../../../src/modules/tensor/TF.cpp#L1028)。
- `reshape1(a: Tensor, d0: int)` → `Tensor`；[绑定实现 L1018](../../../../src/modules/tensor/TF.cpp#L1018)。
- `reshape3(a: Tensor, d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L1020](../../../../src/modules/tensor/TF.cpp#L1020)。
- `reshape4(a: Tensor, d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L1021](../../../../src/modules/tensor/TF.cpp#L1021)。
- `reshape5(a: Tensor, d0: int, d1: int, d2: int, d3: int, d4: int)` → `Tensor`；[绑定实现 L1022](../../../../src/modules/tensor/TF.cpp#L1022)。
- `reshape6(a: Tensor, d0: int, d1: int, d2: int, d3: int, d4: int, d5: int)` → `Tensor`；[绑定实现 L1023](../../../../src/modules/tensor/TF.cpp#L1023)。
- `rmsnormW(a: Tensor, scale: Tensor, eps: float)` → `Tensor`；[绑定实现 L1036](../../../../src/modules/tensor/TF.cpp#L1036)。
- `setRandomSeed(seed: int)` → `void`；[绑定实现 L984](../../../../src/modules/tensor/TF.cpp#L984)。
- `sigmoid(a: Tensor)` → `Tensor`；[绑定实现 L1004](../../../../src/modules/tensor/TF.cpp#L1004)。
- `sin(a: Tensor)` → `Tensor`；[绑定实现 L1000](../../../../src/modules/tensor/TF.cpp#L1000)。
- `sqrt(a: Tensor)` → `Tensor`；[绑定实现 L997](../../../../src/modules/tensor/TF.cpp#L997)。
- `subScalar(a: Tensor, s: float)` → `Tensor`；[绑定实现 L992](../../../../src/modules/tensor/TF.cpp#L992)。
- `tanh(a: Tensor)` → `Tensor`；[绑定实现 L1002](../../../../src/modules/tensor/TF.cpp#L1002)。
- `zeros1(d0: int)` → `Tensor`；[绑定实现 L948](../../../../src/modules/tensor/TF.cpp#L948)。
- `zeros2(d0: int, d1: int)` → `Tensor`；[绑定实现 L949](../../../../src/modules/tensor/TF.cpp#L949)。
- `zeros3(d0: int, d1: int, d2: int)` → `Tensor`；[绑定实现 L950](../../../../src/modules/tensor/TF.cpp#L950)。
- `zeros4(d0: int, d1: int, d2: int, d3: int)` → `Tensor`；[绑定实现 L951](../../../../src/modules/tensor/TF.cpp#L951)。
- `zeros5(d0: int, d1: int, d2: int, d3: int, d4: int)` → `Tensor`；[绑定实现 L952](../../../../src/modules/tensor/TF.cpp#L952)。
- `zeros6(d0: int, d1: int, d2: int, d3: int, d4: int, d5: int)` → `Tensor`；[绑定实现 L953](../../../../src/modules/tensor/TF.cpp#L953)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
