# 水面与环境控制

[返回图形渲染模块](../graphics.md)

### Pcg 水面系统控制器

`eve.WaterSystemSettings()` 配置 `refreshRate`、`infiniteMode`、`autoRefresh`、
`ignoreSceneConditions`，并通过 `setAutoUpdateMode(0|1)` 选择 Interval 或 SceneConditions。
`eve.WaterSceneConditions()` 提供 `sunIntensity`、`hour`、`minute`、`sunAvailable`、
`timeAvailable`，以及 `setSunColor(r,g,b)`、`setSunDirection(x,y,z)`。调用者提交快照，
因此控制器不会搜索全局场景，也不会持有相机、太阳或玩家指针。

`eve.WaterSystemState()` 暴露权威 `seaLevel`、`positionX`、`positionY`、`positionZ`、
`refreshRemaining`、`sceneCheckFrames`、`refreshRevision` 和 `initialized` 状态。
先调用 `initializeWaterSystem(state,settings,scene,initialFrames)`；随后每帧调用
`advanceWaterSystem(state,settings,scene,playerX,playerZ,dt,nextFrames)`。返回的 Result 值为
本帧是否应重新生成反射。无限模式把 X/Z 跟随输入位置并始终把 Y 锁定为 seaLevel。
Interval 模式保留 Pcg 的严格小于零触发和 refreshRate 重置；SceneConditions 比较太阳颜色、
强度、方向与可选时间，并使用调用者注入的 nextFrames 取代隐藏随机数。
`updateWaterSeaLevel(state,level,regenerate)` 原子更新高度，并在要求重建时递增 refreshRevision。
所有失败都保留原状态；dt、刷新间隔、场景值和延迟会完整验证。
### Pcg WindManager 音量控制

`eve.VegetationWindAudioState()` 与
`eve.advanceVegetationWindAudio(state,windStrength,transitionTime,dt,enabled,clipAvailable)` 移植
WindManager 的风声控制器。状态拥有 currentWindSpeed、anchorVolume、volume、processing 和 playing；
函数只计算确定性的音量状态，不跨模块持有 Audio Source。脚本应创建循环、listener-relative Source，
每帧把 state.volume 写入 Source。实现保留源码行为：currentWindSpeed 不更新，非零风速每帧重启处理；
上升在目标下方 0.05 内吸附，下降比较不对称而通常渐近目标。dt 与 transitionTime 均显式注入。

### Pcg 程序化水面网格

`eve.WaterMeshSettings()` 提供 `sizeX`、`sizeY`、`sizeZ`、`densityX`、`densityY`、`height`，
并通过 `setType(0|1)` 选择 Plane 或 Circle。`eve.calculateWaterMeshTriangles(settings)` 在分配前返回
Pcg 算法的三角形数量；`water.createProceduralMesh(settings)` 验证并上传网格，返回顶点数量。
Plane 按 `round(density/40*size)+1` 建立规则格点。Circle 使用同心六边环，环数取
`round(densityX/40*sizeX/2)`，并保留 Pcg 的环缝合索引、UV 缩放与非对称 X/Z 密度行为。
无效、过小或超过单轴 4096 点的输入返回诊断且保留 Water 当前网格。Custom 对应调用者直接提供
EVEngine Mesh，不复制 Unity MeshFilter 的资源引用模式。

### PWS 水波方向

`water.setWaveDirectionAngle(degrees)` 返回标准 Result，并设置 PWS_WaterSystem 的 `directionAngle`，并将任意有限角度归一化到
[0,360)；`water.getWaveDirectionAngle()` 返回当前角度。Vulkan GLSL 与 WebGPU WGSL 都按该角度旋转
顶点位移和片元细浪坐标。实现把角度编码在 128 字节水参数块的 ripple-count 小数部分；整数涟漪数量保持
不变，避免超过 Vulkan 保证支持的最小 push-constant 容量。

### Pcg 水深色带

`eve.WaterDepthGradient()` 分别拥有颜色键和透明度键。`addColorStop(time,r,g,b)` 与
`addAlphaStop(time,a)` 返回 Result；`clear()` 清空两组键。`water.setDepthGradient(gradient,resolution)`
生成 `resolution × resolution` 的 RGBA8 Clamp 色带，并严格按 Pcg 使用 `x/resolution` 采样，
返回像素数量。可通过 `getDepthGradientTexture()` 将同一纹理设置给使用 Water shader 的
Renderable3D normal slot；直接调用 Water.draw 时会自动绑定。`clearDepthGradient()` 恢复深浅双色模式。
无键、重复时间或 2..4096 外的分辨率失败并保留当前纹理。

### Pcg 平面水反射规划

`eve.WaterPlanarReflectionSettings()` 移植 `PcgPlanarReflections.PlanarReflectionSettings`：反射开关、
天空跳过、Full/Half/Third/Quarter 分辨率倍率、clip-plane 偏移、反射层、阴影、统一或逐层渲染距离和
基础纹理分辨率。`setResolutionMultiplier(0..3)` 选择倍率，`setLayerDistance(layer,distance)` 设置 32 层距离。
`eve.WaterPlanarReflectionInput()` 通过 `setCameraPosition`、`setCameraForward` 及 `waterPlaneY`、`renderScale`、
`orthographic`、`reflectionOrPreviewCamera` 提供不可变帧快照。

调用 `buildWaterPlanarReflectionPlan(output,settings,input)` 后，output 包含镜像相机位置/方向、世界裁剪平面、
反射矩阵、排除水层 4 的 culling mask、32 层距离、捕获分辨率和 `shouldRender`。Third 精确保留 Pcg 的
0.33 倍率及整数截断。规划器纯计算且不持有相机、纹理或场景节点；调用者用结果配置现有离屏捕获，再将纹理
传给 `Water.drawWithPlanarReflection`。非法枚举、非有限值、负距离或无效分辨率返回 Result，保持 output 不变。

设置字段为 `disableSkyboxReflections`、`clipPlaneOffset`、`reflectLayers`、`shadows`、
`enableRenderDistance`、`enablePerLayerDistances`、`customRenderDistance` 和 `textureResolution`。
结果字段为 `textureWidth`、`textureHeight`、`cullingMask`、`renderShadows`；读取镜像数据使用
`getCameraX()`、`getCameraY()`、`getCameraZ()`、`getForwardY()`、`getLayerDistance(layer)` 与
`getReflectionMatrix(column,row)`。

### Pcg 水下效果状态机

`UnderwaterColorGradient.addStop(time,r,g,b)` 建立深度或时段颜色渐变，`clear()` 清空键。
`WaterUnderwaterSettings` 提供 `enabled`、`supportFog`、`supportPostFx`、`enableTransitionFx`、
`useCaustics`、`hdrp`、`overrideFogColor`、`fogDepth`、`fogDistance`、`hdrpFogDistance`、
`nearFogDistance`、`fogDensity`、`playbackVolume`、`causticSize`、`framesPerSecond`、
`causticTextureCount`、`overrideFogCurve`、`anisotropyShallow`、`anisotropyDeep`，并用
`setFogColorMultiplier`、`setOverrideFogMultiplier` 写入颜色乘数。

`WaterUnderwaterInput` 的 `cameraY`、`seaLevel`、`timeOfDay`、`causticTicks`、`hasMainLight` 和
`setMainLightColor` 是不可变帧输入。调用
`advanceWaterUnderwaterEffects(state,output,settings,input,depthGradient,timeGradient,postExposureCurve,postColorGradient)` 后，
`WaterUnderwaterState` 保存 `initialized`、`isUnderwater`、`causticFrame`；output 发布
`entered`、`exited`、`playSubmergeDown`、`playSubmergeUp`、`loopAudio`、`particles`、`horizon`、
`surfaceVfx`、`postFx`、`transitionFx`、`caustics`、`causticSize`、`depth01`、`fogStart`、
`fogEnd`、`hdrpBaseHeight`、`hdrpMeanFreePath`、`hdrpProbeDimmer`、`hdrpAnisotropy`、
`hdrpDepthExtent`，颜色由 `getFogR()`、`getFogG()`、`getFogB()` 读取。

相机位于海面或以下视为水下。普通路径按深度渐变、主光颜色和非负分量偏移生成雾；override 路径按
timeOfDay 渐变和乘数生成雾。HDRP 输出精确保留 Pcg 的深度插值常量。音频、粒子、Volumetric、灯光
cookie 与后处理仍由各 provider 消费该快照；状态机不持有跨模块对象。所有输入先完整验证，失败时 state
与 output 均保持不变。

`WaterSurfaceFogSnapshot` 保存离水后恢复的 `density`、`start`、`end`、`height`、`heightFalloff`，
颜色通过 `setColor(r,g,b)` 设置。`applyWaterUnderwaterFog(volumetric,output,surface)` 在完整验证后，把水下
`fogColor`、`fogDensity`、`fogStart`、`fogEnd`、`fogHeight` 写入借用的 `Volumetric`；离水时恢复 surface。
EVEngine 的距离雾要求非负起点，因此 Pcg 默认 `nearFogDistance=-4` 在 provider 边界钳制到 0。
`Volumetric.projectDirectionalCookie(graphics,depth,cookie,worldSize,intensity)` 从硬件深度重建世界位置，
沿主光方向建立投影平面并重复采样 cookie；参数只在本次渲染调用中借用，不跨帧保存纹理指针。

`UnderwaterScalarCurve.addKey(time,value)` 与 `clear()` 定义时段曝光曲线。设置中的
`timeDrivenPostFx`、`constantPostExposure`、`setConstantPostColor(r,g,b)` 选择 Pcg 的天气时段模式或
固定模式。输出的 `postExposure` 和 `getPostR()`、`getPostG()`、`getPostB()` 是本帧后处理值；
`WaterSurfacePostFxSnapshot` 用 `exposureEv` 和 `setColor(r,g,b)` 保存表面状态。
`applyWaterUnderwaterPostFx(graphics,camera,output,surface)` 将水下曝光写入借用的 Camera3D，并把 RGB
颜色过滤写入 Graphics 的最终 HDR 曝光阶段；离水时原子恢复 snapshot。Vulkan SPIR-V 与 WebGPU WGSL
使用相同的线性颜色乘法。
Pcg 内置 transition prefab 的 Y 尺寸 0.05 和 `blendDistance=0.025` 分别对应设置中的
`transitionHalfHeight=0.025` 与 `transitionBlendDistance=0.025`。水下距海面不超过半高时
`transitionWeight=1`，随后在 0.025 米内衰减到 0；`transitionVignette=0.25` 和
`transitionLensDistortion=0.252` 来自原始 Post Processing asset。输出发布同名 vignette/lens 值，
`WaterSurfacePostFxSnapshot.vignette` 与 `lensDistortion` 保存表面状态；适配器将它们接入 Vulkan/WebGPU
最终 HDR 合成的径向暗角和 UV 畸变。该 profile 启用的 Lift/Gamma/Gain trackball 先按 Unity Post Processing v2 的
`ColorToLift`、`ColorToInverseGamma`、`ColorToGain` 转为 HDR 系数，再按 `transitionWeight` 从
中性值混合；`setTransitionLift`、`setTransitionInverseGamma`、`setTransitionGain` 可覆盖资源值，
`WaterSurfacePostFxSnapshot.setLiftGammaGain(...)` 保存离水后的恢复状态。profile 的黑色 Color Filter 同样按
过渡权重从白色混合，并与既有水下时段颜色相乘；`setTransitionColorFilter(r,g,b)` 可覆盖该资源值。
脚本可用 `getTransitionLiftR()`、`getTransitionInverseGammaR()`、`getTransitionGainR()` 和
`getTransitionColorFilterR()` 读取代表性的红通道结果以诊断当前混合状态。
`transitionVignetteSmoothness=0.8`、`transitionLensScale=1.02`（surface snapshot 中对应
`vignetteSmoothness`、`lensScale`）保留 profile 的其余启用值；混合时分别从
Unity 的中性值 0.2 和 1.0 过渡。最终 pass 使用 Post Processing v2 的 classic vignette 幂函数以及正镜头
畸变的 theta/sigma/tangent 采样公式，而非简单二次 UV 偏移。
`WaterSurfaceMaterialSnapshot` 用 `setColor(r,g,b)` 和 `alpha` 保存水面 tint。
`applyWaterUnderwaterMaterial(renderable,output,surface)` 对应 Pcg `UpdateUnderwaterMaterial`：水下把
`getUnderwaterMaterialR()`、`getUnderwaterMaterialG()`、`getUnderwaterMaterialB()` 发布的颜色写入专用水面
Renderable3D，离水时恢复 snapshot。有主光时颜色来自 `mainLightColor`，没有主光时使用 Pcg 的
`#CFCFCF` 回退值；适配器同步借用对象且在验证完成前不修改 tint。
`applyWaterUnderwaterHorizon(horizon,output)` 同步借用 Renderable3D，并严格按状态机的 `horizon` 输出控制
可见性；HDRP 路径和离水状态都会隐藏该网格。`getVisible()` 可读取最终可见状态。

`WaterUnderwaterDisableTriggerState` 与 `WaterUnderwaterTriggerEvent` 对应 Pcg
`DisableUnderwaterFXTrigger`。事件的 `sensorTag`、`visitorTag`、`entered`、`exited` 从 `World3D` 的
begin/end trigger 缓冲复制 Sensor/Visitor Shape Tag，
再调用 `advanceWaterUnderwaterDisableTrigger(state,event,expectedSensorTag,expectedVisitorTag)`；匹配的 enter
把 `effectsEnabled` 设为 false，匹配的 exit 恢复为 true，其他标签不改变状态。每个事件必须恰有一个方向，
非法标签或方向在发布前失败。将结果写入 `WaterUnderwaterSettings.enabled` 后再推进水下状态机。

Pcg 自动景深跟焦计算使用 `DepthOfFieldFocusSettings/Input/State/Output` 和
`evaluateDepthOfFieldFocus(state, output, settings, input)`。应用层负责用 World3D 或其他权威场景查询提供
中心射线命中距离或目标距离；计算器复现 FollowScreen、FollowTarget、FixedOffset、玩家遮挡回退到最大距离
以及 `deltaSeconds * response` 平滑，并输出 focusDistance、aperture 和 focalLength。状态由调用者持有。
字段 `tracking`、`interactWithPlayer`、`focusOffset` 控制模式与偏移；输入字段 `hasRayHit`、
`hitPlayer`、`rayHitDistance`、`hasTarget`、`targetDistance` 描述查询结果。状态字段
`maximumExceeded` 记录玩家遮挡或最大距离回退，输出字段 `active` 表示效果是否启用。

`gfx.newDepthOfField()` 创建跨后端景深 compositor。`DepthOfField.apply(source, linearDepth,
focusDistance, focusRange, maximumBlurPixels, nearPlane, farPlane)` 返回借用的内部 HDR 结果纹理；
`applyTo` 写入调用者提供的 Canvas。`linearDepth` 使用 GBuffer `getDepthTexture()` 的归一化线性深度。
焦距附近保持清晰，离焦程度按世界距离差增长，并以 `maximumBlurPixels` 限制采样半径。
活动 Camera3D 可调用 `setDepthOfField(focusDistance, focusRange, maximumBlurPixels)`，把镜头状态接入
最终场景合成；`clearDepthOfField()` 关闭效果。启用时应同时在 RenderControl 开启 G-buffer。

### Pcg 反射探针距离裁剪

`ReflectionProbeRegistry.setDistanceCullingEnabled(true)` 启用 Pcg 风格的硬距离资格；
`setMaxRenderDistance(distance)` 设置正且有限的相机到探针中心距离。每次 `updateCamera(camera)` 更新
所有探针的资格，`getLastDistanceCulledCount()` 返回被拒绝数量。距离外探针不会消耗 `tick()` 的捕获和
过滤预算；重新进入范围会排队一次完整 recapture。距离判断使用严格的 `distance >= maximum` 淘汰，
对应 Pcg 的 `distance < maximum` 启用规则。注册表仍负责八槽稳定选择、优先级和滞回。

`getDistanceCullingEnabled()` 与 `getMaxRenderDistance()` 返回当前配置，便于运行时面板和存档系统读取。

Camera3D 的物理镜头与裁剪读取接口为 setPhysicalLens、getAperture、getFocalLength、getNearClip 和 getFarClip。物理参数保存在相机权威组件中，供照片模式和后处理共同读取。

### Pcg PhotoMode Water 权威配置

PcgWaterPhotoModeAuthority 通过 setAuthority 管理唯一 Water 字段租约。它接收反射开关、额外距离、分辨率、LOD bias、Underwater fog color、density 与 distance 七个字段。applyToPlanarSettings 将值写入 WaterPlanarReflectionSettings；其中 lodBias 同时缩放实际 capture layer culling distance。applyToUnderwaterSettings 将颜色和雾参数写入 WaterUnderwaterSettings，水下求值会直接使用照片模式颜色。

### Pcg PhotoMode Grass 权威配置

`GrassField.setPhotoModeAuthority(true)` 将当前 GPU 草地场注册为照片模式 Grass 域唯一所有者；关闭或销毁字段时撤销。`m_globalGrassDensity` 控制 shader 的确定性实例密度丢弃，`m_globalGrassDistance` 与 `m_cameraCellDistance` 缩放硬裁剪、淡出和渲染距离，`m_cameraCellSubdivision` 以二次幂调整 cell 覆盖范围。`getPhotoModeDensity`、`getPhotoModeDistance`、`getPhotoModeCellDistance` 和 `getPhotoModeCellSubdivision` 返回当前倍率。重新上传 foliage 或调用 `setTerrainDetailOverwrite` 后会再次应用这些倍率。

### Pcg PhotoMode Graphics 权威配置

`PcgGraphicsPhotoModeAuthority` 绑定 Graphics、RenderControl 与 Camera3D 后，以显式租约接收 LOD bias、anti-aliasing、shadow distance、shadow resolution 和 shadow cascades。LOD bias 直接缩放所有 Renderable3D 网格 LOD 的选择距离；AA 枚举切换真实 FXAA、SMAA、TAA 或关闭合成；阴影距离写入 Camera3D 每层 caster 裁剪，并投影到 `ShadowSchemeSettings::directionalMaxDistance`（钳制到 \[8, 256\] m）以扩展 CSM 覆盖。`m_shadowResolution`（0..3）映射为 atlas 边长 512/1024/2048/4096 并写入 scheme，触发动态阴影分辨率重建；级联数量目前仍为编译期 3 级（`m_shadowCascades` 保存在状态中供后续扩展）。

状态读取方法为 getAuthority、getReflectionEnabled、getReflectionDistance、getReflectionResolution、getReflectionLodBias、getFogDensity、getFogDistance、getFogRed、getFogGreen 和 getFogBlue。WaterPlanarReflectionSettings 也公开 lodBias。

### Pcg PhotoMode 全局阴影倍率

`PcgGraphicsPhotoModeAuthority` 同时接受 Lighting 域的 `m_globalShadowDistanceMultiplier`。实际 32 层 shadow caster 裁剪距离为 `m_shadowDistance * multiplier`，基础距离与倍率分别保存，任一字段变化都会重新投影。
### GPU skin influence limit

The native `Graphics::setMesh3DSkinInfluenceLimit` draw state selects the strongest 1, 2, or 4
vertex influences for subsequent skinned mesh draws. Retained weights are
renormalized in the forward, G-buffer, and shadow vertex paths. `Renderable3D`
applies Pcg LOD `SkinQuality` per selected level; `Auto` resolves to the engine
default of four influences.

### Pcg ReflectionMasker

`eve.WaterReflectionMasker()` 根据玩家在当前地形中的归一化 X/Z 位置控制平面反射。
`configure(channel,min,max,enabled)` 的 channel 为 0 R、1 G、2 B、3 A、4 RGBA，阈值为闭区间；
RGBA 模式任一通道命中即启用。`setMask(imageData)` 会复制像素，不跨帧保留图片指针，`clearMask()`
清除遮罩。`evaluate(playerX,playerZ,terrainX,terrainZ,width,depth,reflectionSettings)` 返回 0 Unchanged、
1 Enabled 或 2 Disabled，并在状态变化时同步 `WaterPlanarReflectionSettings.enabled`。
`getEnabled()`、`getHeightFeaturesEnabled()`、`getSampleX()` 和 `getSampleY()` 返回最近状态与采样坐标。

独立天空的 `eve.sky-wisps/3` 资源增加 `sunDisk.color`（线性 HDR RGB）和
`sunDisk.shape`（弧度半径、正柔化指数、非负反射倍率）。旧版本禁用圆盘。
参数随资源准备复制，太阳方向复用独立帧快照；绘制不推进模拟时间。
主视图和反射复用同一材质，圆盘在薄云混合、父材质对比度和高度雾之前合成。
圆盘颜色目前是明确的配置常量，尚未接入昼夜颜色曲线或二维动态云遮罩。

`eve.sky-wisps/4` 进一步要求十二个 `sunDiskCurve` 键（RGB 各四个）：
`[height,value,arriveTangent,leaveTangent]`，每通道高度严格递增，末键值为正。
按独立太阳方向计算高度，采用无权重三次 Hermite 插值及常量端点外推；
颜色相对于末键缩放。准备阶段验证并复制所有数据，绘制期间不读取文件或推进时钟。
版本 1—3 保留原有恒定圆盘颜色。该路径尚不包含日食、天气衰减或曲线 HSV 调整。

