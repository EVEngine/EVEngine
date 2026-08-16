# EVEngine NormalCrafter

给**序列帧素材**批量生成**连续、不闪烁**的法线贴图序列的**可部署工具**。

基于 ICCV 2025 论文
[*NormalCrafter: Learning Temporally Consistent Video Normal from Video
Diffusion Priors*](https://arxiv.org/abs/2504.11427)（Yanrui Bin et al.，
[官方实现](https://github.com/Binyr/NormalCrafter)，MIT 协议）。本工具**自包含重写了该
推理算法**（不 import 上游 `normalcrafter` 包），并做成**可部署的 GPU 推理微服务**：
GPU 上跑重活，美术机上只跑轻量 CPU 客户端。

```
┌───────────────────────────── CPU (美术机/管线) ─────────────────────────────┐
│  PNG序列帧目录  ──►  (ffmpeg/OpenCV 打包成 mp4)  ──►  HTTP POST              │
│  视频文件       ───────────────────────────────────►  HTTP POST              │
└──────────────────────────────────────────────┬───────────────────────────────┘
                                               ▼
┌─────────────────────────────── GPU (单副本 Pod) ─────────────────────────────┐
│  FastAPI 服务 (预加载模型, concurrency=1)                                     │
│    NormalCrafter 算法: 单步去噪 + 时序滑窗 + 线性融合                          │
│  ──► 返回法线 PNG 的 ZIP (无损)                                               │
└──────────────────────────────────────────────┬───────────────────────────────┘
                                               ▼
┌───────────────────────────── CPU ───────────────────────────────────────────┐
│  解包 ZIP ──► 连续不闪烁的法线 PNG 序列  (可选: 再合成法线 mp4)                 │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 为什么它不闪烁

普通做法是逐帧用单图法线估计 → 每帧噪声/歧义独立 → 相邻帧法线抖动（闪烁）。
NormalCrafter 把整段序列当一个**时空整体**去扩散生成：

- **单步去噪**：latent 从零初始化，一次 UNet 前向直接生成整窗法线（`num_inference_steps=1`、CFG 关闭）；
- **时序滑窗 + 线性融合**：长序列切成 `window_size` 的窗口，相邻窗口重叠区用线性权重融合 →
  法线跨帧连续流动，这就是不闪烁的关键；
- **两阶段训练**（latent + pixel 空间）保留高频细节，任意长度可滑窗连贯。

## 目录结构

```
normal-crafter/
├── server/                        # GPU 推理服务（自包含算法 + FastAPI）
│   ├── algorithm.py               # NormalCrafter 推理算法（重写，无上游依赖）
│   ├── model.py                   # 预加载权重 (UNet + 时序VAE)
│   ├── engine.py                  # 单飞推理引擎 (concurrency=1)
│   ├── app.py                     # FastAPI: POST /v1/normal-crafter, /health
│   └── __main__.py                # 服务入口 (uvicorn)
├── normal_crafter/                # CPU 客户端
│   ├── frames.py                  # PNG目录 ⇄ 视频 桥接
│   ├── client.py                  # HTTP 客户端 + ZIP 解包
│   └── __main__.py                # CLI
├── bin/                           # 启动脚本 (server + client)
├── Dockerfile                     # GPU 服务镜像 (nvcr pytorch 基底)
├── docker-compose.yml             # 单副本本地部署
├── requirements-server.txt        # GPU 服务依赖
├── requirements-client.txt        # CPU 客户端依赖 (无 torch)
├── LICENSE                        # MIT + 论文/原作者署名
└── test/                          # 客户端逻辑 + 端到端(桩服务) 单测
```

## 部署方案（调研结论）

针对「游戏工作室素材管线：PNG 序列帧 → 法线 PNG 序列」，**推荐架构**：

| 决策点 | 结论 | 理由 |
|--------|------|------|
| 运行时 | **PyTorch eager + diffusers**（不走 ONNX/TensorRT） | 模型是 ~2B 参数的 SVD 时空注意力 UNet，3D attention 在 ONNX/TRT 上难导出且易数值漂移；第一天用 eager 风险最低 |
| 服务形态 | **FastAPI 微服务 + Docker**，单副本 | 预加载模型，进程内常驻；天然可观测/可扩缩容 |
| 并发 | **concurrency=1**（单飞 + 锁） | 2B UNet 跨视频难批处理，单飞最稳 |
| 模型权重 | 打包进镜像或挂载只读卷 | 部署时无需联网、无训练代码 |
| 帧桥接 | **GPU 服务保持无状态**；PNG⇄mp4 全放 CPU 客户端 | 美术机免 GPU，传输用 mp4 + 返回 ZIP PNG（无损） |
| 不用 | vLLM / SGLang / ONNX / TRT-LLM | 前两者是 LLM 引擎不跑扩散；TRT 高成本低收益，日后再优化吞吐时再上 |

**显存参考**：`--max-res 1024` 约 20GB；`512` 约 6GB。12–24GB GPU（RTX 4090 / A10 / L4）单副本舒适。
**CPU-only 不推荐**：2B 时空注意力 UNet 纯 CPU 每窗要数分钟到数十分钟。

## 安装

### GPU 服务（有 NVIDIA 显卡的机器 / K8s GPU 节点）

```bash
# 方案 A：Docker（推荐）
docker compose up --build -d            # 自动拉权重(HF)到 normalcrafter-models 卷

# 方案 B：裸机
pip install -r requirements-server.txt
./bin/start_normal_crafter_server.sh --port 8000    # 或 .bat
```

首次启动自动从 HuggingFace 拉取 `Yanrui95/NormalCrafter` + SVD 基础权重（约 4.5GB）。
离线部署：把权重拷进镜像并设 `NORMAL_CRAFTER_UNET_REPO` / `NORMAL_CRAFTER_BASE_REPO`
指向本地路径（见 Dockerfile 注释）。

### CPU 客户端（美术机/管线，无 GPU、无 torch）

```bash
pip install -r requirements-client.txt   # 建议加装 ffmpeg
```

## 用法

先起服务（见上），再跑客户端：

```bash
# PNG 序列帧目录 → 法线 PNG 序列（最常用）
./bin/run_normal_crafter.sh --server http://gpu-01:8000 \
    --input frames/ --output out/normals/ --max-res 512

# 视频 → 法线 PNG 序列（可选再合成法线视频）
python -m normal_crafter --server http://127.0.0.1:8000 \
    --input shot.mp4 --output out/normals/ --output-video out/normals.mp4
```

### 关键参数

| 参数 | 默认 | 说明 |
|------|------|------|
| `--max-res` | `1024` | 分辨率上限。`1024`≈20GB 显存；`512`≈6GB |
| `--window-size` | `14` | 时序一致性滑窗（越大越连贯，越耗显存） |
| `--time-step-size` | `10` | 窗口步长 |
| `--target-fps` | `0` | 服务端重采样 FPS；`<=0` 保持源帧率 |
| `--seed` | `42` | 复现用种子 |

## HTTP API

- `GET /health` → `{"service":"eve.normal.crafter","status":"ok",...}`
- `POST /v1/normal-crafter`：`multipart`，字段 `file`(视频) + `max_res`/`window_size`/`time_step_size`/`decode_chunk_size`/`target_fps`/`seed`
  返回 `application/zip`（`frame_%06d.png` 法线序列），头部带 `X-Normal-Frames/Width/Height`。

服务端配置走环境变量：`NORMAL_CRAFTER_WINDOW_SIZE` / `TIME_STEP_SIZE` / `DECODE_CHUNK_SIZE` / `MAX_RES` / `CPU_OFFLOAD` / `WEIGHT_DTYPE`。

## 测试

无需 GPU / 模型，测客户端逻辑 + 端到端（桩服务）：

```bash
pip install pytest
cd tools/normal-crafter
python -m pytest test -q     # 9 passed
```

> 真实 GPU 推理（`server/algorithm.py`）需在部署机按上述安装步骤验证；本仓库测试不依赖它。

## 致谢

算法与预训练权重出自 *NormalCrafter: Learning Temporally Consistent Video
Normal from Video Diffusion Priors*（Yanrui Bin, Wenbo Hu, Haoyuan Wang,
Xinya Chen, Bing Wang，ICCV 2025）。本工具为 MIT 协议下的自包含重实现 + 部署封装，
见 `LICENSE`。
