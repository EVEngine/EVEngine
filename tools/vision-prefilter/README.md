# EVEngine vision-prefilter

本地轻量视觉预筛选服务 —— **低成本前置视觉过滤器**。

基于 llama.cpp `llama-server` 对 Qwen2-VL-2B **GGUF Q4_K_M 量化** 推理封装，批量评估
「渲染快照 + 精简几何文本快照」，输出结构化风险评级。**过滤无问题视角**，把
「高价高精度 VLM」的调用量压到最低：只有 `need_high_precision_review == true`
的场景才值得继续走昂贵模型。

## 为什么不用 PyTorch

本服务**不依赖 torch / transformers / bitsandbytes**（避免几十 GB 依赖、CUDA 匹配
地狱、脆弱性）。推理交给 llama.cpp `llama-server`（纯 C/C++ 单二进制 + 一个 GGUF
模型文件），本服务只是它前面的一个薄 FastAPI 网关。

`llama.cpp` 的 **GBNF grammar 在 token 采样层强制输出**，从根上杜绝模型吐出自由
文本或 markdown —— 这比 prompt 约束硬得多，正好满足「只输出标准 JSON」的硬需求。

## 定位

```
渲染快照 + 几何文本快照 ──► [vision-prefilter 网关] ──► [llama-server (Qwen2-VL-2B GGUF)]
                                  │
                    risk_score 0~3 / has_problem / problem_regions
                                  │
                 需要高精度复查? ──否──► 通过（无需再花大模型钱）
                                  │是
                                  ▼
                        高价高精度 VLM
```

- **独立、可后台运行**（`bin/start_vision_prefilter.*` 同时拉起 llama-server + 网关）。
- **标准化输入输出协议**（见下）。
- **批量推理接口** `POST /v1/prefilter/batch`。
- **内置业务判定规则**：道路遮挡=高风险、植被扎堆=中风险、布局均衡=无风险。

## 目录结构

```
vision-prefilter/
├── README.md
├── requirements.txt
├── vision_prefilter/
│   ├── __init__.py        # 常量 / 元信息
│   ├── protocol.py        # 标准化输入输出 schema（pydantic）+ 校验
│   ├── grammar.py         # GBNF grammar：token 采样层强制标准 JSON
│   ├── rules.py           # 内置业务判定规则（道路/植被/均衡）
│   ├── model.py           # llama-server OpenAI 兼容 API 客户端 + 解析
│   ├── server.py          # FastAPI HTTP 网关
│   └── __main__.py        # CLI 入口
├── bin/
│   ├── start_vision_prefilter.sh    # Linux/macOS 后台启动整套
│   └── start_vision_prefilter.bat   # Windows 后台启动整套
├── scripts/
│   ├── client_batch.py    # 批量客户端示例
│   └── download_model.sh  # 下载 Qwen2-VL-2B Q4_K_M GGUF
└── test/
    └── test_core.py       # 规则 + JSON 解析单测
```

## 安装

Python 3.10+（仅网关用），并安装 llama.cpp：

```bash
# 1) 安装 llama.cpp（单二进制；Windows 从 releases 下载 llama-server.exe，
#    Linux/macOS 见 https://github.com/ggml-org/llama.cpp 或 brew/包管理器）
#   确保 llama-server 在 PATH。

# 2) 下载 GGUF 模型（Q4_K_M 约 1.6GB）
./scripts/download_model.sh          # 或按需换别的 Qwen2-VL-2B GGUF

# 3) 安装网关依赖（轻量，无 torch）
cd tools/vision-prefilter
python -m venv .venv
# Windows: .venv\Scripts\activate   Linux/macOS: source .venv/bin/activate
pip install -r requirements.txt
```

## 启动（后台）

Linux / macOS：

```bash
./bin/start_vision_prefilter.sh models/qwen2-vl-2b-instruct-q4_k_m.gguf
```

Windows：

```bat
bin\start_vision_prefilter.bat models\qwen2-vl-2b-instruct-q4_k_m.gguf
```

脚本会拉起 llama-server（默认 :8080，`-ngl 99` 尽可能 GPU 卸载）再拉起网关
（默认 :8531），各写 `llama-server.log` / `vision-prefilter.log` + `.pid`，可后台运行。

前台手动启动（供调试）：

```bash
# 终端 1：llama-server
llama-server -m models/qwen2-vl-2b-instruct-q4_k_m.gguf --host 127.0.0.1 --port 8080 -ngl 99

# 终端 2：网关
python -m vision_prefilter --host 127.0.0.1 --port 8531 --backend http://127.0.0.1:8080
```

环境变量：`VISION_PREFILTER_HOST` / `VISION_PREFILTER_PORT` / `VISION_PREFILTER_BACKEND`
（默认 `http://127.0.0.1:8080`，即 llama-server）/ `VISION_PREFILTER_GGUF`。

## HTTP API

### `GET /health`

```json
{ "service": "eve.vision.prefilter", "status": "ok",
  "backend": "http://127.0.0.1:8080", "backend_ready": true }
```

### `GET /protocol`

返回当前输入/输出的 JSON Schema，调用方可按此对接。

### `POST /v1/prefilter/batch`

批量推理。**输入**（每张快照）：

```json
{
  "protocol_version": "1.0",
  "scenes": [
    {
      "id": "view-001",
      "image": "data:image/png;base64,iVBORw0KGgo...",
      "geometry": "road=(0,0)->(100,0)\ntreeCluster N=6@(10,10)",
      "prompt": null
    }
  ]
}
```

字段：

| 字段 | 必填 | 说明 |
|------|------|------|
| `id` | 是 | 调用方场景标识，原样回显 |
| `image` | 是 | 渲染快照，PNG/JPEG 的 base64（`data:...;base64,...` 或裸 base64） |
| `geometry` | 否 | 精简几何文本快照（如道路线段、植被簇位置） |
| `prompt` | 否 | 覆盖固定任务 Prompt |

**输出**（每个场景一个）：

```json
{
  "protocol_version": "1.0",
  "service": "eve.vision.prefilter",
  "results": [
    {
      "id": "view-001",
      "ok": true,
      "result": {
        "risk_score": 3,
        "has_problem": true,
        "problem_regions": [
          { "bbox": [120, 40, 300, 210], "type": "道路遮挡", "note": "..." }
        ],
        "need_high_precision_review": true
      },
      "error": null
    }
  ]
}
```

单场景失败不会影响整批：`ok=false` 且 `error` 为结构化消息（不包含模型原文）。

## 输出契约（模型侧）

GBNF grammar（`grammar.py`）在 token 采样层强制模型只输出该 JSON，禁止自由文本：

```json
{
  "risk_score": 0,                // 0~3
  "has_problem": false,
  "problem_regions": [            // bbox 为像素坐标
    { "bbox": [x1,y1,x2,y2], "type": "遮挡|过密|空旷|穿插|道路遮挡|植被扎堆", "note": "..." }
  ],
  "need_high_precision_review": false
}
```

## 内置业务规则（`rules.py`）

模型负责**感知**画面内容，判定走确定性规则，结果可解释：

| 判定 | 风险 | 高精度复查 |
|------|------|-----------|
| 道路遮挡（几何提示含 road / 模型检出道路遮挡/遮挡） | 3 | 是 |
| 植被扎堆 / 过密 / 穿插 | 2 | 否 |
| 布局均衡（无问题） | 0 | 否 |

`need_high_precision_review == true` 才触发高价高精度 VLM。

## 测试

无需后端即可跑核心逻辑单测（规则 + JSON 解析）：

```bash
pip install pytest
cd tools/vision-prefilter
python -m pytest test -q
```

## 调用方示例

```bash
python scripts/client_batch.py --image build/.../shot.png --geometry geom.txt --id view-001
```
