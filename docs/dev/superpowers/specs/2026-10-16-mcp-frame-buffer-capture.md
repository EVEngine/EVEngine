# MCP 同帧渲染附件捕获设计

日期：2026-10-16
状态：首版已实现（Vulkan）；使用方式与验证结果见 [MCP 帧捕获](../../mcp-frame-capture.md)。

## 目标与范围

MCP 客户端可以请求捕获下一次正常渲染产生的一帧，随后查看该帧的最终画面、G-buffer、阴影附件和原始像素。所有结果绑定同一个不可变快照，避免动态场景中连续截图来自不同帧。捕获观察正常渲染，不为生成附件重新执行场景或改变玩法状态。

这里的 buffer 指有图像语义的渲染附件，不包含任意 vertex/index/storage buffer 内存转储，也不提供 draw-call 回放或着色器单步调试。

首版完成 Vulkan 路径的最终画面、实际生成的 G-buffer 和 CSM 阴影级联。WebGPU 使用同一契约，未实现部分必须返回 Unsupported。其他 pass 的输入输出、历史帧、GPU 耗时和对象拾取留作后续扩展，不算首版完成条件。

## 当前代码基础

- `src/engine/common/RenderCapture.h`：已有 `IRenderCapture` capability，包含最终画面和 `gbufferPng` 接口。
- `src/engine/devtools/McpRuntimeTools.cpp`：`eve_screenshot_image` 返回 MCP image content，初次开启读回后需要等待呈现。
- `src/modules/graphics/GraphicsCapabilities.cpp`：现有 graphics capability 实现。
- `src/modules/graphics/GBuffer.h`：定义 depth、hwDepth、normal、albedo、pbrParams、emissive；velocity 与 depth 共用物理纹理的不同通道。阴影由独立 CSM 路径管理。
- `src/modules/graphics/vulkan/Graphics.h`：已有 swapchain staging ring，可复用其同步经验；不能据此假设任意附件已支持读回。

这些是源码基础，不代表已有完整的同帧捕获能力。

## 用户工作流

1. 查询后端支持的附件与编码。
2. 指定需要的附件，提交一次捕获请求，得到 token。
3. 查询状态，等待正常渲染完成及 GPU 读回完成。
4. 获取冻结的附件清单，按需请求预览、原始像素或导出。
5. 释放快照，或等待过期回收。

例如：同时捕获 final、gbuffer.hwDepth、gbuffer.normal、gbuffer.pbrParams，先观察法线预览，再读取异常位置的原始深度和粗糙度。读取图片、查询像素均不触发新一帧。

## MCP 工具契约

所有新工具采用结构化结果：`schemaId="eve.renderCapture"`、`schemaVersion=1`，并返回 status、诊断 code/message/retryable。状态与错误码分别表达进行阶段和失败原因，不能用单个 bool 代替。

### eve_frame_capture_capabilities

廉价查询，返回 backend、providerGeneration、支持的资源类别、支持的解码视图、读回限制及当前配置下的候选资源。候选不承诺目标帧一定生成，最终以快照清单为准。

### eve_frame_capture

参数：`buffers` 为必填非空附件 ID 列表；`timeoutMs` 默认 5000，范围 100 至 30000；`onUnavailable` 为 `fail` 或 `report`，默认 fail；可选 `requestKey` 支持同一连接中的提交重试去重。

返回 token 和 queued 状态。成功接纳不等于捕获完成。token 在本次 provider generation 内唯一且不重用，MCP 将其作为不透明字符串。

首版只支持下一次正常渲染，不支持任意历史 frameId。队列接纳时预留预算；实际资源尺寸超出预算则失败，不发布部分像素。

`fail` 下任一附件不可用，整次捕获失败；`report` 下可以得到 ready 快照，但清单必须逐项标注缺席原因。所有成功附件仍来自同一目标帧。

### eve_frame_capture_status

参数 token；返回 queued、recording、readbackPending、ready、failed、cancelled、expired、released 之一。

进入 ready 时原子发布完整 manifest，包括 captureId、frameId、simulationTick（若可获得）、backend、providerGeneration、targetGeneration、viewId、视口、相机矩阵与深度约定，以及每项附件的资源身份、生产阶段、尺寸、格式、采样数、颜色空间、编码、可用状态、原始字节数。

frameId 表示渲染帧序号，不能与玩法 tick 混用。存在多个视图时，屏幕附件绑定主视图；阴影附件另带 light/cascade 身份和矩阵。

### eve_frame_buffer_image

参数 token、buffer、view，以及可选 channel、range、exposure、maxWidth、maxHeight、maxBytes。只允许 ready 快照。

返回 MCP image content 与结构化元数据。PNG 是显示预览；返回实际使用的解码参数、缩放尺寸和颜色空间。默认最长边 1024，base64 预算 2 MiB，上限 32 MiB；超限返回 ImageTooLarge，不能静默丢图或更换帧。

默认 view 根据附件语义选择；调用者也可显式选择 rawChannels、linearDepth、normalWorld、velocity、scalar 或 hdrExposure。编码不支持该 view 时返回 UnsupportedView，不猜测解码。

### eve_frame_pixel

参数 token、buffer、x、y；坐标始终使用原始附件尺寸、左上角为原点，不受预览缩放影响。返回 native 格式的存储值、解码后的语义值及单位；不能从预览 PNG 反推原始值。

深度可返回 deviceDepth、linearViewDepth；法线返回 world XYZ；速度注明空间和单位；PBR 各通道单独命名。NaN/Inf 用分类字段和字符串表示，保证 JSON 有效。越界返回 PixelOutOfBounds。

### eve_frame_capture_export

参数 token、buffer、encoding、受现有工具输出路径策略约束的 path。encoding 显式选择 previewPng 或 nativeBinary，昂贵导出返回独立 jobToken，由 status 查询任务类型和进度，不让文件写入阻塞 MCP 处理线程。

nativeBinary 使用小端、紧密行布局，并附版本化 JSON 元数据，描述格式、通道、尺寸和解码参数；不宣称是 GPU 内存布局的原封转储。临时文件成功后原子发布，失败不留下可误认的完整文件。首版不做导入或恢复。

### eve_frame_capture_release

参数 token；取消未完成请求或释放已完成快照。重复释放返回 AlreadyReleased。GPU 在途资源必须等 fence 完成再回收；释放期间已有 CPU 读操作持有不可变租约直至结束。

## 附件及显示语义

- `final`：最终呈现内容，记录 UI 是否包含在内及其颜色空间；屏幕截图定义沿用现有工具。
- `gbuffer.depth`：当前 RGBA8 编码中的线性深度通道；G/B 是打包速度，不把整张纹理误称为纯深度。
- `gbuffer.hwDepth`：D32 device depth。线性化使用捕获帧的投影矩阵与约定，支持反向 Z、透视和正交，不能统一套一个 near/far 公式。
- `gbuffer.normal`：RGB 解码为世界空间法线；alpha 单独说明旧编码，不能混作透明度。
- `gbuffer.albedo`：RGB 为 albedo × tint，alpha 为当前兼容路径中的线性深度。显示时不直接当普通 RGBA 贴图合成。
- `gbuffer.pbrParams`：R metallic、G roughness、B occlusion、A specularFactor；可逐通道灰度查看。
- `gbuffer.emissive`：当前头文件声明为 RGBA8，不能据名称声称保留完整 HDR。未来 HDR 格式由 manifest 表达。
- `gbuffer.velocity`：depth 纹理 G/B 的逻辑视图，0.5 表示零运动；实际比例、方向与单位由 producer 提供，验证前禁止猜测。
- `shadow.csm.<cascade>`：每级阴影深度，记录 light/cascade 身份、矩阵、深度约定、更新帧号；已有缓存而本帧未更新时明确标注，不能声称为本帧重新生成。

逻辑视图共享同一物理资源读回，避免 depth 与 velocity 重复复制。首版遇到不能正确读回的格式或 MSAA 附件明确返回 UnsupportedFormat/UnsupportedSampleCount，不隐式采用会改变数据意义的 resolve。

## 同帧与 GPU 同步

请求在帧开始前锁定 frameId 和所需 producer。每个附件在生产结束、覆盖或资源别名复用之前，记录到捕获专用 staging 的复制；最终画面在最终颜色完成后复制。不能等帧末再假设所有中间附件仍然存在。

复制必须尊重资源原有布局、访问掩码、队列所有权与后续消费者，完成后恢复所需状态。以现有同步机制中的 fence 或等价完成信号判断可读，不使用每次捕获 deviceWaitIdle。具体 Vulkan barrier 实现须在编码阶段核对官方规范并开启 validation 验证。

GPU 完成后，后台任务执行行距整理、格式转换、PNG 编码与导出。禁止后台任务保留 Texture 裸指针，也禁止网络线程访问实时 GPU 资源。

快照保留的是目标帧所观察到的资源内容；时域历史或缓存阴影可能具有更早的 producerFrameId，必须区分 snapshotFrameId 与 producerFrameId。

## 状态所有权与生命周期

Graphics 所有的 CaptureManager 是请求状态、预算和快照的唯一权威 owner。MCP 只做参数验证、调度和序列化；后台任务仅持有 CPU 快照租约。capability 调用按主线程亲和性排队；查询返回 owning value，不跨帧保存 provider 裸指针。

状态路径：queued → recording → readbackPending → ready。非终态均可转 failed 或 cancelled；ready 经 release 或 TTL 回收。failed/cancelled 保留小型诊断墓碑，像素与 staging 分别在安全时机释放。

- 窗口 resize：排队请求采用目标帧尺寸；已开始录制请求依靠资源租约完成；旧资源无法保留时返回 TargetRecreated。已有 ready CPU 快照不变。
- 设备丢失：在途请求失败并报告 DeviceLost；已完成 CPU 快照仍可读取。
- 游戏软重启、provider 注销或热重载：递增 providerGeneration、取消在途任务并清空对外 token；旧 token 返回 StaleCapture。后台租约可以完成清理，但不再发布结果。
- CaptureManager 先销毁：先撤销入口、取消任务、等待 GPU 安全回收；worker 后完成只销毁租约。worker 先完成：正常发布，由 manager 接管不可变结果。
- MCP 断线：连接所属请求和快照被取消/释放；请求去重记录同连接同代有效，相同 key 但不同参数返回 RequestConflict。

超时按 steady clock 的注入接口计算，不使用仿真时间。暂停时不得隐式 resume、执行脚本或推进玩法；没有新渲染就保持等待直至超时，诊断提示显式推进渲染。

## 预算与性能

建议初始默认值：每连接一个在途捕获，全局最多两个在途捕获；最多两个 ready 快照，ready TTL 60 秒；staging 与 CPU 原始快照合计 256 MiB，预览缓存额外上限 32 MiB。数值可配置，属于待性能测量确认的设计值。

预算不足返回 BudgetExceeded，不自动驱逐仍有效的快照；重复状态查询不延长 TTL。已取消但 GPU 未完成的 staging 仍计入预算。预览请求先预留输出内存与编码预算，避免限额仅覆盖存储而遗漏瞬时峰值。

捕获关闭时不创建 staging、不复制、不读回、不编码。帧热路径只检查已解析的请求与 typed attachment ID，不逐帧做字符串 capability 查询。记录复制字节数、CPU 转换/编码耗时及完成延迟；不在没有计时查询证据时声称 GPU 耗时。

## 模块边界和兼容性

在 `common/RenderCapture.h` 同一家族扩展受窄职责约束的 `IRenderFrameCapture`，使用现有 `cap::provide/query`；不新增模块或自建 provider 注册体系。选择依据：单个活动后端 provider、低频显式请求、已知单个 consumer、整批附件原子发布，因此采用 typed capability + Result + opaque token，而不是 ECS 或广播事件。

新接口概念操作为 capabilities、request、status、image、pixel、export、release；返回类型全部使用已有 `[[nodiscard]] Result<T>`，状态使用强枚举。公开声明补齐 Doxygen 的所有权、线程、重入与 @cost：request 按附件像素量预留预算，image/export 按输出像素量产生 CPU 编码/I/O 成本，status/pixel 为 CPU 快照查询。禁止以默认参数把昂贵捕获藏在廉价查询里。

Graphics 提供实现，devtools 消费接口；公共头不包含 graphics/backend 头，协议层不引用 VkImage。新增实现放独立 TU，不继续扩充已有超大 McpServer.cpp。

现有 eve_screenshot、eve_screenshot_image 和 gbufferPng 保留兼容。实施时最终图与单附件导出的读回、转换逻辑收敛到同一内核；旧接口文档明确是兼容入口，不获得同帧多附件保证。不可为兼容接口保留第二套权威捕获缓存。

跨进程协议 schema v1 忽略未知输出字段，严格拒绝未知请求参数和枚举，未知 major version 拒绝处理；未来破坏性变更显式提升 major。接口以 typed C++ 数据为源，JSON 只在工具边界转换，不引入 eve::Value 作为内部模型。

实施时在 graphics 所属 architecture contract shard 登记 module-interface 六面：capability 提供/消费、无新增 ECS/Link、无广播事件、无新增脚本接口、MCP 工具协议。devtools 同步登记消费面。不得改总目录基线来压掉检查。

## 验证与交付条件

1. 共享契约测试覆盖 provider present/absent、Unsupported 后端、状态迁移、过期、取消、双重释放、token generation、预算、失败原子性、去重、并发租约和注入时钟。
2. 纯 CPU 解码测试覆盖法线、PBR 通道、深度投影约定、速度 producer 编码、颜色空间、NaN/Inf、原始坐标与预览缩放。使用有独立预期值的已知输入，不能以实现自身作判据。
3. Vulkan 集成场景用每帧变化的标记证明 final 与附件 frameId 一致，检查选定像素与数值容差；验证级联缓存来源帧，不能只看 PNG 存在。
4. 开启 validation 做短程捕获，覆盖 resize、资源复用、设备失效注入和关闭过程。对预算拒绝与读回失败注入，证明没有部分 ready 和资源泄漏。
5. MCP 端到端完成 request → status → image/pixel/export → release；实际查看引擎导出的最终图、深度、法线、PBR 和阴影预览。不得使用桌面截图作附件正确性证据。
6. 旧 screenshot 调用保持兼容；暂停捕获不会推进玩法；关闭捕获时验证零读回提交。比较启用捕获前后输出及仿真状态，记录测量环境和容差。
7. Windows SHARED 与 OBJECT 验证链接；仅运行相关 graphics/devtools 域测试，全平台全套交由 CI。执行 make check 和分层检查，不要求本轮设计文档已有运行测试结果。
8. 实施阶段验证无 graphics provider 的裁剪配置；分别报告 L1 编译、L2 链接、L3 缺席启动、L4 MCP 可观察 Unsupported 的证据，不能用 include 图替代。

适用规则：单一权威 owner、Result 与强状态、跨帧生命周期、线程亲和性、可观察缺席、共享后端契约、跨进程 schema、成本分级、六面边界声明。未新增 ECS System、玩法继承树或持久恢复，因此相关模型变更不适用。无拟议例外；任何偏离须重新评审并取得明确批准。

## 实施顺序

第一阶段交付 typed 协议、CaptureManager 与状态/预算契约测试；第二阶段在同一接口变更 PR 中补齐 Vulkan producer、WebGPU 显式能力结果、MCP consumer 与旧入口兼容，避免接口和消费者分批破坏构建。第三阶段补真实帧证据、双链接验证与 source-only gate，满足首版全部条件后才可宣称功能完成。
