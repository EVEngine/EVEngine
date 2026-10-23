# animation_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `deleteSelectedKey()` → `dynamic`；[绑定实现 L95](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L95)。
- `getBoneCount()` → `dynamic`；[绑定实现 L167](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L167)。
- `getBoneName(index: int)` → `dynamic`；[绑定实现 L168](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L168)。
- `getBoneParent(index: int)` → `dynamic`；[绑定实现 L169](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L169)。
- `getDuration()` → `dynamic`；[绑定实现 L131](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L131)。
- `getEventCount()` → `dynamic`；[绑定实现 L194](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L194)。
- `getEventName(index: int)` → `dynamic`；[绑定实现 L200](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L200)。
- `getEventX(index: int)` → `dynamic`；[绑定实现 L197](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L197)。
- `getKeyCount()` → `dynamic`；[绑定实现 L182](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L182)。
- `getKeySelected(index: int)` → `dynamic`；[绑定实现 L191](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L191)。
- `getKeyX(index: int)` → `dynamic`；[绑定实现 L185](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L185)。
- `getKeyY(index: int)` → `dynamic`；[绑定实现 L188](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L188)。
- `getLayoutHeight()` → `dynamic`；[绑定实现 L144](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L144)。
- `getLayoutWidth()` → `dynamic`；[绑定实现 L141](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L141)。
- `getLoop()` → `dynamic`；[绑定实现 L137](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L137)。
- `getPlayhead()` → `dynamic`；[绑定实现 L138](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L138)。
- `getPlayheadX()` → `dynamic`；[绑定实现 L147](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L147)。
- `getPrimitiveB(index: int)` → `dynamic`；[绑定实现 L233](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L233)。
- `getPrimitiveCount()` → `dynamic`；[绑定实现 L203](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L203)。
- `getPrimitiveDirX(index: int)` → `dynamic`；[绑定实现 L215](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L215)。
- `getPrimitiveDirY(index: int)` → `dynamic`；[绑定实现 L218](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L218)。
- `getPrimitiveG(index: int)` → `dynamic`；[绑定实现 L230](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L230)。
- `getPrimitiveKind(index: int)` → `dynamic`；[绑定实现 L206](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L206)。
- `getPrimitiveLength(index: int)` → `dynamic`；[绑定实现 L221](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L221)。
- `getPrimitiveR(index: int)` → `dynamic`；[绑定实现 L227](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L227)。
- `getPrimitiveRadius(index: int)` → `dynamic`；[绑定实现 L224](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L224)。
- `getPrimitiveX(index: int)` → `dynamic`；[绑定实现 L209](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L209)。
- `getPrimitiveY(index: int)` → `dynamic`；[绑定实现 L212](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L212)。
- `getSampleRate()` → `dynamic`；[绑定实现 L134](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L134)。
- `getSelectedBone()` → `dynamic`；[绑定实现 L150](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L150)。
- `getSelectedKeyTime()` → `dynamic`；[绑定实现 L157](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L157)。
- `getSelectedMaskWeight()` → `dynamic`；[绑定实现 L153](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L153)。
- `getSelectedPositionX()` → `dynamic`；[绑定实现 L158](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L158)。
- `getSelectedPositionY()` → `dynamic`；[绑定实现 L159](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L159)。
- `getSelectedPositionZ()` → `dynamic`；[绑定实现 L160](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L160)。
- `getSelectedRotationX()` → `dynamic`；[绑定实现 L161](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L161)。
- `getSelectedRotationY()` → `dynamic`；[绑定实现 L162](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L162)。
- `getSelectedRotationZ()` → `dynamic`；[绑定实现 L163](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L163)。
- `getSelectedScaleX()` → `dynamic`；[绑定实现 L164](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L164)。
- `getSelectedScaleY()` → `dynamic`；[绑定实现 L165](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L165)。
- `getSelectedScaleZ()` → `dynamic`；[绑定实现 L166](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L166)。
- `getTrackBone(index: int)` → `dynamic`；[绑定实现 L173](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L173)。
- `getTrackCount()` → `dynamic`；[绑定实现 L170](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L170)。
- `getTrackId(index: int)` → `dynamic`；[绑定实现 L176](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L176)。
- `getTrackSelected(index: int)` → `dynamic`；[绑定实现 L179](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L179)。
- `hasSelectedKey()` → `dynamic`；[绑定实现 L156](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L156)。
- `isPlaying()` → `dynamic`；[绑定实现 L130](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L130)。
- `keySelectedBone()` → `dynamic`；[绑定实现 L91](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L91)。
- `moveSelectedKey(time: float)` → `dynamic`；[绑定实现 L87](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L87)。
- `pause()` → `dynamic`；[绑定实现 L116](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L116)。
- `play()` → `dynamic`；[绑定实现 L113](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L113)。
- `pointerDown(x: float, y: float)` → `dynamic`；[绑定实现 L63](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L63)。
- `seekSeconds(seconds: float)` → `dynamic`；[绑定实现 L59](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L59)。
- `seekX(x: float)` → `dynamic`；[绑定实现 L56](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L56)。
- `selectBone(bone: string)` → `dynamic`；[绑定实现 L67](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L67)。
- `setDuration(duration: float)` → `dynamic`；[绑定实现 L75](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L75)。
- `setLoop(loop: bool)` → `dynamic`；[绑定实现 L83](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L83)。
- `setMaskWeight(weight: float)` → `dynamic`；[绑定实现 L71](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L71)。
- `setSampleRate(sampleRate: float)` → `dynamic`；[绑定实现 L79](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L79)。
- `setSelectedPosition(x: float, y: float, z: float)` → `dynamic`；[绑定实现 L99](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L99)。
- `setSelectedRotation(x: float, y: float, z: float)` → `dynamic`；[绑定实现 L103](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L103)。
- `setSelectedScale(x: float, y: float, z: float)` → `dynamic`；[绑定实现 L107](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L107)。
- `setViewport(width: float, rowHeight: float, labelWidth: float)` → `dynamic`；[绑定实现 L51](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L51)。
- `stop()` → `dynamic`；[绑定实现 L119](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L119)。
- `writeRuntimeClip(clip: AnimClip, skeleton: AnimSkeleton)` → `dynamic`；[绑定实现 L45](../../../../src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp#L45)。

- `update`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。

重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
