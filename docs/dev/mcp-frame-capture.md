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

默认最多保留两个活动快照，每次最多一个在途请求；原始 CPU 数据和 staging 总预算 256 MiB，包含已释放但仍由 worker 使用的 CPU 租约。ready 快照 60 秒过期，查询不续期。预算不足不会驱逐其他有效快照。

resize 不改变已有 CPU 快照；无法完成的在途请求报告 TargetRecreated。软重启或 MCP 连接更换会撤销旧 token。release 取消未完成请求，GPU 资源等完成信号后才释放；旧 job 也会被取消。

所有新工具使用 eve.renderCapture schemaVersion=1。未知参数、枚举或附件不会被静默忽略。onUnavailable=report 允许在同一快照中列出缺失附件及原因；默认 fail 要求全部附件成功。后端没有该附件或本帧没有生成时，不会返回另一帧来代替。

架构与验证要求见[设计文档](superpowers/specs/2026-10-16-mcp-frame-buffer-capture.md)。Vulkan 复制遵循 [vkCmdCopyImageToBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImageToBuffer.html) 的 transfer usage、布局及单采样约束。


## 实现与验证

快照由 Graphics 独占管理；MCP worker 只持有不可变 CPU 数据租约。旧截图、旧 G-buffer 导出与新快照共用 Vulkan 原始读回内核，包含 transfer-write 到 host-read 屏障，并在 fence 完成后读取；同步依据见 [Khronos CPU readback 示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。旧接口保留其已有生命周期，新快照的唯一权威数据源是 FrameCaptureStore。

2026-10-16 在独立验证工作树完成严格警告 SHARED、OBJECT 引擎构建，以及 headless 公共头文件、provider present/absent 链接与运行检查。49 个相关 devtools 测试通过；最终 12 个捕获及旧渲染接口测试通过（其中 7 个与前者重叠，共 54 个不同用例）。新 GPU 捕获用例启用 Vulkan validation，通过同帧标记验证全部 11 个附件及快照冻结。

真实 SHARED 与 OBJECT 引擎均通过 MCP 捕获、11 个附件的 PNG/像素查询、原始 D32 导出、释放和旧截图兼容验证；最终共享读回内核再次通过 SHARED 全流程，预览经过人工查看。

源码门禁 `make check` 已运行，但被干净基线中已有的 10 个架构契约问题阻断，未修改基线或增加豁免。lighting-showcase 的持续运行还存在 descriptor 更新与 VMA 映射断言；零捕获请求的对照运行可复现，因此本次不宣称该示例全程 validation 无错误。WebGPU 首版明确返回 Unsupported。没有架构规则例外。
