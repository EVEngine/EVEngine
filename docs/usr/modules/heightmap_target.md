# 高度场可编辑目标：heightmap_target

**脚本入口：** `eve.HeightmapTargetModule()`；`config.modules` 使用槽位 `"heightmapTarget"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

通过 HeightmapTargetModule 的 create 入口关联稳定 targetId 和已有 Heightmap。readScalar/writeScalar 操作格坐标；显示比例 cellSize/heightScale 与原始高度样本分开。写入后读取 getRevision。dirty region 由原生适配器读取，当前脚本没有脏矩形 getter；
脚本可按 revision 批量更新预览，不能调用一个虚构的 getDirtyRegion。

## 任务二：预览、提交与清理

第二个任务是同步渲染和碰撞：先结束编辑提交，再生成候选 mesh/collider；所有消费者接受同一 revision 后清 dirty region。target 借用 Heightmap，先解绑工具/target 再释放高度场；清 dirty 不代表重建已完成。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[HeightmapTargetModule.cpp](../../../src/modules/procgen/heightmap_target/HeightmapTargetModule.cpp)。流程与边界测试：[terrain_heightmap_access.cpp](../../../test/terrain_heightmap_access.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](heightmap_target/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/terrain-editor](../../../examples/terrain-editor/README.md)。
运行 `eve run examples/terrain-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
