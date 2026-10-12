# 反射探针可视化：graphics_editor

**脚本入口：** `eve.GraphicsEditorModule()`；`config.modules` 使用槽位 `"graphicsEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

先创建 Graphics 的 ReflectionProbeCapture，再 createReflectionProbeVisualizer(probe)。设置 extents，读取 getLineCount/getLineStart/getLineEnd，在编辑视口的世界坐标绘制线框；getStatusLabel 解释捕获状态。

## 任务二：预览、提交与清理

第二个任务是调整捕获范围：修改候选范围并检查探针状态，捕获与每帧线框绘制分开，不能每次拖拽都执行昂贵捕获。Visualizer 借用 probe，先停止绘制并释放 visualizer，再销毁 probe/Graphics 设备。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[GraphicsEditorModule.cpp](../../../src/modules/graphics/editor/GraphicsEditorModule.cpp)。流程与边界测试：[reflection_probe_quality.cpp](../../../test/reflection_probe_quality.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](graphics_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。
