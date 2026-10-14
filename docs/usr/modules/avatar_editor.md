# 角色文档编辑：avatar_editor

**脚本入口：** `eve.AvatarEditorModule()`；`config.modules` 使用槽位 `"avatarEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

在 Avatar 文档创建 layer、parameter 和 expression，ID 稳定且唯一；选择具体条目再编辑属性。表达式是参数组合，不能直接把编辑器 selection 当游戏角色身份。候选文档验证成功后才更新 runtime。

## 任务二：预览、提交与清理

第二个任务是换装/表达式预览：保持骨架、附件和材质引用有效，预览修改不覆盖角色运行时的权威状态。撤销恢复文档，再重建投影；退出先解绑角色预览，再释放对应 Avatar/Skeleton。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[avatar_editor.cpp](../../../src/modules/avatar/editor/avatar_editor.cpp)。流程与边界测试：[editor_avatar_document_script.cpp](../../../test/editor_avatar_document_script.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](avatar_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/avatar-document-editor](../../../examples/avatar-document-editor/README.md)。
运行 `eve run examples/avatar-document-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
