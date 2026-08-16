"""外部素材管线 Agent：资源素材获取 & 自动化标准化管线（合规 CC0/CC-BY）。

需求解析 -> 检索合规候选（Sketchfab / Poly Haven）-> 缓存 + MD5 校验 ->
Headless Blender 标准化 -> 引擎格式导出 -> MCP 入库 -> 异步回调场景 Agent。
"""

from .config import load_config
from .parser import RequirementParser
from .react import AssetPipelineBrain

__all__ = ["load_config", "RequirementParser", "AssetPipelineBrain"]
__version__ = "0.1.0"
