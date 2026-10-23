"""Run isolated high-poly cases and retain raw logs plus machine-readable results."""

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=Path("artifacts/highpoly"))
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--model", type=Path)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument(
        "--profile", action="store_true", help="Capture CPU zones and aggregation overhead"
    )
    parser.add_argument("--recording", choices=("auto", "parallel", "serial"), default="auto")
    parser.add_argument(
        "--show-window", action="store_true", help="Show the otherwise hidden rendering window"
    )
    parser.add_argument("--gpu-timing", choices=("on", "off"), default="on")
    parser.add_argument("--runtime-dir", type=Path, help="Additional engine DLL directory")
    args = parser.parse_args()
    if args.frames < 9 or args.timeout < 1:
        parser.error("frames must be >= 9 and timeout must be positive")
    if not (1 <= args.width <= 8192 and 1 <= args.height <= 8192):
        parser.error("width and height must be in 1..8192")
    binary = args.binary.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    build_config = {}
    build_root = None
    allowed = {
        "CMAKE_BUILD_TYPE",
        "CMAKE_HOME_DIRECTORY",
        "EVENGINE_PROFILE",
        "EVENGINE_MODULE_LINKAGE",
        "EVENGINE_THIRD_PARTY_PIN",
        "EVENGINE_MODULE_devtools",
        "EVENGINE_BUILD_DEMO",
    }
    for parent in list(binary.parents)[:4]:
        cache = parent / "CMakeCache.txt"
        if cache.is_file():
            build_root = parent
            for line in cache.read_text(encoding="utf-8").splitlines():
                key = line.split(":", 1)[0]
                if (key in allowed or key.startswith("EVENGINE_MODULE_")) and "=" in line:
                    build_config[key] = line.split("=", 1)[1]
            break

    gpu = "unavailable"
    if platform.system() == "Windows":
        probe = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | ConvertTo-Json -Compress",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
            check=False,
        )
        gpu = probe.stdout.strip()
    report = {
        "schema": "evengine.highpoly-benchmark",
        "version": 1,
        "utc": datetime.now(timezone.utc).isoformat(),
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "gpu_selection_request": os.environ.get(
            "EVENGINE_GPU_DEVICE", "default (active device not verified)"
        ),
        "build_config": build_config,
        "engine_libraries": [
            {"file": dll.name, "sha256": hashlib.sha256(dll.read_bytes()).hexdigest()}
            for dll in sorted(build_root.glob("EV*.dll"))
        ]
        if build_root
        else [],
        "platform": platform.platform(),
        "cpu": platform.processor(),
        "gpu_inventory": gpu,
        "binary": str(binary),
        "model": str(args.model.resolve()) if args.model else None,
        "profiling_enabled": args.profile,
        "recording": args.recording,
        "gpu_timing": args.gpu_timing,
        "hidden_window": not args.show_window,
        "notes": "Static shared mesh, one material, no imported textures/skinning/LOD. Wall frame time includes present/waits; GPU timing is asynchronous.",
        "cases": [],
    }
    cases = [
        (32, 1, False),
        (128, 1, False),
        (256, 1, False),
        (512, 1, False),
        (128, 4, False),
        (128, 16, False),
        (128, 64, False),
        (128, 16, True),
    ]
    if args.quick:
        cases = cases[:3]
    if args.model:
        cases = [
            (128, n, shadows) for n, shadows in [(1, False), (4, False), (16, False), (16, True)]
        ]
    failed = False
    for side, count, shadows in cases:
        name = f"subdiv-{side}-count-{count}-shadows-{int(shadows)}"
        env = os.environ.copy()
        env["EVENGINE_BENCH_HIDDEN"] = "0" if args.show_window else "1"
        env["EVENGINE_GPU_TIMING"] = "1" if args.gpu_timing == "on" else "0"
        env["EVENGINE_FRAMEGRAPH_RECORDING"] = args.recording
        env.pop("EVENGINE_BENCH_PROFILE", None)
        if args.profile:
            env["EVENGINE_BENCH_PROFILE"] = "1"
        runtime_dirs = [str(p.resolve()) for p in (args.runtime_dir, build_root) if p]
        if runtime_dirs:
            env["PATH"] = os.pathsep.join(runtime_dirs + [env.get("PATH", "")])
        env.pop("EVENGINE_PERF_FRAMES", None)
        env.pop("EVENGINE_BENCH_MODEL", None)
        env["EVENGINE_BENCH_SUBDIV"] = str(side)
        if args.model:
            env["EVENGINE_BENCH_MODEL"] = str(args.model.resolve(strict=True))
        command = [
            str(binary),
            "3d-highpoly-shadows" if shadows else "3d-highpoly",
            str(args.frames),
            str(count),
            str(args.width),
            str(args.height),
        ]
        entry = {"name": name, "command": command}
        try:
            run = subprocess.run(
                command,
                cwd=binary.parent,
                env=env,
                capture_output=True,
                timeout=args.timeout,
                check=False,
            )
            output = (run.stdout + run.stderr).decode("utf-8", errors="replace")
            (args.output / f"{name}.log").write_text(output, encoding="utf-8")
            visibility = [
                line.split("=", 1)[1]
                for line in output.splitlines()
                if line.startswith("BENCHMARK_WINDOW_VISIBLE=")
            ]
            if visibility != ["1" if args.show_window else "0"]:
                raise RuntimeError("window visibility was not confirmed; inspect raw log")
            entry["window_visible"] = args.show_window
            rows = [
                json.loads(line.split("=", 1)[1])
                for line in output.splitlines()
                if line.startswith("RENDER_BENCHMARK_JSON=")
            ]
            if (
                run.returncode
                or len(rows) != 1
                or rows[0]["frames"] != args.frames
                or rows[0]["meshTriangles"] <= 0
            ):
                raise RuntimeError(
                    f"exit={run.returncode}; expected one valid measurement; inspect raw log"
                )
            entry["metrics"] = rows[0]
            entry["status"] = "ok"
            entry["p95_within_60fps_budget"] = rows[0]["p95Ms"] <= 1000 / 60
            print(f"{name}: {rows[0]['avgFps']:.1f} FPS, p95 {rows[0]['p95Ms']:.2f} ms", flush=True)
        except (subprocess.TimeoutExpired, RuntimeError, ValueError, KeyError) as exc:
            if isinstance(exc, subprocess.TimeoutExpired):
                partial = (exc.stdout or b"") + (exc.stderr or b"")
                (args.output / f"{name}.log").write_text(
                    partial.decode("utf-8", errors="replace"), encoding="utf-8"
                )
            entry.update(status="failed", error=str(exc))
            failed = True
            print(f"{name}: FAILED {exc}", flush=True)
        report["cases"].append(entry)
        (args.output / "results.json").write_text(
            json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8"
        )
    return int(failed)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
