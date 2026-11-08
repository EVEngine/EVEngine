#!/usr/bin/env python3
"""Reproduce every authored upstream pose-track comparison with the native production decoder."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

import compare_pixelfx_rig
import utf8_stdio


def sample_times(keys: list) -> list[float]:
    """Retain exact, adjacent, held-step and interval probes, including duplicate key times."""
    times = {keys[0][0] - 0.1, keys[-1][0] + 0.1}
    for row in keys:
        time = row[0]
        times.update((time, time - 1 / 240, time + 1 / 240, time + 0.05))
    for a, b in zip(keys, keys[1:]):
        if a[0] != b[0]:
            times.update(a[0] + (b[0] - a[0]) * f for f in (0.25, 0.5, 0.75))
    return sorted(times)


def run(command: list[str]) -> str:
    """Execute argument vectors without a shell; retain exact UTF-8 failure output."""
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"{command[0]} exited {result.returncode}: {result.stdout}{result.stderr}")
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run([args.node, str(root / "pixelfx/reference.mjs"), "--upstream", str(args.upstream.resolve()), "--out", str(output)])
    catalog = json.loads((output / "catalog.json").read_text(encoding="utf-8"))
    reports = []
    for effect in catalog["effects"]:
        keys = (effect.get("act") or {}).get("poses")
        if not keys:
            continue
        folder = output / effect["id"].replace("/", "_")
        folder.mkdir(parents=True, exist_ok=True)
        track = {"schema": "eve.pixelfx.pose-track", "version": 1, "keys": keys}
        (folder / "track.json").write_text(json.dumps(track) + "\n", encoding="utf-8")
        (folder / "times.json").write_text(json.dumps(sample_times(keys)) + "\n", encoding="utf-8")
        try:
            run([args.node, str(root / "pixelfx/track_reference.mjs"), "--upstream", str(args.upstream.resolve()),
                 "--effect", effect["id"], "--times", str(folder / "times.json"), "--out", str(folder / "reference.json")])
            run([str(args.probe.resolve()), "--track", str(folder / "track.json"), "--times", str(folder / "times.json"),
                 "--out", str(folder / "native.json")])
            reference = json.loads((folder / "reference.json").read_text(encoding="utf-8"))
            native = json.loads((folder / "native.json").read_text(encoding="utf-8"))
            report = compare_pixelfx_rig.compare(reference, native)
            presence_errors = [
                {"time": source["time"], "reference": sorted(source["presence"]),
                 "native": sorted(actual["presence"])}
                for source, actual in zip(reference["frames"], native["frames"], strict=True)
                if set(source["presence"]) != set(actual["presence"])
            ]
            report["presenceErrors"] = presence_errors
            report["passed"] = report["passed"] and not presence_errors
        except (RuntimeError, ValueError) as error:
            report = {"passed": False, "error": str(error)}
        (folder / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        reports.append({"id": effect["id"], **report})
        print(f"{effect['id']}: passed={report['passed']}")
    summary = {"schema": "eve.pixelfx-track-suite/v1", "revision": catalog["revision"],
               "nodeVersion": run([args.node, "--version"]).strip(), "expectedTracks": 73, "tracks": len(reports),
               "samples": sum(r.get("samples", 0) for r in reports), "passed": len(reports) == 73 and all(r["passed"] for r in reports),
               "reports": reports}
    (output / "comparison.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"Pose tracks: {summary['tracks']}; samples={summary['samples']}; passed={summary['passed']}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
