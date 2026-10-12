# 动画片段编辑：animation_editor

**脚本入口：** `eve.AnimationEditorModule()`；`config.modules` 使用槽位 `"animationEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

先加载 Skeleton 和 Runtime Clip，再 loadRuntimeClip 建立编辑投影；选择骨骼和轨道，在明确的秒时间轴插入/修改 key。编辑文档为权威，预览求值不推进游戏角色。事件轨道和曲线分别检查名称、时间范围和类型。

## 任务二：预览、提交与清理

第二个任务是热更动画：构造候选片段、验证后发布，再让 Animator 切换；切换前停止借用旧 Skeleton/Clip 的预览。撤销针对文档命令，不用反向修改当前骨骼姿态模拟撤销。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[AnimationEditorModule.cpp](../../../src/modules/animation/editor/AnimationEditorModule.cpp)、[animation_editor.cpp](../../../src/modules/animation/editor/animation_editor.cpp)。流程与边界测试：[editor_animation_clip_script.cpp](../../../test/editor_animation_clip_script.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](animation_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/animation-clip-editor](../../../examples/animation-clip-editor/README.md)。
运行 `eve run examples/animation-clip-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
