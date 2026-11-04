#!/usr/bin/env python3
"""Run a fresh process; record observed runtime evidence, never replay old logs."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=120)
    parser.add_argument("--engine", type=Path, default=Path("../../build/cloud-minimal/src/engine/eve"))
    args = parser.parse_args()
    if args.seconds < 5:
        parser.error("run for at least five seconds")
    root = Path(__file__).resolve().parent
    artifacts = root / "artifacts"
    artifacts.mkdir(exist_ok=True)
    log = artifacts / "runtime.txt"
    samples = []
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(["xvfb-run", "-a", str((root / args.engine).resolve()), "run"],
                                   cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        start = time.monotonic()
        try:
            while process.poll() is None and time.monotonic() - start < args.seconds:
                rss = 0
                # Process-group RSS: includes engine and display wrapper.
                for status in Path("/proc").glob("[0-9]*/stat"):
                    try:
                        fields = status.read_text(encoding="utf-8").split()
                        if int(fields[4]) == process.pid:
                            rss += int(fields[23]) * os.sysconf("SC_PAGE_SIZE") // 1024
                    except (OSError, ValueError, IndexError):
                        continue
                samples.append({"seconds": round(time.monotonic()-start, 2), "rssKiB": rss})
                time.sleep(0.5)
            premature = process.poll() is not None
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    text = log.read_text(encoding="utf-8", errors="replace")
    errors = re.findall(
        r"eve_init failed|frame error:|Run failed:|AN ERROR HAS OCCURRED|"
        r"\bERROR\b|actor stalled|Segmentation fault|Assertion.*failed", text)
    checks = {"survived": not premature, "ready": "terrain-lab READY" in text,
              "bothArrived": text.count("terrain-lab ARRIVED") >= 2,
              "progress": "terrain-lab PROGRESS" in text, "noDiagnostics": not errors}
    warmed = [s["rssKiB"] for s in samples if s["seconds"] >= 10 and s["rssKiB"] > 0]
    report = {"durationSeconds": round(time.monotonic()-start, 2), "checks": checks,
              "passed": all(checks.values()), "errors": errors,
              "rssGrowthKiB": warmed[-1]-warmed[0] if warmed else None, "samples": samples}
    (artifacts / "runtime-report.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k != "samples"}, indent=2))
    return 0 if report["passed"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
