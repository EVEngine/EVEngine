# 体素模型目录编辑：voxel_editor

**脚本入口：** `eve.VoxelEditorModule()`；`config.modules` 使用槽位 `"voxelEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

createModel(id,name,sx,sy,sz) 创建尺寸确定的模型，检查合法尺寸、调色板与唯一 ID；选择模型后修改单元。模型文档为权威，mesh 是派生缓存；修改脏块后批量重建。

## 任务二：预览、提交与清理

第二个任务是模型组合：join partner 的 ID 来自当前文档，确认目标尚存并检查结果。删除模型前取消借用它的预览和选择；保存包含 schema/version 的目录，恢复后重新生成网格和 GPU 资源。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[voxel_editor.cpp](../../../src/modules/voxel/editor/voxel_editor.cpp)。流程与边界测试：[editor_voxel_catalog_script.cpp](../../../test/editor_voxel_catalog_script.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](voxel_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/voxel-catalog-editor](../../../examples/voxel-catalog-editor/README.md)。
运行 `eve run examples/voxel-catalog-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
