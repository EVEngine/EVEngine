# Shader 与资源管理

[返回图形渲染模块](../graphics.md)

### HDR、景深与反射探针绑定

下列绑定用于 HDR 图像、反射探针采集/注册、天空环境、曝光、Bloom、Gaussian 景深（DOF）与反射链质量控制。
`Camera3D.setDepthOfField(focusDistance, maxBlurPx, focusRange)` 配置最终 HDR resolve 上的可分离 Gaussian 景深；
`maxBlurPx <= 0` 关闭效果。焦点平面用视空间距离表示；`getDofFocusDistance` / `getDofMaxBlur` /
`getDofFocusRange` 读取当前参数。HD-2D 缩微感可配合 `hd2d.Hd2dLook` 一键写入（见 [HD2D](../hd2d.md)）。
Cutout/masked 精灵需写入深度，DOF 才能正确对焦角色。

`applyConfiguredToCamera`、`applyToCamera`、`clearReflectionProbe`、`configureInfluence`、`filterAndPublish`、`getActiveCubemap`、`getAdaptiveFaceBudget`、`getAdaptiveFilterSamples`、`getBloomIntensity`、`getBloomThreshold`、`getCaptureClusteredLighting`、`getCaptureFarDistance`、`getCaptureLodDistanceScale`、`getCaptureMask`、`getCaptureTransparent`、`getCenterX`、`getCenterY`、`getCenterZ`、`getCount`、`getDofFocusDistance`、`getDofFocusRange`、`getDofMaxBlur`、`getEnvProbeCenterX`、`getEnvProbeCenterY`、`getEnvProbeCenterZ`、`getEnvProbeExtentX`、`getEnvProbeExtentY`、`getEnvProbeExtentZ`、`getEnvironmentLighting`、`getEnvironmentLightingIntensity`、`getExposure`、`getFaceCanvas`、`getGpuBudgetMs`、`getInfluenceBlendDistance`、`getInfluenceExtentX`、`getInfluenceExtentY`、`getInfluenceExtentZ`、`getInfluenceIntensity`、`getInfluencePriority`、`getLastCandidateCount`、`getLastCapturedFaceCount`、`getLastFilterSampleCount`、`getLastPublishedCount`、`getLastSelectedCount`、`getMaxRoughness`、`getPendingFaceCount`、`getPostProcessQuality`、`getPublishedRevision`、`getReflectionCaptureEnabled`、`getReflectionCaptureMask`、`getReflectionProbeCount`、`getReflectionQuality`、`getRefreshInterval`、`getResolution`、`getResolutionScale`、`getRevision`、`getSelectionHysteresis`、`getSkyB`、`getSkyFaceColor`、`getSkyFaceTexture`、`getSkyFaceTextureScale`、`getSkyG`、`getSkyIntensity`、`getSkyR`、`getSmoothedGpuDurationMs`、`getStagedRevision`、`getStagingCubemap`、`getThickness`、`getTotalCapturedFaceCount`、`getUpdateMode`、`hasEnvProbe`、`isAutoExposure`、`isCaptureComplete`、`isCapturePending`、`isRecaptureQueued`、`newHDRImageData`、`newReflectionProbeCapture`、`newReflectionProbeRegistry`、`queueCapture`、`queueCaptureAABB`、`remove`、`reportGpuDurationMs`、`requestCapture`、`setAutoExposure`、`setBloom`、`setCaptureClusteredLighting`、`setCaptureLodDistanceScale`、`setCaptureMask`、`setCaptureTransparent`、`setDepthOfField`、`setEnvironmentLighting`、`setExposure`、`setGpuBudgetMs`、`setMaxRoughness`、`setReflectionCaptureEnabled`、`setReflectionCaptureMask`、`setReflectionProbe`、`setReflectionQuality`、`setRefreshInterval`、`setResolutionScale`、`setSelectionHysteresis`、`setSkyColor`、`setSkyFaceColor`、`setSkyFaceTexture`、`setSkyFaceTextureScale`、`setUpdateMode`、`stageCapturedFaces`、`tick`、`tickAdaptive`、`updateCamera`。

### 预编译网格 Shader 与画布快照

`gfx.loadMeshShaderSpv(vertexPath, fragmentPath)` 从 VFS 加载 Vulkan SPIR-V，返回统一 Result。
空 vertexPath 使用默认 Mesh3D 顶点着色器；fragmentPath 必填。成功后 `value` 是 Graphics
所有的借用 Shader，只在 Graphics 生命周期内使用。调用者须预先编译匹配 Mesh3D 布局的程序。
加载失败返回诊断，不修改已有材质。仅在渲染/VM owner 线程同步调用。

`canvas.readPixels()` 返回独立 RGBA8 ImageData 的 Result；成功时脚本拥有 `value`，
之后修改或清空画布不影响快照。调用前先提交离屏绘制批次；此操作等待 GPU，适合验证、
导出，不适合每帧执行。失败返回诊断，不改变画布。HDR 使用既有独立接口。

Vulkan RGBA8 画布在分次提交之间保留像素，显式 clear 在下一次绘制提交时执行。
参考 `examples/ink-arena`，可用片元着色器和 alpha 混合在 GPU 上累积表面墨迹。

### 自定义网格资源和光栅状态

以下接口同步返回统一 Result，必须检查 `ok`。仅在 Graphics 所在线程、帧提交之外
调用，Shader 仍由 Graphics 所有；失败保留此前已提交的程序、资源和状态。
当前 Vulkan 提供实现，其他后端返回 Unsupported。

`gfx.replaceMeshShaderResourcesFromFiles(shader, vertexPath, fragmentPath, images, constantsPath)`
事务式替换普通网格 Shader 与独立 set 1 资源。vertexPath 为空使用默认顶点程序；
constantsPath 可为空。images 每项包含 `binding, path, format, dimension, width, height,
layers, mips, filter, wrapU, wrapV, anisotropy`。格式支持 `r8-unorm`、`rg8-unorm`、
`r16-unorm`、`rgba8-unorm`、`bgra8-unorm` 以及 RGBA/BGRA/BC1/BC3/BC7 对应
`-srgb` 格式；BC1/3/7 也支持 `-unorm`。dimension 为 `2d/array2d/cube`。
另支持 `rgba16-unorm`、`rgba16-float`、`rgba32-float`，浮点通道保留原始
IEEE-754 小端字节，不裁剪 HDR 数值。`dimension = "3d"` 时必须提供正整数
`depth` 和 `wrapW`，且 `layers = 1`；体纹理按 mip、Z 切片、行的顺序排列，
每级 mip 同时将 XYZ 尺寸减半并下限为 1。体纹理不接受 BC 压缩，XYZ 各不超过
2048，单纹理仍受 1 GiB 限制及设备能力约束。其他维度的 depth 省略或为 1。
纯常量缓冲或实例矩阵程序可提供空 images；图片、常量、实例矩阵不能同时为空。
wrapW 与 wrapU/V 同样使用 0 repeat、1 clamp；不提供时保持既有 2D 行为。
字节严格按 layer-major、mip-major 紧密排列，不经过图片解码、预乘或缩放。
filter 为 0 最近、1 双线性加最近 mip、2 三线性；wrapU/V 为 0 repeat、1 clamp。
Cube 必须是六个等宽高面；最多 16 个图像，binding 在 0..31，常量使用 binding 32，
16 字节对齐且最多 64 KiB。输入在调用内复制到 GPU 所有的状态，文件缓冲不跨帧借用。
普通 SPIR-V 重载会继续校验该资源布局。

`gfx.configureMeshShaderSurface(shader, blend, depthWrite, doubleSided)` 配置混合、
深度写入和剔除。blend 支持 `opaque/alpha/premultiplied/additive/multiply`。
`gfx.configureMeshShaderRaster(shader, compare, constantBias, slopeBias, colorMask)` 配置
深度比较、原生深度偏移和颜色写入。compare 支持 `less/lessEqual/always`；
colorMask 的位 0..3 对应 RGBA，7 表示仅 RGB。两个接口独立更新各自的状态，
状态在程序重载、资源替换和目标重建后保留。不适用于专用 hair/X-ray 管线。
偏移参数属于原生深度缓冲单位，跨后端不承诺相同数字产生逐像素相同结果。


### 自定义 Mesh Shader 实例资源

`replaceInstancedMeshShaderResourcesFromFiles(shader, vertex, fragment, images, constants, instances)`
与 `replaceMeshShaderResourcesFromFiles` 共享纹理/常量校验，并读取不可变实例矩阵文件。
`instances` 是小端 float32、列主序 mat4 记录（每条 64 字节，最多 64 MiB），必须有限、
仿射且可逆；上传复制到 Graphics 拥有的资源，调用者无需跨帧保留输入内存。
顶点程序可使用 set 1 / binding 33 的 `readonly buffer { mat4 transforms[]; }`，
其他可写或片段阶段存储缓冲不被接受。重载保留相同矩阵布局验证，失败不替换旧资源。

`drawMeshShaderInstances(mesh, shader, model, first, count)` 返回结构化 Result，必须检查 `ok`。
`model` 是 16 个列主序浮点值；`first`/`count` 选择已上传矩阵范围，顶点 Shader 的
`gl_InstanceIndex` 包含 first。零 count 为已校验的空操作；越界在绘制前拒绝。
仅在渲染线程的已开启 3D pass 内调用，不保留调用者引用、不调用脚本回调。
当前 Vulkan 实现该路径，其他后端明确返回 Unsupported；没有静默逐实例 CPU 绘制。
资源由 Graphics 统一释放，实例绘制本身不提供阴影、剔除或 ECS 注册，调用者按其
场景规则提交可见范围。小型 GPU 探针与地图装饰的集成证据分别记录，不能相互替代。

`Renderable3D.setInstanceRange(first, count, minimum, maximum, maximumHorizontalDistance)`
为场景对象配置值所有的实例范围并返回 Result；失败保留旧范围。
minimum/maximum 是实例矩阵输出空间中的 float3 包围盒，随后应用对象变换。
零距离禁用水平距离裁剪；正距离加上世界包围盒 X/Z 最大半径，与相机 X/Z
距离比较。主场景和离屏场景均执行 Vulkan 零到一深度的视锥裁剪。
`clearInstanceRange()` 恢复普通对象提交。范围元数据不持有资源指针，不是持久格式；
调用者在恢复场景时重新配置。更新必须在场景更新/渲染所属线程、绘制遍历以外执行。

当前仅支持单个静态自定义 Mesh Shader，使用 forward 绘制；调用者必须关闭
材质/对象投射阴影及对象遮挡投射，不支持 parts、蒙皮、LOD、hair 或 Xray。
设置时检查这些条件，后续改变对象能力时渲染阶段也检查，避免静默绘错。
实例缓冲实际容量仍由绘制接口校验。该路径读取既有 Renderable3D/Transform3D/
MeshRenderer 数据，不新增 ECS System 或在遍历中改变实体结构。

Vulkan 资源 Mesh Shader 的静态顶点输入允许 location 5 的 `vec4 tangent`：
xyz 是导入的对象空间切线，w 是相对于 `cross(normal, tangent)` 的副切线手性。
没有导入切线的网格提供零向量（w=0），消费者必须明确处理缺失数据；引擎不隐式
生成近似切线。带场景变换的导入会变换并归一化切线/副切线，保留镜像手性。
该输入目前属于 Vulkan 资源 Shader ABI；未实现此资源路径的后端仍返回 Unsupported。

`gfx.setSceneToneMapping("none" | "aces" | "filmic")` 返回 Result；默认 `aces` 保持既有输出。
`gfx.getSceneToneMapping()` 返回当前模式。`none` 仅在最终场景显示时将曝光后的
线性 RGB 裁剪到显示范围，然后执行需要的 sRGB 编码，不应用 ACES 曲线。
Bloom、曝光和 HDR 离屏纹理保持各自的职责；二维 UI 不经过此场景映射。
设置属于 Graphics 的显示状态，在 graphics/render 线程修改，不持有调用者引用
或触发回调。非法模式不改变旧值。Vulkan 支持三种模式；其他后端当前仅接受保留
默认模式的空操作，切换返回 Unsupported，不静默使用不同映射。

### 显示器 HDR（swapchain 输出）

渲染侧 HDR（线性 `RGBA16F` scene color、曝光、Bloom、ACES）与显示器 HDR 是两层。
下列 API 控制 **present 色彩空间**：

- `gfx.setDisplayOutputMode("sdr" | "auto" | "hdr10" | "scrgb")` → Result。
  `auto` 优先 HDR10，其次 scRGB，否则 SDR。`hdr10` / `scrgb` 在 Vulkan 上写入偏好并
  重建 swapchain；表面不支持时软回退到 SDR（不失败）。WebGPU 仅接受 `sdr`/`auto`。
- `gfx.getDisplayOutputMode()` / `gfx.getActiveDisplayColorSpace()` /
  `gfx.isDisplayHdrActive()` 区分“请求”与“实际选中”。
- `gfx.setDisplayHdrCalibration(paperWhiteNits, peakNits)`：纸白默认 200 nits、峰值 1000；
  用于 HDR10 PQ 与 scRGB 高光上限。
  `gfx.getDisplayPaperWhiteNits()` / `gfx.getDisplayPeakNits()` 读取当前校准值。
- `gfx.queryDisplayOutputSupport()` → `{sdr, hdr10, scRgb}`；需要已初始化的窗口 surface。

HDR10 使用 `A2B10G10R10` + `HDR10_ST2084`（PQ）；scRGB 使用 `RGBA16F` +
`EXTENDED_SRGB_LINEAR`（数值 1.0 = 80 nits，引擎按 `paperWhite/80` 缩放）。切换
模式时会按新 attachment format 重建 present render pass 与 pipelines。HDR 路径先在
线性 compose 目标上合成 scene + 2D/UI overlays（纸白相对 display-linear），再一次
fullscreen encode 到 swapchain；避免透明 UI 用不透明 encode pipeline 盖住场景，也避免
overlays 直接写入未编码的 HDR swapchain。Lavapipe / 无 HDR 显示器环境会保持 SDR。
# Gaussian scatter bloom

`gfx.setBloomFilter("gaussianScatter", 0.68, 6, 65472.0)` returns a checked Result
and selects half-resolution prefiltering, separable Gaussian downsampling, and
linear interpolation between pyramid levels. `"karisTent"` restores the default
four-level filter. `gfx.getBloomFilter()` returns the selected name.
`"gaussianPyramid"` selects up to six additively weighted photographic Gaussian
scales instead; it uses fixed stage sizes and weights, with `maxIterations`
capped at six. Its scatter argument does not alter those fixed kernels.
Zero threshold passes the whole scene. Camera intensity multiplies the weighted
sum once; the default filter remains `"karisTent"`.
Camera bloom intensity and linear threshold remain controlled by `Camera3D.setBloom`.
Scatter must be finite in [0,1], maximum iterations an integer in [1,16], and
clamp finite in (0,65504]. Invalid settings leave the previous filter unchanged.
Settings are copied, render-thread only, with no callbacks or retained script
references. Graphics owns generated shaders/targets; targets allocate on the next
build. Both GLSL and WGSL implementations exist; backend runtime validation status
is recorded by the relevant test run, not implied by source availability.

### 资源纹理内部共享

`ShaderImageInput::contentOwner` 可携带不可变文件快照身份。相同快照、尺寸、格式、mip/层数及
完整采样器配置复用 GPU 图像；无身份则独立上传。调用者必须保证该身份的字节永不修改。
GPU 仅持有弱身份，shader 共同拥有图像；释放一个 shader 不影响其他持有者，新文件快照不复用旧内容。
新图像在内部按约 64 MiB 暂存批次提交（单个大图像可超过阈值）。同步 Result 失败保留原 shader，
脚本无需 begin/end 上传批次。

