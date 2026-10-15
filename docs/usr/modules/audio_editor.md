# 声音资源与试听编辑：audio_editor

**脚本入口：** `eve.AudioEditorModule()`；`config.modules` 使用槽位 `"audioEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

加载音频文档后读取 duration、bucketMin/bucketMax，波形 bucket 是显示摘要，不能当成逐样本 PCM。编辑 Source 的增益、循环和衰减参数先做范围验证，再通过 attachLiveAudition 接入有效音频设备。

## 任务二：预览、提交与清理

第二个任务是比较参数：明确播放、暂停与停止的 transport，试听只修改预览 Source，确认后才提交文档。退出先停止试听、解除 live provider，再释放 Decoder/SoundData；设备不可用的诊断与资产解码失败分别显示。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[AudioEditorModule.cpp](../../../src/modules/audio/editor/AudioEditorModule.cpp)、[audio_editor.cpp](../../../src/modules/audio/editor/audio_editor.cpp)。流程与边界测试：[editor_audio_source_script.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_editor_tests.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](audio_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/audio-source-editor](../../../examples/audio-source-editor/README.md)。
运行 `eve run examples/audio-source-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
