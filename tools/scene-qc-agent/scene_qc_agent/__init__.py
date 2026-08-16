"""Scene QC Agent — ReAct 范式场景质检优化大脑.

多视角自检 -> 大小模型分级调度 -> 融合 3D 几何真值/本地模型/云端 VLM 综合判定
-> 自动生成修改方案并应用 -> 迭代优化（硬上限 max_rounds）。
"""

from .react import SceneQCBrain
from .config import load_config

__all__ = ["SceneQCBrain", "load_config"]
__version__ = "0.1.0"
