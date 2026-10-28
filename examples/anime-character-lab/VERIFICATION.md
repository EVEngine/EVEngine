# 本机验证记录 · 2026-09-10

基于 EVEngine `e02add066` 加本工作区未提交修改。未创建 PR，未运行远端 CI。

- Windows MSVC Debug：`eve` 与聚焦测试程序编译成功；实际 Vulkan 场景已运行。
- 14 项聚焦测试逐进程通过：8 项原有 Stylize、2 项 anime、3 项 shader reload、1 项 deferred targets。
  新的 deferred targets 用例在修复前出现像素断言失败及 Vulkan 图像/尺寸错误，修复后 17 个断言通过。
- 上述 GPU 用例及最终角色场景启用 `EVENGINE_VULKAN_VALIDATION=1`：没有 `[ERROR: Validation]`；
  驱动仍提示现有管线中未被顶点着色器使用的属性，属于性能提示。
- 实际 MCP 检查了近景、全身、侧光、背光、转角、投影关闭状态；投影开关确实改变画面。
- 错误着色器数组（字符串、浮点、负数、超过 uint32、错误魔数）均被拒绝，截图逐字节不变；
  重载同一有效二进制后截图也逐字节不变。
- GLSL 经 `glslc` 编译、`spirv-val` 验证；组合后的 WGSL 经官方 Naga 30.0.1 验证成功。
  **未完成 WebGPU 引擎实际绘制或跨后端像素一致性测试**。本机 Python WebGPU 验证器在设备初始化时崩溃，
  因此使用离线 Naga 验证，不能把它当作 WebGPU 运行证明。
- `python scripts/check_architecture_contracts.py --base HEAD`、7 项 architecture fixtures、
  `python scripts/module_depgraph.py --check` 与 `git diff --check` 均通过。
- Windows 资产转换器已编译运行；模型、纹理和派生产物留在忽略目录，原下载未修改。

本机编译使用 minimal profile 加 graphics/model3d/stylize/camera/font/devtools，依赖闭包为 52/185 模块。
第三方源码和只读预编译库使用相同的本机已有提交 `761fc836ebcc37820c779e0c8ee30a975bd0d148`，
通过 CMake 缓存临时覆盖 pin；**没有修改仓库默认 pin**。
因此这证明该本机依赖组合通过，不代表默认依赖 pin、完整模块构建或所有平台 CI 已验证。

本次适用的架构要求：GPU 资源保持 Graphics 单一所有者；先等待 GPU、销毁渲染图引用，再释放目标；
脚本重载使用现有 Result，失败保留旧状态；参数目录单一来源；无新向上依赖；大文件抽出独立 TU；
覆盖延迟创建、重建、清空、重载失败与可选投影开关。没有申请或采用架构规则例外。

实际截图位于忽略的 `verification/`：`legacy-portrait.png`、`anime-portrait.png`、
`anime-full.png`、`anime-side.png`、`anime-back.png`、`anime-orbit.png`、`anime-no-shadows.png`。
这些是引擎截图接口读回的 GPU 画面，没有图像生成或离线修饰。
