# 场景文档与实时编辑：scene_editor

**脚本入口：** `eve.SceneEditorModule()`；`config.modules` 使用槽位 `"sceneEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

createSession(id) 创建离线文档会话；createLiveSession(id,hostName) 接到已经存在的 SceneHost。先建立对象稳定 ID，再通过会话命令修改 TRS 和组件 payload，检查 Result 及 validation；不要直接改不可追踪的临时节点。

## 任务二：预览、提交与清理

第二个任务是物理表面摆放：beginPhysicsPlacement 创建候选，alignPhysicsPlacementToSurface 使用同一游戏物理世界的查询结果，commitPhysicsPlacement 才提交；取消调用 cancelPhysicsPlacement。缓存 hull/compound 是资源准备步骤，不能在鼠标每帧重新 cook。关卡销毁前解除 live 会话。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[SceneEditorModule.cpp](../../../src/modules/scene/editor/SceneEditorModule.cpp)。流程与边界测试：[scene_editor_script.cpp](../../../test/scene_editor_script.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](scene_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/scene-editor](../../../examples/scene-editor/README.md)。
运行 `eve run examples/scene-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
