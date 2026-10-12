# 体素世界可编辑目标：voxelworld_target

**脚本入口：** `eve.VoxelWorldTargetModule()`；`config.modules` 使用槽位 `"voxelWorldTarget"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

通过 VoxelWorldTargetModule.create(id,world) 把已有体素世界接到工作区。readInt3/writeInt3 操作三维整数格，写入 Result 要检查；dirtyMin/Max 与 revision 用于批量重建，不能把世界坐标直接当格索引。

## 任务二：预览、提交与清理

第二个任务是体素挖掘预览：编辑候选体素并验证，再更新可见 mesh/物理派生状态；commit 前的预览不能变成游戏权威。重建消费者成功后 clearDirtyVolume。target 借用 world，退出先移除目标和工具，再销毁 world。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[VoxelWorldTargetModule.cpp](../../../src/modules/voxel/voxelworld_target/VoxelWorldTargetModule.cpp)。流程与边界测试：[editor_voxel_target_script.cpp](../../../test/editor_voxel_target_script.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](voxelworld_target/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。
