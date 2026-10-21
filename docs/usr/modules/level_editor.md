# 关卡瓦片笔刷：level_editor

**脚本入口：** `eve.LevelEditorModule()`；`config.modules` 使用槽位 `"levelEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

newTileBuffer(width,height) 建立权威瓦片缓冲，newBrush/newHistory 建立工具和撤销历史；createTarget(id,buffer) 后 bind 到工作区。屏幕位置先经地图投影转换成格坐标，一次拖拽形成一次命令。

## 任务二：预览、提交与清理

第二个任务是复制区域：候选 tile 数据先校验范围和图集 ID，再提交；撤销使用 history，不从渲染网格反推瓦片。先解绑 target，再释放借用的 TileBuffer；脏区域用于局部重建可见网格。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[LevelEditorModule.cpp](../../../src/modules/map/level/editor/LevelEditorModule.cpp)。流程与边界测试：[editor_level.cpp](../../../test/unit/editor/editor_tests.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](level_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/editor-api-v2](../../../examples/editor-api-v2/README.md)。
运行 `eve run examples/editor-api-v2`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
