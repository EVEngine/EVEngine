# fluids：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## FluidSim

- `getMaxParticles()` → `int`；[绑定实现 L448](../../../../src/modules/fluids/Fluids.cpp#L448)。
- `getParticleCount()` → `int`；[绑定实现 L447](../../../../src/modules/fluids/Fluids.cpp#L447)。
- `setAdhesion(adhesion: float)` → `void`；[绑定实现 L440](../../../../src/modules/fluids/Fluids.cpp#L440)。
- `setCohesion(cohesion: float)` → `void`；[绑定实现 L439](../../../../src/modules/fluids/Fluids.cpp#L439)。
- `setDamping(damping: float)` → `void`；[绑定实现 L442](../../../../src/modules/fluids/Fluids.cpp#L442)。
- `setGravity(x: float, y: float, z: float)` → `void`；[绑定实现 L437](../../../../src/modules/fluids/Fluids.cpp#L437)。
- `setParticleRadius(radius: float)` → `void`；[绑定实现 L443](../../../../src/modules/fluids/Fluids.cpp#L443)。
- `setPbfIterations(passes: int)` → `void`；[绑定实现 L441](../../../../src/modules/fluids/Fluids.cpp#L441)。
- `setSdfSphere(cx: float, cy: float, cz: float, radius: float, res: int)` → `void`；[绑定实现 L436](../../../../src/modules/fluids/Fluids.cpp#L436)。
- `setSupportRadius(h: float)` → `void`；[绑定实现 L444](../../../../src/modules/fluids/Fluids.cpp#L444)。
- `setViscosity(viscosity: float)` → `void`；[绑定实现 L438](../../../../src/modules/fluids/Fluids.cpp#L438)。
- `spawnDrop(center: vec3, radius: float, count: int)` → `int`；[绑定实现 L445](../../../../src/modules/fluids/Fluids.cpp#L445)。
- `usingGpu()` → `bool`；[绑定实现 L449](../../../../src/modules/fluids/Fluids.cpp#L449)。

## FluidSurface

- `compositeConfiguredSceneRefraction()` → `dynamic`；[绑定实现 L541](../../../../src/modules/fluids/Fluids.cpp#L541)。
- `compositeConfiguredSurfaceBlend()` → `dynamic`；[绑定实现 L560](../../../../src/modules/fluids/Fluids.cpp#L560)。
- `compositeDiffuse()` → `dynamic`；[绑定实现 L507](../../../../src/modules/fluids/Fluids.cpp#L507)。
- `compositeSceneRefraction()` → `dynamic`；[绑定实现 L519](../../../../src/modules/fluids/Fluids.cpp#L519)。
- `configureAnisotropy()` → `dynamic`；[绑定实现 L610](../../../../src/modules/fluids/Fluids.cpp#L610)。
- `configureColors()` → `dynamic`；[绑定实现 L606](../../../../src/modules/fluids/Fluids.cpp#L606)。
- `configureFoam()` → `dynamic`；[绑定实现 L515](../../../../src/modules/fluids/Fluids.cpp#L515)。
- `configureMaterial()` → `dynamic`；[绑定实现 L597](../../../../src/modules/fluids/Fluids.cpp#L597)。
- `configureParticleBlend()` → `dynamic`；[绑定实现 L556](../../../../src/modules/fluids/Fluids.cpp#L556)。
- `configureProjection()` → `dynamic`；[绑定实现 L619](../../../../src/modules/fluids/Fluids.cpp#L619)。
- `configureReflection()` → `dynamic`；[绑定实现 L603](../../../../src/modules/fluids/Fluids.cpp#L603)。
- `configureRefraction()` → `dynamic`；[绑定实现 L533](../../../../src/modules/fluids/Fluids.cpp#L533)。
- `configureRefractionEnabled()` → `dynamic`；[绑定实现 L538](../../../../src/modules/fluids/Fluids.cpp#L538)。
- `configureRendererSettings()` → `dynamic`；[绑定实现 L581](../../../../src/modules/fluids/Fluids.cpp#L581)。
- `configureSurface()` → `dynamic`；[绑定实现 L572](../../../../src/modules/fluids/Fluids.cpp#L572)。
- `configureSurfaceBlend()` → `dynamic`；[绑定实现 L552](../../../../src/modules/fluids/Fluids.cpp#L552)。
- `configureSurfaceBlurRadius()` → `dynamic`；[绑定实现 L594](../../../../src/modules/fluids/Fluids.cpp#L594)。
- `configureSurfaceDownsample()` → `dynamic`；[绑定实现 L613](../../../../src/modules/fluids/Fluids.cpp#L613)。
- `configureSurfaceEnabled()` → `dynamic`；[绑定实现 L578](../../../../src/modules/fluids/Fluids.cpp#L578)。
- `configureThicknessDownsample()` → `dynamic`；[绑定实现 L616](../../../../src/modules/fluids/Fluids.cpp#L616)。
- `copyToImage()` → `dynamic`；[绑定实现 L631](../../../../src/modules/fluids/Fluids.cpp#L631)。
- `copyToTexture()` → `dynamic`；[绑定实现 L494](../../../../src/modules/fluids/Fluids.cpp#L494)。
- `getHeight()` → `int`；[绑定实现 L628](../../../../src/modules/fluids/Fluids.cpp#L628)。
- `getWidth()` → `int`；[绑定实现 L627](../../../../src/modules/fluids/Fluids.cpp#L627)。
- `prepare()` → `dynamic`；[绑定实现 L491](../../../../src/modules/fluids/Fluids.cpp#L491)。
- `renderConfiguredVolume()` → `dynamic`；[绑定实现 L483](../../../../src/modules/fluids/Fluids.cpp#L483)。
- `renderGasVolume()` → `dynamic`；[绑定实现 L467](../../../../src/modules/fluids/Fluids.cpp#L467)。
- `renderVolume(solver: VolumeFluid)` → `dynamic`；[绑定实现 L455](../../../../src/modules/fluids/Fluids.cpp#L455)。
- `renderVolumeColorOnly(solver: VolumeFluid)` → `dynamic`；[绑定实现 L465](../../../../src/modules/fluids/Fluids.cpp#L465)。
- `renderVolumeColorToTexture()` → `dynamic`；[绑定实现 L498](../../../../src/modules/fluids/Fluids.cpp#L498)。
- `renderVolumeInterpolated()` → `dynamic`；[绑定实现 L457](../../../../src/modules/fluids/Fluids.cpp#L457)。
- `renderVolumeWithoutSurface()` → `dynamic`；[绑定实现 L475](../../../../src/modules/fluids/Fluids.cpp#L475)。
- `rendererSettings()` → `dynamic`；[绑定实现 L590](../../../../src/modules/fluids/Fluids.cpp#L590)。
- `setCamera(eye: vec3, target: vec3, up: vec3, fovYDeg: float)` → `void`；[绑定实现 L623](../../../../src/modules/fluids/Fluids.cpp#L623)。
- `setCameraXYZ(ex: float, ey: float, ez: float, tx: float, ty: float, tz: float, fov: float)` → `dynamic`；[绑定实现 L624](../../../../src/modules/fluids/Fluids.cpp#L624)。
- `setMode(mode: int)` → `void`；[绑定实现 L571](../../../../src/modules/fluids/Fluids.cpp#L571)。
- `usingGpu()` → `bool`；[绑定实现 L629](../../../../src/modules/fluids/Fluids.cpp#L629)。
- `writePpm(path: string)` → `void`；[绑定实现 L630](../../../../src/modules/fluids/Fluids.cpp#L630)。

## VolumeFluid

- `accumulateExternalForceZones(object: dynamic, fixedTimeSeconds: float)` → `dynamic`；[绑定实现 L254](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L254)。
- `accumulateWindZones(object: dynamic, fixedTimeSeconds: float)` → `dynamic`；[绑定实现 L247](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L247)。
- `actorMassProperties(actorGroup: int)` → `dynamic`；[绑定实现 L502](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L502)。
- `addRandomVelocity(intensity: float, seed: int)` → `dynamic`；[绑定实现 L483](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L483)。
- `applyActorGroupColors()` → `dynamic`；[绑定实现 L459](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L459)。
- `applyDataColors(channel: int, object: dynamic)` → `dynamic`；[绑定实现 L461](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L461)。
- `applyMaterialChannels()` → `dynamic`；[绑定实现 L599](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L599)。
- `applyMaterialChannelsWithColors(object: dynamic)` → `dynamic`；[绑定实现 L601](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L601)。
- `applyParticleDrag(particleIndex: int, x: float, y: float, z: float, stiffness: float, damping: float, seconds: float)` → `dynamic`；[绑定实现 L559](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L559)。
- `applyQueryColors(queryObject: dynamic, colorObject: dynamic, r: float, g: float, b: float, a: float, maxHitsPerQuery: int)` → `dynamic`；[绑定实现 L657](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L657)。
- `applyQueryColorsPreservingOutside(queryObject: dynamic, colorObject: dynamic, maxHitsPerQuery: int)` → `dynamic`；[绑定实现 L674](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L674)。
- `applyRandomColors(object: dynamic, seed: int)` → `dynamic`；[绑定实现 L472](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L472)。
- `applySolidColor(r: float, g: float, b: float, a: float)` → `dynamic`；[绑定实现 L453](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L453)。
- `applyVelocityColors(sensibility: float)` → `dynamic`；[绑定实现 L456](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L456)。
- `bindDynamicParticles(object: dynamic, colliderLabel: int, compliance: float, breakThreshold: float, constrainOrientation: bool)` → `dynamic`；[绑定实现 L317](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L317)。
- `bindStaticParticles(object: dynamic, colliderLabel: int, constrainOrientation: bool)` → `dynamic`；[绑定实现 L304](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L304)。
- `clear()` → `dynamic`；[绑定实现 L295](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L295)。
- `configureParticleEvents(capacity: int)` → `dynamic`；[绑定实现 L283](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L283)。
- `contacts()` → `dynamic`；[绑定实现 L736](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L736)。
- `debugParticleFrames(actorGroup: int, size: float, maxParticles: int)` → `dynamic`；[绑定实现 L369](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L369)。
- `debugParticleGrid(maxCells: int)` → `dynamic`；[绑定实现 L353](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L353)。
- `debugSdfSlice(colliderLabel: int, axis: int, slice: float, maxDistance: float, maxSamples: int)` → `dynamic`；[绑定实现 L430](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L430)。
- `drainParticleEvents()` → `dynamic`；[绑定实现 L291](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L291)。
- `emitBurst(object: dynamic, count: int)` → `dynamic`；[绑定实现 L739](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L739)。
- `getParticleCount()` → `dynamic`；[绑定实现 L748](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L748)。
- `grabContactParticles(colliderLabel: int, x: float, y: float, z: float, qx: float, qy: float, qz: float, qw: float, distanceThreshold: float)` → `dynamic`；[绑定实现 L568](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L568)。
- `killActorParticles(actorGroup: int)` → `dynamic`；[绑定实现 L551](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L551)。
- `killParticle(particleIndex: int)` → `dynamic`；[绑定实现 L296](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L296)。
- `overlapBox(x: float, y: float, z: float, hx: float, hy: float, hz: float, qx: float, qy: float, qz: float, qw: float)` → `dynamic`；[绑定实现 L620](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L620)。
- `overlapSphere(x: float, y: float, z: float, radius: float)` → `dynamic`；[绑定实现 L608](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L608)。
- `paintSphere(x: float, y: float, z: float, radius: float, material: dynamic)` → `dynamic`；[绑定实现 L613](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L613)。
- `particleImpostors(actorGroup: int, radiusScale: float, r: float, g: float, b: float, a: float, alpha: float, maxInstances: int)` → `dynamic`；[绑定实现 L408](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L408)。
- `particleInstances(actorGroup: int, sx: float, sy: float, sz: float, alpha: float, maxInstances: int)` → `dynamic`；[绑定实现 L387](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L387)。
- `queryBatch(object: dynamic, maxHitsPerQuery: int)` → `dynamic`；[绑定实现 L648](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L648)。
- `queryBox(x: float, y: float, z: float, hx: float, hy: float, hz: float, qx: float, qy: float, qz: float, qw: float, contactOffset: float, maxDistance: float, maxHits: int, phaseMask: int)` → `dynamic`；[绑定实现 L639](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L639)。
- `querySimplexes(object: dynamic, maxHitsPerQuery: int)` → `dynamic`；[绑定实现 L699](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L699)。
- `querySphere(x: float, y: float, z: float, radius: float, contactOffset: float, maxDistance: float, maxHits: int, phaseMask: int)` → `dynamic`；[绑定实现 L632](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L632)。
- `raycast(ox: float, oy: float, oz: float, dx: float, dy: float, dz: float, maxDistance: float, maxHits: int, phaseMask: int)` → `dynamic`；[绑定实现 L626](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L626)。
- `releaseGrabbedParticles(colliderLabel: int)` → `dynamic`；[绑定实现 L590](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L590)。
- `restore(object: dynamic)` → `dynamic`；[绑定实现 L271](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L271)。
- `sampleField(object: dynamic)` → `dynamic`；[绑定实现 L344](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L344)。
- `setActorCollisionFilter(actorGroup: int, collisionFilter: int)` → `dynamic`；[绑定实现 L524](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L524)。
- `setActorFilterCategory(actorGroup: int, category: int)` → `dynamic`；[绑定实现 L516](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L516)。
- `setActorSelfCollisions(actorGroup: int, enabled: bool)` → `dynamic`；[绑定实现 L544](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L544)。
- `setColliders(object: dynamic)` → `dynamic`；[绑定实现 L708](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L708)。
- `setGravity(x: float, y: float, z: float)` → `dynamic`；[绑定实现 L230](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L230)。
- `setHeightFieldColliders(object: dynamic)` → `dynamic`；[绑定实现 L722](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L722)。
- `setParticleExternalForces(object: dynamic)` → `dynamic`；[绑定实现 L240](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L240)。
- `setParticleWinds(object: dynamic)` → `dynamic`；[绑定实现 L233](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L233)。
- `setSdfColliders(object: dynamic)` → `dynamic`；[绑定实现 L715](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L715)。
- `setSimplexes(object: dynamic)` → `dynamic`；[绑定实现 L692](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L692)。
- `setStitches(object: dynamic)` → `dynamic`；[绑定实现 L337](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L337)。
- `snapshot()` → `dynamic`；[绑定实现 L268](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L268)。
- `stepWithThermalContacts(dt: float, substeps: int, object: dynamic)` → `dynamic`；[绑定实现 L261](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L261)。
- `teleportActor(currentObject: dynamic, targetObject: dynamic)` → `dynamic`；[绑定实现 L490](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L490)。
- `unbindStaticParticles(object: dynamic)` → `dynamic`；[绑定实现 L331](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L331)。
- `updateActorMaterial(actorGroup: int, emissionObject: dynamic)` → `dynamic`；[绑定实现 L533](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L533)。
- `updateGrabbedParticles(colliderLabel: int, x: float, y: float, z: float, qx: float, qy: float, qz: float, qw: float)` → `dynamic`；[绑定实现 L579](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L579)。
- `updateSdfColliderPoses(object: dynamic)` → `dynamic`；[绑定实现 L729](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L729)。

## VolumeFluidContactTracker

- `reset()` → `dynamic`；[绑定实现 L18](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L18)。

## VolumeFluidCoupling

- `attach(body: Body3D, object: dynamic)` → `dynamic`；[绑定实现 L95](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L95)。
- `detach(label: int)` → `dynamic`；[绑定实现 L108](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L108)。
- `lastClampedBodyCount()` → `int`；[绑定实现 L112](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L112)。
- `setImpulseLimits(linear: float, angular: float)` → `dynamic`；[绑定实现 L109](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L109)。
- `stepWithThermalContacts(world: World3D, fluid: VolumeFluid, dt: float, substeps: int, object: dynamic)` → `dynamic`；[绑定实现 L123](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L123)。

## VolumeFluidDiffuse

- `clear()` → `dynamic`；[绑定实现 L92](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L92)。
- `getAvailableCapacity()` → `dynamic`；[绑定实现 L91](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L91)。
- `getParticleCount()` → `dynamic`；[绑定实现 L90](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L90)。
- `restore(object: dynamic)` → `dynamic`；[绑定实现 L69](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L69)。
- `snapshot()` → `dynamic`；[绑定实现 L66](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L66)。

## VolumeFluidEmitter

- `advanceBurst(solver: VolumeFluid, object: dynamic, count: int, threshold: float)` → `dynamic`；[绑定实现 L210](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L210)。
- `emitParticle(solver: VolumeFluid, object: dynamic, offset: float, dt: float)` → `dynamic`；[绑定实现 L182](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L182)。

## VolumeFluidFoam

- `advanceFromActor(fluid: VolumeFluid, pool: VolumeFluidDiffuse, dt: float, actorGroup: int)` → `dynamic`；[绑定实现 L40](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L40)。
- `advanceFromActorInterpolated(fluid: VolumeFluid, pool: VolumeFluidDiffuse, dt: float, actorGroup: int, alpha: float)` → `dynamic`；[绑定实现 L52](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L52)。
- `restore(object: dynamic)` → `dynamic`；[绑定实现 L24](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L24)。
- `snapshot()` → `dynamic`；[绑定实现 L21](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L21)。

## VolumeFluidJetEmitter

- `advanceMoving(solver: VolumeFluid, object: dynamic, begin: dynamic, end: dynamic, dt: float, limit: int, threshold: float, inheritVelocity: float)` → `dynamic`；[绑定实现 L141](../../../../src/modules/fluids/VolumeFluidBindings.cpp#L141)。

## 接收者见绑定源码

- `composeVolumeEmitterShapes(baseObject: dynamic, shapesObject: dynamic)` → `dynamic`；[绑定实现 L177](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L177)。
- `fluidRendererSettingsDefaults()` → `dynamic`；[绑定实现 L428](../../../../src/modules/fluids/Fluids.cpp#L428)。
- `getName()` → `string`；[绑定实现 L645](../../../../src/modules/fluids/Fluids.cpp#L645)。
- `getRendererCount()` → `int`；[绑定实现 L649](../../../../src/modules/fluids/Fluids.cpp#L649)。
- `getSimulatorCount()` → `int`；[绑定实现 L647](../../../../src/modules/fluids/Fluids.cpp#L647)。
- `newSimulator(maxParticles: int)` → `FluidSimulator`；[绑定实现 L646](../../../../src/modules/fluids/Fluids.cpp#L646)。
- `newSurfaceRenderer(width: int, height: int)` → `FluidSurfaceRenderer`；[绑定实现 L648](../../../../src/modules/fluids/Fluids.cpp#L648)。
- `prepareVolumeFluidEmitterBlueprint3D(object: dynamic)` → `dynamic`；[绑定实现 L204](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L204)。
- `prepareVolumeGranularEmitterBlueprint3D(object: dynamic)` → `dynamic`；[绑定实现 L218](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L218)。
- `volumeColliderDefaults()` → `dynamic`；[绑定实现 L16](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L16)。
- `volumeEmissionFromCube(sx: float, sy: float, sz: float, spacing: float, surface: bool)` → `dynamic`；[绑定实现 L150](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L150)。
- `volumeEmissionFromDisk(radius: float, spacing: float, edgeEmission: bool)` → `dynamic`；[绑定实现 L166](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L166)。
- `volumeEmissionFromEdge(length: float, spacing: float, radialVelocityDegrees: float)` → `dynamic`；[绑定实现 L158](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L158)。
- `volumeEmissionFromImage(image: ImageData, pixelScale: float, maximumSize: float, spacing: float, threshold: float, srgb: bool)` → `dynamic`；[绑定实现 L109](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L109)。
- `volumeEmissionFromModel(model: ModelData, meshIndex: int, sx: float, sy: float, sz: float, spacing: float)` → `dynamic`；[绑定实现 L75](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L75)。
- `volumeEmissionFromSphere(radius: float, spacing: float, surface: bool)` → `dynamic`；[绑定实现 L142](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L142)。
- `volumeEmitterBlueprintMetrics(resolution: float, restDensity: float, smoothing: float)` → `dynamic`；[绑定实现 L228](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L228)。
- `volumeFluidEmitterBlueprintDefaults()` → `dynamic`；[绑定实现 L200](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L200)。
- `volumeGranularEmitterBlueprintDefaults()` → `dynamic`；[绑定实现 L214](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L214)。
- `volumeHeightFieldColliderDefaults()` → `dynamic`；[绑定实现 L24](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L24)。
- `volumeSdfColliderDefaults()` → `dynamic`；[绑定实现 L19](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L19)。
- `volumeSdfColliderFromModel(model: ModelData, meshIndex: int, sx: float, sy: float, sz: float, resolution: int)` → `dynamic`；[绑定实现 L33](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L33)。
- `volumeSdfPoseDefaults()` → `dynamic`；[绑定实现 L29](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L29)。
- `volumeThermalRuleDefaults()` → `dynamic`；[绑定实现 L9](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L9)。
- `volumeWindZoneDefaults()` → `dynamic`；[绑定实现 L13](../../../../src/modules/fluids/VolumeFluidFactoryBindings.cpp#L13)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
