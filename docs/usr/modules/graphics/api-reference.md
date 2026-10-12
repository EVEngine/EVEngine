# graphics：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## AmbientOcclusion

- `applyFromDepthTo(gfx: Graphics, linearDepth: Texture, dest: Canvas)` → `void`；[绑定实现 L1213](../../../../src/modules/graphics/Graphics.cpp#L1213)。
- `applyOverlayTo(gfx: Graphics, aoMap: Texture, dest: Canvas)` → `void`；[绑定实现 L1211](../../../../src/modules/graphics/Graphics.cpp#L1211)。
- `getBias()` → `float`；[绑定实现 L1197](../../../../src/modules/graphics/Graphics.cpp#L1197)。
- `getBlurShader()` → `Shader`；[绑定实现 L1219](../../../../src/modules/graphics/Graphics.cpp#L1219)。
- `getDownscale()` → `float`；[绑定实现 L1204](../../../../src/modules/graphics/Graphics.cpp#L1204)。
- `getFromDepthShader()` → `Shader`；[绑定实现 L1221](../../../../src/modules/graphics/Graphics.cpp#L1221)。
- `getGtaoShader()` → `Shader`；[绑定实现 L1218](../../../../src/modules/graphics/Graphics.cpp#L1218)。
- `getHbaoShader()` → `Shader`；[绑定实现 L1217](../../../../src/modules/graphics/Graphics.cpp#L1217)。
- `getIntensity()` → `float`；[绑定实现 L1198](../../../../src/modules/graphics/Graphics.cpp#L1198)。
- `getMode()` → `string`；[绑定实现 L1189](../../../../src/modules/graphics/Graphics.cpp#L1189)。
- `getOverlayShader()` → `Shader`；[绑定实现 L1220](../../../../src/modules/graphics/Graphics.cpp#L1220)。
- `getPower()` → `float`；[绑定实现 L1199](../../../../src/modules/graphics/Graphics.cpp#L1199)。
- `getQuality()` → `string`；[绑定实现 L1187](../../../../src/modules/graphics/Graphics.cpp#L1187)。
- `getSsaoShader()` → `Shader`；[绑定实现 L1216](../../../../src/modules/graphics/Graphics.cpp#L1216)。
- `hasParam(name: string)` → `bool`；[绑定实现 L1200](../../../../src/modules/graphics/Graphics.cpp#L1200)。
- `setThickness(thickness: float)` → `void`；[绑定实现 L1195](../../../../src/modules/graphics/Graphics.cpp#L1195)。

## AntiAliasing

- `getFxaaShader()` → `Shader`；[绑定实现 L1316](../../../../src/modules/graphics/Graphics.cpp#L1316)。
- `getMode()` → `string`；[绑定实现 L1305](../../../../src/modules/graphics/Graphics.cpp#L1305)。
- `getNfaaShader()` → `Shader`；[绑定实现 L1319](../../../../src/modules/graphics/Graphics.cpp#L1319)。
- `getQuality()` → `string`；[绑定实现 L1303](../../../../src/modules/graphics/Graphics.cpp#L1303)。
- `getSmaaShader()` → `Shader`；[绑定实现 L1317](../../../../src/modules/graphics/Graphics.cpp#L1317)。
- `getSsaaShader()` → `Shader`；[绑定实现 L1318](../../../../src/modules/graphics/Graphics.cpp#L1318)。
- `hasParam(name: string)` → `bool`；[绑定实现 L1306](../../../../src/modules/graphics/Graphics.cpp#L1306)。

## GBuffer

- `getAlbedoTexture()` → `Texture`；[绑定实现 L657](../../../../src/modules/graphics/Graphics.cpp#L657)。
- `getBuffer(name: string)` → `Texture`；[绑定实现 L659](../../../../src/modules/graphics/Graphics.cpp#L659)。
- `hasBuffer(name: string)` → `bool`；[绑定实现 L658](../../../../src/modules/graphics/Graphics.cpp#L658)。

## GlobalIllumination

- `applyFromDepthTo(gfx: Graphics, packedAlbedo: Texture, dest: Canvas)` → `void`；[绑定实现 L1268](../../../../src/modules/graphics/Graphics.cpp#L1268)。
- `getIntensity()` → `float`；[绑定实现 L1260](../../../../src/modules/graphics/Graphics.cpp#L1260)。
- `getQuality()` → `string`；[绑定实现 L1251](../../../../src/modules/graphics/Graphics.cpp#L1251)。
- `hasParam(name: string)` → `bool`；[绑定实现 L1263](../../../../src/modules/graphics/Graphics.cpp#L1263)。
- `setThickness(thickness: float)` → `void`；[绑定实现 L1255](../../../../src/modules/graphics/Graphics.cpp#L1255)。

## GrassField

- `bakePlane(sizeX: float, sizeZ: float, segX: int, segZ: int, params: BakeParams)` → `void`；[绑定实现 L886](../../../../src/modules/graphics/Graphics.cpp#L886)。
- `getAtlas()` → `Texture`；[绑定实现 L900](../../../../src/modules/graphics/Graphics.cpp#L900)。
- `getDenseCount()` → `int`；[绑定实现 L901](../../../../src/modules/graphics/Graphics.cpp#L901)。
- `getDenseMesh()` → `Mesh`；[绑定实现 L897](../../../../src/modules/graphics/Graphics.cpp#L897)。
- `getFrameDuration()` → `float`；[绑定实现 L891](../../../../src/modules/graphics/Graphics.cpp#L891)。
- `getSparseCount()` → `int`；[绑定实现 L902](../../../../src/modules/graphics/Graphics.cpp#L902)。
- `getSparseMesh()` → `Mesh`；[绑定实现 L898](../../../../src/modules/graphics/Graphics.cpp#L898)。
- `setFrameDuration(seconds: float)` → `void`；[绑定实现 L890](../../../../src/modules/graphics/Graphics.cpp#L890)。

## Material

- `getAlbedoTexture()` → `Texture`；[绑定实现 L594](../../../../src/modules/graphics/Graphics.cpp#L594)。
- `getHeightTexture()` → `Texture`；[绑定实现 L598](../../../../src/modules/graphics/Graphics.cpp#L598)。
- `getMetallic()` → `float`；[绑定实现 L614](../../../../src/modules/graphics/Graphics.cpp#L614)。
- `getReceiveShadow()` → `bool`；[绑定实现 L624](../../../../src/modules/graphics/Graphics.cpp#L624)。
- `getShadingModel()` → `string`；[绑定实现 L581](../../../../src/modules/graphics/Graphics.cpp#L581)。
- `hasParam(name: string)` → `bool`；[绑定实现 L645](../../../../src/modules/graphics/Graphics.cpp#L645)。

## Outline

- `getColorB()` → `float`；[绑定实现 L1228](../../../../src/modules/graphics/Graphics.cpp#L1228)。
- `getColorG()` → `float`；[绑定实现 L1227](../../../../src/modules/graphics/Graphics.cpp#L1227)。
- `getColorR()` → `float`；[绑定实现 L1226](../../../../src/modules/graphics/Graphics.cpp#L1226)。
- `getDepthSensitivity()` → `float`；[绑定实现 L1234](../../../../src/modules/graphics/Graphics.cpp#L1234)。
- `getDepthThreshold()` → `float`；[绑定实现 L1232](../../../../src/modules/graphics/Graphics.cpp#L1232)。
- `getNormalThreshold()` → `float`；[绑定实现 L1236](../../../../src/modules/graphics/Graphics.cpp#L1236)。
- `hasParam(name: string)` → `bool`；[绑定实现 L1240](../../../../src/modules/graphics/Graphics.cpp#L1240)。
- `setDepthSensitivity(sensitivity: float)` → `void`；[绑定实现 L1233](../../../../src/modules/graphics/Graphics.cpp#L1233)。
- `setDepthThreshold(threshold: float)` → `void`；[绑定实现 L1231](../../../../src/modules/graphics/Graphics.cpp#L1231)。
- `setNormalThreshold(threshold: float)` → `void`；[绑定实现 L1235](../../../../src/modules/graphics/Graphics.cpp#L1235)。

## RenderControl

- `getPassCount()` → `int`；[绑定实现 L702](../../../../src/modules/graphics/Graphics.cpp#L702)。
- `getPassName(index: int)` → `string`；[绑定实现 L703](../../../../src/modules/graphics/Graphics.cpp#L703)。
- `hasPass(name: string)` → `bool`；[绑定实现 L704](../../../../src/modules/graphics/Graphics.cpp#L704)。
- `isCompiled()` → `bool`；[绑定实现 L701](../../../../src/modules/graphics/Graphics.cpp#L701)。

## Renderable3D

- `clearParts()` → `void`；[绑定实现 L55](../../../../src/modules/graphics/Renderable3DBindings.cpp#L55)。
- `getMaterial()` → `Material`；[绑定实现 L50](../../../../src/modules/graphics/Renderable3DBindings.cpp#L50)。
- `getPartCount()` → `int`；[绑定实现 L56](../../../../src/modules/graphics/Renderable3DBindings.cpp#L56)。
- `getPartMaterial(index: int)` → `Material`；[绑定实现 L59](../../../../src/modules/graphics/Renderable3DBindings.cpp#L59)。
- `getPartMesh(index: int)` → `Mesh`；[绑定实现 L58](../../../../src/modules/graphics/Renderable3DBindings.cpp#L58)。
- `getPartName(index: int)` → `string`；[绑定实现 L57](../../../../src/modules/graphics/Renderable3DBindings.cpp#L57)。

## ScreenSpaceReflection

- `applyFromSceneTo(gfx: Graphics, sceneColor: Texture, hwDepth: Texture, worldNormal: Texture, albedo: Texture, dest: Canvas)` → `void`；[绑定实现 L1294](../../../../src/modules/graphics/Graphics.cpp#L1294)。
- `getQuality()` → `string`；[绑定实现 L1279](../../../../src/modules/graphics/Graphics.cpp#L1279)。
- `getStrength()` → `float`；[绑定实现 L1287](../../../../src/modules/graphics/Graphics.cpp#L1287)。
- `hasParam(name: string)` → `bool`；[绑定实现 L1290](../../../../src/modules/graphics/Graphics.cpp#L1290)。
- `setMaxDistance(meters: float)` → `void`；[绑定实现 L1280](../../../../src/modules/graphics/Graphics.cpp#L1280)。
- `setMaxSteps(steps: int)` → `void`；[绑定实现 L1282](../../../../src/modules/graphics/Graphics.cpp#L1282)。
- `setStepLength(meters: float)` → `void`；[绑定实现 L1281](../../../../src/modules/graphics/Graphics.cpp#L1281)。
- `setStrength(strength: float)` → `void`；[绑定实现 L1284](../../../../src/modules/graphics/Graphics.cpp#L1284)。
- `setThickness(meters: float)` → `void`；[绑定实现 L1283](../../../../src/modules/graphics/Graphics.cpp#L1283)。

## Volumetric

- `applyFogTo(gfx: Graphics, linearDepth: Texture, dest: Canvas)` → `void`；[绑定实现 L755](../../../../src/modules/graphics/Graphics.cpp#L755)。
- `applyFromSceneTo(gfx: Graphics, scene: Texture, dest: Canvas)` → `void`；[绑定实现 L740](../../../../src/modules/graphics/Graphics.cpp#L740)。
- `drawOccluderSolid(gfx: Graphics, x: float, y: float, w: float, h: float)` → `void`；[绑定实现 L734](../../../../src/modules/graphics/Graphics.cpp#L734)。
- `drawOccluderTexture(gfx: Graphics, texture: Texture, x: float, y: float, w: float, h: float)` → `void`；[绑定实现 L735](../../../../src/modules/graphics/Graphics.cpp#L735)。
- `getDownscale()` → `float`；[绑定实现 L730](../../../../src/modules/graphics/Graphics.cpp#L730)。
- `getFogShader()` → `Shader`；[绑定实现 L839](../../../../src/modules/graphics/Graphics.cpp#L839)。
- `getLightScreenU()` → `float`；[绑定实现 L714](../../../../src/modules/graphics/Graphics.cpp#L714)。
- `getLightScreenV()` → `float`；[绑定实现 L715](../../../../src/modules/graphics/Graphics.cpp#L715)。
- `getMode()` → `string`；[绑定实现 L712](../../../../src/modules/graphics/Graphics.cpp#L712)。
- `getQuality()` → `string`；[绑定实现 L710](../../../../src/modules/graphics/Graphics.cpp#L710)。
- `getRayMarchShader()` → `Shader`；[绑定实现 L838](../../../../src/modules/graphics/Graphics.cpp#L838)。
- `hasParam(name: string)` → `bool`；[绑定实现 L726](../../../../src/modules/graphics/Graphics.cpp#L726)。
- `rayMarchTo(gfx: Graphics, linearDepth: Texture, dest: Canvas)` → `void`；[绑定实现 L742](../../../../src/modules/graphics/Graphics.cpp#L742)。
- `scatterTo(gfx: Graphics, occlusion: Texture, dest: Canvas)` → `void`；[绑定实现 L738](../../../../src/modules/graphics/Graphics.cpp#L738)。
- `setFogHeightFalloff(falloff: float)` → `void`；[绑定实现 L744](../../../../src/modules/graphics/Graphics.cpp#L744)。
- `setFogNoise(amount: float)` → `void`；[绑定实现 L747](../../../../src/modules/graphics/Graphics.cpp#L747)。
- `setLightScreenPos(x: float, y: float, width: float, height: float)` → `void`；[绑定实现 L716](../../../../src/modules/graphics/Graphics.cpp#L716)。
- `setLightScreenUV(u: float, v: float)` → `void`；[绑定实现 L713](../../../../src/modules/graphics/Graphics.cpp#L713)。
- `setShaftColor(r: float, g: float, b: float)` → `void`；[绑定实现 L719](../../../../src/modules/graphics/Graphics.cpp#L719)。

## Water

- `bindParams()` → `void`；[绑定实现 L1175](../../../../src/modules/graphics/Graphics.cpp#L1175)。
- `getReflectionIntensity()` → `float`；[绑定实现 L1160](../../../../src/modules/graphics/Graphics.cpp#L1160)。
- `getRippleAmplitude()` → `float`；[绑定实现 L1141](../../../../src/modules/graphics/Graphics.cpp#L1141)。
- `getRippleCount()` → `int`；[绑定实现 L1145](../../../../src/modules/graphics/Graphics.cpp#L1145)。
- `getRippleInterval()` → `float`；[绑定实现 L1147](../../../../src/modules/graphics/Graphics.cpp#L1147)。
- `getScreenSpaceReflection()` → `bool`；[绑定实现 L1164](../../../../src/modules/graphics/Graphics.cpp#L1164)。
- `getScreenSpaceReflectionStrength()` → `float`；[绑定实现 L1165](../../../../src/modules/graphics/Graphics.cpp#L1165)。
- `getSunIntensity()` → `float`；[绑定实现 L1162](../../../../src/modules/graphics/Graphics.cpp#L1162)。
- `getViewportHeight()` → `float`；[绑定实现 L1174](../../../../src/modules/graphics/Graphics.cpp#L1174)。
- `getViewportWidth()` → `float`；[绑定实现 L1173](../../../../src/modules/graphics/Graphics.cpp#L1173)。
- `getWaveAmplitude()` → `float`；[绑定实现 L1139](../../../../src/modules/graphics/Graphics.cpp#L1139)。
- `getWaveScale()` → `float`；[绑定实现 L1149](../../../../src/modules/graphics/Graphics.cpp#L1149)。
- `getWaveSpeed()` → `float`；[绑定实现 L1137](../../../../src/modules/graphics/Graphics.cpp#L1137)。
- `setReflectionIntensity(intensity: float)` → `void`；[绑定实现 L1159](../../../../src/modules/graphics/Graphics.cpp#L1159)。
- `setReflectionTint(r: float, g: float, b: float)` → `void`；[绑定实现 L1158](../../../../src/modules/graphics/Graphics.cpp#L1158)。
- `setRippleAmplitude(amp: float)` → `void`；[绑定实现 L1140](../../../../src/modules/graphics/Graphics.cpp#L1140)。
- `setRippleCount(count: int)` → `void`；[绑定实现 L1144](../../../../src/modules/graphics/Graphics.cpp#L1144)。
- `setRippleInterval(seconds: float)` → `void`；[绑定实现 L1146](../../../../src/modules/graphics/Graphics.cpp#L1146)。
- `setScreenSpaceReflection(enabled: bool, strength: float)` → `void`；[绑定实现 L1163](../../../../src/modules/graphics/Graphics.cpp#L1163)。
- `setSunIntensity(intensity: float)` → `void`；[绑定实现 L1161](../../../../src/modules/graphics/Graphics.cpp#L1161)。
- `setWaterColor(r: float, g: float, b: float)` → `void`；[绑定实现 L1157](../../../../src/modules/graphics/Graphics.cpp#L1157)。
- `setWaveAmplitude(amp: float)` → `void`；[绑定实现 L1138](../../../../src/modules/graphics/Graphics.cpp#L1138)。
- `setWaveScale(scale: float)` → `void`；[绑定实现 L1148](../../../../src/modules/graphics/Graphics.cpp#L1148)。
- `setWaveSpeed(speed: float)` → `void`；[绑定实现 L1136](../../../../src/modules/graphics/Graphics.cpp#L1136)。

## Waterfall

- `bindParams()` → `void`；[绑定实现 L980](../../../../src/modules/graphics/Graphics.cpp#L980)。
- `getBottomFoam()` → `float`；[绑定实现 L972](../../../../src/modules/graphics/Graphics.cpp#L972)。
- `getFlowSpeed()` → `float`；[绑定实现 L962](../../../../src/modules/graphics/Graphics.cpp#L962)。
- `getFoamAmount()` → `float`；[绑定实现 L974](../../../../src/modules/graphics/Graphics.cpp#L974)。
- `getReflectionIntensity()` → `float`；[绑定实现 L977](../../../../src/modules/graphics/Graphics.cpp#L977)。
- `getStreakCount()` → `int`；[绑定实现 L966](../../../../src/modules/graphics/Graphics.cpp#L966)。
- `getStreakScale()` → `float`；[绑定实现 L968](../../../../src/modules/graphics/Graphics.cpp#L968)。
- `getSunIntensity()` → `float`；[绑定实现 L979](../../../../src/modules/graphics/Graphics.cpp#L979)。
- `getTopFoam()` → `float`；[绑定实现 L970](../../../../src/modules/graphics/Graphics.cpp#L970)。
- `getTurbulence()` → `float`；[绑定实现 L964](../../../../src/modules/graphics/Graphics.cpp#L964)。
- `setBottomFoam(v: float)` → `void`；[绑定实现 L971](../../../../src/modules/graphics/Graphics.cpp#L971)。
- `setFlowSpeed(speed: float)` → `void`；[绑定实现 L961](../../../../src/modules/graphics/Graphics.cpp#L961)。
- `setFoamAmount(v: float)` → `void`；[绑定实现 L973](../../../../src/modules/graphics/Graphics.cpp#L973)。
- `setReflectionIntensity(intensity: float)` → `void`；[绑定实现 L976](../../../../src/modules/graphics/Graphics.cpp#L976)。
- `setStreakCount(count: int)` → `void`；[绑定实现 L965](../../../../src/modules/graphics/Graphics.cpp#L965)。
- `setStreakScale(scale: float)` → `void`；[绑定实现 L967](../../../../src/modules/graphics/Graphics.cpp#L967)。
- `setSunIntensity(intensity: float)` → `void`；[绑定实现 L978](../../../../src/modules/graphics/Graphics.cpp#L978)。
- `setTopFoam(v: float)` → `void`；[绑定实现 L969](../../../../src/modules/graphics/Graphics.cpp#L969)。
- `setTurbulence(t: float)` → `void`；[绑定实现 L963](../../../../src/modules/graphics/Graphics.cpp#L963)。
- `setWaterColor(r: float, g: float, b: float)` → `void`；[绑定实现 L975](../../../../src/modules/graphics/Graphics.cpp#L975)。

## 接收者见绑定源码

- `drawCanvas(canvas: Canvas, x: float, y: float, w: float, h: float)` → `void`；[绑定实现 L1415](../../../../src/modules/graphics/Graphics.cpp#L1415)。
- `getCanvas()` → `Canvas`；[绑定实现 L1418](../../../../src/modules/graphics/Graphics.cpp#L1418)。
- `getOutline()` → `Outline`；[绑定实现 L1461](../../../../src/modules/graphics/Graphics.cpp#L1461)。
- `getSceneColorTexture()` → `Texture`；[绑定实现 L1453](../../../../src/modules/graphics/Graphics.cpp#L1453)。
- `newCanvas(width: int, height: int)` → `Canvas`；[绑定实现 L1416](../../../../src/modules/graphics/Graphics.cpp#L1416)。
- `newGrassField()` → `GrassField`；[绑定实现 L1387](../../../../src/modules/graphics/Graphics.cpp#L1387)。
- `newGrassShader()` → `Shader`；[绑定实现 L1385](../../../../src/modules/graphics/Graphics.cpp#L1385)。
- `newOutline()` → `Outline`；[绑定实现 L1458](../../../../src/modules/graphics/Graphics.cpp#L1458)。
- `newScreenSpaceReflection()` → `ScreenSpaceReflection`；[绑定实现 L1463](../../../../src/modules/graphics/Graphics.cpp#L1463)。
- `newShaderFromWgsl(vertWgsl: string, fragWgsl: string)` → `Shader`；[绑定实现 L1379](../../../../src/modules/graphics/Graphics.cpp#L1379)。
- `newTextureFromFileRepeated(filename: string, repeatU: bool, repeatV: bool)` → `Texture`；[绑定实现 L1362](../../../../src/modules/graphics/Graphics.cpp#L1362)。
- `saveFramePng(path: string)` → `bool`；[绑定实现 L1413](../../../../src/modules/graphics/Graphics.cpp#L1413)。
- `setMesh3DCameraPos(x: float, y: float, z: float)` → `void`；[绑定实现 L1410](../../../../src/modules/graphics/Graphics.cpp#L1410)。
- `setMesh3DView(a: Array<dynamic>)` → `void`；[绑定实现 L1409](../../../../src/modules/graphics/Graphics.cpp#L1409)。
- `setMesh3DViewProj(a: Array<dynamic>)` → `void`；[绑定实现 L1408](../../../../src/modules/graphics/Graphics.cpp#L1408)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
