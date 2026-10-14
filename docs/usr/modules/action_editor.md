# 动作时间线编辑：action_editor

**脚本入口：** `eve.ActionEditorModule()`；`config.modules` 使用槽位 `"actionEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

创建 AssetCatalog 并 refresh 资产索引，检查诊断与 generation，再 create 时间线会话。configureWorkspace 把会话接到同一个 workspace。选中轨道后添加 notify/state，使用游标和 snapSeconds 控制时间。复制粘贴前确认目标类型，提交后用资产 GUID/URI 关联动作。

## 任务二：预览、提交与清理

第二个任务是预览与撤销：编辑文档持有时间线，Action runtime 消费已验证版本；拖拽期间更新预览，指针抬起才提交命令。动作正在播放时不能直接改内部实例。关闭会话后撤销 preview，保留资产文档，不把运行时 handle 写入文件。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[ActionEditorModule.cpp](../../../src/modules/action/editor/ActionEditorModule.cpp)。流程与边界测试：[action_timeline_widget.cpp](../../../test/action_timeline_widget.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](action_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/combat-action-editor](../../../examples/combat-action-editor/README.md)。
运行 `eve run examples/combat-action-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
