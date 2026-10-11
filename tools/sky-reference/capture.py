#!/usr/bin/env python3
"""Capture a repeatability-checked UDS calibration set with a local Unreal editor."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
import uuid
from pathlib import Path

from capture_spec import validate, validate_camera
from compare import compare_frames


def source_fingerprint(project):
    """Bind a reference set to actual source bytes, independent of local paths."""
    content = project.parent / "Content" / "UltraDynamicSky"
    files = sorted(path for path in content.rglob("*") if path.suffix in (".uasset", ".umap") and path.is_file())
    if not files:
        raise ValueError("project contains no UltraDynamicSky content")
    entries = []
    for path in files:
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        entries.append({"file": path.relative_to(content).as_posix(), "sha256": digest.hexdigest()})
    return {"mount": "/Game/UltraDynamicSky", "files": entries,
            "sha256": hashlib.sha256(json.dumps(entries, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()}


def validate_result(staging, spec):
    result = json.loads((staging / "result.json").read_text(encoding="utf-8"))
    if result.get("schema") != "eve.sky-reference-result/2" or result.get("status") != "captured":
        raise ValueError("capture failed: " + str(result.get("error", result.get("status"))))
    if result.get("request") != spec:
        raise ValueError("capture request mismatch")
    validate_camera(result.get("resolvedCamera"), spec["camera"])
    payload = staging / "payload"
    expected = {scene["name"] + suffix: kind for scene in spec["scenarios"]
                for suffix, kind in ((".png", "reference"), ("-repeat.png", "repeat"), (".actor.t3d", "actor"))}
    artifacts = result.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != len(expected):
        raise ValueError("incomplete capture artifacts")
    seen = set()
    for entry in artifacts:
        name = entry.get("file")
        if name not in expected or name in seen or entry.get("kind") != expected[name]:
            raise ValueError("unexpected capture artifact")
        seen.add(name)
        path = payload / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size != entry.get("byteLength"):
            raise ValueError("missing capture artifact or size mismatch")
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry.get("sha256"):
            raise ValueError("capture artifact hash mismatch")
    from PIL import Image
    result["repeatability"] = {}
    for scene in spec["scenarios"]:
        name = scene["name"]
        for suffix in (".png", "-repeat.png"):
            with Image.open(payload / (name + suffix)) as image:
                if list(image.size) != spec["size"] or image.mode not in ("RGB", "RGBA"):
                    raise ValueError("unexpected screenshot dimensions or format")
        report = compare_frames(payload / (name + ".png"), payload / (name + "-repeat.png"),
                                payload / (name + "-repeatability"), 0, 1.0, .999999, {})
        result["repeatability"][name] = report
        if report["status"] != "pass":
            raise ValueError("reference is not pixel-repeatable: " + name)
    result["status"] = "validated"
    (payload / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def capture(project, editor, output, scenarios, timeout):
    project, editor, output = project.resolve(), editor.resolve(), output.resolve()
    spec = validate(json.loads(scenarios.read_text(encoding="utf-8")))
    if not project.is_file() or project.suffix != ".uproject" or not editor.is_file():
        raise ValueError("project and Unreal editor must exist")
    if output.exists() or timeout <= 0:
        raise ValueError("output must not exist and timeout must be positive")
    provenance = source_fingerprint(project)
    # Verify the host's dependencies before starting an expensive editor session.
    import numpy  # noqa: F401
    from PIL import Image  # noqa: F401
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.parent / ("." + output.name + ".staging-" + uuid.uuid4().hex)
    staging.mkdir()
    (staging / "request.json").write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    command_base = [str(editor), str(project),
               "-ExecutePythonScript=" + str(Path(__file__).with_name("ue_capture.py").resolve()),
               "-unattended", "-nop4", "-nosplash", "-NoSound", "-RenderOffscreen",
               "-shaderworkingdir=" + str(project.parent / "ShaderWorking"),
               "-DDC=(Local(Path=" + str(project.parent / "DerivedDataCache") + "))"]
    # A new process per scenario avoids both cross-scene temporal history and
    # editor map replacement from inside Slate's screenshot-completion callback.
    payload = staging / "payload"
    payload.mkdir()
    aggregate = {"schema": "eve.sky-reference-set/2", "request": spec, "scenarios": [],
                 "sourceContent": provenance,
                 "projectSha256": hashlib.sha256(project.read_bytes()).hexdigest(),
                 "captureWorkerSha256": hashlib.sha256(Path(__file__).with_name("ue_capture.py").read_bytes()).hexdigest()}
    started = time.monotonic()
    for scene in spec["scenarios"]:
        work = staging / scene["name"]
        work.mkdir()
        scene_spec = {**spec, "scenarios": [scene]}
        request = work / "request.json"
        request.write_text(json.dumps(scene_spec, indent=2) + "\n", encoding="utf-8")
        env = os.environ.copy()
        env["EVENGINE_SKY_CAPTURE_REQUEST"] = str(request)
        command = command_base + ["-abslog=" + str(work / "unreal.log")]
        remaining = timeout - (time.monotonic() - started)
        if remaining <= 0:
            raise TimeoutError("capture set exceeded timeout")
        with (work / "process.log").open("w", encoding="utf-8") as log:
            process = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                     timeout=remaining, check=False,
                                     creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        if process.returncode:
            raise RuntimeError(f"Unreal exited {process.returncode}; inspect {work / 'unreal.log'}")
        validate_result(work, scene_spec)
        (work / "payload").rename(payload / scene["name"])
        aggregate["scenarios"].append({"name": scene["name"], "manifest": scene["name"] + "/manifest.json"})
        print("Validated " + scene["name"], flush=True)
    if source_fingerprint(project) != provenance:
        raise ValueError("source assets changed during capture; publication refused")
    aggregate["status"] = "validated"
    (payload / "manifest.json").write_text(json.dumps(aggregate, indent=2) + "\n", encoding="utf-8")
    if output.exists():
        raise ValueError("output appeared during capture; publication refused")
    (staging / "payload").rename(output)
    return output / "manifest.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenarios", type=Path, default=Path(__file__).with_name("scenarios.json"))
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    try:
        print(capture(args.project, args.editor, args.output, args.scenarios, args.timeout))
        return 0
    except (OSError, ValueError, RuntimeError, ImportError, subprocess.TimeoutExpired) as error:
        print(f"sky-reference capture: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    raise SystemExit(main())
