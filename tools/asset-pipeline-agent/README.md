# asset-pipeline-agent · 外部素材管线

资源素材获取 & 自动化标准化管线 Agent（**外部资产供给链路**）。接收场景 Agent 的素材
需求，检索合规 3D 素材、自动下载、批处理修复、转为引擎标准格式并入库，随后异步回调
场景 Agent 告知就绪 / 失败。

> ⚠️ **强制前置约束**：仅使用 CC0 / CC-BY 等**商用免费合规**素材，**永久留存版权信息**；
> 禁止付费资源、禁止违规爬取。合规白名单与致谢文本在整条链路硬性执行。

## 定位

外部资产供给链路：`需求解析 -> 检索合规候选 -> 缓存下载 + MD5 校验 -> Headless Blender
标准化 -> 引擎格式导出 -> MCP 入库 -> 异步回调`。模型无法自动修复时自动回退下一候选。

## 能力

1. **需求解析**：LLM 解析资产用途、风格、面数区间、授权约束、坐标系规范（`parser.py`）。
2. **公开素材 API 对接**：Sketchfab（API v3，需 token）+ Poly Haven（全 CC0），按授权 /
   面数 / 风格筛选（`sources.py`）。
3. **缓存管理 + MD5 完整性校验**：复用下载、校验损坏、保留原始压缩包含版权信息（`cache.py`）。
4. **Headless Blender 自动处理管线**：解压 -> 导入 -> 清理网格 -> 轴心 / 坐标系校正 ->
   法线修复 -> 材质标准化 -> 纹理压缩 -> 导出引擎格式（`blender_pipeline.py` +
   `blender_scripts/standardize.py`）。
5. **MCP 入库 + 自动标签元数据**：处理成功调用 MCP 入库，生成资产标签与元数据；失败
   自动尝试下一候选（`registry.py` + `react.py`）。
6. **异步回调**：素材就绪 / 获取失败通知场景生成 Agent。

## 架构

```
                    +------------------------ AssetPipelineBrain (ReAct) ------------------------+
                    |  需求解析 -> 检索候选 -> LLM 精排 -> 逐候选下载/处理/入库，失败回退下一候选  |
                    +-----+----------------+---------------+----------------+-----------------+
                         | search          | download       | process        | ingest / notify
                         v                 v               v                v
                  +-------------+     +----------+    +---------------+   +------------------+
                  | sources.py  |     | cache.py |    | blender_pipeline.py (bpy 子进程)  |
                  | Skfb/Poly   |     | MD5 校验  |    | standardize.py 标准化脚本         |
                  +-------------+     +----------+    +---------------+   +------------------+
                    合规授权硬过滤                         引擎格式导出          MCP tools/call
```

## 目录

```
tools/asset-pipeline-agent/
├── config.example.json            # 模型端点 / 合规白名单 / 检索源 / Blender / MCP
├── asset_pipeline_agent/
│   ├── cli.py                     # 命令行入口
│   ├── config.py                  # 配置加载（深合并 + 默认值）
│   ├── models.py                  # OpenAI 兼容 / opencode-go 客户端（零依赖）
│   ├── report.py                  # 数据模型（需求/候选/结果/报告）
│   ├── parser.py                  # LLM 需求解析 + 启发式兜底
│   ├── sources.py                 # 素材检索封装（Poly Haven / Sketchfab / 干跑源）
│   ├── cache.py                   # 缓存管理 + MD5 完整性
│   ├── blender_pipeline.py        # Headless Blender 编排（子进程调用）
│   ├── registry.py                # MCP 入库 + 异步状态回调
│   └── react.py                   # ReAct 调度大脑（候选回退 + 硬上限）
├── blender_scripts/standardize.py # 独立 bpy 处理脚本（解压->导入->清洗->导出）
└── tests/                         # 单元测试
```

## 用法

### 干跑（无需引擎 / API / Blender，验证闭环）

```bash
python -m asset_pipeline_agent --dry-run --trace \
    --request "需要一块写实的岩石道具，CC0，低模，2000 面，导出 glb"
```

输出：`accepted:1`、`asset_uri: engine://asset/dry_prop_0`、`tri=512`、并打印 `notify::READY`。

### 连接真实检索源 + Blender + 引擎 MCP

1. 安装 bpy（Blender 官方 Python 包，需 CPython 3.13）：
   ```bash
   py -3.13 -m pip install bpy
   ```
2. 配置 `config.example.json`：模型端点（`models.parser/selector.base_url`）、
   Sketchfab `SKETCHFAB_TOKEN`（可选，Poly Haven 无需 key）、`blender.python`。
3. 起引擎 MCP：`eve run --debug --mcp-port=7529 <scene>`
4. 运行：
   ```bash
   python -m asset_pipeline_agent --config config.example.json \
       --request "石头道具 CC0 低模 5000 面 glb" --up-axis Z --callback <scene-task-id>
   ```

### 参数

| 参数 | 说明 |
|------|------|
| `--config` | 配置文件（默认 `config.example.json`） |
| `--request` | 场景 Agent 的素材需求文本（必填） |
| `--dry-run` | 不连引擎/API/Blender，使用仿真源闭合闭环 |
| `--trace` | 打印调度轨迹 |
| `--report` | 报告输出 JSON 路径 |
| `--up-axis` | 坐标系上轴 `Y` / `Z` |
| `--max-triangles` | 面数上限 |
| `--callback` | 就绪 / 失败异步回调标识 |

## 测试

```bash
python -m unittest discover -s tests -p "test_*.py"
```

## 合规执行点

- `report.ALLOWED_LICENSES = ("cc0", "cc-by", "cc-by-sa")`，检索与解析都硬校验。
- `sources` 中 Sketchfab 按授权白名单过滤，未匹配直接丢弃。
- 每个候选携带 `author` / `author_url` / `attribution`，处理脚本在导出时把版权信息
  写入 `<asset>.attribution.json`，入库元数据同步带出——**版权信息永久留存**。
- Poly Haven 全部资产为 CC0，天然合规；付费资源与违规爬取在检索层即被拒绝。

## openCode 集成

仓库提供 `.opencode/agent/asset-pipeline-brain.md`，把该工具封装为 openCode 子代理；
也可直接命令行调用。修改 openCode 配置后需**退出并重启 openCode** 生效。
