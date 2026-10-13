# 瓦片层可编辑目标：tilelayer_target

**脚本入口：** `eve.TileLayerTargetModule()`；`config.modules` 使用槽位 `"tileLayerTarget"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

创建 TileLayerTarget 时传稳定 ID 和已有 TileLayer；readInt/writeInt 用格坐标读写，检查返回值和边界。相机、地图投影与屏幕输入由调用方转换，target 不自动拾取鼠标。

## 任务二：预览、提交与清理

第二个任务是局部更新：原生适配器按 dirty region 重建地图缓存；当前脚本没有脏矩形 getter，可按
getRevision 检测批次变化，确认消费者更新后 clearDirtyRegion。撤销恢复层数据并重新发出变更，而不是只回滚屏幕贴图。先解绑 target 再释放 TileLayer。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[TileLayerTargetModule.cpp](../../../src/modules/map/tilelayer_target/TileLayerTargetModule.cpp)。流程与边界测试：[editor_tilemap_target_script.cpp](../../../test/editor_tilemap_target_script.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](tilelayer_target/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。
