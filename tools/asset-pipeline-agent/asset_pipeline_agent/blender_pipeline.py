"""Headless Blender 处理管线编排器：写 job.json -> 子进程跑 standardize.py -> 读 result.json。

子进程失败 / bpy 不可用时不抛出，返回 BlenderResult(ok=False)，由 ReAct 大脑转下一候选。
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from typing import Dict, Optional

from .cache import AssetCache
from .report import BlenderResult


class BlenderPipeline:
    def __init__(self, cfg, cache: AssetCache, dry_run: bool = False):
        self.cfg = cfg["blender"]
        self.cache = cache
        self.dry_run = dry_run
        self.python = self.cfg.get("python", "py -3.13")
        self.script = self._resolve_script(self.cfg.get("script", "blender_scripts/standardize.py"))
        self.timeout = int(self.cfg.get("timeout_s", 600))

    @staticmethod
    def _resolve_script(script: str) -> str:
        if os.path.isabs(script):
            return script
        here = os.path.dirname(os.path.abspath(__file__))
        # 相对包路径：blender_scripts 在 tools/asset-pipeline-agent/ 下。
        cand = os.path.normpath(os.path.join(here, "..", "..", script))
        if os.path.exists(cand):
            return cand
        return os.path.normpath(os.path.join(here, "..", script))

    @staticmethod
    def _python_split(python: str) -> list:
        # 支持 "py -3.13" / "python" / "C:/.../python.exe" 等。
        if os.sep in python or python.lower().endswith(".exe"):
            return [python]
        parts = python.split()
        return parts

    def process(self, archive_path: str, work_dir: str, output_path: str,
                candidate, params: Dict) -> BlenderResult:
        """对单个候选执行完整处理；返回 BlenderResult。"""
        t0 = time.time()
        job = {
            "archive": archive_path,
            "work_dir": work_dir,
            "output": output_path,
            "result_path": os.path.join(work_dir, "result.json"),
            "license": candidate.license,
            "attribution": candidate.attribution,
            "author_url": candidate.author_url,
            "params": {**self.cfg.get("defaults", {}), **params},
        }
        job_path = os.path.join(work_dir, "job.json")
        with open(job_path, "w", encoding="utf-8") as fh:
            json.dump(job, fh, ensure_ascii=False, indent=2)

        if self.dry_run:
            # 仿真：直接生成占位输出与结果，闭合流程便于无 bpy 环境演示。
            return self._dry(job, work_dir, output_path, t0)

        try:
            cmd = self._python_split(self.python) + [self.script, "--job", job_path]
            proc = subprocess.run(
                cmd, capture_output=True, text=True, timeout=self.timeout,
                cwd=os.path.dirname(self.script),
            )
        except subprocess.TimeoutExpired:
            return BlenderResult(ok=False, error="Blender 处理超时", duration_s=round(time.time() - t0, 2))
        except FileNotFoundError as e:
            return BlenderResult(ok=False, error=f"找不到 Python 解释器：{e}", duration_s=round(time.time() - t0, 2))
        except Exception as e:  # noqa: BLE001
            return BlenderResult(ok=False, error=str(e), duration_s=round(time.time() - t0, 2))

        result_path = os.path.join(work_dir, "result.json")
        if not os.path.isfile(result_path):
            tail = (proc.stderr or "").strip()[-500:]
            return BlenderResult(ok=False, error=f"未生成 result.json；rc={proc.returncode}；stderr={tail}",
                                 duration_s=round(time.time() - t0, 2))
        try:
            with open(result_path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except json.JSONDecodeError as e:
            return BlenderResult(ok=False, error=f"result.json 解析失败：{e}", duration_s=round(time.time() - t0, 2))

        return BlenderResult(
            ok=bool(data.get("ok")),
            output_path=data.get("output", output_path),
            error=data.get("error", ""),
            vertices=int(data.get("vertices", 0)),
            triangles=int(data.get("triangles", 0)),
            materials=int(data.get("materials", 0)),
            textures=int(data.get("textures", 0)),
            duration_s=float(data.get("duration_s", round(time.time() - t0, 2))),
            warnings=list(data.get("warnings", [])),
        )

    def _dry(self, job: Dict, work_dir: str, output_path: str, t0: float) -> BlenderResult:
        os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
        with open(output_path, "wb") as fh:
            fh.write(b"dry-run placeholder asset")
        os.makedirs(work_dir, exist_ok=True)
        meta = os.path.splitext(output_path)[0] + ".attribution.json"
        with open(meta, "w", encoding="utf-8") as fh:
            json.dump({"license": job.get("license"), "attribution": job.get("attribution")}, fh)
        return BlenderResult(
            ok=True, output_path=output_path, vertices=256, triangles=512,
            materials=1, textures=1, duration_s=round(time.time() - t0, 2),
            warnings=["dry-run: 未调用真实 Blender"],
        )
