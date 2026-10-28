"""Plain-text job protocol between the C++ plugin and this Python runtime.

To keep the C++ side dependency-free we use `key=value` lines for both the job
file and the result file; params travel as an embedded JSON string which Python
parses with the standard library.
"""

from __future__ import annotations

import json
from typing import Dict, List, Optional


class Job:
    def __init__(self) -> None:
        self.converter: str = ""
        self.input_model: str = ""
        self.output_model: str = ""
        self.format: str = "glb"
        self.converter_dir: str = ""
        self.temp_dir: str = ""
        self.params: Dict[str, object] = {}

    @staticmethod
    def _parse_line(line: str) -> Optional[str]:
        line = line.strip()
        if not line or line.startswith("#"):
            return None
        if "=" not in line:
            return None
        return line

    @classmethod
    def from_file(cls, path: str) -> "Job":
        job = cls()
        with open(path, "r", encoding="utf-8") as fh:
            for raw in fh:
                line = cls._parse_line(raw)
                if line is None:
                    continue
                key, _, value = line.partition("=")
                key = key.strip()
                value = value.strip()
                if key == "converter":
                    job.converter = value
                elif key == "input":
                    job.input_model = value
                elif key == "output":
                    job.output_model = value
                elif key == "format":
                    job.format = value
                elif key == "converter_dir":
                    job.converter_dir = value
                elif key == "temp_dir":
                    job.temp_dir = value
                elif key == "params":
                    if value:
                        try:
                            job.params = json.loads(value)
                        except json.JSONDecodeError:
                            job.params = {}
        return job


class Result:
    def __init__(self) -> None:
        self.ok = False
        self.output_model = ""
        self.error = ""
        self.vertices = 0
        self.triangles = 0

    def write(self, path: str) -> None:
        lines = [
            f"ok={1 if self.ok else 0}",
            f"output={self.output_model}",
            f"error={self.error}",
            f"vertices={self.vertices}",
            f"triangles={self.triangles}",
        ]
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")

        detail = {
            "ok": self.ok,
            "output": self.output_model,
            "error": self.error,
            "stats": {"vertices": self.vertices, "triangles": self.triangles},
        }
        try:
            with open(path + ".json", "w", encoding="utf-8") as fh:
                json.dump(detail, fh, indent=2, ensure_ascii=False)
        except OSError:
            pass


def params_to_json(params: Dict[str, object]) -> str:
    return json.dumps(params, ensure_ascii=False)


def list_matches(converter_dir: str) -> List[str]:
    """Return converter ids by scanning the converter directory."""
    import os

    if not converter_dir or not os.path.isdir(converter_dir):
        return []
    out: List[str] = []
    for name in sorted(os.listdir(converter_dir)):
        entry = os.path.join(converter_dir, name)
        manifest = os.path.join(entry, "manifest.json")
        if os.path.isdir(entry) and os.path.isfile(manifest):
            out.append(name)
    return out
