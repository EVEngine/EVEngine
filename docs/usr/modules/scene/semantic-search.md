# 场景语义搜索：从建索引到 MCP 空间查询

## 这篇解决什么

本教程帮助开发者让 LLM 根据“装金币的箱子”“宝箱左边的木门”寻找真实场景对象。
优先入口是 Python stdio MCP 工具 `eve_scene_semantic_query`；已有 embedding 服务的集成方
可以使用原生 prepare / publish / query 协议。查询不会修改权威场景。

适用范围是已构建的 SceneHost，不包含仓库代码搜索、知识图谱遍历或图像识别。
名字与作者标签提供对象含义；引擎坐标提供空间关系；检索命中本身不赋予操作权限。
给 LLM 的简明操作手册是 [eve-scene-search skill](../../../../.cursor/skills/eve-scene-search/SKILL.md)。

## 1. 理解三个职责

1. **embedding 模型**把对象名称、标签和查询文本转成同一向量空间的向量。
   默认是 `sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2`，384 维，支持中英文；
   固定权重 revision、CPU FP32、attention-mask mean pooling、L2 归一化，最长 128 tokens，超长明确拒绝。
2. **C++ hnswlib**在 publish 阶段构建一个包含该 host 各对象的 HNSW 索引，后续查询复用。
   每个对象是一条向量，不是每个对象分别建一张图。
3. **Scene 查询**读取实时几何，精确过滤标签、可见性、距离和方向，再在允许集合中执行向量检索。
   空间方位不由 embedding 分数推断。

当前需要包含新功能的引擎构建，以及本仓库 `tools/scene-search/`。不要假定旧 SDK 或 SDK 中的
普通 `eve-mcp` 桥已经带有 Python 模型客户端。裁剪构建必须包含 Scene 与 DevTools。
模型下载和推理位于独立 Python 进程，不在游戏帧循环内。

## 2. 运行真实场景

在仓库根目录打开终端。已有本次功能构建时直接运行；需要源码编译时按根目录构建指南配置依赖：

```sh
make build/win32-debug
make run/win32-debug GAME=examples/semantic-scene-search RUN_ARGS="--mcp-port=7547"
```

Linux 对应 `build/linux-debug` 和 `run/linux-debug`；无桌面的运行环境还需按根 AGENTS.md
配置 Vulkan、显示和音频。终端出现 `Semantic scene search ready: semantic-demo` 后再启动客户端。

示例 host 为 `semantic-demo`，含根节点、两处矿门、石门、装饰门、宝箱、古树与观察者。
接入自己的游戏时，先通过正式建树/mount 路径建立 SceneHost，为对象写有含义的名称和标签。
例如名称“废弃矿井入口的木门”，标签 `door`、`interactable`；标签表达作者意图，不是权限证明。
不要把频繁变化的坐标拼进描述，也不要每帧重建语义索引。

## 3. 安装模型客户端并预热索引

另开终端，在仓库根目录使用独立 Python 环境。Windows PowerShell 示例：

```powershell
python -m venv build/scene-search-venv
$ScenePython = (Resolve-Path build/scene-search-venv/Scripts/python.exe).Path
& $ScenePython -m pip install -r tools/scene-search/requirements.txt
& $ScenePython tools/scene-search/scene_search.py --build --host semantic-demo --port 7547
& $ScenePython tools/scene-search/scene_search.py "装有金币的箱子" --host semantic-demo --port 7547
```

Linux/macOS 使用该环境的 `build/scene-search-venv/bin/python`。首次运行下载固定模型到
`build/model-cache`；缓存完整后可追加 `--offline`。首次下载、模型加载和索引构建可能较慢。
缺少依赖或模型时会失败，不会使用随机向量或关键词结果冒充语义检索。
`--build` 显式准备并发布索引；普通查询会复用匹配的索引，语义源失效时客户端才重新构建。

示例查询应以 `chest` 为首命中；检查返回的 `backend` 为 `hnswlib-0.8.0`，
`model` 为固定模型身份，`hits` 包含实际对象 ID、名称、位置与分数。分数是相似度，不是正确概率。

## 4. 给 LLM 接入自然文本 MCP 工具

模型客户端提供 stdio MCP 服务：

```powershell
& $ScenePython tools/scene-search/scene_search.py --serve --host semantic-demo --port 7547 --offline
```

该命令供 MCP 客户端启动，不是交互式聊天程序。配置中的 command 使用所选 Python 的绝对路径，
args 使用脚本绝对路径，host 与 port 对应正在运行的引擎。以下是使用 `mcpServers` 格式的配置示意，
需将 `C:/work/EVEngine` 换成本机仓库绝对路径；其他客户端按其配置格式填写相同 command/args：

```json
{
  "mcpServers": {
    "eve-scene-semantic": {
      "command": "C:/work/EVEngine/build/scene-search-venv/Scripts/python.exe",
      "args": [
        "C:/work/EVEngine/tools/scene-search/scene_search.py",
        "--serve", "--host", "semantic-demo", "--port", "7547",
        "--model-cache", "C:/work/EVEngine/build/model-cache", "--offline"
      ]
    }
  }
}
```

连接后通过 `tools/list` 确认存在 `eve_scene_semantic_query`。LLM 提交 query 文本及结构化过滤，
桥接负责模型推理、索引就绪与原生调用；不要传入自造的 embedding、model 或 snapshot。

**原生 TCP MCP 当前只接受一个活动客户端。** stdio 模型桥接会持有这个连接；不要同时启动另一个
直连该端口的 MCP 客户端。需要执行原生工具或验证脚本时，先停止桥接并释放连接；
多 Agent 应串行协调，或使用各自独立的引擎实例和端口。

## 5. 实战：寻找宝箱左边 5 单位内的可交互入口

下列 JSON 是 MCP `tools/call` 的 `arguments`，工具名均为 `eve_scene_semantic_query`。

第一步先解析“宝箱”，不能猜一个 ID 当作参照：

```json
{"query":"装金币的宝箱","limit":3}
```

阅读候选的名字、标签和位置。示例应返回 `chest`；如果有多个符合描述的宝箱，先利用上下文消歧，
必要时询问用户。`limit:1` 只缩小返回页，不证明对象唯一。

第二步把对象含义与几何条件分开。这里明确采用世界坐标的左侧：

```json
{
  "query": "入口木门",
  "tags": ["door", "interactable"],
  "spatial": [
    {"relation":"left-of","reference":"chest","frame":"world"},
    {"relation":"near","reference":"chest","distance":5}
  ],
  "limit": 5
}
```

示例应返回 `mine-door`。其 `spatialEvidence` 中相对坐标约为 `[-2,0,-3]`，距离约 `3.606`：
第一个分量为负，满足世界左侧；距离不超过 5。其他游戏必须使用第一步实际返回的参照 ID。
回答应引用这些数值依据，不能仅因为名称像“门”就声称空间条件成立。

### 世界、参照对象与观察者三种方向

- `frame:"world"`：3D 世界 +X 右、+Y 上、+Z 前。
- `frame:"reference"`：使用参照对象方向基底，适合“这栋建筑自身右侧”。
- `frame:"observer"`：额外传实际 `observer` ID，按观察者方向描述参照对象与候选的相对偏移。
  观察者只提供方向基底；距离与偏移的原点仍是 `reference`。

例如已解析观察者 ID 为 `observer`，寻找按其朝向位于宝箱前方附近的门：

```json
{
  "query":"门",
  "tags":["door","interactable"],
  "spatial":[
    {"relation":"in-front-of","reference":"chest","frame":"observer","observer":"observer"},
    {"relation":"near","reference":"chest","distance":5}
  ]
}
```

示例返回 `stone-gate`，相对观察者基底的前向偏移约 `+1`。这是“按观察者朝向，宝箱前方”，
不是“在观察者位置前方”；后者应将 reference 也设为观察者 ID。

对象基底采用 local +Z 为前，不自动等同于 Camera3D 的 -Z 视线，更不等同于屏幕左侧。
2D 场景 +Y 向下，而证据中的 up 分量统一表示向上。用户说“左边”但上下文无法确定参考系时，
先澄清参考系，不默认为相机视角。

支持 `left-of`、`right-of`、`above`、`below`、`in-front-of`、`behind`、`near`、`inside`。
所有 spatial 条件和其他过滤为 AND。方向用原点偏移与 margin 比较；near 使用原点欧氏距离；
inside 表示候选原点落入参照对象作者包围盒的世界 AABB，缺盒明确失败。
这些条件不能证明物体整体包含、碰撞、无遮挡、可见或可达。

## 6. 读取结果、分页和处理变化

- `matched` 是通过精确过滤的对象数，不代表全部对象都与文本语义相符。
- `approximate` 表示 HNSW 近似检索；`candidates` 控制候选窗口，默认 128。
  `candidateTruncated:true` 表示窗口未容纳全部允许对象，可在预算内扩大 candidates 后从第一页重查，
  但更大窗口仍不构成全局精确 top-k 保证。
- `truncated` 与 `pageToken` 表示当前窗口内还有后续页。继续分页时原样重发 query、过滤和预算，
  只更新 pageToken；不要插入另一条成功查询。一个进程只有一个搜索会话和活动游标。
- 坐标、旋转、可见性、layer 或 bounds 变化会读取实时值，复用语义索引，但使旧几何分页失效。
  遇到 stale page 时放弃旧页，从第一页重新查询。
- 名称、标签、节点集合、层级、空间类型或宿主代际改变要求重新 prepare/publish。
  Python 桥接下一次查询自动检查；原生调用方显式重建，不能重用旧向量顺序或旧 token。
- `isError` 是失败，不是空结果。未知参照重新解析，缺 observer 补正确 ID；
  缺 bounds 不改成猜测的 inside；预算不足明确说明，没有返回“完整子集”。

结果 ID 需结合当前 host 与搜索会话使用；snapshot 是进程内不透明令牌，不是持久 UUID 或全世界版本。
查询只读，后续移动、删除或交互必须使用正式操作接口并重新验证对象，不用 eval 绕过。

## 7. 原生集成与验收

已有 embedding producer 的系统可以直连原生 MCP，先从 `tools/list` 读取实际 schema：

1. `eve_scene_search_prepare({"host":"semantic-demo"})` 获取 token 和 records。
2. 对每个 `record.text` 编码，按 records 原顺序提交
   `eve_scene_search_publish({snapshot,model,vectors})`。model 标识必须包含固定权重及推理策略。
3. 由相同模型编码查询，调用 `eve_scene_search({snapshot,model,embedding,...})`。
   这里用的是 embedding，不能向原生工具传 `query` 文本。
4. 用 `eve_scene_search_status({})` 获取模型身份、token 和实时几何，不重建索引。
   无 embedding 的结构化 query 不依赖模型；失败 publish 保留旧索引。

启动桥接前或停止桥接后，在独占引擎连接的终端运行真实模型验收：

```powershell
& $ScenePython examples/semantic-scene-search/verify.py --port 7547 --offline
```

脚本覆盖检索、空间关系、分页、失败输入及 stdio 工具，并通过正式命令临时移动对象再恢复。
输出保存在 `build/scene-search-verification.json`，不要对他人正在使用的会话运行这段验证。
“能连接”“模型成功 import”不等于语义闭环成功，应核对实际 hit ID、位置和 spatialEvidence。

当前每个会话最多 4096 节点、1024 维、每页 256 命中、16 个空间谓词。空间与源校验仍线性扫描，
只面向低频工具调用；小场景通过不构成大规模性能或泛化召回率证据。
更多协议边界见[客户端说明](../../../../tools/scene-search/README.md)，
所有权、生命周期和 module-interface 见[设计文档](../../../dev/2026-11-11-scene-semantic-search.md)。
