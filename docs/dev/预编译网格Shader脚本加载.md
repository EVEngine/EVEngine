# 预编译网格 Shader 脚本加载

`gfx.loadMeshShaderSpv(vertexPath, fragmentPath)` 将 VFS 中的预编译 SPIR-V
加载到已有 `Graphics::newMeshShaderFromSpv` 管线，返回统一 Result 投影。
Windows 不再需要运行时 GLSL 编译器即可使用自定义网格材质。

```squirrel
local loaded = gfx.loadMeshShaderSpv("", "shaders/ink.frag.spv");
if (!loaded.ok) throw loaded.status;
renderable.getMaterial().setShader(loaded.value);
```

空 vertexPath 使用引擎默认 Mesh3D vertex shader。fragmentPath 必填。
只支持 Vulkan；其他后端返回 Unsupported，不尝试替换材质。
调用者必须离线编译符合 Mesh3D 顶点输入、descriptor 和 UBO 布局的着色器。
加载器检查文件可读性、SPIR-V 头及字节对齐；这些检查不代表验证了任意 shader 的 ABI。

脚本 `value` 是 Graphics 所有的 Shader facade，与现有材质 Shader 参数一致。
Graphics 生命周期内保持地址稳定；脚本不得销毁或在 Graphics 关闭后访问它。
加载在 VM/render owner 线程同步执行，不保留路径或临时文件数据、不调用用户回调。
解析阶段先拥有并校验两个阶段的数据，之后才调用 GPU 工厂；失败以诊断返回，
不会修改现有材质的 shader。墨迹示例只在成功后发布到材质。

原有两个 shader reload 绑定原样移入独立 `ShaderScriptBindings.cpp`，
没有新增公共 C++ 虚接口、模块、ECS System 或持久格式。
SPIR-V 使用 Khronos 既有格式；不另外定义二进制协议。
唯一墨迹状态仍由示例 ImageData 持有，GPU 纹理是其显示副本。

验证入口：`graphics.shaderSpvScript.failuresAreStructuredBeforeUpload`；
`examples/ink-arena/verify.nut` 覆盖真实着色器加载、错误输入与现有材质保持可用，
以及喷墨场累积、同色融合、异色覆盖、射线与 UV 接缝。

## Canvas.readPixels 与 GPU 喷墨

`canvas.readPixels()` 返回统一 Result 投影，成功时 `value` 为独立 ImageData，
`ownership` 为 script-owned；脚本 GC 持有其所有权，后续清空或销毁 Canvas 不改变快照。
在渲染/VM owner 线程同步调用，等待 GPU 并复制 RGBA8，不能作为每帧更新路径。
不调用用户回调、不保留 Canvas 临时引用，读回失败返回诊断并保留 Canvas 内容。
调用前必须提交待绘制的离屏批次；示例通过切回主画布完成提交。
该接口只承诺 RGBA8；HDR 继续使用现有独立接口。

正常运行时 Canvas 拥有唯一墨迹状态，Material 借用 Canvas 的 texture，二者由 Graphics
持有至销毁；不存在 CPU 状态回写。重启通过固定种子命令重建，未引入持久数据格式。
参考命令日志只在验证模式启用。绘制顺序固定，GPU/CPU 允许 RGBA8 舍入误差，不声明位级一致。

RGBA8 render pass 的 LOAD 修正属于同一 backend 内的行为修正。清空由 pending clear
在下一次实际提交时执行，首次提交默认 clear。HDR 未更改其原有加载行为。
持久性、alpha 累积、局部修改、显式清空和独立快照由
`test/graphics_canvas_persistence.cpp` 验证；真实 GPU/CPU 对比见 `verify_gpu.nut`。
未新增 ECS、模块边界或跨域 Link，未采用架构规则例外。
