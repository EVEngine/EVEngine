# 多尺度球体资源批量绘制验证

## 范围

这是现有 graphics 资源接口上的原生集成验证，并新增显式 SphereBatch 准备/绘制接口，
不表示百万对象或 16.67 ms 整帧验收通过。旧渲染默认路径没有改变。

每个实例使用 RGBA32Float 数值纹理的四个 texel，保存归一化中心/半径、视角/物理指数、
线性颜色、投影矩形；binding 32 的常量显式提供纹理宽度。binding 33 保留接口要求的
有效单位仿射矩阵，不能借矩阵缓冲绕过资源数据验证。一次实例化索引绘制读取独立记录。

资源上传使用 replaceMeshShaderResources，在提交之外复制输入并创建不可变资源。
它包含程序替换成本，不能放进逐帧 draw 并宣称廉价。SphereBatch::prepare 返回拥有资源的对象，
draw 只开始专用深度通路并提交已上传的实例；由宿主 present。原有 SphereView 参考接口保持不变。
目前每实例记录与单位矩阵合计 128 字节，另有 CPU 镜像、填充和管线资源，未测完整峰值。

## 接口与所有权

SphereFrame 是唯一 CPU 几何快照，SphereBatch 复制其已验证的可见记录，最多 1,000,000 个。
每行最多 2048 texel，记录占四个连续 texel；尾部填充初始化为零，不提交为实例。
空快照也上传一个不绘制的占位记录，以保持能力缺席的可观察失败，不以空输入跳过验证。
失败只销毁本次准备的临时资源，不修改旧 batch。移动失效的 CPU 帧在 GPU 操作前拒绝。
成功准备后 CPU 帧可销毁；Graphics 先销毁时，弱生命周期令牌阻止 draw 和析构访问失效资源。
接口采用 Result、nodiscard、Pimpl 和 WORLD 导出；公共头不引入 graphics 后端依赖。
未新增 ECS 所有权、持久格式或模块依赖，继续使用既有硬依赖 graphics 的同步调用。

## 已发现并修正的问题

- textureSize 使 SPIR-V 声明 ImageQuery，超出现有验证器允许的 Shader/Matrix 能力。
  改为显式纹理尺寸常量，没有放宽现有验证规则。
- 显示读回已经经过呈现变换，不能直接与片元线性颜色比较。旧参考测试中的 CHECK
  还可能只报告警告，不能将其进程成功视作颜色断言成功。参考遮挡测试改为 REQUIRE，
  检查明显的通道差；批量测试与同一进程参考路径的呈现后颜色逐通道比较。

## 验证状态

第一次原生测试成功创建资源和绘制，但错误的线性颜色读回断言失败；这次失败如实保留。
修订后的 Windows SHARED 原生构建成功；开启 Vulkan validation 的 8/8 个多尺度图形用例通过，
包括同进程参考/批量颜色比较、上传后清空调用方数据、不同实例物理深度的遮挡、firstInstance
选择及越界拒绝。参考路径原有遮挡断言也已全部改为 REQUIRE 并通过。
日志为 `build/sphere-batch-parity-build.log`、`build/sphere-batch-parity-tests.log`、
`build/sphere-batch-parity-validation.log`，没有观察到 Validation Error / VUID 错误。
资源通路版本的完整源质量门禁 `build/sphere-batch-parity-source-check.log` 已退出 0，
529 项脚本测试通过（5 项既有跳过）。新公开接口随后通过架构门禁
`build/sphere-batch-api-architecture.log`；API 版本完整 `make check` 已退出 0，
529 项脚本测试通过、5 项既有跳过，记录在 `build/sphere-batch-api-source-check.log`。
随后新增脚本绑定的 coverage/architecture 检查已通过，单独记录在 sphere-batch-script-contracts.log，
不能将较早启动的完整门禁当作所有后续修改的最终验证。

新增接口第一次构建通过，8 个用例中 7 个通过。生命周期用例错误地在 headless provider 上开始
呈现，崩溃栈明确落在 begin3DFrame → beginPresentCommandBuffer → acquireForFrame。
专用参考和批量视图现均在提交前对 isHeadless 返回 Unsupported；headless 仍可准备资源及验证
provider-first 销毁。真实窗口用例新增空快照呈现与 513 个记录跨纹理行验证。
修订后 Windows SHARED 构建（含 eve 和 unit_test_graphics）成功，8/8 聚焦图形测试通过；
Vulkan validation 未观察到 Validation Error / VUID 错误。记录在
`build/sphere-batch-api-repair-build.log`、`build/sphere-batch-api-repair-tests.log` 和
`build/sphere-batch-api-repair-validation.log`。CPU 帧先销毁、Graphics 先销毁、空帧、
跨纹理行及与参考路径颜色一致性均使用强断言。
当前验证依赖 SPIR-V 验证 provider。缺席配置、裁剪构建、百万实例上传、
重建帧及稳态整帧时间仍待实现和验收。批量减少 CPU 提交，不改变重叠球体最坏光栅工作量。

## 脚本与正常示例接入

新增 prepareSphereBatch(gfx, frame) 和拥有实例 MultiscaleSphereBatch.draw()。
沿用 common Result 表、VM 所有权桥、原生类型标签检查及接收者检查；空/错误类型返回诊断。
旧 newSphereView、prepareSphereFrame 和参考 draw(frame) 均保留。没有新的启动注册通道。
examples/multiscale-spheres 在启动时准备两个批次，CPU 帧随后可释放，逐帧只切换并绘制，
退出时释放批次。新增 README 明确上传和管线创建成本，未宣称逐帧重建廉价。

Windows 构建 build/sphere-batch-script-build.log 成功，8/8 图形/脚本用例通过
（build/sphere-batch-script-tests.log），包含空/错误 provider 与错误 draw 接收者。
正常启动 smoke 12 秒通过（build/sphere-batch-script-smoke.log），示例布局检查通过。
引擎拥有的第 60/300 帧捕获为 build/multiscale-captures/batch-near.png、batch-far.png，
两张均已查看；与 rectangle-near.png、rectangle-far.png 的 RGB 逐像素差均为零。
尺寸 1920x1080，中心分别 [225,99,99] 和 [99,99,225]，比较结果在 batch-image-comparison.json。
捕获使用正常入口和 gfx.saveFramePng，没有使用 eval 替代玩法。

进一步的真实 provider VM 用例已通过：CPU 帧与输入表 GC 后批次仍绘制参考颜色；
合法 provider 搭配错误/空帧返回失败，最后由 VM GC 释放批次。
最终构建 build/sphere-batch-script-vm-build.log 成功，8/8 聚焦用例通过，记录在
build/sphere-batch-script-vm-tests.log。完整验证输出为 sphere-batch-script-vm-validation.log，
未观察到 Validation Error / VUID 错误。以上是 Vulkan/Windows 实测，不覆盖其他后端或裁剪配置。
