# MCP 同帧附件调试

通过一份不可变快照检查最终画面、G-buffer 和阴影附件。请求不会暂停或恢复玩法，也不会为了获取附件重新渲染场景。需要下一次正常呈现才能完成；暂停后没有新帧会超时。

当前 Vulkan 提供 final、gbuffer.depth、gbuffer.hwDepth、gbuffer.normal、gbuffer.albedo、gbuffer.pbrParams、gbuffer.emissive、gbuffer.velocity，以及 shadow.csm.0 至 shadow.csm.2。WebGPU 的同帧捕获暂不支持，能力查询返回空候选列表，请求返回 Unsupported。旧截图接口保持原有用途。

## 捕获和查看

以下表示 MCP 工具名及 arguments；token 和 jobToken 必须使用实际返回值。

```text
eve_frame_capture_capabilities {}
eve_frame_capture {"buffers":["final","gbuffer.hwDepth","gbuffer.normal","gbuffer.pbrParams"],"timeoutMs":5000,"onUnavailable":"fail","requestKey":"lighting-01"}
eve_frame_capture_status {"token":"<token>"}
```

等待 status 为 ready。失败时查看 reason；不同附件的原始尺寸可能不同。frameId 是渲染帧序号，不是仿真 tick。相同连接内重复提交同一 requestKey 和相同参数返回同一 token；参数不同返回 RequestConflict。

```text
eve_frame_buffer_image {"token":"<token>","buffer":"gbuffer.normal","view":"normalWorld"}
eve_frame_capture_status {"token":"<jobToken>"}
eve_frame_pixel {"token":"<token>","buffer":"gbuffer.hwDepth","x":320,"y":180}
eve_frame_buffer_image {"token":"<token>","buffer":"gbuffer.hwDepth","view":"linearDepth","rangeMin":0.1,"rangeMax":40}
eve_frame_capture_status {"token":"<jobToken>"}
eve_frame_capture_release {"token":"<token>"}
```

图像编码和文件导出在 worker 上执行，首先返回 pending 和 jobToken，随后用 status 取结果；图像结果包含 MCP image content。像素坐标始终对应原始附件，原点为左上角，不对应缩小后的预览。

预览默认最长边 1024；可指定 maxWidth/maxHeight，范围 1 至 2048。maxBytes 是 base64 预算，默认 2 MiB，上限 16 MiB。超限返回 ImageTooLarge，可降低预览尺寸后重试。最多同时运行两个 worker，完成的预览缓存可以被新请求替换；快照自身不会因此失效。

## 多帧拼图

依次捕获并等待每个 `final` 快照 ready 后，可以合并成一张图：

```text
eve_frame_contact_sheet {"tokens":["<first>","<second>","<third>","<fourth>"],"columns":2,"cellWidth":512,"cellHeight":512}
eve_frame_capture_status {"token":"<jobToken>"}
```

支持 1–16 格，按输入顺序从左至右、从上至下排列；每格保持纵横比、黑边填充、不放大小图。
单张拼图上限 2048×2048，默认 base64 预算 2 MiB，可用 maxBytes 调整至最多 16 MiB。
16 格时应设置 columns=4；默认 columns 最多为 2，尺寸超限会明确拒绝。
返回 PNG image item 和 cells 元数据（来源 token、frameId、producerFrameId、格子矩形）。
释放任意来源 token 会取消关联拼图，重启也会使任务失效。

此工具只拼接已捕获的画面，不切换相机、不推进动画，也不证明同一模拟时刻。
`sameSimulationInstantVerified=false` 明确标记该限制。实体四视图和动画采样的后续设计见
[实体与动画检查设计](superpowers/specs/2026-10-25-entity-animation-inspection.md)。

## 各种视图

- auto 根据附件选择可视化；rawChannels 显示存储通道，并转换 BGRA 顺序。预览 alpha 固定不透明，避免打包的深度或 PBR 元数据让图片消失。
- scalar 用 channel 选择 0 至 3 通道，再按 rangeMin/rangeMax 映射为灰度。PBR 的通道依次是 metallic、roughness、occlusion、specularFactor。
- normalWorld 将解码的世界法线映射到 RGB；velocity 显示打包的运动方向。
- linearDepth 仅适用于 gbuffer.hwDepth，利用捕获时的逆投影矩阵重建正的视空间深度，支持透视、正交与反向 Z；范围使用引擎世界单位。D32 和 linearDepth 只允许 channel=0。
- hdrExposure 对颜色附件使用 exposure 指定的曝光预览；当前 RGBA8 自发光附件没有完整 HDR 精度。

gbuffer.depth 的 R 是线性归一化深度，G/B 同时存储速度；velocity 是同一物理资源的逻辑视图，不重复读回。法线 alpha、Albedo alpha 也有打包语义，以 manifest 的 encoding 为准。pixel 查询返回存储数值与解码后的命名通道；深度还返回 linearViewDepth。PNG 是显示预览，不能替代原始数值。

## 导出和生命周期

```text
eve_frame_capture_export {"token":"<token>","buffer":"gbuffer.hwDepth","encoding":"nativeBinary","path":"out/capture-depth.bin"}
eve_frame_capture_status {"token":"<jobToken>"}
eve_frame_capture_export {"token":"<token>","buffer":"final","encoding":"previewPng","path":"out/capture-final.png"}
eve_frame_capture_status {"token":"<jobToken>"}
```

nativeBinary 保留原始通道，使用紧密行布局和小端字节序，并附带 .json 元数据。导出先写临时文件，再在同一文件系统发布；已有目标不会被覆盖。二进制文件出现表示元数据也已就绪。文件系统若不支持硬链接，导出明确失败。previewPng 是可视化输出。

默认最多保留 16 个活动快照，每次最多一个在途请求；原始 CPU 数据和 staging 总预算 256 MiB，包含已释放但仍由 worker 使用的 CPU 租约。ready 快照 60 秒过期，查询不续期。预算不足不会驱逐其他有效快照。

resize 不改变已有 CPU 快照；无法完成的在途请求报告 TargetRecreated。软重启或 MCP 连接更换会撤销旧 token。release 取消未完成请求，GPU 资源等完成信号后才释放；旧 job 也会被取消。

所有新工具使用 eve.renderCapture schemaVersion=1。未知参数、枚举或附件不会被静默忽略。onUnavailable=report 允许在同一快照中列出缺失附件及原因；默认 fail 要求全部附件成功。后端没有该附件或本帧没有生成时，不会返回另一帧来代替。

架构与验证要求见[设计文档](superpowers/specs/2026-10-16-mcp-frame-buffer-capture.md)。Vulkan 复制遵循 [vkCmdCopyImageToBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImageToBuffer.html) 的 transfer usage、布局及单采样约束。


## 实现与验证

快照由 Graphics 独占管理；MCP worker 只持有不可变 CPU 数据租约。旧截图、旧 G-buffer 导出与新快照共用 Vulkan 原始读回内核，包含 transfer-write 到 host-read 屏障，并在 fence 完成后读取；同步依据见 [Khronos CPU readback 示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。旧接口保留其已有生命周期，新快照的唯一权威数据源是 FrameCaptureStore。

2026-10-16 在独立验证工作树完成严格警告 SHARED、OBJECT 引擎构建，以及 headless 公共头文件、provider present/absent 链接与运行检查。49 个相关 devtools 测试通过；最终 12 个捕获及旧渲染接口测试通过（其中 7 个与前者重叠，共 54 个不同用例）。新 GPU 捕获用例启用 Vulkan validation，通过同帧标记验证全部 11 个附件及快照冻结。

真实 SHARED 与 OBJECT 引擎均通过 MCP 捕获、11 个附件的 PNG/像素查询、原始 D32 导出、释放和旧截图兼容验证；最终共享读回内核再次通过 SHARED 全流程，预览经过人工查看。

源码门禁 `make check` 已运行，但被干净基线中已有的 10 个架构契约问题阻断，未修改基线或增加豁免。lighting-showcase 的持续运行还存在 descriptor 更新与 VMA 映射断言；零捕获请求的对照运行可复现，因此本次不宣称该示例全程 validation 无错误。WebGPU 首版明确返回 Unsupported。没有架构规则例外。

## 实体四视图（几何诊断首版，2026-10-26）

```text
eve_entity_views {"domain":"renderable3d","entityId":7,"generation":2,"mode":"geometry","cellSize":512}
eve_entity_views_status {"token":"<token>"}
eve_entity_views_release {"token":"<token>"}
```

`entityId` / `generation` 来自目标 Renderable3D 的 `getEntityId()` / `getEntityGeneration()`。
它们是实体身份，不是截图 provider generation。工具 family 由 provider 源文件自动注册。
本组响应使用 `eve.entityViews` schemaVersion=1；未知字段拒绝，版本不匹配不得作为同一契约解析。

运行 Vulkan 后端并至少 present 一次后可用；未编译/运行 provider 时明确返回 unavailable。
`mode: geometry` 必填，用来明确此次查看的是 **孤立实体的世界法线 RGB 诊断图**，没有
游戏材质、纹理透明裁剪、灯光、背景遮挡或后处理，不是最终游戏画面。颜色用于观察表面方向。
当前支持静态单 Mesh、多 part、已上传的 CPU morph 结果，以及 GPU skinning 的当前姿态。
请求时复制 Mesh 调色板，按内置四权重蒙皮公式计算位置/法线和变形后 AABB；不推进动画。
每 Mesh 的调色板最多 65,536 个仿射矩阵，四个 joint（包括零权重项）均在范围内，权重非负且总和大于 1e-8。
不满足条件明确失败，不改成 bind pose。未烘焙 morph、
自定义 Shader、位移、hair、LOD、实例化和非默认相机 override 明确拒绝。

布局固定：左上 front（从 +Z 看）、右上 right（从 +X 看）、左下 top（从 +Y 看，up=-Z）、
右下 active game camera。前三格同中心、同世界单位/像素；右下保留原相机投影及纵横比，
以补边适配格子，不将离屏对象重新对准。响应带各格的视口矩形、view/projection、世界 AABB、
实体身份和冻结时的 renderFrameId；它是渲染序号，不冒充模拟 tick。

请求在 owner thread 检查实体代际并复制完整几何/相机值，不留实体或 Mesh 指针。
随后的 GPU 帧使用私有顶点、颜色/深度目标、pipeline、command pool、staging 和 fence；
不修改游戏相机、实体可见性、共享调色板或活跃 Canvas。请求后源实体/资源销毁不会改变
已冻结的结果。GPU 回读复用 FrameReadback 内核，fence 完成后才发布；PNG 编码复用现有
FrameCapturePreview worker 路径。status 不等待 GPU，也不推进游戏。

一次只接受一个任务；完成后释放再申请下一个。cellSize=64..1024，输出为 2×2 atlas，
每 Mesh 最多 250,000 顶点，整个实体最多 450,000 索引。超额直接失败，不静默降级或截断。
捕获超时 5 秒，ready CPU 快照 60 秒到期；PNG 默认 base64 预算 2 MiB，超额请缩小格子。
取消后 GPU 资源仍保留到完成信号；后端重建撤销旧 provider/token，不能继续取旧任务。
本组目前没有指定动画片段/时间的采样入口，动画序列仍是后续阶段。

原生层已增加 `AnimInspectionBuilder`：可按独立时间表采样片段，返回自有 palette，再通过
`EntityViewRequest.skinPalettes` 逐帧渲染，最终复用 contact-sheet 拼图。这条组合已用
三帧 GPU 测试验证，不会修改游戏 Mesh 的 palette。尚无 MCP 动画任务入口，实体与动画
资源的身份关联及整段统一取景仍待接入；现有 `eve_entity_views` JSON 参数没有改变。
