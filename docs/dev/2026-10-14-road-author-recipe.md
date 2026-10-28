# 完整作者网络进入标准道路 recipe

2026-10-14。核心对齐的一项进展，整体仍未完成。

`setRoadRecipeNetwork(params, network)` 验证源网络并将既有 `eve.procgen.roadNetwork` schemaVersion 1 快照保存为 Params 的 `networkSnapshot` owning canonical JSON。失败不改变 Params。源网络仍由作者文档或调用者独占；快照是一次资产生成输入，不成为第二个可编辑作者状态。调用者需同步源网络与 Params；同步调用无回调、ECS、provider、时间或 RNG 依赖。

`captureRoadNetwork` / `restoreRoadNetwork` 以 common Value 暴露相同格式。native 和编辑器共用 private scalar/point/style/connection/record `.inc` 实现；编辑兼容类型与 common Value 各自编译同一算法，不复制字段清单。旧快照迁移沿用原有规则，未知字段忽略，未知版本拒绝，所有语义约束在私有候选图上完成。native 输入上限：65,536 节点/边、262,144 活跃或禁用连接、每边 4,096 marker，值树深度 64/262,144 项/8 MiB 键与字符串，recipe JSON 8 MiB。缺少 blockedLaneLinks 迁移为空；显式错误类型拒绝。

`bakeRoadNetworkRecipe` 现在优先解析显式快照。只有未提供快照时才使用 scene/span/lanes 等程序场景参数。空、错误类型、错误 JSON、无效作者图都明确失败，不能默默退回 straight。现有 mesh、overlay、artifact 调用相同 recipe；artifact LOD 修改采样参数时保留快照，因而不会退回程序场景。Params 的既有 typed canonicalString/BuildKey 将完整快照计入生成身份；helper 生成 canonical JSON，调用者直接提供非 canonical JSON 会形成不同构建键，不宣称语义去重。

使用：

```cpp
Params params;
auto prepared = setRoadRecipeNetwork(params, document.network());
if (!prepared.ok()) return Result<GeneratedArtifact>::failure(prepared.status());
return generateRoadNetworkArtifact(params, artifactId);
```

recipe 只保存解析后的作者网络，不持久化共享预设历史、名称锁定或全 native World 对象/探针。预设文档继续通过自己的 capture/prepareRestore 保存；这些状态不参与道路烘焙。专用 UI 和脚本便利入口仍待补，不能把原生入口当作这些界面完成。

验证：新增测试覆盖编辑与 native 快照一致、JSON/图恢复、完整截面/桥隧/逐点宽度横坡进入实际 recipe，三档 LOD 保留相同作者图，显式错误输入无场景回退、深度与 marker 预算、Params 失败原子性。全部 11 个高级功能夹具现在通过标准 bakeRoadNetworkRecipe 导出，并逐个比较直接 bake 的 positions/indices；包含 prefab、自动间距、多臂设施、真实自定义接入口、地形多通道和共享类型历史恢复。它们写出 recipe_network.json。此证据证明 recipe 消费，不等同于 Unity 高级设施渲染或专用编辑 UI 验收。

STATIC/SHARED focused suite 39 cases passed。正式 procgen-core-only OBJECT 编译通过，覆盖新 native codec/recipe 和 RoadArtifact，排除了 editing 依赖。这是 L1/L2 证据，不宣称全引擎 L3/L4。新增独立生产 artifact 测试实际调用 generateRoadNetworkArtifact 与 ArtifactStore：验证主 mesh 与同快照 recipe 完全一致，结构与驾驶碰撞、导航、placement、footprint、surface-weight 各角色存在，各 LOD chunk 的总索引数与原生同采样 bake 相同，生成身份随作者变化、发布后 JSON 保存恢复、重复发布与无效生成不改变已有状态。此测试编译真实 GeneratedArtifact.cpp/RoadArtifact.cpp/ArtifactPublication.cpp，不使用 stub 或旧 SDK 资产实现。GeneratedArtifactRecipes.cpp 从原先大文件纯移出通用/城堡/六边形工厂及其私有 helpers，稳定所有公开声明和格式；正式 procgen-core-only inventory 同步包含新 TU。架构适用项：Result、唯一作者所有权、版本与迁移、原子恢复、无向上依赖、六面边界与分级裁剪；无架构例外。

本次 recipe 多臂设施场景已捕获实际 EVEngine engine-owned readback，路径 build/road-parity/recipe-scenes/multi-arm-facilities/ev-frame.png；预编译 renderer 仅上传当前 recipe 导出的 mesh。当前源码生产 artifact 在独立 CPU executable 中构建、发布、保存恢复；不宣称 renderer 已直接执行其跨模块 GPU/physics provider 发布。

默认 make check 已运行，仍因未修改的 IStateProvider.h 三项 bool 返回与 RenderSystem.h 两项裸指针寿命说明失败；本改动的增量架构检查另行验证，不更改基线或 allowlist。
