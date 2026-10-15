# 生态规则编辑：biome_editor

**脚本入口：** `eve.BiomeEditorModule()`；`config.modules` 使用槽位 `"biomeEditor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

在规则文档创建 layer 和 asset，稳定 id 关联噪声、植被与材料输入。改规则后生成候选预览，保留 seed、区域和参数；当前可见结果与待提交规则分别记录 revision。

## 任务二：预览、提交与清理

第二个任务是复现植被变化：同一 seed/高度场/规则重建，比较密度与分布，不只比较截图。规则验证失败时保留旧发布结果；关闭预览后再释放 Procgen 生成对象。

编辑文档、撤销命令、运行时投影分别有自己的 owner；持久化保存文档版本与逻辑 ID，
不保存临时指针或本次进程的 handle。未知字段及迁移策略沿所属文档 schema，
加载失败保留旧可见版本。编辑器 provider 需在其依赖还存活时注销。

## 核对证据

实现：[biome_editor.cpp](../../../src/modules/procgen/biome/editor/biome_editor.cpp)。流程与边界测试：[editor_biome_rules_editor.cpp](../../../test/integration/gameplay-composition/editor_biome_rules_editor.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](biome_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/biome-rules-editor](../../../examples/biome-rules-editor/README.md)。
运行 `eve run examples/biome-rules-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
