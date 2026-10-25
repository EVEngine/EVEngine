# gpgpu：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## ComputeShader

- `clearBindings()` → `void`；[绑定实现 L420](../../../../src/modules/gpgpu/Gpgpu.cpp#L420)。
- `getBoundBuffer(binding: int)` → `GpuBuffer`；[绑定实现 L417](../../../../src/modules/gpgpu/Gpgpu.cpp#L417)。
- `getFloat(index: int)` → `float`；[绑定实现 L419](../../../../src/modules/gpgpu/Gpgpu.cpp#L419)。
- `setFloat(index: int, value: float)` → `void`；[绑定实现 L418](../../../../src/modules/gpgpu/Gpgpu.cpp#L418)。

## EcsShaderSystem

- `clearBuffers()` → `void`；[绑定实现 L469](../../../../src/modules/gpgpu/Gpgpu.cpp#L469)。
- `getFloat(index: int)` → `float`；[绑定实现 L462](../../../../src/modules/gpgpu/Gpgpu.cpp#L462)。
- `getGpgpu()` → `Gpgpu`；[绑定实现 L452](../../../../src/modules/gpgpu/Gpgpu.cpp#L452)。
- `getLocalSize()` → `int`；[绑定实现 L457](../../../../src/modules/gpgpu/Gpgpu.cpp#L457)。
- `getShader()` → `ComputeShader`；[绑定实现 L455](../../../../src/modules/gpgpu/Gpgpu.cpp#L455)。
- `setFloat(index: int, value: float)` → `void`；[绑定实现 L461](../../../../src/modules/gpgpu/Gpgpu.cpp#L461)。
- `setLocalSize(localSize: int)` → `void`；[绑定实现 L456](../../../../src/modules/gpgpu/Gpgpu.cpp#L456)。

## GpuBuffer

- `fillFloat32(value: float)` → `void`；[绑定实现 L430](../../../../src/modules/gpgpu/Gpgpu.cpp#L430)。
- `getSize()` → `int`；[绑定实现 L424](../../../../src/modules/gpgpu/Gpgpu.cpp#L424)。
- `getUsage()` → `string`；[绑定实现 L425](../../../../src/modules/gpgpu/Gpgpu.cpp#L425)。
- `writeData(data: ByteData, dstOffset: int)` → `void`；[绑定实现 L426](../../../../src/modules/gpgpu/Gpgpu.cpp#L426)。
- `writeFloat32(floatIndex: int, value: float)` → `void`；[绑定实现 L428](../../../../src/modules/gpgpu/Gpgpu.cpp#L428)。

## GpuSequence

- `getStatus()` → `string`；[绑定实现 L444](../../../../src/modules/gpgpu/Gpgpu.cpp#L444)。

## 接收者见绑定源码

- `getName()` → `string`；[绑定实现 L478](../../../../src/modules/gpgpu/Gpgpu.cpp#L478)。
- `newShaderFromBytecode(path: string)` → `ComputeShader`；[绑定实现 L481](../../../../src/modules/gpgpu/Gpgpu.cpp#L481)。
- `packEcsFloats(entities: dynamic, slot: string, fields: dynamic, buf: GpuBuffer)` → `int`；[绑定实现 L471](../../../../src/modules/gpgpu/Gpgpu.cpp#L471)。
- `packEcsFloatsRange(entities: dynamic, slot: string, fields: dynamic, buf: GpuBuffer, firstEntity: int, entityCount: int)` → `int`；[绑定实现 L472](../../../../src/modules/gpgpu/Gpgpu.cpp#L472)。
- `unpackEcsFloats(entities: dynamic, slot: string, fields: dynamic, buf: GpuBuffer, entityCount: int)` → `int`；[绑定实现 L473](../../../../src/modules/gpgpu/Gpgpu.cpp#L473)。
- `unpackEcsFloatsRange(entities: dynamic, slot: string, fields: dynamic, buf: GpuBuffer, firstEntity: int, entityCount: int)` → `int`；[绑定实现 L474](../../../../src/modules/gpgpu/Gpgpu.cpp#L474)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
