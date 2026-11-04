"""Run deterministic 3D scene/animation/submission workloads in isolated processes."""

import argparse
import hashlib
import json
import math
import os
import platform
import statistics
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=Path("artifacts/gameplay"))
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument(
        "--quick", action="store_true", help="Small smoke matrix, not performance evidence"
    )
    parser.add_argument(
        "--cpu-only",
        action="store_true",
        help="Skip render/present; still initialize real mesh/palette resources",
    )
    parser.add_argument(
        "--profile",
        action="store_true",
        help="Diagnostic run; do not compare with unprofiled timings",
    )
    parser.add_argument("--shadows", action="store_true")
    parser.add_argument("--skin-influences", type=int, choices=(1, 2, 3, 4), default=2)
    parser.add_argument("--lighting", choices=("forwardPlus", "hybrid"), default="forwardPlus")
    parser.add_argument(
        "--no-msaa", action="store_true", help="Disable MSAA for both lighting paths"
    )
    parser.add_argument("--recording", choices=("auto", "serial", "parallel"), default="auto")
    parser.add_argument(
        "--scenes",
        nargs="+",
        choices=("static", "dynamic", "animated", "mixed"),
        default=["static", "dynamic", "animated", "mixed"],
    )
    parser.add_argument("--runtime-dir", type=Path)
    args = parser.parse_args()
    if not 9 <= args.frames <= 36000 or not 1 <= args.repeats <= 20 or args.timeout < 1:
        parser.error("frames must be 9..36000, repeats 1..20 and timeout positive")
    if not 1 <= args.width <= 8192 or not 1 <= args.height <= 8192:
        parser.error("width and height must be 1..8192")
    binary = args.binary.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(
        EVENGINE_BENCH_HIDDEN="1",
        EVENGINE_BENCH_RENDER="0" if args.cpu_only else "1",
        EVENGINE_BENCH_SHADOWS="1" if args.shadows else "0",
        EVENGINE_FRAMEGRAPH_RECORDING=args.recording,
        EVENGINE_GPU_TIMING="1",
        EVENGINE_BENCH_LIGHTING=args.lighting,
        EVENGINE_BENCH_INFLUENCES=str(args.skin_influences),
    )
    env.pop("EVENGINE_BENCH_MSAA", None)
    if args.no_msaa:
        env["EVENGINE_BENCH_MSAA"] = "0"
    # The explicit command line owns frame count; inherited diagnostic knobs must not alter a comparison.
    env.pop("EVENGINE_PERF_FRAMES", None)
    env.pop("EVENGINE_BENCH_PROFILE", None)
    env.pop("EVENGINE_BENCH_CAPTURE", None)
    if args.profile:
        env["EVENGINE_BENCH_PROFILE"] = "1"
    root = next((p for p in binary.parents if (p / "CMakeCache.txt").is_file()), binary.parent)
    runtime = args.runtime_dir.resolve() if args.runtime_dir else root
    env["PATH"] = str(runtime) + os.pathsep + env.get("PATH", "")
    config = {}
    cache = root / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8").splitlines():
            key = line.split(":", 1)[0]
            if "=" in line and (
                key.startswith("EVENGINE_")
                or key in {"CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS_RELEASE"}
            ):
                config[key] = line.split("=", 1)[1]
    source = Path(__file__).resolve().parents[2]
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=source,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=True,
    ).stdout.strip()
    diff = subprocess.run(
        ["git", "diff", "--binary", "HEAD"], cwd=source, capture_output=True, check=True
    ).stdout
    report = {
        "schema": "evengine.gameplay-matrix",
        "version": 1,
        "status": "running",
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "source_revision": revision,
        "tracked_diff_sha256": hashlib.sha256(diff).hexdigest(),
        "benchmark_source_sha256": {
            name: hashlib.sha256((Path(__file__).parent / name).read_bytes()).hexdigest()
            for name in (
                "GameplayBenchmark.cpp",
                "GameplayBenchmark.h",
                "main.cpp",
                "run_gameplay.py",
            )
        },
        "render_environment": {
            key: env.get(key, "default")
            for key in (
                "EVENGINE_FRAMEGRAPH_RECORDING",
                "EVENGINE_GPU_TIMING",
                "EVENGINE_GPU_DEVICE",
                "EVENGINE_VULKAN_VALIDATION",
                "VK_ICD_FILENAMES",
            )
        },
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "processor": platform.processor(),
        },
        "build_config": config,
        "cpu_only": args.cpu_only,
        "profile": args.profile,
        "quick": args.quick,
        "shadows": args.shadows,
        "runs": [],
        "engine_libraries": [
            {"name": p.name, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in sorted(runtime.glob("EV*.dll"))
        ],
    }
    output = args.output / "results.json"
    sizes = [(64, 4)] if args.quick else [(256, 8), (1000, 32), (5000, 64)]
    cases = [(scene, props, actors) for props, actors in sizes for scene in args.scenes]
    checksums = {}
    try:
        for repeat in range(args.repeats):
            order = cases if repeat % 2 == 0 else list(reversed(cases))
            for scene, props, actors in order:
                key = f"{scene}-{props}-{actors}"
                env["EVENGINE_BENCH_CHARACTERS"] = str(actors)
                command = [
                    str(binary),
                    f"3d-gameplay-{scene}",
                    str(args.frames),
                    str(props),
                    str(args.width),
                    str(args.height),
                ]
                proc = subprocess.run(
                    command,
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=args.timeout,
                    check=False,
                )
                (args.output / f"{key}-{repeat + 1}.log").write_text(proc.stdout, encoding="utf-8")
                lines = [
                    x.removeprefix("GAMEPLAY_BENCHMARK_JSON=")
                    for x in proc.stdout.splitlines()
                    if x.startswith("GAMEPLAY_BENCHMARK_JSON=")
                ]
                if proc.returncode != 0 or len(lines) != 1:
                    raise RuntimeError(
                        f"{key}: exit {proc.returncode}, expected one report; inspect log"
                    )
                result = json.loads(lines[0])
                if (
                    result.get("schema") != "evengine.gameplay-benchmark"
                    or result.get("version") != 1
                ):
                    raise RuntimeError(f"{key}: unknown result schema")
                if result.get("validation") != "passed" or result["frames"] != args.frames:
                    raise RuntimeError(f"{key}: failed validation or frame mismatch")
                if result.get("lightingMode") != args.lighting:
                    raise RuntimeError(f"{key}: effective lighting mode mismatch")
                if result.get("skinInfluences") != args.skin_influences:
                    raise RuntimeError(f"{key}: skin influence workload mismatch")
                if args.no_msaa and result.get("msaaEnabled") is not False:
                    raise RuntimeError(f"{key}: MSAA was not disabled")
                if result["renderEnabled"] != (not args.cpu_only) or result["props"] != props:
                    raise RuntimeError(f"{key}: workload mismatch")
                if result["characters"] != (actors if scene in ("animated", "mixed") else 0):
                    raise RuntimeError(f"{key}: character workload mismatch")
                for phase in result["phases"].values():
                    if any(not math.isfinite(value) or value < 0 for value in phase.values()):
                        raise RuntimeError(f"{key}: invalid timing")
                previous = checksums.setdefault(key, result["stateChecksum"])
                if previous != result["stateChecksum"]:
                    raise RuntimeError(f"{key}: repeated run changed simulation state")
                report["runs"].append(
                    {"case": key, "repeat": repeat + 1, "command": command, "result": result}
                )
                output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                print(
                    f"{key} repeat {repeat + 1}: {result['phases']['frame']['p50Ms']:.3f} ms",
                    flush=True,
                )
        report["summary"] = {
            key: {
                phase: statistics.median(
                    run["result"]["phases"][phase]["p50Ms"]
                    for run in report["runs"]
                    if run["case"] == key
                )
                for phase in next(
                    run["result"]["phases"] for run in report["runs"] if run["case"] == key
                )
            }
            for key in checksums
        }
        report["status"] = "passed"
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        report["status"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
