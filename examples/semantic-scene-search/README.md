# 真实场景语义搜索

这个例子创建一个真实 `SceneHost`，包括矿门、城门、远处木门、装饰门、宝箱、树和朝世界 +X 的观察者。
窗口显示简单位置预览；自然语言搜索由本地真实 embedding 模型和 C++ 查询管线共同完成。

从仓库根目录运行本次构建的引擎（已发布旧 SDK 不包含新增工具）：

```sh
make run/win32-debug GAME=examples/semantic-scene-search RUN_ARGS="--mcp-port=7547"
```

另一个终端安装模型客户端依赖并查询：

```sh
python -m pip install -r tools/scene-search/requirements.txt
python tools/scene-search/scene_search.py "废弃矿井的木制入口" --tag door --tag interactable --center 0 0 0 --radius 10
python tools/scene-search/scene_search.py --serve
python examples/semantic-scene-search/verify.py
```

Linux 对应 `make run/linux-debug`；无 GPU 的 CI 按根 AGENTS.md 配置 Lavapipe、Xvfb 与 null audio。
首次模型运行下载固定权重到 `build/model-cache`，之后可以加 `--offline`。模型使用 CPU FP32，
不要求 CUDA、HIP 或在线推理账户。验证脚本输出 `build/scene-search-verification.json`。

`--serve` 提供自然文本 MCP 工具；调用模型通过工具 schema 把对象含义和精确空间限制分开提交，
例如先定位宝箱 ID，再查询位于其左边的门。hnswlib 只检索对象含义，空间判断使用当前真实场景坐标。
`interactable` 是作者标签，不能据此获得操作权限。空间范围按节点世界原点计算，不代表碰撞或可达性。
名字与标签组成模型文本，不会自动识别画面、材质或任意脚本字段。

验证包括真实模型中英文检索、无模型的结构化查询、过滤先于排序、分页、预算、模型身份错误、
零向量与未知字段拒绝、只读不修改节点、世界左侧和观察者右侧、stdio MCP 自然文本查询，
以及通过正式场景命令注入移动后复用同一索引并按新位置过滤。移动验证最后恢复原始节点。
删除、包围盒内、上下前后、候选窗口与几何变化使分页失效、宿主销毁/重建在 `scene.search.*` C++ 用例覆盖。

第一次接入请阅读[完整教程](../../docs/usr/modules/scene/semantic-search.md)；
给调用模型的操作规范见 [eve-scene-search skill](../../.cursor/skills/eve-scene-search/SKILL.md)。
更多边界与协议见 [客户端](../../tools/scene-search/README.md) 和
[设计与模块契约](../../docs/dev/2026-11-11-scene-semantic-search.md)。
