# UI 主题与预览编辑：ui_editor

**脚本入口：** `eve.UiEditorModule()`；`config.modules` 使用槽位 `"uiEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

createFromPreset(id,name,preset) 创建主题文档，选择样式后按字段类型读写。UI ID 稳定；事件消费由游戏 UI Host 拥有。主题编辑不直接改每个控件的内部布局缓存。

## 任务二：预览、提交与清理

第二个任务是换肤：applyPreviewHost(hostName) 接入已有 host，查看候选主题；验证成功后保存主题资产，再在游戏加载阶段应用。deleteSelected/duplicateSelected 操作编辑文档，不能当成运行时控件销毁方法。关闭预览再释放 host。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[UiEditorModule.cpp](../../../src/modules/ui/editor/UiEditorModule.cpp)。流程与边界测试：[editor_ui_theme.cpp](../../../test/editor_ui_theme.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](ui_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/ui-theme-editor](../../../examples/ui-theme-editor/README.md)。
运行 `eve run examples/ui-theme-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
