# HDR 与反射链 AAA 升级

## 当前基线

- TAA、RTGI、SSR 已形成自动时域链，并共享质量档位与历史失效规则。
- PBR、clustered、GPU-driven、visibility resolve 已统一使用解析 split-sum DFG。
- 水面与瀑布使用坡度粗糙度、GGX 直射高光和 SSR 到环境 IBL 的回退。
- scene color 已统一为线性 RGBA16F，材质阶段不再执行 tone mapping。
- cubemap mip 已由后端无关的 Hammersley + GGX importance sampling 预过滤器生成，Vulkan 与 WebGPU 上传同一 mip-major 数据。

## 目标帧图

1. 所有不透明、透明和体积光照写入线性 `RGBA16F` scene color。
2. RTGI 与 SSR 读取未 tone-map 的 HDR scene color，并输出 HDR radiance。
3. TAA 在 HDR 域完成重投影、裁剪、历史融合和锐化。
4. 自动曝光、bloom 与最终 tone mapping 在 TAA 后执行一次。
5. UI 在 tone mapping 后以显示空间合成，避免曝光影响界面颜色。
6. 环境 cubemap 在上传阶段生成跨面连续的 GGX specular mip 链及 diffuse irradiance。

## 实施阶段

### Phase 1: HDR scene target

- [x] Vulkan scene color 改为 `R16G16B16A16Sfloat`，WebGPU 改为 `RGBA16Float`。
- [x] scene、MSAA resolve、GPU-driven 和 visibility pipeline 从同一 HDR target format 构建。
- [x] 保持普通 Canvas 为 RGBA8，禁止用 Canvas 格式假设 scene color 格式。
- [x] scene alpha 恢复为透明覆盖率/不透明 1；TAA 从 GBuffer motion-depth 的 R 通道读取线性深度，并仅在自己的 history alpha 中保存深度。

### Phase 2: 末端 tone mapping

- [x] 从 Mesh3D、clustered、GPU-driven 和 visibility resolve 移除 `tonemapPeak`。
- [x] 新增 Vulkan/WebGPU 独立 ACES tone-map resolve，位于 TAA 后、UI 前。
- [x] ACES 输出按交换链格式统一传递函数：UNORM 目标由最终 shader 执行 sRGB OETF，SRGB 目标交给硬件编码；UI 保持 tone-map 后的既有显示空间合成，避免场景重复 gamma。
- [x] Vulkan 与 WebGPU 均优先选择 BGRA/RGBA UNORM 交换链，保证 tone-map 后 UI 使用相同的显示空间约定；仅在平台不支持 UNORM 时回退 SRGB 并由硬件执行传递函数。
- [x] Camera3D 支持 `setExposure(ev)` 曝光补偿，两后端在最终 resolve 中以 `exp2(ev)` 应用。
- [x] `setAutoExposure(enabled, minEV, maxEV)` 在 GPU 上对 4x4 log-luminance 样本做 percentile metering，裁掉两端各 12.5% 异常值，并将中间 75% 的几何平均映射到 18% 灰。
- [x] 最终 resolve 同时计量 TAA 历史平滑的 HDR radiance 与当前 raw scene，以前者作曝光历史近似，实现进暗处 1.5/s、进亮处 3.0/s 的非对称适应。
- [x] camera cut 复用 TAA 历史失效，曝光历史同步重置；TAA 关闭时退化为即时曝光。
- [x] 自动曝光计量已移出 final resolve：独立 1x1 percentile meter 裁掉两端各 12.5%，1x1 ping-pong 历史执行非对称眼适应，全分辨率 HDR pass 执行一次 pre-exposure；计量读取 TAA 后、bloom 前 radiance，避免泛光导致曝光泵动。
- [x] 显式 `drawScene3D` 与默认自动场景合成共享 `AA -> Bloom -> Exposure` HDR 帧图；WebGPU 在 scene/decal 后提交首个 command buffer，再运行共享 Canvas 后处理并以第二个 command buffer 完成 ACES、UI 和 present，避免读取未提交 attachment。
- [x] Camera3D `setBloom(intensity, threshold)` 在 ACES/曝光前对 HDR radiance 执行 soft-knee 预过滤和 12-tap 多尺度旋转 Poisson 扩散；核权重归一化，强度 0 时零额外采样。
- [x] 独立 bloom 金字塔使用 Vulkan/WebGPU 对等 shader 与共享管线：惰性持有四级 HDR Canvas，执行首级 soft-knee + Karis 降采样、后续 HDR 降采样和归一化 additive tent 重建；TAA 后在线性全分辨率目标合成，final resolve 不再执行逐像素 Poisson bloom。
- [x] readback 明确提供两种路径：`Canvas::newHDRImageData()` 从 HDR Canvas 原样返回线性 `RGBA16F`，现有 `newImageData()`/帧截图继续返回 tone-mapped 显示空间 `RGBA8`，禁止隐式曝光或量化。

### Phase 3: HDR 时域效果

- [x] TAA、SSR 和 RTGI 的工作目标与时域 ping-pong 历史迁移为内部 `RGBA16F` Canvas。
- [x] 时域历史保存曝光前线性 radiance，手动 EV 改变不需要曝光比修正。
- [x] TAA neighborhood clipping、颜色拒绝与锐化在可逆 `log2(1+Y)` + 归一化 CoCg 域执行，输出解码回线性 HDR radiance。
- [x] SSR/RTGI 共享的 temporal resolve 在 `log2(1+Y)` + 归一化 CoCg 域执行方差裁剪，并将孤立当前高亮限制在邻域均值约 3 stops 内。
- additive/premultiplied 合成使用浮点 target，避免每个 pass 后发生 UNORM 饱和。
- 半分辨率 RTGI/SSR 上采样继续使用深度、法线、粗糙度和 hit confidence 引导。

### Phase 4: IBL 预过滤（specular 已完成）

- [x] 建立后端无关的 cubemap 方向映射与跨面双线性采样工具。
- [x] 用 Hammersley + GGX importance sampling 生成 specular mip；roughness 与 mip 一一对应。
- [x] 在最后一级生成 cosine-weighted diffuse irradiance，不再把 box/specular mip 当 irradiance。
- [x] Vulkan/WebGPU 上传同一份预过滤 CPU 数据，避免方向、接缝和能量差异。
- [x] 内存缓存预过滤结果，资源 key 包含源像素、尺寸、mip 数和采样数，并限制驻留条目。

### Phase 5: 多局部反射探针

- [x] Camera3D 提供 8 个场景探针槽，每个槽保存独立 cubemap、box 影响体、intensity、blend distance 和 priority，并暴露 Squirrel API。
- [x] CPU 按 priority、相机到影响体距离和 slot 稳定选出两个候选，通过后端无关 `ReflectionProbeUpload` 进入 Vulkan/WebGPU 状态。
- [x] Vulkan/WebGPU 常规与 clustered descriptor 绑定两个局部 cubemap；两路 UBO 在旧 ABI 尾部追加双探针数据，descriptor/bind-group cache key 包含探针资源。Vulkan GPU-driven/visibility 通过现有 64 槽 bindless cubemap 数组上传全局与双局部 slot，WebGPU 复用扩展后的 mesh bind group。
- [x] 常规、clustered、GPU-driven 与 visibility PBR 路径均以 box projection 方向、边界距离执行双探针归一化混合，剩余权重连续回退全局 IBL；specular/diffuse 共用权重与 intensity 模型。
- [x] SSR 以 premultiplied replacement 覆盖已含局部探针/全局 IBL 的场景：命中时 RGB 与 alpha 共用估算可替换镜面份额，miss/低 confidence/出屏/高粗糙度输出零覆盖，因而连续保留“局部双探针 -> 全局 IBL”回退链，不重复采样 cubemap。
- [x] 透明 Mesh 复用主 PBR 双探针链；Water/Waterfall 自定义 shader 在 Vulkan/WebGPU 中使用同样的 box projection、边界权重、局部 intensity 与全局 IBL 剩余权重。Water 将 premultiplied SSR 仅 over 到环境反射结果，避免 alpha 二次相乘。
- [x] Water/Waterfall 移除材质阶段 `tonemapPeak`/高光压缩，线性 HDR 高光统一进入曝光、Bloom 和最终 ACES。
- [x] `ReflectionProbeCapture` 提供标准 `+X,-X,+Y,-Y,+Z,-Z` 六面线性 HDR Canvas 捕获，按每帧 face budget 增量更新，支持 dirty restart、完成 revision 和脚本状态查询。捕获相机关闭环境、局部探针、自动曝光与 Bloom，避免探针自反馈和六面曝光不一致。
- [x] 后端无关 API 区分 HDR staging cubemap 创建和逐 face Canvas 拷贝；`ReflectionProbeCapture::stageCapturedFaces()` 只在六面 capture 完整时提交，六个 face 全部成功后才推进 `stagedRevision`。
- [x] WebGPU 创建完整 mip 层级的 `RGBA16Float` 六层 staging cubemap，使用 GPU texture-to-texture copy 从 HDR Canvas 写入 base mip；staging 资源不会自动作为 active probe 发布。
- [x] WebGPU 使用 base-mip-only cube view 作为读源，逐 face/逐 mip `RGBA16Float` render pass 生成 Hammersley GGX specular 链，最后一级使用 cosine hemisphere 生成 diffuse irradiance；读写 texture subresource 范围显式不重叠。
- [x] `filterAndPublish()` 仅在 staging revision 与完整 capture revision 一致且 GPU 过滤提交成功时交换 active cubemap；之后的 capture 使用新 staging 资源，不覆盖光照正在采样的 active revision。
- [x] 探针刷新策略提供 `static` / `on_demand` / `time_sliced` / `realtime`；`tick(faceBudget, filterSamples)` 统一驱动 capture -> stage -> filter -> publish，`time_sliced` 通过 frame interval 限频。active/staging 在分辨率一致时 ping-pong 复用，避免持续刷新无界增长 GPU 资源。
- [x] `applyToCamera()` 将已发布 active cubemap 与 capture center 原子提交到 Camera3D 探针槽；无 published revision 时拒绝提交。工具 API 暴露 pending faces、当次/累计 face 数和最近 filter sample count，作为确定性工作量预算，GPU timestamp 仍需后续接入。
- [x] 3D 对象与探针捕获分别提供 32-bit reflection capture mask，离屏遍历在提交 draw 前执行按位裁剪，可排除探针自身、角色或特效层；掩码变化会使六面 capture revision 失效并重捕获。
- [x] 捕获相机可显式使用独立的全局 cubemap 环境光，局部探针始终关闭以阻断递归反馈；该能力只影响被捕获材质的 IBL，天空背景与大气仍需单独的 capture sky pass，不能视为已完成天空捕获。
- [x] 探针调度器提供毫秒预算、GPU duration EMA 与 70%/110% 迟滞控制；超预算时先减少每帧 face 数、再降低 filter samples，连续 4 次低预算才逐级恢复，避免负载振荡。duration 必须由后端 timestamp 上报，控制器不会用 CPU 提交时间冒充 GPU 时间。
- [x] Vulkan/WebGPU 的 3D 离屏 pass 正确消费 Canvas 独立 clear color，不再强制覆盖为窗口背景；探针可设置线性 HDR sky backup，确保空像素和反射 miss 不被固定黑色污染。该 backup 为无方向底色，方向性天空 cubemap/大气 capture pass 仍未完成。
- [x] Editor 模块提供 renderer-independent `ReflectionProbeVisualizer`：输出影响盒 12 条边、捕获中心、pending/captured/published 状态色，以及 capture/staged/published revision 标签；viewport 宿主无需依赖特定 Graphics 后端即可绘制与诊断探针。
- [x] Editor 对 Graphics 的既有实际依赖已补入唯一模块 manifest；探针可视化源码随模块目录自动收集，裁剪配置、链接闭包与脚本 boot list 不再依赖隐式链接关系。
- [x] 探针 HDR sky backup 支持 `+X,-X,+Y,-Y,+Z,-Z` 六面独立线性能量，可表达天顶/地平线/地面的方向差异并参与后续 GGX 过滤；它仍是每面常量，逐像素天空 cubemap/大气 capture pass 保持未完成状态。
- [x] 探针持久保存 influence box、intensity、blend distance 与 priority；`applyConfiguredToCamera()` 和 Editor visualizer 读取同一状态，旧 `applyToCamera(...extents...)` 保持兼容并同步配置，消除编辑器范围与实际光照范围漂移。
- [x] `ReflectionProbeRegistry` 支持场景注册任意数量探针，并按 priority、相机到 influence box 的距离、稳定注册顺序筛选最多 8 个已发布 revision 写入 Camera；未完成 capture/filter 的探针不会挤掉当前可用反射，GPU/渲染侧继续从 Camera 候选中选前两个参与混合。
- [x] Probe 与 Registry 建立双向非拥有关系并在双方析构时自动注销，脚本 GC 顺序不再产生 registry 悬空探针指针；重复 add/remove/clear 保持幂等。
- [x] Registry 提供场景级 `tick(faceBudget, filterBudget, filterSamples)`：所有探针 refresh policy 每帧正常推进，但 capture face 和 filter/publish 严格受全局预算约束；priority 相同时按等待年龄再按注册顺序调度，避免探针长期饥饿，并暴露当次 face/publish 工作量。
- [x] Registry 工作量统计累加 probe 实际完成 face 数而非请求预算；后端未初始化或拒绝工作时不会虚报 capture 负载，失败 filter 尝试仍只占用有限的当帧 filter budget。
- [x] DayNight 提供 `applyReflectionProbeSky()`：从当前太阳、大气、云量、闪电与曝光参数计算六个主方向的线性 HDR 辐射，并把 procedural skyCube 作为捕获几何的非递归 IBL；天空状态变化自动触发 probe revision 重捕获，不再要求脚本手工同步两套天空参数。
- [x] Registry 相机选择提供可配置 world-space hysteresis：上一帧已选探针在边界附近获得距离偏置，同 priority/距离时优先保留，减少相机跨 influence box 时的 cubemap popping、slot 重排和描述符抖动；探针移除/析构会同步清理迟滞历史。
- [x] 动态天空/IBL 采用 capture-input snapshot 与 deferred dirty：当前六面 revision 固定使用同一组天空颜色、强度和环境贴图，捕获期间的新 DayNight 状态只标记“发布后重捕获”，不会反复把 face cursor 重置到 `+X`，也不会发布由不同时间天空拼接的 cubemap。
- [x] 初始 capture 在尚未提交任何 face (`nextFace == 0`) 时允许天空/IBL 直接刷新快照，避免先发布一次黑色 cubemap；一旦已有 face 完成则严格转为 deferred dirty，保持 revision 内输入一致。
- [x] 动态场景可调用 probe `queueCapture()` 或 Registry `queueCapture(changedMask)`；Registry 只 dirty capture mask 相交的探针，已开始 revision 不被重置，适合 transform/material/lighting 变更事件驱动的局部反射更新。
- [x] Registry `queueCaptureAABB()` 同时按 changed layer mask 与 world AABB/capture-radius sphere 相交测试裁剪 dirty；半径取 capture far plane，确保 influence box 外但仍会出现在反射中的物体不会漏更新；min/max 反向输入自动规范化。
- [x] 离屏 3D capture 不再使用 legacy 单 mesh 简化路径：收集 multipart Material、距离 LOD、masked/transparent/hair surface，opaque 优先且透明按距离背到前排序；材质通过 `Material::bind()` 复用主 forward 语义，同时仍裁剪 shadow/GBuffer/AO 以守住增量探针预算。
- [x] 修复 HDR Canvas 的 3D attachment/pipeline 格式一致性：WebGPU 按 Canvas 选择 RGBA8/RGBA16F pipeline；Vulkan 为两种格式分别创建兼容 render pass、framebuffer 与 surface pipeline variants，并在 begin/draw/end 全程保持同一 HDR 状态。探针 face 现为真实线性 RGBA16F 渲染目标而非名义 HDR。
- [x] Vulkan/WebGPU HDR capture 均保留 custom material shader；Vulkan 为普通 Mesh3D 与 Hair shader 在创建时预建 RGBA8/RGBA16F offscreen pipeline variants，capture pass 按目标格式直接选择，不再退回默认 PBR。
- [x] 每个 probe face 在 draw-list 收集阶段对实际 LOD/part mesh 执行 world-space bounding sphere vs 90° capture frustum 裁剪；未知 bounds 保守保留。part material backup、hair 判定及透明 sort priority 与主 forward 收集规则对齐，face budget 不再为视锥外对象提交 draw。
- [x] Probe capture 提供 `[0.25,4]` LOD distance scale（小于 1 延后降级）与 transparent/hair capture 开关；两者进入 capture-input snapshot，质量档在六面 revision 中保持一致，支持静态高质量和实时低预算探针采用不同内容预算。
- [x] Probe capture 可选 per-face clustered lighting：当可见灯数超过 legacy pack 上限时，按该 face 的 view/clip/90° FOV 和目标分辨率构建 clustered table；unlit draw 临时关闭、后续 lit draw 恢复，选项进入 revision snapshot。低预算实时探针可显式关闭。
- [ ] Vulkan/WebGPU 已接入真实 timestamp query：Vulkan 同步读取 offscreen face render、六面 copy 与 GGX/irradiance compute filter，WebGPU 使用可选 feature 与三槽异步 readback 测量 face render 及完整 filter，并自动回传调度器。WebGPU 标准 timestamp 只能写在 pass 边界，纯 `CopyTextureToTexture` 尚未单独计时；Vulkan filter 也需 embedded SPIR-V 生成后才会实际执行，因此仍不宣称双后端 capture/copy/filter 全链闭环。
- [x] Vulkan 不再等待 VKBuilder cubemap 封装：使用原生 Vulkan-Hpp `RGBA16F` cube-compatible image 与逐 layer GPU copy。
- [ ] Vulkan 补齐 staging cubemap、GGX specular mip、diffuse irradiance 与同样的 active revision 交换；未完成的 capture/staged revision 不得对光照可见。
- [x] Vulkan 已使用原生 Vulkan-Hpp `vk::UniqueImage/Memory/ImageView` 分配 RGBA16F cube-compatible 全 mip image，并支持 HDR Canvas base mip 到指定 cubemap face 的 GPU copy、逐 subresource layout barrier、sampler 与 bindless cube 注册；GGX/irradiance filter 未完成前 staging 仍不会发布。
- [ ] Vulkan compute filter 源码与后端录制已完成：base-mip-only `samplerCube` -> 逐 mip `rgba16f image2DArray`，Hammersley GGX + 最末 mip cosine irradiance、6 face dispatch 与逐 subresource barrier。当前环境同时缺少 `glslc`/`glslangValidator`，embedded SPIR-V 尚未生成；代码以 `__has_include` 守卫，资产缺失时明确返回 unsupported 并禁止 staging publish，不能标记 Vulkan filter 完成。
- [x] DayNight 逐 texel 天空面会作为 capture far-plane background 写入 HDR face，保留大气渐变、太阳盘、星空与天气方向性；六张纹理进入 revision snapshot，time-sliced capture 不混用不同时刻天空，单色 HDR face 仅作为无纹理 backup。
- [x] DayNight 的逐 texel face 虽以 RGBA8 缓存，capture 会根据每面中心的线性大气 radiance / tone-mapped texture luminance 自动恢复 HDR 能量；每面 scale 与纹理共同进入 revision snapshot，并可由自定义天空脚本覆盖。

## 不变量

- Vulkan 与 WebGPU 使用相同色彩空间、曝光、DFG 和 cubemap 朝向约定。
- Vulkan compute 与 WebGPU render filter 使用相同 Hammersley 序列、tangent-frame 选轴、GGX alpha 下限与 cosine irradiance 分布，低 sample quality 档也不得产生后端特有的方向性噪声。
- Diffuse irradiance 使用 cosine-distributed hemisphere 的直接 radiance 平均；禁止在 cosine importance sampling 后再次乘 `NoL` 形成 `cos²` 偏置。Specular GGX 路径继续按 `NoL` 归一化。
- 显式 static/on-demand filter 支持 8-512 Hammersley samples；自适应 realtime controller 仍限制在 8-64，避免动态探针因高质量静态上限产生 GPU 尖峰。
- TAA/RTGI/SSR 历史不得跨分辨率、曝光突变、相机 cut 或功能停用复用。
- SSR miss、出屏、高粗糙度和低置信度区域连续回退到 IBL。
- SSR premultiplied 覆盖率按 metallic 从 15% 到 90% 估算可替换镜面份额，避免电介质反射连同漫反射整体衰减。
- RTGI 只向非金属漫反射份额注入间接 radiance，金属回退到 SSR/IBL 镜面链。
- SSR/RTGI 共享 temporal filter 时显式区分预乘反射与 RTGI `normalized radiance + energy`编码；过滤内部统一为预乘 radiance，避免低能量 GI 样本非线性放大。
- SSR/RTGI 共享空间滤波升级为 9-tap A-trous 核：轴向/对角核权重、局部深度梯度、去预乘 HDR log-luminance 与 confidence 联合引导，保留 `spatialStep` 参数供后续 1/2/4 多迭代。
- SSR/RTGI A-trous 使用双 HDR Canvas ping-pong，并在 temporal blend 前完成：low 复用单次合并 pass，medium 执行 step 1，high 执行 step 1/2，ultra 执行 step 1/2/4；history 保存最终滤波结果，质量切换同步失效历史。
- 后处理专用层次深度使用共享 RG min/max atlas：跨后端 2x2 reduction、最多八级、水平单纹理打包，独立于仅在 GPU-driven cull 时可用的 storage-buffer HZB；GBuffer 后每帧构建一次并由 RTGI/SSR 共享，level-0 深度读取已接入，hierarchical traversal 继续使用高层 RG 范围。
- SSR hierarchical trace 按 roughness cone footprint 选择 depth mip；射线位于 cell 最近表面之前时按 `2^mip` 安全跳步，进入 RG 区间后回落 level 0 执行二分精化、world-gap、hit-facing 与 continuity 验证。
- RTGI 半球样本按屏幕 footprint 选择 depth mip，先用 RG min/max 与 world-thickness padding 拒绝不可能命中的候选，再执行 level-0 world reconstruction、mismatch 与双余弦能量验证。
- 质量档只能减少采样或分辨率，不得改变材质能量模型。
- 自定义 legacy Mesh3D shader 的 UBO 前缀 ABI 保持兼容。

## 反射链质量预算

| 档位 | SSR 追踪步数 | SSR 分辨率 | RTGI 样本数 | RTGI 分辨率 | HZB 层数 | A-trous 显式迭代 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| low | 48 | 0.5x | 8 | 0.5x | 4 | 0（temporal resolve 内合并 step 1） |
| medium | 96 | 1.0x | 16 | 1.0x | 6 | step 1 |
| high | 160 | 1.0x | 24 | 1.0x | 8 | step 1/2 |
| ultra | 256 | 1.0x | 32 | 1.0x | 8 | step 1/2/4 |

- HZB 由 SSR 与 RTGI 共享，当帧取两者所需的统一反射质量预算，不重复构建。
- 分辨率、质量档、功能开关或 camera cut 变化时，SSR、RTGI、TAA 与曝光历史按所属链路失效，禁止跨预算复用。
- 该表是实现预算而非已验收的帧时结论；最终档位仍需在目标 GPU 上以 GPU timestamp 和动态场景图像证据校准。

## 完成证据

- HDR target 格式由 Vulkan/WebGPU 后端资源与 pipeline descriptor 共同证明。
- 抓帧证明 tone mapping 只发生在最终 resolve，scene color 中允许 RGB 大于 1。
- HDR 高光运动序列中 TAA、SSR、RTGI 无 NaN、拖尾和曝光闪烁。
- 六面接缝测试在每个 roughness mip 上连续，误差阈值由图像测试固定。
- 白炉测试覆盖 metallic 0/1、roughness 0.04/0.25/0.5/1，验证能量不增益。
- Vulkan 与 WebGPU 相同场景的 tone-mapped 图像差异进入既定 parity 阈值。
