#!/usr/bin/env python3
"""Measure path batches or synthetic normal render/present frames, not full game acceptance."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
GRIDS = {10000: (100, 50), 100000: (250, 200), 1000000: (1000, 500)}


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def read_samples(path: Path, triangles: int, width: int, height: int,
                 frames: int, repeats: int, moving: bool = False, spatial: bool = False, runtime: bool = False,
                 instances: int = 1, texture_size: int = 0, pbr_material: bool = False, normal_map: bool = False, temporal: bool = False, scene: str = "planar_grid", moving_camera: bool = False, warmup: int = 120) -> list[list[float]]:
    groups: list[list[float]] = [[] for _ in range(repeats)]
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != frames * repeats:
        raise ValueError(f"{path}: incomplete sample count")
    for index, row in enumerate(rows):
        repeat, frame = divmod(index, frames)
        expected = {"schema_version": "2" if runtime else "1", "scene": scene, "triangles": str(triangles),
                    "instances": str(instances), "width": str(width), "height": str(height), "spp": "1",
                    "bounces": "4", "repeat": str(repeat), "frame": str(frame)}
        if any(row.get(key) != value for key, value in expected.items()):
            raise ValueError(f"{path}: unexpected configuration or sample order at row {index}")
        if row.get("camera_motion", "static") != ("sine_x_look_at_origin" if moving_camera else "static"):
            raise ValueError("camera motion does not match requested workload")
        if moving_camera:
            camera_x = float(row.get("camera_x", "nan"))
            expected_x = .35 * math.sin((warmup + frame) * .01)
            if not math.isfinite(camera_x) or not math.isclose(camera_x, expected_x, rel_tol=2e-5, abs_tol=2e-6):
                raise ValueError("camera trajectory does not match measured frame")
        if row.get("normal_map", "0") != str(int(normal_map)):
            raise ValueError("normal map mode does not match requested workload")
        if row.get("pbr_material", "0") != str(int(pbr_material)):
            raise ValueError("PBR material mode does not match requested workload")
        if row.get("texture_size", "0") != str(texture_size):
            raise ValueError(f"{path}: unexpected texture size at row {index}")
        if row.get("instance_motion", "static") != ("translate_x" if moving else "static"):
            raise ValueError(f"{path}: unexpected instance motion at row {index}")
        if row.get("denoise", "none") != ("temporal" if temporal else "spatial" if spatial else "none"):
            raise ValueError(f"{path}: unexpected denoise mode at row {index}")
        if runtime and any(row.get(key) != value for key, value in
                           {"path_active": "1", "vsync": "0", "readback": "0"}.items()):
            raise ValueError(f"{path}: invalid runtime measurement state at row {index}")
        if runtime and row.get("present_mode") not in ("immediate", "mailbox"):
            raise ValueError(f"{path}: unsupported throttled present mode at row {index}")
        elapsed = float(row["frame_wall_ms" if runtime else "batch_wall_ms"])
        if not math.isfinite(elapsed) or elapsed <= 0:
            raise ValueError(f"{path}: invalid elapsed time at row {index}")
        stage_names = ("motion_ms", "render_ms", "present_ui_ms")
        if runtime and any(name in row for name in stage_names):
            stages = [float(row.get(name) or "nan") for name in stage_names]
            if any(not math.isfinite(value) or value < 0 for value in stages) or not math.isclose(
                    sum(stages), elapsed, rel_tol=2e-5, abs_tol=2e-5):
                raise ValueError(f"{path}: invalid runtime stage times at row {index}")
        if runtime and ("gpu_available" in row or "gpu_last_completed_ms" in row):
            gpu = float(row.get("gpu_last_completed_ms") or "nan")
            if (row.get("gpu_available") not in ("0", "1") or not math.isfinite(gpu) or gpu < 0
                    or (row["gpu_available"] == "1") != (gpu > 0)):
                raise ValueError(f"{path}: invalid completed GPU timing at row {index}")
        update = float(row.get("instance_update_ms", "0"))
        if (not math.isfinite(update) or update < 0 or update > elapsed or (moving and not runtime and update == 0)
                or (not moving and update != 0)):
            raise ValueError(f"{path}: invalid instance-update time at row {index}")
        groups[repeat].append(elapsed)
    return groups


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/win32-arm64-debug")
    parser.add_argument("--output", type=Path, required=True, help="New evidence directory (must not exist)")
    parser.add_argument("--triangles", type=int, choices=sorted(GRIDS), nargs="+", default=sorted(GRIDS))
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--warmup", type=int, default=120)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--moving-instance", action="store_true", help="Update the retained instance before each batch")
    parser.add_argument("--spatial-denoise", action="store_true", help="Include guided spatial filtering in the batch")
    parser.add_argument("--runtime", action="store_true", help="Measure normal render3D/present with spatial denoising")
    parser.add_argument("--instances", type=int, default=1, help="Shared-mesh tiled instances (runtime only)")
    parser.add_argument("--texture-size", type=int, default=0, help="Shared RGBA8 checker texture edge, 0 disables (runtime only)")
    parser.add_argument("--pbr-material", action="store_true", help="Use automatic PBR base-color and metallic/roughness bindings (textured runtime only)")
    parser.add_argument("--normal-map", action="store_true", help="Add a linear tangent normal map (requires --pbr-material)")
    parser.add_argument("--temporal", action="store_true", help="Enable static-world temporal reprojection (runtime only)")
    parser.add_argument("--scene", choices=("planar_grid", "corrugated_grid"), default="planar_grid")
    parser.add_argument("--moving-camera", action="store_true", help="Translate camera along sine X while looking at scene center (runtime only)")
    parser.add_argument("--profile-tail-ms", type=float, default=0., help="Diagnostic only: collect CPU zone reports for frames above this threshold")
    args = parser.parse_args()
    if not math.isfinite(args.profile_tail_ms) or args.profile_tail_ms < 0 or (args.profile_tail_ms and not args.runtime):
        parser.error("profile threshold must be finite and nonnegative; profiling requires --runtime")
    if args.moving_camera and not args.runtime:
        parser.error("camera motion requires --runtime")
    if args.scene != "planar_grid" and not args.runtime:
        parser.error("nonplanar scenes require --runtime")
    if args.temporal and not args.runtime:
        parser.error("temporal reconstruction requires --runtime")
    if args.normal_map and not args.pbr_material:
        parser.error("normal map requires --pbr-material")
    if args.pbr_material and (not args.runtime or not args.texture_size):
        parser.error("PBR material requires --runtime and nonzero --texture-size")
    if not 0 <= args.texture_size <= 4096 or (args.texture_size and not args.runtime):
        parser.error("texture size must be 0..4096; textured scenes require --runtime")
    if args.instances != 1 and not args.runtime:
        parser.error("multiple instances require --runtime")
    if args.instances < 1 or math.isqrt(args.instances) ** 2 != args.instances:
        parser.error("instances must be a positive square for a complete tile grid")
    if args.runtime:
        args.spatial_denoise = True
    for value, maximum in ((args.width, 4096), (args.height, 4096), (args.warmup, 10000),
                           (args.frames, 10000), (args.repeats, 20), (args.instances, 10000)):
        if not 1 <= value <= maximum:
            parser.error(f"settings must be positive and within benchmark limits (got {value})")
    if len(set(args.triangles)) != len(args.triangles):
        parser.error("duplicate triangle counts are not allowed")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source_hash = hashlib.sha256()
    sources = sorted((ROOT / "src/modules/graphics").rglob("*"))
    sources += [ROOT / "test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp"]
    for path in sources:
        if path.is_file():
            source_hash.update(path.relative_to(ROOT).as_posix().encode("utf-8") + b"\0" + path.read_bytes())
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
                          capture_output=True, text=True, encoding="utf-8").stdout.strip()
    summary = {"schema": "evengine.path-batch-benchmark", "version": 1,
               "metric": "synchronous_batch_wall_ms", "whole_game_frame": False,
               "scene": "synthetic planar grid, static camera, constant environment, opaque diffuse",
               "source_head": head, "graphics_source_sha256": source_hash.hexdigest(),
               "build_directory": str(args.build.resolve()),
               "width": args.width, "height": args.height, "spp": 1, "bounces": 4,
               "warmup": args.warmup, "frames": args.frames, "repeats": args.repeats,
               "validation": False, "completed": False, "results": []}
    summary["scene_id"] = args.scene
    if args.scene == "corrugated_grid":
        summary["scene"] = summary["scene"].replace("planar grid", "corrugated height field z=0.5+0.4*sin(4*x)*sin(4*y)")
    if args.texture_size:
        summary["scene"] += ", shared native RGBA8 checker texture with authored UVs"
    summary["pbr_material"] = args.pbr_material
    summary["normal_map"] = args.normal_map
    if args.normal_map:
        summary["scene"] += ", procedural tangent normal map with triangle UV0 basis"
    if args.pbr_material:
        summary["scene"] += ", automatic PBR base-color and metallic/roughness with independent UV/sampler bindings"
    summary["texture_size"] = args.texture_size
    summary["texture_sampling"] = "base-level linear/clamp, sRGB decode" if args.texture_size else "none"
    if args.pbr_material:
        summary["scene"] = summary["scene"].replace("opaque diffuse", "opaque metallic-roughness")
        summary["texture_sampling"] = "base-level: base-color linear/clamp/sRGB; metallic-roughness linear/mirror/linear data, UV scale 2"
    summary["instances"] = args.instances
    summary["triangle_count_scope"] = "unique shared geometry; instanced total is triangles times instances"
    summary["instance_layout"] = "complete square tile grid in XY"
    summary["instrumented_diagnostic"] = args.profile_tail_ms > 0
    summary["profile_tail_ms"] = args.profile_tail_ms
    summary["profiling_scope"] = "CPU zones collected during frames; aggregation/report formatting between frames affects cadence; not an acceptance performance run" if args.profile_tail_ms else "disabled"
    summary["camera_motion"] = "sine_x_look_at_origin" if args.moving_camera else "static"
    summary["camera_trajectory"] = "eye=(0.35*sin(frame*0.01),0,-1), target=(0,0,0.5); frame includes warmup and resets per repeat" if args.moving_camera else "fixed"
    summary["instance_motion"] = "translate_x" if args.moving_instance else "static"
    summary["denoise"] = "temporal" if args.temporal else "spatial" if args.spatial_denoise else "none"
    if args.moving_instance:
        summary["metric"] = "synchronous_instance_update_and_batch_wall_ms"
    summary["gpu_timing_scope"] = "latest completed renderer graphics queue interval after preparation; delayed by frames in flight; includes submission idle gaps; excludes synchronous AS preparation before the frame prefix"
    summary["normal_present_frame"] = args.runtime
    if args.runtime:
        summary.update({"schema": "evengine.path-runtime-benchmark", "version": 2,
                        "metric": "render_present_wall_ms", "vsync_requested": False,
                        "actual_present_mode_in_csv": True, "readback_in_timing": False,
                        "scope": "synthetic ECS capture/render/HDR/UI/present; excludes game simulation"})
    artifacts = list(args.build.glob("EV*.dll")) + list((args.build / "test").glob("unit_test_graphics*"))
    summary["build_artifacts_sha256"] = {
        path.relative_to(args.build).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in artifacts if path.is_file() and path.suffix.lower() in (".exe", ".dll", "")}
    summary["gpu_selection"] = os.environ.get("EVENGINE_GPU_DEVICE", "engine default")
    cache = (args.build / "CMakeCache.txt").read_text(encoding="utf-8")
    for line in cache.splitlines():
        if line.startswith("EVENGINE_THIRD_PARTY_BINARY_DIR:PATH="):
            stamp = Path(line.split("=", 1)[1]) / "third-party-version.txt"
            if stamp.is_file():
                (output / "dependency-version.txt").write_text(stamp.read_text(encoding="utf-8"), encoding="utf-8")
        if line.startswith(("CMAKE_BUILD_TYPE:STRING=", "EVENGINE_MODULE_LINKAGE:STRING=")):
            key, value = line.split("=", 1)
            summary[key.split(":", 1)[0].lower()] = value
    summary_path = output / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    inventory = subprocess.run(["vulkaninfo", "--summary"], check=True, capture_output=True,
                               text=True, encoding="utf-8", errors="replace")
    (output / "vulkan-inventory.txt").write_text(inventory.stdout + inventory.stderr, encoding="utf-8")
    for count in args.triangles:
        columns, rows = GRIDS[count]
        samples = output / f"{count}.csv"
        environment = os.environ.copy()
        environment["EVENGINE_VULKAN_VALIDATION"] = "0"
        environment["EVE_PT_BENCHMARK_TEMPORAL"] = str(int(args.temporal))
        environment["EVE_PT_BENCHMARK_SPATIAL_DENOISE"] = str(int(args.spatial_denoise))
        environment.update({"EVE_PT_BENCHMARK_CSV": str(samples),
                            "EVE_PT_BENCHMARK_SCENE": args.scene,
                            "EVE_PT_BENCHMARK_PROFILE_TAIL_MS": str(args.profile_tail_ms),
                            "EVE_PT_BENCHMARK_MOVING_CAMERA": str(int(args.moving_camera)),
                            "EVE_PT_BENCHMARK_UPDATE_INSTANCES": str(int(args.moving_instance)),
                            "EVE_PT_BENCHMARK_COLUMNS": str(columns), "EVE_PT_BENCHMARK_ROWS": str(rows),
                            "EVE_PT_BENCHMARK_WIDTH": str(args.width), "EVE_PT_BENCHMARK_HEIGHT": str(args.height),
                            "EVE_PT_BENCHMARK_WARMUP": str(args.warmup), "EVE_PT_BENCHMARK_FRAMES": str(args.frames),
                            "EVE_PT_BENCHMARK_REPEATS": str(args.repeats),
                            "EVE_PT_BENCHMARK_INSTANCES": str(args.instances),
                            "EVE_PT_BENCHMARK_NORMAL_MAP": str(int(args.normal_map)),
                            "EVE_PT_BENCHMARK_PBR_MATERIAL": str(int(args.pbr_material)),
                            "EVE_PT_BENCHMARK_TEXTURE_SIZE": str(args.texture_size)})
        testcase = "runtime_benchmark" if args.runtime else "emission_and_lifetime"
        with (output / f"{count}.log").open("w", encoding="utf-8") as log:
            subprocess.run(["ctest", "--test-dir", str(args.build.resolve()), "-V", "--no-tests=error",
                            "-R", rf"^graphics\.raytracing\.path_scene\.{testcase}$"],
                           cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        groups = read_samples(samples, count, args.width, args.height, args.frames, args.repeats,
                              args.moving_instance, args.spatial_denoise, args.runtime, args.instances, args.texture_size, args.pbr_material, args.normal_map, args.temporal, args.scene, args.moving_camera, args.warmup)
        summary["results"].append({"triangles": count, "raw_csv": samples.name,
                                   "per_repeat": [{"p50_ms": percentile(group, .5),
                                                   "p95_ms": percentile(group, .95),
                                                   "p99_ms": percentile(group, .99)} for group in groups]})
        if args.runtime:
            with samples.open(encoding="utf-8", newline="") as stream:
                gpu_values = [float(row["gpu_last_completed_ms"]) for row in csv.DictReader(stream)
                              if row.get("gpu_available") == "1"]
            summary["results"][-1]["gpu_completed"] = {
                "samples": len(gpu_values), "expected_samples": args.frames * args.repeats,
                "p50_ms": percentile(gpu_values, .5) if gpu_values else None,
                "p95_ms": percentile(gpu_values, .95) if gpu_values else None,
                "p99_ms": percentile(gpu_values, .99) if gpu_values else None}
        summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print(f"{count} triangles: evidence saved to {samples}", flush=True)
    summary["completed"] = True
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
