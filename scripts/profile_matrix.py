#!/usr/bin/env python3
"""Configure, build and smoke-test the supported module profiles.

The matrix has two layers:

* ``--check`` is a fast, toolchain-free contract check used by CI. It resolves
  the manifest in Python and verifies that hostless profiles cannot acquire a
  renderer or window.
* ``--configure --build --smoke`` invokes CMake directly (never ``make``). The
  build target is ``eve_profile_smoke``: selected module OBJECT targets,
  standalone public-header translation units and independent capability probes.
  It intentionally does not link the full ``unit_test`` executable.

Examples::

    python3 scripts/profile_matrix.py --check
    python3 scripts/profile_matrix.py --profile physics-core-only \
        --configure --build --smoke
    python3 scripts/profile_matrix.py --all --dry-run

The ``web`` profile uses ``BUILD_PLATFORM=webgpu``. Native Dawn or an
Emscripten toolchain must be supplied by the caller; the script does not hide
that platform requirement behind a soft skip.
"""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import check_module_manifest as manifest  # noqa: E402


PROFILES = (
    "minimal",
    "2d",
    "3d",
    "runtime-3d",
    "web",
    "procgen-core-only",
    "physics-core-only",
    "asset-core-only",
    "headless",
    "server",
)
HOSTLESS = {"procgen-core-only", "physics-core-only", "asset-core-only", "headless", "server"}
RUNTIME_ONLY = {"runtime-3d"}


def is_editor_module(name: str) -> bool:
    return name in {"editor", "editing"} or name.endswith("_editing")
CORE_SEEDS = {
    "procgen-core-only": ("common",),
    "physics-core-only": ("common", "platform_event"),
    "asset-core-only": ("common", "data", "asset"),
    "headless": ("common", "data", "platform_event", "timer"),
    "server": (
        "common",
        "data",
        "platform_event",
        "timer",
        "network",
        "authority",
        "decision",
        "definitions",
        "effects",
        "game_event",
        "orders",
        "schema",
        "social",
        "statepatch",
        "steering",
        "tags",
        "transaction",
        "economy",
        "attributes",
        "sensing",
        "spatial",
        "action",
        "settlement",
    ),
}
FORBIDDEN_HOSTLESS = {"cmdline", "window", "graphics", "physics", "procgen"}
PHYSICS_CORE_SOURCES = (
    "physics/Body.cpp",
    "physics/Body3D.cpp",
    "physics/Fixture.cpp",
    "physics/Joint3D.cpp",
    "physics/PhysicsHandles.cpp",
    "physics/PhysicsLink.cpp",
    "physics/Shape3D.cpp",
    "physics/SimulationBackend.cpp",
    "physics/World.cpp",
    "physics/World3D.cpp",
)


@dataclass(frozen=True)
class ModuleContract:
    name: str
    deps: tuple[str, ...]
    third_party: tuple[str, ...]
    groups: tuple[str, ...]
    required: bool
    core: bool


_KEYWORDS = {
    "NAME",
    "LIB",
    "LAYER",
    "DEPS",
    "OPTIONAL_DEPS",
    "THIRDPARTY",
    "SCRIPT",
    "SLOT",
    "GROUP",
    "REQUIRED",
    "CORE",
}
_ONE_VALUE = {"NAME", "LIB", "LAYER"}
_MULTI_VALUE = {"DEPS", "OPTIONAL_DEPS", "THIRDPARTY", "SCRIPT", "SLOT", "GROUP"}


def _cmake_blocks(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    blocks: list[str] = []
    for start, line in enumerate(lines):
        if not line.lstrip().startswith("eve_declare_module("):
            continue
        depth = 0
        quoted = False
        escaped = False
        block: list[str] = []
        for current in lines[start:]:
            block.append(current)
            for char in current:
                if escaped:
                    escaped = False
                elif char == "\\" and quoted:
                    escaped = True
                elif char == '"':
                    quoted = not quoted
                elif not quoted and char == "(":
                    depth += 1
                elif not quoted and char == ")":
                    depth -= 1
            if depth == 0:
                blocks.append("".join(block))
                break
        else:
            raise ValueError(f"unterminated eve_declare_module at line {start + 1}")
    return blocks


def _parse_contract(block: str) -> ModuleContract:
    tokens = shlex.split(block.replace("\n", " "))
    if not tokens or not tokens[0].startswith("eve_declare_module("):
        raise ValueError(f"invalid module declaration: {block!r}")
    tokens[0] = tokens[0][len("eve_declare_module("):]
    if not tokens[0]:
        tokens.pop(0)
    values: dict[str, list[str] | bool] = {}
    index = 0
    while index < len(tokens):
        key = tokens[index].rstrip(")")
        index += 1
        if key in {"REQUIRED", "CORE"}:
            values[key] = True
        elif key in _ONE_VALUE:
            if index >= len(tokens):
                raise ValueError(f"{key} has no value in {block!r}")
            values[key] = [tokens[index].rstrip(")")]
            index += 1
        elif key in _MULTI_VALUE:
            result: list[str] = []
            while index < len(tokens) and tokens[index].rstrip(")") not in _KEYWORDS:
                result.append(tokens[index].rstrip(")"))
                index += 1
            values[key] = result
        else:
            # The first token is the command spelling; an unknown token after
            # it indicates a manifest syntax that this matrix cannot resolve.
            raise ValueError(f"unknown manifest token {key!r} in {block!r}")
    name = values.get("NAME", [])
    if not isinstance(name, list) or not name:
        raise ValueError(f"manifest declaration has no NAME: {block!r}")
    return ModuleContract(
        name=name[0],
        deps=tuple(values.get("DEPS", []) if isinstance(values.get("DEPS", []), list) else []),
        third_party=tuple(
            values.get("THIRDPARTY", [])
            if isinstance(values.get("THIRDPARTY", []), list)
            else []
        ),
        groups=tuple(values.get("GROUP", []) if isinstance(values.get("GROUP", []), list) else []),
        required=bool(values.get("REQUIRED", False)),
        core=bool(values.get("CORE", False)),
    )


def contracts() -> list[ModuleContract]:
    return [
        _parse_contract(block)
        for path in manifest.manifest_files()
        for block in _cmake_blocks(path)
    ]


def resolve(profile: str, declared: list[ModuleContract] | None = None) -> tuple[list[str], list[str]]:
    modules = declared or contracts()
    by_name = {module.name: module for module in modules}
    if profile in CORE_SEEDS:
        wanted = list(CORE_SEEDS[profile])
    else:
        wanted = []
        for module in modules:
            selection_profile = "3d" if profile == "runtime-3d" else profile
            enabled = module.required or profile == "full" or selection_profile in module.groups
            if profile in RUNTIME_ONLY and is_editor_module(module.name):
                enabled = False
            if enabled:
                wanted.append(module.name)

    pending = list(wanted)
    while pending:
        name = pending.pop(0)
        if name not in by_name:
            raise ValueError(f"profile {profile}: undeclared module {name}")
        for dependency in by_name[name].deps:
            if dependency not in by_name:
                raise ValueError(f"module {name} depends on undeclared {dependency}")
            if dependency not in wanted:
                wanted.append(dependency)
                pending.append(dependency)

    ordered = [module.name for module in modules if module.name in wanted]
    groups: list[str] = []
    for module in modules:
        if module.name in wanted:
            for group in module.third_party:
                if group not in groups:
                    groups.append(group)
    return ordered, groups


def check_contracts() -> int:
    declarations = manifest.parse_manifest()
    errors = manifest.validate(declarations, manifest.source_modules())
    try:
        module_contracts = contracts()
    except ValueError as error:
        errors.append(str(error))
        module_contracts = []
    names = {module.name for module in module_contracts}
    if set(PROFILES) & names:
        errors.append("profile name collides with a module name")

    for profile in PROFILES:
        try:
            enabled, groups = resolve(profile, module_contracts)
        except ValueError as error:
            errors.append(str(error))
            continue
        enabled_set = set(enabled)
        if profile in RUNTIME_ONLY:
            leaked = sorted(name for name in enabled_set if is_editor_module(name))
            if leaked:
                errors.append(f"{profile}: runtime-only profile enables {', '.join(leaked)}")
        if profile in HOSTLESS:
            leaked = sorted(enabled_set & FORBIDDEN_HOSTLESS)
            if leaked:
                errors.append(f"{profile}: hostless profile enables {', '.join(leaked)}")
        if profile == "procgen-core-only" and "EVProcgenCoreCheck" not in (
            ROOT / "src" / "modules" / "CMakeLists.txt"
        ).read_text(encoding="utf-8"):
            errors.append("procgen-core-only: EVProcgenCoreCheck target is not wired")
        if profile == "physics-core-only" and "EVPhysicsCoreCheck" not in (
            ROOT / "src" / "modules" / "CMakeLists.txt"
        ).read_text(encoding="utf-8"):
            errors.append("physics-core-only: EVPhysicsCoreCheck target is not wired")
        if profile == "physics-core-only":
            physics_cmake = (ROOT / "src" / "modules" / "CMakeLists.txt").read_text(
                encoding="utf-8"
            )
            missing_sources = [
                source for source in PHYSICS_CORE_SOURCES if source not in physics_cmake
            ]
            if missing_sources:
                errors.append(
                    "physics-core-only: domain sources missing from core check: "
                    + ", ".join(missing_sources)
                )
            core_block = physics_cmake.split("set(_eve_physics_core_sources", 1)[-1].split(
                "set(_eve_physics_core_boundary_files", 1
            )[0]
            if "graphics/" in core_block or "gpgpu/" in core_block:
                errors.append("physics-core-only: core source block mentions graphics/gpgpu")
        print(
            f"profile {profile:18s}: {len(enabled):2d} modules; "
            f"third-party groups: {', '.join(groups) or '(none)'}"
        )

    profile_cmake = (ROOT / "cmake" / "profile_checks.cmake").read_text(encoding="utf-8")
    if "eve_public_header_checks" not in profile_cmake:
        errors.append("independent public-header target is missing")
    if "eve_capability_present_check" not in profile_cmake or "eve_capability_absent_check" not in profile_cmake:
        errors.append("independent optional-capability targets are incomplete")
    if any(
        "target_link_libraries" in line and "unit_test" in line
        for line in profile_cmake.splitlines()
    ):
        errors.append("profile checks must not link unit_test")
    test_cmake = (ROOT / "test" / "CMakeLists.txt").read_text(encoding="utf-8")
    if "eve_thirdparty_libs(_eve_test_tp_libs" not in test_cmake:
        errors.append("test runner is not using manifest-derived third-party groups")
    if errors:
        for error in errors:
            print(f"FAIL {error}", file=sys.stderr)
        return 1
    print(f"profile matrix contract OK: {len(PROFILES)} profiles")
    return 0


def build_directory(root: Path, profile: str) -> Path:
    return root / profile


def cmake_configure_command(args: argparse.Namespace, profile: str) -> list[str]:
    platform = args.platform or ("webgpu" if profile == "web" else "linux")
    cmake_command = shlex.split(args.cmake_command)
    if not cmake_command:
        raise ValueError("--cmake-command must not be empty")
    return cmake_command + [
        "-S",
        str(ROOT),
        "-B",
        str(build_directory(Path(args.build_root), profile)),
        "-G",
        args.generator,
        "-DCMAKE_BUILD_TYPE=Debug",
        f"-DBUILD_PLATFORM={platform}",
        "-DBUILD_TESTING=OFF",
        "-DEVENGINE_BUILD_DEMO=OFF",
        "-DEVENGINE_BUILD_PROFILE_CHECKS=ON",
        f"-DEVENGINE_PROFILE={profile}",
    ]


def run_command(command: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    print("$ " + shlex.join(command))
    subprocess.run(command, cwd=cwd, env=env, check=True)


def run_smoke(args: argparse.Namespace, profile: str) -> None:
    directory = build_directory(Path(args.build_root), profile)
    candidates: list[Path] = []
    for stem in ("eve_capability_present_check", "eve_capability_absent_check"):
        candidates.extend(sorted(directory.rglob(stem)))
        candidates.extend(sorted(directory.rglob(stem + ".js")))
    if not candidates:
        raise RuntimeError(
            f"{profile}: capability smoke binaries are missing; build eve_profile_smoke first"
        )
    for candidate in candidates:
        if candidate.suffix == ".js":
            node = os.environ.get("NODE", "node")
            run_command([node, str(candidate)], ROOT)
        elif candidate.is_file() and os.access(candidate, os.X_OK):
            run_command([str(candidate)], ROOT)


def execute_profile(args: argparse.Namespace, profile: str) -> None:
    command = cmake_configure_command(args, profile)
    directory = build_directory(Path(args.build_root), profile)
    if args.dry_run:
        if args.configure:
            print("$ " + shlex.join(command))
        if args.build:
            print(
                "$ "
                + shlex.join(
                    [
                        "cmake",
                        "--build",
                        str(directory),
                        "--target",
                        "eve_profile_smoke",
                        "--parallel",
                        str(args.jobs),
                    ]
                )
            )
        if args.smoke:
            print(f"$ <run {directory}/profile/eve_capability_{'{present,absent}'}_check>")
        return
    if args.configure:
        run_command(command, ROOT)
    if args.build:
        run_command(
            [
                "cmake",
                "--build",
                str(directory),
                "--target",
                "eve_profile_smoke",
                "--parallel",
                str(args.jobs),
            ],
            ROOT,
        )
    if args.smoke:
        run_smoke(args, profile)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--profile", choices=PROFILES)
    selection.add_argument("--all", action="store_true", help="run every profile in matrix order")
    parser.add_argument("--check", action="store_true", help="run the static contract check")
    parser.add_argument("--configure", action="store_true", help="run CMake configure")
    parser.add_argument("--build", action="store_true", help="build eve_profile_smoke")
    parser.add_argument("--smoke", action="store_true", help="run independent capability probes")
    parser.add_argument("--dry-run", action="store_true", help="print commands without executing them")
    parser.add_argument("--build-root", default=str(ROOT / "build" / "profile-matrix"))
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--platform", help="override BUILD_PLATFORM (default: linux, webgpu for web)")
    parser.add_argument(
        "--cmake-command",
        default=os.environ.get("EVENGINE_CMAKE_COMMAND", "cmake"),
        help="configure launcher, e.g. 'emcmake cmake' for Emscripten",
    )
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 4))
    args = parser.parse_args(argv)

    if not any((args.check, args.configure, args.build, args.smoke, args.dry_run)):
        args.check = True
    if args.dry_run and not any((args.configure, args.build, args.smoke)):
        args.configure = args.build = args.smoke = True
    if args.check:
        result = check_contracts()
        if result:
            return result
    selected = list(PROFILES) if args.all else ([args.profile] if args.profile else [])
    if (args.configure or args.build or args.smoke or args.dry_run) and not selected:
        parser.error("--profile or --all is required with configure/build/smoke/dry-run")
    for profile in selected:
        execute_profile(args, profile)
    return 0


if __name__ == "__main__":
    sys.exit(main())
