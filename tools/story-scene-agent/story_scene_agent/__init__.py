"""story-scene-agent — EVEngine 剧情场景编排大脑。

把「玩家一句话剧情」变成「可交付的合格舞台」：
story → stage brief → creative-brain 生成计划 → scene_director 搭台 →
scene-qc 多视角质检 + 自动修复 → 输出报告与渲染截图。

复用仓库内既有 AI 工具：
  - tools/creative-brain/brain     意图解析 / 资产匹配 / 布局 / 计划
  - tools/scene-qc-agent           巡检 / 融合 / 修复 / ReAct 迭代
  - src/scripts/scene_director.nut 引擎脚本侧搭台 kit
  - 引擎 MCP（eve_scene_modify / eve_scene_info / eve_camera_generate / eve_screenshot）
"""

from .config import DefaultConfig, load_config
from .story import StageBrief, parse_stage
from .orchestrator import StorySceneBrain

__all__ = [
    "DefaultConfig",
    "load_config",
    "StageBrief",
    "parse_stage",
    "StorySceneBrain",
]