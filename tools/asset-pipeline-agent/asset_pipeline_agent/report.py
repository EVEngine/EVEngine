"""外部素材管线数据模型。

覆盖需求 -> 候选 -> 处理 -> 入库 全链路的可序列化数据结构。
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field, asdict
from typing import List, Optional

# 商用免费合规授权白名单（强制前置约束：仅允许这些授权）。
# 永久留存版权信息，禁止付费资源 / 违规爬取。
ALLOWED_LICENSES = ("cc0", "cc-by", "cc-by-sa")

LICENSE_LABELS = {
    "cc0": "CC0 1.0 (Public Domain)",
    "cc-by": "CC BY 4.0 (Attribution required)",
    "cc-by-sa": "CC BY-SA 4.0 (Attribution + ShareAlike)",
}

UP_AXES = ("Y", "Z")  # 引擎坐标系统上轴规范（如 glTF 默认 +Y）
ENGINE_FORMATS = ("glb", "gltf", "fbx", "obj")

VERDICTS = ("ACCEPTED", "FAILED", "SKIPPED")


@dataclass
class AssetRequest:
    """场景 Agent 的素材需求（需求解析的输出）。"""

    purpose: str = ""              # 资产用途（如 props / environment / character）
    description: str = ""          # 自然语言语义描述
    style: str = ""                # 风格（realistic / stylized / low_poly ...）
    max_triangles: int = 200000    # 面数上限
    min_triangles: int = 0
    license: str = "cc0"           # 授权约束（必须属于 ALLOWED_LICENSES）
    up_axis: str = "Y"             # 坐标系上轴
    output_format: str = "glb"
    scale: float = 1.0             # 目标缩放 / 单位
    callback: str = ""             # 就绪 / 失败异步回调标识（场景 Agent 会话 / 任务 id）
    tags: List[str] = field(default_factory=list)

    def validate(self) -> List[str]:
        errs: List[str] = []
        if self.license not in ALLOWED_LICENSES:
            errs.append(f"license '{self.license}' 不在合规白名单 {list(ALLOWED_LICENSES)} 内")
        if self.output_format not in ENGINE_FORMATS:
            errs.append(f"output_format '{self.output_format}' 不在 {ENGINE_FORMATS} 内")
        if self.up_axis not in UP_AXES:
            errs.append(f"up_axis '{self.up_axis}' 不在 {UP_AXES} 内")
        if self.max_triangles < self.min_triangles:
            errs.append("max_triangles 不能小于 min_triangles")
        if self.max_triangles <= 0:
            errs.append("max_triangles 必须为正")
        return errs

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class Candidate:
    """检索到的合规候选模型（来源 + 授权元数据，版权信息永久留存）。"""

    source: str = ""               # poly_haven | sketchfab | dry_run
    asset_id: str = ""
    name: str = ""
    download_url: str = ""
    license: str = "cc0"
    author: str = ""               # 作者 / 版权归属
    author_url: str = ""           # 版权信息源链接
    attribution: str = ""          # 永久留存的致谢文本
    triangles: int = 0             # 预估面数
    style: str = ""
    thumb: str = ""
    score: float = 0.0             # 相关性 / 排序分
    reason: str = ""               # LLM 选择该候选的理由

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class BlenderResult:
    """Headless Blender 处理结果统计。"""

    ok: bool = False
    output_path: str = ""
    error: str = ""
    vertices: int = 0
    triangles: int = 0
    materials: int = 0
    textures: int = 0
    duration_s: float = 0.0
    warnings: List[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class AssetOutcome:
    """单个候选的最终处理结果（入库前状态机）。"""

    candidate: Candidate = field(default_factory=Candidate)
    phase: str = "new"             # new -> downloading -> processing -> ingesting -> done | failed
    status: str = "PENDING"        # PENDING | ACCEPTED | FAILED | SKIPPED
    download_md5: str = ""
    blender: Optional[BlenderResult] = None
    ingested: bool = False
    asset_uri: str = ""            # 入库后返回的引擎资产引用
    tags: List[str] = field(default_factory=list)
    error: str = ""
    reason: str = ""

    def to_dict(self) -> dict:
        d = asdict(self)
        d["candidate"] = self.candidate.to_dict()
        if self.blender:
            d["blender"] = self.blender.to_dict()
        return d


@dataclass
class PipelineReport:
    """整条素材管线的最终报告。"""

    request: AssetRequest = field(default_factory=AssetRequest)
    outcomes: List[AssetOutcome] = field(default_factory=list)
    accepted: int = 0
    total: int = 0
    summary: str = ""
    meta: dict = field(default_factory=dict)

    @property
    def succeeded(self) -> bool:
        return self.accepted > 0

    def to_dict(self) -> dict:
        return {
            "request": self.request.to_dict(),
            "outcomes": [o.to_dict() for o in self.outcomes],
            "accepted": self.accepted,
            "total": self.total,
            "summary": self.summary,
            "meta": self.meta,
        }

    def to_json(self) -> str:
        return json.dumps(self.to_dict(), ensure_ascii=False, indent=2)


def license_label(code: str) -> str:
    return LICENSE_LABELS.get(code, code)
