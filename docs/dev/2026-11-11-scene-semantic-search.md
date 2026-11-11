# 只读场景语义搜索：原生闭环与 module-interface

## 所有权和机制选择

SceneHost 是节点名称、标签、空间类型、可见性和 TRS 的唯一权威 owner。
`scene::SceneSearch` 只拥有可重建的 owning 快照、向量和私有 HNSW 索引，不保留 host、node、component 或 link target 指针。
DevTools 通过 common 的 `ISceneSearch` 窄虚接口提问，Scene 注册进程期 provider，沿用 `cap` 机制。
这是一对一、低频、已知 consumer、无跨 owner 原子写入的服务查询（R-MECH-1..4、R-COHESION-6）。
不是对象 Link，故选择消费者契约；无需引入新领域或通用 GameObject 根。

module-interface `scene-search` 的六个面：

- E1：Scene 提供 `ISceneSearch`；缺席返回 `scene-unavailable`。
- E2：DevTools 可选查询该 capability；没有 Scene 的链接符号依赖。模型是独立进程的可选 producer，
  未发布索引时语义查询返回 `vector-index-unavailable`，结构化查询继续使用。
- E3：读取 `View<SceneHost,Meta>`，同步读取 Tree/SceneNode；没有 ECS 写入或结构修改，不新增 ECS System。
  host ECS handle 的 table/id/generation 仅用于内部源比较，不跨协议发布。
- E4：不监听/发布事件，不调用 script、link callback 或领域写入。每次显式操作重新验证投影。
- E5：不新增脚本绑定；示例复用 Scene 正式建树/标签 API。
- E6：`McpSceneSearch.cpp` 本地声明 `EVE_MCP_TOOL_FAMILY`，编译源生成组合，
  不改中央 schema/dispatch。协议为 `eve.scene-search` v1，未知字段拒绝；不引入持久格式。

公共接口线程亲和为引擎安全点，非重入；输入只在调用期间借用，结果 owning。
provider 为进程期静态对象，比 Scene 模块实例活得久；宿主先销毁返回 NotFound，
宿主重建由 generation 检测，provider 先释放仅丢弃投影。Scene 构造/析构主动 invalidate，
运行时重启或 ECS table 地址重用不会沿用旧会话。restore/hot reload 后源改变需重新准备。
模型下载、tokenization 和推理在 Python 客户端，C++ 不等待网络/模型回调，也不持锁调用外部代码。

## 查询与一致性

prepare 构建有界投影并返回 token；publish 验证全部维度、有限值、非零范数和语义源，
在临时 HNSW 中加入每个对象，全部成功后一次发布。失败不改变之前的索引。
query 验证语义投影并读取实时几何，先精确计算允许集合，再把它作为 HNSW filter 查询候选窗口，
最后按分数降序、host-local id 升序排序分页，不使用固定 top-k 再过滤。
向量检索明确返回 approximate；matched 是允许集合大小，candidateTruncated 表示候选窗口不足以容纳全部允许对象，
与窗口内的分页 truncated 区分。无 embedding 的结构化查询仍然精确。

hnswlib 0.8.0 使用固定 SHA256 的上游原始 archive，Scene 私有 SYSTEM include，无类型泄露或修改依赖源。
M=16、efConstruction=200、seed=100，efSearch=max(128,候选窗口)，候选窗口默认 128。
MSVC C4701 后端诊断不遵循 SYSTEM：只在 upstream include 的词法作用域抑制该警告；
该路径的变量读写由同一 is_vacant_place 分支保护，本服务禁用删除替换。自有代码仍为 /W4 /WX。

场景变换以当前本地 TRS 重算，沿用 `detail::localMatrix`，不调用带 pull/push 副作用的 TransformSystem。
父级隐藏影响子级；parent 不要求在 arena 前面，cycle 和非法 parent 拒绝。
球形范围是节点世界原点距离；空间谓词支持左右、上下、前后、near 和 inside。
frame 明确区分世界、参照对象基底和观察者基底；3D 对象 local +Z 为前，不隐式冒充 Camera3D -Z 视线。
inside 为节点原点在作者包围盒的世界 AABB，缺盒或退化基底明确失败，不推断碰撞、可见性或可达性。
返回逐谓词相对坐标与距离。Python stdio MCP 的 schema 教调用模型先消歧参照 ID，再提交语义和空间参数，
不使用文本 embedding 猜测几何，也不声称实现独立自然语言空间解析器。
kind 指 link kind，标签表达作者意图，不声称可推断任意 ECS 玩法类型。

水位是源投影 token，不冒称全世界版本/MVCC；仅支持 source-validated 查询。
完整比较语义源值及宿主代际，因此检测直接字段写入和旁路脏标记。
删除后旧查询拒绝，不返回幽灵对象。坐标不进入模型文本；移动、旋转、可见性、layer 和 bounds 变化，
读取当前值并复用同一语义索引；分页额外绑定几何投影，移动后不能继续旧页。
同一源改动又恢复成完全相同的查询投影可再次使用令牌：这是投影一致性，而非历史冲突检测。

不使用模拟时间；HNSW 构建使用固定命名索引 seed=100。相同插入顺序和参数复现候选，候选内有稳定排序，
不承诺跨平台 ANN 候选完全相同或全局精确 top-k，浮点余弦分数按 1e-5 容差；
模型 FP32 不承诺跨框架/硬件位级一致。模型 identity 包含不可变权重 revision、pooling、
max token、dtype 和归一化策略。输入超过模型长度预算拒绝，禁止静默截断。

## 预算和验收入口

节点上限 4096，维度上限 1024，模型文本每节点 4096 bytes，每页至多 256。
prepare/publish 两步分离昂贵模型工作；公共接口 `@cost` 说明场景扫描、图构建和数据相关查询成本。
空间/源验证仍为线性扫描；HNSW 仅替代向量全量打分，不承诺整条查询子线性或百万对象每帧使用。

- `scene.search.*`：真实 SceneHost、父级世界坐标、过滤先于排名、分页绑定、失败发布保留旧值、
  模型缺席/错配/维度/NaN/零向量、移动/删除/宿主生命周期。
- MCP catalogue 与协议用例：真实 family 组合、缺席 provider、未知字段和类型、只读/profile 可发现。
- `examples/semantic-scene-search/verify.py`：真实多语言模型到 C++ HNSW；世界/观察者谓词、移动复用索引与 stdio MCP 查询。
- `make check`：source-only 架构门禁；SHARED/OBJECT 构建、精简 profile 及宿主 smoke 单独报告。

精简宿主用 `cmake --build build/win32-debug --target eve scene_search_contract_test` 构建。
`scene_search_contract_test` 复用同一份 scene 单元测试与 MCP catalogue 测试，避免精简 profile
链接无关编辑器/其他可选领域消费者。CTest 名称前缀为 `scene-search-contract/`，
正常完整 CI 仍由原有 per-domain suite 发现、运行这些测试。

裁剪声明为 DevTools→Scene 的 L2 虚接口边界和 L4 缺席行为；是否完成实际精简构建以本次验收日志为准。
不以 include 图代替链接证据。无新增架构豁免，也不引入平行 JSON catalogue。
