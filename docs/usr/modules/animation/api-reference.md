# animation：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## AnimClip

- `getSampleRate()` → `float`；[绑定实现 L117](../../../../src/modules/animation/AnimationClipBindings.cpp#L117)。

## AnimLattice

- `bindModel(model: ModelData, meshIndex: int)` → `void`；[绑定实现 L822](../../../../src/modules/animation/Animation.cpp#L822)。
- `bindPositionsFromArray(arr: Array<dynamic>)` → `void`；[绑定实现 L823](../../../../src/modules/animation/Animation.cpp#L823)。
- `clearBind()` → `void`；[绑定实现 L825](../../../../src/modules/animation/Animation.cpp#L825)。
- `getDeformedNormals()` → `Array<dynamic>`；[绑定实现 L846](../../../../src/modules/animation/Animation.cpp#L846)。
- `getDeformedPositionX(vertexIndex: int)` → `float`；[绑定实现 L842](../../../../src/modules/animation/Animation.cpp#L842)。
- `getDeformedPositionY(vertexIndex: int)` → `float`；[绑定实现 L843](../../../../src/modules/animation/Animation.cpp#L843)。
- `getDeformedPositionZ(vertexIndex: int)` → `float`；[绑定实现 L844](../../../../src/modules/animation/Animation.cpp#L844)。
- `getDeformedPositions()` → `Array<dynamic>`；[绑定实现 L845](../../../../src/modules/animation/Animation.cpp#L845)。
- `getPointOffsetX(ix: int, iy: int, iz: int)` → `float`；[绑定实现 L819](../../../../src/modules/animation/Animation.cpp#L819)。
- `getPointOffsetY(ix: int, iy: int, iz: int)` → `float`；[绑定实现 L820](../../../../src/modules/animation/Animation.cpp#L820)。
- `getPointOffsetZ(ix: int, iy: int, iz: int)` → `float`；[绑定实现 L821](../../../../src/modules/animation/Animation.cpp#L821)。
- `getPointScaleX(ix: int, iy: int, iz: int)` → `float`；[绑定实现 L816](../../../../src/modules/animation/Animation.cpp#L816)。
- `getPointScaleY(ix: int, iy: int, iz: int)` → `float`；[绑定实现 L817](../../../../src/modules/animation/Animation.cpp#L817)。
- `getPointScaleZ(ix: int, iy: int, iz: int)` → `float`；[绑定实现 L818](../../../../src/modules/animation/Animation.cpp#L818)。
- `hasDeformedNormals()` → `bool`；[绑定实现 L841](../../../../src/modules/animation/Animation.cpp#L841)。
- `hasDeformedPositions()` → `bool`；[绑定实现 L840](../../../../src/modules/animation/Animation.cpp#L840)。
- `updateDeformedNormalsFromArray(posArr: Array<dynamic>, nrmArr: Array<dynamic>)` → `bool`；[绑定实现 L836](../../../../src/modules/animation/Animation.cpp#L836)。
- `updateDeformedPositions(inPosXYZ: Array<dynamic>)` → `bool`；[绑定实现 L830](../../../../src/modules/animation/Animation.cpp#L830)。
- `updateDeformedPositionsFromArray(arr: Array<dynamic>)` → `bool`；[绑定实现 L833](../../../../src/modules/animation/Animation.cpp#L833)。

## AnimPlayer

- `getTime()` → `float`；[绑定实现 L49](../../../../src/modules/animation/AnimationPlayerBindings.cpp#L49)。
- `isPlaying()` → `bool`；[绑定实现 L52](../../../../src/modules/animation/AnimationPlayerBindings.cpp#L52)。

## AnimPose

- `getLocalPositionX(boneIndex: int)` → `float`；[绑定实现 L709](../../../../src/modules/animation/Animation.cpp#L709)。
- `getLocalPositionY(boneIndex: int)` → `float`；[绑定实现 L710](../../../../src/modules/animation/Animation.cpp#L710)。
- `getLocalPositionZ(boneIndex: int)` → `float`；[绑定实现 L711](../../../../src/modules/animation/Animation.cpp#L711)。
- `getLocalRotationW(boneIndex: int)` → `float`；[绑定实现 L715](../../../../src/modules/animation/Animation.cpp#L715)。
- `getLocalRotationX(boneIndex: int)` → `float`；[绑定实现 L712](../../../../src/modules/animation/Animation.cpp#L712)。
- `getLocalRotationY(boneIndex: int)` → `float`；[绑定实现 L713](../../../../src/modules/animation/Animation.cpp#L713)。
- `getLocalRotationZ(boneIndex: int)` → `float`；[绑定实现 L714](../../../../src/modules/animation/Animation.cpp#L714)。
- `getLocalScaleX(boneIndex: int)` → `float`；[绑定实现 L716](../../../../src/modules/animation/Animation.cpp#L716)。
- `getLocalScaleY(boneIndex: int)` → `float`；[绑定实现 L717](../../../../src/modules/animation/Animation.cpp#L717)。
- `getLocalScaleZ(boneIndex: int)` → `float`；[绑定实现 L718](../../../../src/modules/animation/Animation.cpp#L718)。
- `setLocalRotation(boneIndex: int, x: float, y: float, z: float, w: float)` → `void`；[绑定实现 L707](../../../../src/modules/animation/Animation.cpp#L707)。
- `setLocalScale(boneIndex: int, x: float, y: float, z: float)` → `void`；[绑定实现 L708](../../../../src/modules/animation/Animation.cpp#L708)。

## AnimSkin

- `getInfluenceCount()` → `int`；[绑定实现 L766](../../../../src/modules/animation/Animation.cpp#L766)。
- `getSkinnedPositionY(vertexIndex: int)` → `float`；[绑定实现 L783](../../../../src/modules/animation/Animation.cpp#L783)。
- `getSkinnedPositionZ(vertexIndex: int)` → `float`；[绑定实现 L784](../../../../src/modules/animation/Animation.cpp#L784)。

## AnimStateMachine

- `getBool(name: string)` → `bool`；[绑定实现 L931](../../../../src/modules/animation/Animation.cpp#L931)。
- `getCurrentState()` → `string`；[绑定实现 L920](../../../../src/modules/animation/Animation.cpp#L920)。
- `getFloat(name: string)` → `float`；[绑定实现 L929](../../../../src/modules/animation/Animation.cpp#L929)。
- `getStateCount()` → `int`；[绑定实现 L921](../../../../src/modules/animation/Animation.cpp#L921)。
- `getStateTime()` → `float`；[绑定实现 L935](../../../../src/modules/animation/Animation.cpp#L935)。
- `hasState(name: string)` → `bool`；[绑定实现 L919](../../../../src/modules/animation/Animation.cpp#L919)。
- `isBlending()` → `bool`；[绑定实现 L936](../../../../src/modules/animation/Animation.cpp#L936)。
- `resetTrigger(name: string)` → `void`；[绑定实现 L933](../../../../src/modules/animation/Animation.cpp#L933)。
- `setHasExitTime(transitionId: int, enabled: bool)` → `void`；[绑定实现 L927](../../../../src/modules/animation/Animation.cpp#L927)。

## AnimTrail

- `getColorA()` → `float`；[绑定实现 L1066](../../../../src/modules/animation/Animation.cpp#L1066)。
- `getColorB()` → `float`；[绑定实现 L1065](../../../../src/modules/animation/Animation.cpp#L1065)。
- `getColorG()` → `float`；[绑定实现 L1064](../../../../src/modules/animation/Animation.cpp#L1064)。
- `getColorR()` → `float`；[绑定实现 L1063](../../../../src/modules/animation/Animation.cpp#L1063)。
- `getDrawOffsetX()` → `float`；[绑定实现 L1075](../../../../src/modules/animation/Animation.cpp#L1075)。
- `getDrawOffsetY()` → `float`；[绑定实现 L1076](../../../../src/modules/animation/Animation.cpp#L1076)。
- `getDrawScaleX()` → `float`；[绑定实现 L1072](../../../../src/modules/animation/Animation.cpp#L1072)。
- `getDrawScaleY()` → `float`；[绑定实现 L1073](../../../../src/modules/animation/Animation.cpp#L1073)。
- `getPointX(index: int)` → `float`；[绑定实现 L1084](../../../../src/modules/animation/Animation.cpp#L1084)。
- `getPointY(index: int)` → `float`；[绑定实现 L1085](../../../../src/modules/animation/Animation.cpp#L1085)。
- `getPointZ(index: int)` → `float`；[绑定实现 L1086](../../../../src/modules/animation/Animation.cpp#L1086)。

## MotionDatabase

- `getClipCount()` → `int`；[绑定实现 L42](../../../../src/modules/animation/AnimationMotionBindings.cpp#L42)。
- `getFeatureBone(index: int)` → `int`；[绑定实现 L50](../../../../src/modules/animation/AnimationMotionBindings.cpp#L50)。
- `getFeatureBoneCount()` → `int`；[绑定实现 L49](../../../../src/modules/animation/AnimationMotionBindings.cpp#L49)。
- `getFrameClipIndex(frameIndex: int)` → `int`；[绑定实现 L48](../../../../src/modules/animation/AnimationMotionBindings.cpp#L48)。
- `getFrameTime(frameIndex: int)` → `float`；[绑定实现 L47](../../../../src/modules/animation/AnimationMotionBindings.cpp#L47)。
- `isBaked()` → `bool`；[绑定实现 L44](../../../../src/modules/animation/AnimationMotionBindings.cpp#L44)。

## MotionMatcher

- `getBlendTime()` → `float`；[绑定实现 L154](../../../../src/modules/animation/AnimationMotionBindings.cpp#L154)。
- `getDesiredVelocityX()` → `float`；[绑定实现 L147](../../../../src/modules/animation/AnimationMotionBindings.cpp#L147)。
- `getDesiredVelocityZ()` → `float`；[绑定实现 L148](../../../../src/modules/animation/AnimationMotionBindings.cpp#L148)。
- `getDesiredYaw()` → `float`；[绑定实现 L150](../../../../src/modules/animation/AnimationMotionBindings.cpp#L150)。
- `getIgnoreRadius()` → `int`；[绑定实现 L168](../../../../src/modules/animation/AnimationMotionBindings.cpp#L168)。
- `getLastSearchCost()` → `float`；[绑定实现 L176](../../../../src/modules/animation/AnimationMotionBindings.cpp#L176)。
- `getMatchedFrame()` → `int`；[绑定实现 L173](../../../../src/modules/animation/AnimationMotionBindings.cpp#L173)。
- `getMatchedTime()` → `float`；[绑定实现 L175](../../../../src/modules/animation/AnimationMotionBindings.cpp#L175)。
- `getPoseWeight()` → `float`；[绑定实现 L164](../../../../src/modules/animation/AnimationMotionBindings.cpp#L164)。
- `getSearchInterval()` → `float`；[绑定实现 L152](../../../../src/modules/animation/AnimationMotionBindings.cpp#L152)。
- `getTrajectoryWeight()` → `float`；[绑定实现 L162](../../../../src/modules/animation/AnimationMotionBindings.cpp#L162)。
- `getVelocityWeight()` → `float`；[绑定实现 L166](../../../../src/modules/animation/AnimationMotionBindings.cpp#L166)。
- `setPoseWeight(w: float)` → `void`；[绑定实现 L163](../../../../src/modules/animation/AnimationMotionBindings.cpp#L163)。
- `setTrajectoryWeight(w: float)` → `void`；[绑定实现 L161](../../../../src/modules/animation/AnimationMotionBindings.cpp#L161)。
- `setVelocityWeight(w: float)` → `void`；[绑定实现 L165](../../../../src/modules/animation/AnimationMotionBindings.cpp#L165)。

## SpineAnim

- `getAtlas()` → `SpineAtlas`；[绑定实现 L1253](../../../../src/modules/animation/Animation.cpp#L1253)。
- `getDrawSlotHeight(index: int)` → `float`；[绑定实现 L1291](../../../../src/modules/animation/Animation.cpp#L1291)。
- `getDrawSlotRegion(index: int)` → `string`；[绑定实现 L1293](../../../../src/modules/animation/Animation.cpp#L1293)。
- `getDrawSlotRotation(index: int)` → `float`；[绑定实现 L1292](../../../../src/modules/animation/Animation.cpp#L1292)。
- `getDrawSlotWidth(index: int)` → `float`；[绑定实现 L1290](../../../../src/modules/animation/Animation.cpp#L1290)。
- `getDrawSlotY(index: int)` → `float`；[绑定实现 L1289](../../../../src/modules/animation/Animation.cpp#L1289)。
- `getFlipY()` → `bool`；[绑定实现 L1268](../../../../src/modules/animation/Animation.cpp#L1268)。
- `getPageTexture(pageIndex: int)` → `Texture`；[绑定实现 L1256](../../../../src/modules/animation/Animation.cpp#L1256)。
- `getScaleX()` → `float`；[绑定实现 L1273](../../../../src/modules/animation/Animation.cpp#L1273)。
- `getScaleY()` → `float`；[绑定实现 L1274](../../../../src/modules/animation/Animation.cpp#L1274)。
- `getTime()` → `float`；[绑定实现 L1264](../../../../src/modules/animation/Animation.cpp#L1264)。
- `isPlaying()` → `bool`；[绑定实现 L1278](../../../../src/modules/animation/Animation.cpp#L1278)。

## SpineAtlas

- `getPageCount()` → `int`；[绑定实现 L1189](../../../../src/modules/animation/Animation.cpp#L1189)。
- `getPageHeight(pageIndex: int)` → `int`；[绑定实现 L1192](../../../../src/modules/animation/Animation.cpp#L1192)。
- `getPageName(pageIndex: int)` → `string`；[绑定实现 L1190](../../../../src/modules/animation/Animation.cpp#L1190)。
- `getPageWidth(pageIndex: int)` → `int`；[绑定实现 L1191](../../../../src/modules/animation/Animation.cpp#L1191)。
- `getRegionCount()` → `int`；[绑定实现 L1193](../../../../src/modules/animation/Animation.cpp#L1193)。
- `getRegionHeight(index: int)` → `int`；[绑定实现 L1200](../../../../src/modules/animation/Animation.cpp#L1200)。
- `getRegionName(index: int)` → `string`；[绑定实现 L1195](../../../../src/modules/animation/Animation.cpp#L1195)。
- `getRegionPage(index: int)` → `int`；[绑定实现 L1196](../../../../src/modules/animation/Animation.cpp#L1196)。
- `getRegionRotate(index: int)` → `bool`；[绑定实现 L1201](../../../../src/modules/animation/Animation.cpp#L1201)。
- `getRegionWidth(index: int)` → `int`；[绑定实现 L1199](../../../../src/modules/animation/Animation.cpp#L1199)。
- `getRegionX(index: int)` → `int`；[绑定实现 L1197](../../../../src/modules/animation/Animation.cpp#L1197)。
- `getRegionY(index: int)` → `int`；[绑定实现 L1198](../../../../src/modules/animation/Animation.cpp#L1198)。

## SpineSkeleton

- `getBoneWorldRotation(index: int)` → `float`；[绑定实现 L1244](../../../../src/modules/animation/Animation.cpp#L1244)。
- `getBoneWorldX(index: int)` → `float`；[绑定实现 L1242](../../../../src/modules/animation/Animation.cpp#L1242)。
- `getBoneWorldY(index: int)` → `float`；[绑定实现 L1243](../../../../src/modules/animation/Animation.cpp#L1243)。
- `getData()` → `SpineSkeletonData`；[绑定实现 L1236](../../../../src/modules/animation/Animation.cpp#L1236)。
- `setSlotAttachmentName(slotIndex: int, name: string)` → `void`；[绑定实现 L1247](../../../../src/modules/animation/Animation.cpp#L1247)。

## SpineSkeletonData

- `findSkin(name: string)` → `int`；[绑定实现 L1226](../../../../src/modules/animation/Animation.cpp#L1226)。
- `getAnimationCount()` → `int`；[绑定实现 L1228](../../../../src/modules/animation/Animation.cpp#L1228)。
- `getAnimationName(index: int)` → `string`；[绑定实现 L1230](../../../../src/modules/animation/Animation.cpp#L1230)。
- `getBoneParent(index: int)` → `int`；[绑定实现 L1220](../../../../src/modules/animation/Animation.cpp#L1220)。
- `getSkinCount()` → `int`；[绑定实现 L1225](../../../../src/modules/animation/Animation.cpp#L1225)。
- `getSkinName(index: int)` → `string`；[绑定实现 L1227](../../../../src/modules/animation/Animation.cpp#L1227)。
- `getSlotBone(index: int)` → `int`；[绑定实现 L1224](../../../../src/modules/animation/Animation.cpp#L1224)。
- `getSlotName(index: int)` → `string`；[绑定实现 L1223](../../../../src/modules/animation/Animation.cpp#L1223)。
- `getSpineVersion()` → `string`；[绑定实现 L1216](../../../../src/modules/animation/Animation.cpp#L1216)。

## SpriteAnim

- `getBoundQuad()` → `Quad`；[绑定实现 L1172](../../../../src/modules/animation/Animation.cpp#L1172)。
- `getClipFrame()` → `int`；[绑定实现 L1168](../../../../src/modules/animation/Animation.cpp#L1168)。
- `getTime()` → `float`；[绑定实现 L1158](../../../../src/modules/animation/Animation.cpp#L1158)。
- `isPlaying()` → `bool`；[绑定实现 L1161](../../../../src/modules/animation/Animation.cpp#L1161)。
- `unbindQuad()` → `void`；[绑定实现 L1171](../../../../src/modules/animation/Animation.cpp#L1171)。

## SpriteClip

- `getFrameDuration(index: int)` → `float`；[绑定实现 L1129](../../../../src/modules/animation/Animation.cpp#L1129)。

## 接收者见绑定源码

- `newLatticeFromModel(model: ModelData, meshIndex: int, divX: int, divY: int, divZ: int)` → `AnimLattice`；[绑定实现 L36](../../../../src/modules/animation/AnimationModelImport.cpp#L36)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
