# 程序化脚本与网格编辑：procgen_editor

**脚本入口：** `eve.ProcgenEditorModule()`；`config.modules` 使用槽位 `"procgenEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

loadModule 加载脚本资源，不把未经检查的文本当成已发布工件。createMeshModifier(targetId) 建立可编辑修改器，activateTool(workspace,tool) 接入工作区。预览请求携带 expectedRevision，生成结束后只接收仍匹配的结果。

## 任务二：预览、提交与清理

第二个任务是处理生成失败：failPreview 保留具体 message 和 revision；旧预览维持可见，失败不提交半份 mesh。鼠标连续修改先合并参数，生成与发布分步执行。运行时仅消费已验证产物，编辑线程不能调用 VM/GPU。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[procgen_editor.cpp](../../../src/modules/procgen/editor/procgen_editor.cpp)。流程与边界测试：[editor_procgen_script.cpp](../../../test/editor_procgen_script.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](procgen_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/procgen-script-editor](../../../examples/procgen-script-editor/README.md)。
运行 `eve run examples/procgen-script-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
