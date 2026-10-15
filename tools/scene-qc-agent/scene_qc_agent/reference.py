"""Measured reference-image feedback using native scene commands and frame capture."""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path
from typing import Any

from PIL import Image

from .mcp_client import EvMcpClient


def checked(raw: str | dict) -> dict:
    """Reject tool/Result failures instead of treating them as observations."""
    value = json.loads(raw) if isinstance(raw, str) else raw
    if not isinstance(value, dict) or value.get("ok") is False or value.get("error"):
        raise RuntimeError(f"Engine request failed: {value}")
    return value


def image_metrics(reference: Path, rendered: Path, mask: dict) -> dict[str, float]:
    """Compare explicit foreground color masks at identical resolution.

    Reference and rendered images must share a calibrated camera and resolution.
    RGB/tolerance selects the subject supplied by the author; no guessed semantics.
    Empty masks are errors, including empty/empty, rather than perfect matches.
    """
    with Image.open(reference) as source, Image.open(rendered) as output:
        if source.size != output.size:
            raise ValueError("Reference and rendered resolution must match")
        width, height = source.size
        def pixels(image):
            raw = image.convert("RGB").tobytes()
            return list(zip(raw[0::3], raw[1::3], raw[2::3]))
        reference_pixels = pixels(source)
        rendered_pixels = pixels(output)
    color = mask["rgb"]
    tolerance = float(mask["tolerance"])
    if len(color) != 3 or any(not 0 <= v <= 255 for v in color) or not 0 <= tolerance <= 255:
        raise ValueError("Mask requires RGB [0,255] and tolerance [0,255]")
    def select(pixel):
        return max(abs(pixel[i] - color[i]) for i in range(3)) <= tolerance
    a = {i for i, pixel in enumerate(reference_pixels) if select(pixel)}
    b = {i for i, pixel in enumerate(rendered_pixels) if select(pixel)}
    if not a or not b:
        raise ValueError("Foreground mask is empty; fix subject selection before optimizing")
    def centroid(points):
        return (sum(i % width for i in points) / len(points) / width,
                sum(i // width for i in points) / len(points) / height)
    ca, cb = centroid(a), centroid(b)
    iou = len(a & b) / len(a | b)
    return {"silhouetteIou": iou, "centroidError": math.dist(ca, cb),
            "areaError": abs(len(a) - len(b)) / len(a), "score": iou}


class ReferenceRefiner:
    """Apply reviewable candidate batches; retain only measured improvements.

    The host must call refresh before render and mark_rendered after render3D.
    Single writer is required during refinement. Revision changes abort the run.
    """

    def __init__(self, client: Any, reference: Path, mask: dict, output: Path):
        self.client, self.reference, self.mask, self.output = client, reference, mask, output
        self.output.mkdir(parents=True, exist_ok=True)

    def modify(self, action: str, target: str = "", params: dict | None = None) -> dict:
        return checked(self.client.call_tool("eve_scene_modify", {
            "action": action, "target": target, "params": params or {}}))

    def info(self) -> dict:
        return checked(self.client.call_tool("eve_scene_info", {}))

    def capture(self, label: str) -> tuple[Path, dict]:
        initial = self.info()
        revision, frame = initial["revision"], initial["frame"]
        deadline = time.monotonic() + 15
        while True:
            info = self.info()
            if info["revision"] != revision:
                raise RuntimeError("Concurrent scene edit while waiting for a rendered revision")
            if info["renderedRevision"] == revision and info["frame"] > frame:
                break
            if time.monotonic() >= deadline:
                raise TimeoutError("Host did not render the requested scene revision")
            time.sleep(0.02)
        path = (self.output / f"{label}.png").resolve()
        while True:
            raw = self.client.call_tool("eve_screenshot", {"path": str(path)})
            shot = json.loads(raw) if isinstance(raw, str) else raw
            if not isinstance(shot, dict) or not shot.get("retryable"):
                shot = checked(shot)
                break
            if time.monotonic() >= deadline:
                raise TimeoutError("Native capture did not receive a presented frame")
            info = self.info()
            if info["revision"] != revision:
                raise RuntimeError("Concurrent scene edit while enabling native capture")
            time.sleep(0.02)
        after = self.info()
        if after["revision"] != revision or after["renderedRevision"] != revision:
            raise RuntimeError("Screenshot revision changed during capture")
        if not path.is_file():
            raise RuntimeError(f"Engine capture did not create {path}")
        evidence = {"revision": revision, "frameBefore": info["frame"], "frameAfter": after["frame"],
                    "capture": shot, "path": str(path)}
        return path, evidence

    def run(self, candidates: list[list[dict]]) -> dict:
        baseline, evidence = self.capture("baseline")
        best = image_metrics(self.reference, baseline, self.mask)
        report = {"schemaId": "eve.scene.reference-report", "schemaVersion": 1,
                  "reference": str(self.reference.resolve()), "mask": self.mask,
                  "baseline": {"metrics": best, "evidence": evidence}, "candidates": []}
        for index, batch in enumerate(candidates):
            saved = self.modify("save")
            expected = saved["revision"]
            entry: dict = {"commands": batch, "accepted": False}
            try:
                for command in batch:
                    params = dict(command.get("params", {}))
                    params["expectedRevision"] = expected
                    self.modify(command["action"], command.get("target", ""), params)
                    expected = self.info()["revision"]
                image, receipt = self.capture(f"candidate-{index}")
                metrics = image_metrics(self.reference, image, self.mask)
                entry.update(metrics=metrics, evidence=receipt)
                if metrics["score"] > best["score"] + 1e-9:
                    best = metrics
                    entry["accepted"] = True
            except Exception as error:
                entry["error"] = str(error)
            if not entry["accepted"]:
                # Do not overwrite another writer's changes during recovery.
                if self.info()["revision"] != expected:
                    raise RuntimeError("Concurrent edit detected; automatic rollback refused")
                self.modify("restore", params={"json": saved["json"], "expectedRevision": expected})
            report["candidates"].append(entry)
        final, final_receipt = self.capture("final")
        report["final"] = {"metrics": image_metrics(self.reference, final, self.mask), "evidence": final_receipt}
        (self.output / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--port", type=int, default=7529)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    document = json.loads(args.manifest.read_text(encoding="utf-8"))
    if document.get("schemaId") != "eve.scene.reference-task" or document.get("schemaVersion") != 1:
        raise ValueError("Unsupported reference task schema")
    if set(document) != {"schemaId", "schemaVersion", "reference", "mask", "candidates"}:
        raise ValueError("Unknown or missing reference task fields")
    reference = (args.manifest.parent / document["reference"]).resolve()
    client = EvMcpClient({"engine": {"transport": "tcp", "host": "127.0.0.1", "port": args.port,
                                  "connect_timeout_ms": 15000}})
    try:
        client.initialize()
        report = ReferenceRefiner(client, reference, document["mask"], args.output.resolve()).run(document["candidates"])
        print(json.dumps(report["final"]["metrics"]))
    finally:
        client.close()


if __name__ == "__main__":
    main()
