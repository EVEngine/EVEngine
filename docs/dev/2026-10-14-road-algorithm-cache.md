# 道路生成算法修订与缓存身份

道路标准 artifact 及 BuildKey::forRecipe("mesh.roadNetwork", params) 现在共享生产者拥有的 RoadAlgorithmRevision.h 修订标识。当前键形状为 procgen/v2/mesh.roadNetwork/<revision>/<input-hash>，哈希同时包括 recipe、修订和 canonical Params。顶层 metadata.algorithmRevision 从同一常量派生，mesh/collider/navigation/terrain 子键继承父键；render chunk/LOD 保留独立内容键，升级为 eve.procgen.road.chunk.v2/<revision>/<role>/<content-hash>，同样包含生产者修订，但不因其它 chunk 的输入改变而全量失效。作者 Params 中的同名字段不会覆盖生产者修订。

历史 procgen/v1 键仍作为不透明 canonical 字符串保存和恢复；不自动改写为新键，也不冒充当前生成结果。实际 ArtifactStore 测试验证旧键可恢复并按旧键找到，但不会命中当前修订键；新键和修订元数据贯通 JSON 保存恢复，重复发布的既有原子性保持不变。其它 recipe 的 v1 键算法保持不变。

修订由代码维护者在改变道路生成语义或 artifact 产品布局时更新。这是显式算法版本，不是构建时源文件哈希，不能声称自动检测每一次代码编辑。源文件路径、时间戳、编译机器和 Unity 参考数据都不进入生产键。单个修订也不构成增量 dirty graph、后台流式烘焙或完整资产依赖追踪；这些 B7 剩余项继续保留。

为遵守大文件拆分规则，BuildKey 方法及仅供它使用的字符串校验/FNV 辅助函数从 GeneratedArtifact.cpp 纯移到 BuildKey.cpp，然后只扩展道路分支。正式 procgen-core-only 清单和独立 artifact 测试构建都消费同一个新 TU，无新 provider、跨域依赖或第二份作者状态。保存格式 schema 维持原值，因为键本来就是不透明文本、metadata 本来就支持未知成员；此次变化是生成身份策略更新。

最终验证：STATIC 道路作者、Spline 和实际 artifact 测试全部通过；实际 artifact 1 case / 168 assertions，新增无关参数变化下所有 LOD chunk 内容键保持一致的消费回归。SHARED 道路/Spline 契约通过，但该独立 SHARED harness 不包含 ArtifactStore 测试，不将其结果写成 SHARED artifact 链接证明。正式 EVProcgenCoreCheck 严格警告 OBJECT 编译通过，相对 HEAD 架构检查、10 项证据检查器、ruff 与差异格式检查通过。默认 origin/dev 的 make check 通过格式、层次、脚本协议和 profile 检查后，仍仅报 IStateProvider.h 三个 bool 操作 API 与 RenderSystem.h 两个指针生命周期文档的已知问题，没有改基线或新增豁免。适用规范为唯一所有权、持久身份迁移、返回值检查、公共契约文档、模块六面证据和大文件拆分；无例外。
