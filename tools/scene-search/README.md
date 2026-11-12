# 本地模型到 C++ 场景查询

初次使用请从[完整教程](../../docs/usr/modules/scene/semantic-search.md)开始；
给调用模型的操作规范见 [eve-scene-search skill](../../.cursor/skills/eve-scene-search/SKILL.md)。

`scene_search.py` 是客户端：从引擎获取真实场景快照，在独立 Python 进程执行实际模型推理，
向引擎发布向量，C++ 用 hnswlib 构建一个包含各对象的索引，查询复用该索引。Python 不维护世界、不做候选排序。
模型未安装或离线缓存缺失时明确失败，不用关键词或随机向量冒充 embedding。

默认模型为 [多语言 MiniLM](https://huggingface.co/sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2)，
权重固定为 `e8f8c211226b894fcb81acc59f3b34ba3efd5f42`（Apache-2.0），384 维。
按照模型官方的 attention-mask mean pooling 计算，再做 L2 归一化；最大 128 tokens，超长输入拒绝。
推理为 CPU FP32，`trust_remote_code=False`，只加载 safetensors。

```sh
python -m pip install -r tools/scene-search/requirements.txt
python tools/scene-search/scene_search.py "有金币的箱子" --port 7547 --host semantic-demo
python tools/scene-search/scene_search.py --build --port 7547 --host semantic-demo
python tools/scene-search/scene_search.py --serve --port 7547 --host semantic-demo
```

`--serve` 是 stdio MCP 服务，提供 `eve_scene_semantic_query`，参数 `query` 为自然语言，
其余过滤参数与原生工具一致。把上述命令配置为 MCP 客户端的 command/args 即可。
调用模型先把“箱子”解析为唯一对象 ID，再查询“门”并传入空间谓词；服务不使用关键词规则猜测自然语言方位。
索引在启动准备阶段构建，后续查询只编码查询文本；`--build` 可用于场景构建完成后的显式预热。
未启动模型客户端时，C++ 不会自行下载模型或生成 embedding。

协议为 `eve.scene-search` v1，拒绝未知请求字段与非法类型，无持久格式迁移。

1. `eve_scene_search_prepare({host})` 返回 `snapshot` 和 `records`。模型输入为 `record.text`。
2. 在引擎外生成向量，再调用 `eve_scene_search_publish({snapshot,model,vectors})`，按原始 record 顺序提交。
   model 必须包含固定权重 revision、pooling、长度与归一化策略；不能混用模型空间。
3. `eve_scene_search_status({})` 获取当前 token、模型身份与实时几何，不重建索引。
4. `eve_scene_search({snapshot,model,embedding,tags,kind,space,visible,layer,center,radius,spatial,candidates,limit,scanBudget,pageToken})`。
   `embedding` 可省略，此时无需模型或向量索引。过滤为 AND；kind 是已声明的 scene link kind，
   `scene-node` 匹配所有节点，并非任意玩法类型。球形范围在世界空间按节点原点判定。

返回 `hits`（对象 ID、持久 ID 若有、名称、标签、世界位置、score、evidence）、
源/索引 snapshot 水位、model、backend、scanned、matched、approximate、candidateTruncated、truncated 和 pageToken。
snapshot 是进程内不透明一致性令牌，不是世界 MVCC revision 或持久身份。
没有持久 UUID 的旧节点必须按 host + snapshot + id 使用，不伪造 UUID。

每次操作重新验证语义源并读取实时几何。名称、标签、节点集合、父子关系、空间类型改变，
宿主消失或同名重建后必须重新 prepare/publish；客户端按相同文本缓存 embedding。
移动、旋转、可见性、layer、包围盒变化不重建索引，下一次查询读取新值；几何变化会使旧分页令牌失效。
一次最多 4096 节点、1024 维、每页 256 个，预算不足明确失败而不返回貌似完整的子集。
向量检索为 hnswlib 0.8.0 的近似最近邻：M=16、efConstruction=200、固定 seed=100，
查询 ef 至少 128。先精确计算标签和空间等允许集合，再将允许集合传入 HNSW filter，避免先 top-k 后过滤漏空。
`candidates` 默认 128；`matched` 是精确允许集合大小，`candidateTruncated` 表示候选窗口小于允许集合，
`truncated` 则只表示窗口内还有分页。ANN 不承诺全局精确 top-k 或具体召回率；没有规模性能测量承诺。

空间谓词数组为 AND，例如 `[{"relation":"left-of","reference":"chest","frame":"world"}]`。
支持 left-of/right-of/above/below/in-front-of/behind/near/inside；reference、observer 必须是当前 host 中的确切 ID。
frame 可为 world、reference 或 observer（后者必须额外传 observer ID）。3D 世界 +X 右、+Y 上、+Z 前；
2D +Y 向下；对象基底采用 local +Z 为前，不等同于 Camera3D 的 -Z 视线。
near 按节点原点欧氏距离；inside 是原点位于作者包围盒的世界 AABB，缺失盒子会失败，不代替碰撞或可见性。
每个命中返回 spatialEvidence 的距离和 offsetRightUpForward，使调用模型依据数值解释方位。

每个进程只有一个搜索会话、一个向量空间和一个活动分页游标。新的 prepare 替换会话，
成功 publish 替换索引，新的查询替换游标；继续分页必须原样重发筛选、向量和预算，只更新 pageToken。
失败发布保留原索引；失败查询不消耗游标。需要多个并发 Agent 时由调用方串行协调搜索会话。

模型推理与下载不在引擎帧循环内。索引构建、几何扫描与 HNSW 查询是工具阶段的有界同步操作，适用于低频场景发现。
这实现了 Python 原型中的场景查询切片，不包含事务、通用字段 Schema、关系遍历或 eventual consistency。
