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

## 任务三：群落方案与区域绑定

沿用同一个 `BiomeDocumentTarget`、属性 Inspector、事务及撤销栈，群落新增六个方案槽：
`monsters`（怪物生成表）、`animals`（动物方案）、`plants`（植物与植被生成方案）、
`surface`（地表/地形生成方案）、`weather`（天气方案）、`fog`（雾气方案）。
每个槽保存一个经过 `ResourceUri` 校验的资源引用；一个方案可以包含多个物种、生成规则或天气状态。
空字符串明确表示未关联。URI 的资源内容仍由对应领域管理，不复制进群落文档，
也不另建怪物、植物或天气注册表。现有 `BiomeRules` 和 `TerrainBiomePreset` 继续负责生成。

```squirrel
local made = eve.BiomeEditorModule().create("world.swamp");
if (!made.ok) throw made.status.summary;
local biome = made.value;
local changed = biome.setScheme("weather", "project://weather/wetland");
if (!changed.ok) throw changed.status.summary;
local bound = biome.bindRegion("south.marsh", "project://regions/south-marsh", 42);
if (!bound.ok) throw bound.status.summary;
local region = bound.value;
local current = region.configuration();
if (!current.ok) throw current.status.summary;
// current.value.schemes.weather == "project://weather/wetland"
// 后续 setScheme、undo、redo 后再次 configuration() 自动取得同一群落的已发布配置。
```

`getScheme(kind)` 返回 Result<string>；`setScheme(kind, "")` 清除该关联。
通用 Inspector/automation 可选择 `biome.document`，item 为目标 ID，通过已有
`biome.property.set.v1` 修改 `schemes.weather` 等属性，无第二套编辑命令。
现有示例左栏可选择六种方案、编辑 URI、Apply/Clear，并显示绑定区域当前引用。

区域绑定是区域 owner 持有的弱句柄：只保存区域身份、空间 URI、seed，以及对发布槽的弱引用。
多个区域共享同一群落发布结果，不复制六套配置；成功发布时同时替换规则与方案引用。
失败保持所有区域的旧发布结果。运行时不能换成另一逻辑群落；切换群落需要显式重新绑定。
区域先销毁不影响群落；群落运行时先销毁时 `configuration()` 返回 NotFound。
新建同名运行时不会复活旧句柄，须显式恢复绑定。

`region.snapshotValue()` 返回 `eve.biome.region-binding` v1，保存 biome/region/spatial/seed，
不保存方案副本或进程句柄。`biome.restoreRegion(snapshot)` 验证群落身份后重新建立跟随关系。
群落快照升级为 `eve.biome.rules` v2；旧 v1 显式迁移为空方案槽，未知新版本、
顶层/内容/方案槽未知字段被拒绝。原有 layer/asset 的既有字段策略保持不变。

原生 `BiomeDocumentRuntime::previewRegion` 使用绑定的空间 URI 和 seed 调用原有规则生成，
拒绝来自其他运行时或已失效的绑定。空间提供者只在本次调用借用；相同规则、空间、seed、
spacing/jitter 保持现有 PointSet 确定性契约。

这里统一的是**方案关联和已发布配置跟随**。URI 合法不等于资源存在或领域方案有效；
领域资源加载器仍负责解析、类型检查和执行。配置查询不加载资源、不重刷生物、不改写已生成地形，
也不直接调用全局 Weather/Fog setter。各领域按其正常准备阶段消费当前配置，
不能据此声称已有跨天气/怪物/地形执行的原子事务。

### module-interface：biome_editing / biome_editor

- 权威状态：文档拥有 authored 方案；运行时仅拥有不可变意义的发布投影，区域仅持弱绑定。
- capability in/out：沿用空间 resolver、现有属性/快照能力及编辑命令；无新全局 provider 注册表。
- ECS/Link：不新增 ECS System；绑定为上述 owner-thread 弱 Link，两种销毁顺序与恢复均显式定义。
- events：没有外部回调或锁内调用；发布与查询在 owner thread 的明确同步点执行。
- script/tool：新增 scheme/bind/restore/configuration 方法使用统一 Result 投影；MCP 复用已有编辑协议。
- 机制：一对多、低频发布、调用方已知、规则与引用同次发布；不做逐帧字符串 capability 查询。
- 成本：编辑仍复用现有文档事务和预览成本；configuration 仅复制六个引用，建议按配置变更读取。
- 裁剪：不增加模块依赖；authoring-only 卫星的既有裁剪范围不变，不宣称新增运行时独立裁剪能力。

## 核对证据

实现：[biome_editor.cpp](../../../src/modules/procgen/biome/editor/biome_editor.cpp)。流程与边界测试：[editor_biome_rules_editor.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_editor_camera_target_tests.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

补充入口见 [绑定参考](biome_editor/api-reference.md)；通用工作区与撤销见 [Editor](editor.md)。

完整应用：[examples/biome-rules-editor](../../../examples/biome-rules-editor/README.md)。
运行 `eve run examples/biome-rules-editor`；先沿 main.nut 的初始化、交互和清理顺序看调用，
再修改资源/文档，不把示例里的 fixture ID 当成项目全局 ID。
