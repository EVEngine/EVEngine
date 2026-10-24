#!/usr/bin/env python3
"""Changed-line architecture shape gate (code-first; no JSON catalogue).

This gate lints *added* C/C++ lines for high-signal API mistakes that are
cheap to detect without a C++ parser:

* ambiguous operation ``bool`` / ``lastError`` channels (api-shape);
* undocumented public raw-pointer APIs (api-lifetime);
* ``getModInst`` / ``ModuleManager::getInstance`` in runtime modules (G-4);
* lookup / ``cap::query`` growth inside declared hot-path files (G-3);
* expensive or owning-container public APIs missing ``@cost`` (G-5).

Capability surfaces, Link/System declarations, and script binds are **not**
mirrored into a hand-maintained catalogue — the source is the truth. See
``docs/dev/superpowers/specs/2026-10-14-architecture-contracts-code-first.md``.

Usage::

    python3 scripts/check_architecture_contracts.py
    python3 scripts/check_architecture_contracts.py --base origin/dev
    python3 scripts/check_architecture_contracts.py --all
    python3 scripts/check_architecture_contracts.py --json
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import utf8_stdio

ROOT = Path(__file__).resolve().parent.parent

# Files that must not grow getModInst / ModuleManager / cap::query|forEach*
# primitives on changed lines. Migrated once from the retired JSON catalogue;
# edit this list (not a parallel JSON) when marking a new hot path.
HOT_PATH_GLOBS: tuple[str, ...] = (
    "src/modules/combat/Damage.cpp",
    "src/modules/graphics/DamageNumbers.cpp",
    "src/modules/graphics/DamageNumbersRender.cpp",
    "src/modules/weapon/CombatCarrier.cpp",
    "src/modules/physics/WorldSphereSweep.cpp",
    "src/modules/physics/World3DQueries.cpp",
    "src/modules/physics/forces/*.cpp",
    "src/modules/physics/mechanics/*.cpp",
    "src/modules/vehicle/VehicleTires2D.cpp",
    "src/modules/vehicle/VehicleTires3D.cpp",
    "src/modules/vehicle/VehicleTireRuntime.cpp",
    "src/engine/common/SmoothMax.h",
    "src/modules/audio/EventSink.cpp",
    "src/modules/audio/PcmStream.cpp",
    "src/modules/crowd/CrowdAvoidance.cpp",
    "src/modules/sensing/EnvironmentQuery.cpp",
    "src/modules/crowd/CrowdInternal.h",
    "src/modules/crowd/CrowdSimulation.cpp",
    "src/modules/crowd/CrowdStep.cpp",
    "src/modules/graphics/RenderSystem3D.cpp",
    "src/modules/graphics/hair/GroomAsset.cpp",
    "src/modules/graphics/raytracing/vulkan/VulkanRayTracing.cpp",
    "src/modules/graphics/vulkan/GraphicsSampler.cpp",
    "src/modules/graphics/webgpu/GraphicsSampler.cpp",
    "src/modules/hexmap/HexSphereMap.cpp",
    "src/modules/hexmap/HexSphereTopology.cpp",
    "src/modules/joystick/sdl/EventSink.cpp",
    "src/modules/keyboard/sdl/EventSink.cpp",
    "src/modules/model3d/ModelRenderer.cpp",
    "src/modules/particles/ParticleSystem.cpp",
    "src/modules/physics/World3DContacts.cpp",
    "src/modules/physics/box3d/Box3DWorld.cpp",
    "src/modules/physics/box3d/Box3DWorldContacts.cpp",
    "src/modules/physics/jolt/JoltContactPolicy.cpp",
    "src/modules/physics/jolt/JoltExplosion.cpp",
    "src/modules/physics/jolt/JoltGeometryQueries.cpp",
    "src/modules/physics/jolt/JoltMesh.cpp",
    "src/modules/physics/jolt/JoltSolvedContacts.cpp",
    "src/modules/physics/jolt/JoltState.cpp",
    "src/modules/physics/jolt/JoltWorld.cpp",
    "src/modules/physics/jolt/JoltWorldJoints.cpp",
    "src/modules/physics/jolt/JoltWorldMover.cpp",
    "src/modules/procgen/heightmap/TerrainMeshStamp.cpp",
    "src/modules/procgen/heightmap/TerrainStamp.cpp",
    "src/modules/profiler/Profiler.cpp",
    "src/modules/rts/RTSFormationClearance.cpp",
    "src/modules/rts/RTSMovementGroups.cpp",
    "src/modules/scene/TransformSystem.cpp",
    "src/modules/tactics/TacticsBattle.cpp",
    "src/modules/tactics/TacticsPath.cpp",
    "src/modules/tactics/TacticsTypes.cpp",
    "src/modules/touch/sdl/EventSink.cpp",
    "src/modules/ui/Component.cpp",
    "src/modules/ui/EventSink.cpp",
    "src/modules/ui/UISystem.cpp",
    "src/modules/weather/Lightning.cpp",
    "src/modules/window/sdl/EventSink.cpp",
)

HOT_PATH_PRIMITIVE = re.compile(
    r"\b(?:getModInst|requireModInst)\s*\(|ModuleManager::getInstance|"
    r"\b(?:eve::)?cap::(?:query|forEach|forEachUntil)\s*<"
)
RUNTIME_LOOKUP = re.compile(r"\b(?:getModInst|requireModInst)\s*\(|ModuleManager::getInstance")
EXPENSIVE_API_NAME = re.compile(
    r"\b(?:Readback|ReadAll|Load|Save|Build|Compile|Bake|Upload|Capture|Materialize|Collect)\w*\s*\("
)
OWNED_CONTAINER_RETURN = re.compile(
    r"^\s*(?:(?:\[\[(?:nodiscard|deprecated)(?:\([^]]*\))?\]\]\s*)*"
    r"(?:(?:static|virtual|inline|constexpr|explicit)\s+)*)"
    r"(?:std::)?(?:vector|string|map|unordered_map|set|unordered_set)\s*<"
)
OWNED_CONTAINER_MEMBER = re.compile(
    OWNED_CONTAINER_RETURN.pattern + r"[^;{}()]*>\s+[A-Za-z_]\w*\s*(?:;|=|\{)"
)

BOOL_RETURN = re.compile(
    r"^\s*(?:(?:\[\[(?:nodiscard|deprecated)(?:\([^]]*\))?\]\]\s*)*"
    r"(?:(?:static|virtual|inline|constexpr|explicit)\s+)*)bool\s+"
    r"([A-Za-z_]\w*)\s*\("
)
POINTER_API = re.compile(
    r"^\s*(?:(?:\[\[nodiscard(?:\([^]]*\))?\]\]\s*)?"
    r"(?:static|virtual|inline|constexpr|explicit|const)\s+)*"
    r"(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;{}()]+>)?\s*)?\*\s*"
    r"[A-Za-z_]\w*\s*(?:\(|;|=)"
)

PREDICATE_FIRST_TOKENS = frozenset(
    {
        "is",
        "get",
        "has",
        "can",
        "should",
        "supports",
        "contains",
        "empty",
        "valid",
        "matches",
        "equals",
        "ok",
        "active",
        "paused",
        "passed",
        "owns",
        "using",
        "used",
        "critical",
        "disposed",
        "changed",
        "ready",
        "available",
        "enabled",
        "visible",
        "playing",
        "finished",
        "stale",
        "bound",
        "dirty",
        "as",
        "exact",
        "initialized",
    }
)
PREDICATE_LAST_TOKENS = frozenset(
    {
        "matches",
        "equals",
        "changed",
        "active",
        "paused",
        "passed",
        "disposed",
        "critical",
        "owned",
        "ready",
        "available",
        "valid",
        "visible",
        "enabled",
        "stale",
        "dirty",
    }
)


@dataclass(frozen=True)
class SourceLine:
    path: str
    line: int
    text: str


@dataclass(frozen=True)
class Finding:
    rule: str
    code: str
    path: str
    line: int
    message: str

    def as_dict(self) -> dict[str, Any]:
        return {
            "rule": self.rule,
            "code": self.code,
            "path": self.path,
            "line": self.line,
            "message": self.message,
        }


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def path_matches(path: str, scope: str) -> bool:
    return fnmatch.fnmatch(path.replace("\\", "/"), scope)


def _is_runtime_module_path(path: str) -> bool:
    normalized = path.replace("\\", "/")
    if not normalized.startswith("src/modules/"):
        return False
    parts = normalized.split("/")
    if any(part in {"editing", "editor", "devtools", "graphics_editing"} for part in parts):
        return False
    return True


def _is_hot_path(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return any(path_matches(normalized, glob) for glob in HOT_PATH_GLOBS)


def _git(args: list[str]) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except (OSError, subprocess.CalledProcessError):
        return ""
    return result.stdout


def _changed_lines(base: str | None) -> list[SourceLine]:
    """Return added C/C++ lines from a git diff and untracked source files."""

    diff_args = ["diff", "--no-color", "--unified=0"]
    if base:
        diff_args.append(base)
    diff_args.extend(["--", "src/**/*.h", "src/**/*.hpp", "src/**/*.cpp"])
    diff = _git(diff_args)
    lines: list[SourceLine] = []
    current: str | None = None
    new_line = 0
    for raw in diff.splitlines():
        if raw.startswith("+++ b/"):
            current = raw[6:]
            continue
        if raw.startswith("@@"):
            match = re.search(r"\+(\d+)(?:,(\d+))?", raw)
            if match:
                new_line = int(match.group(1))
            continue
        if current is None or not raw.startswith("+") or raw.startswith("+++"):
            continue
        lines.append(SourceLine(current, new_line, raw[1:]))
        new_line += 1

    untracked = _git(["ls-files", "--others", "--exclude-standard", "--", "src"])
    known = {(item.path, item.line) for item in lines}
    for name in untracked.splitlines():
        path = ROOT / name
        if path.suffix not in {".h", ".hpp", ".cpp"} or not path.is_file():
            continue
        for number, text in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if (name, number) not in known:
                lines.append(SourceLine(name, number, text))
    return lines


def _all_lines() -> list[SourceLine]:
    result: list[SourceLine] = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp"} or not path.is_file():
            continue
        for number, text in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            result.append(SourceLine(relative(path), number, text))
    return result


def _identifier_tokens(name: str) -> list[str]:
    words = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", name).replace("_", " ")
    return [word.lower() for word in words.split() if word]


def _is_predicate_name(name: str) -> bool:
    tokens = _identifier_tokens(name)
    if not tokens:
        return False
    return tokens[0] in PREDICATE_FIRST_TOKENS or tokens[-1] in PREDICATE_LAST_TOKENS


def _is_compatibility_declaration(text: str, context: str) -> bool:
    if re.search(r"\[\[deprecated(?:\([^]]*\))?\]\]", text, re.IGNORECASE):
        return True
    blocks = re.findall(r"/\*\*.*?\*/", context, flags=re.DOTALL)
    if not blocks:
        return False
    return bool(re.search(r"\b(?:deprecated|compatibility|legacy)\b", blocks[-1], re.IGNORECASE))


def _context(lines: list[SourceLine], index: int) -> str:
    item = lines[index]
    source = ROOT / item.path
    if source.is_file():
        content = source.read_text(encoding="utf-8", errors="replace").splitlines()
        start = max(0, item.line - 25)
        return "\n".join(content[start : item.line])
    start = max(0, index - 24)
    return "\n".join(candidate.text for candidate in lines[start : index + 1] if candidate.path == item.path)


def _is_non_public_declaration(item: SourceLine) -> bool:
    source = ROOT / item.path
    if not source.is_file() or item.line <= 0:
        return False
    prefix = source.read_text(encoding="utf-8", errors="replace").splitlines()[: item.line - 1]
    text = "\n".join(prefix)
    text = re.sub(r"/\*.*?\*/", lambda match: "\n" * match.group(0).count("\n"), text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)

    scopes: list[tuple[bool, str]] = []
    pending_class: str | None = None
    for line in text.splitlines():
        class_match = re.search(r"\b(class|struct)\s+[A-Za-z_]\w*[^;{]*", line)
        if class_match:
            pending_class = "public" if class_match.group(1) == "struct" else "private"
        access_match = re.match(r"\s*(public|protected|private)\s*:", line)
        if access_match and scopes and scopes[-1][0]:
            scopes[-1] = (True, access_match.group(1))
        for character in line:
            if character == "{":
                if pending_class is not None:
                    scopes.append((True, pending_class))
                    pending_class = None
                else:
                    scopes.append((False, ""))
            elif character == "}" and scopes:
                scopes.pop()
    return bool(scopes and scopes[-1][0] and scopes[-1][1] in {"private", "protected"})


def lint_api_shapes(lines: list[SourceLine]) -> list[Finding]:
    findings: list[Finding] = []
    for index, item in enumerate(lines):
        text = item.text
        context = _context(lines, index).lower()
        if re.search(
            r"\b(?:bool|Status|int)\s+[A-Za-z_]\w*\s*\([^;{}]*"
            r"(?:lastError|last_error|\b(?:err|error)\b)\s*\*",
            text,
            re.IGNORECASE,
        ):
            findings.append(
                Finding(
                    "api-shape",
                    "bool-error-pointer-channel",
                    item.path,
                    item.line,
                    "operation combines a scalar result with an error pointer; return structured Result/Diagnostic",
                )
            )
        declares_last_error = item.path.endswith((".h", ".hpp")) and re.search(
            r"\b(?:lastError|last_error)(?:_|\b)\s*(?:[;=,)])", text
        )
        if declares_last_error:
            findings.append(
                Finding(
                    "api-shape",
                    "last-error-channel",
                    item.path,
                    item.line,
                    "new code mentions lastError; return structured Result/Diagnostic instead",
                )
            )
        match = BOOL_RETURN.match(text) if item.path.endswith((".h", ".hpp")) else None
        if match:
            name = match.group(1)
            if _is_non_public_declaration(item):
                continue
            lower_name = name.lower()
            predicate = _is_predicate_name(name) or lower_name in {
                "operator bool",
                "operator==",
                "operator!=",
            }
            compatibility = _is_compatibility_declaration(text, context)
            if not predicate and not compatibility:
                findings.append(
                    Finding(
                        "api-shape",
                        "ambiguous-operation-bool",
                        item.path,
                        item.line,
                        f"operation {name} returns bool; use a Result or named status enum",
                    )
                )
        if item.path.endswith((".h", ".hpp")) and "*" in text and "(" in text:
            if POINTER_API.search(text) and not re.search(
                r"@(?:ownership|lifetime|borrowed|owned|outlives|thread)",
                context,
                re.IGNORECASE,
            ):
                findings.append(
                    Finding(
                        "api-lifetime",
                        "undocumented-raw-pointer-api",
                        item.path,
                        item.line,
                        "public raw-pointer API needs Doxygen ownership and lifetime contract",
                    )
                )
    return findings


def lint_runtime_guards(lines: list[SourceLine]) -> list[Finding]:
    """Enforce G-3 / G-4 / G-5 on changed lines (no JSON catalogue)."""

    findings: list[Finding] = []
    for index, item in enumerate(lines):
        text = item.text
        context = _context(lines, index)

        if _is_runtime_module_path(item.path) and RUNTIME_LOOKUP.search(text):
            findings.append(
                Finding(
                    "module-interface",
                    "runtime-convenience-lookup",
                    item.path,
                    item.line,
                    "getModInst/requireModInst/ModuleManager::getInstance belong only to "
                    "non-runtime (editing/editor/devtools) code",
                )
            )

        if _is_hot_path(item.path) and HOT_PATH_PRIMITIVE.search(text):
            findings.append(
                Finding(
                    "module-interface",
                    "hot-path-forbidden-primitive",
                    item.path,
                    item.line,
                    "hot_path files must not add getModInst/requireModInst/cap::query|forEach*",
                )
            )

        if item.path.endswith((".h", ".hpp")) and not _is_non_public_declaration(item):
            expensive = bool(
                ("(" in text and OWNED_CONTAINER_RETURN.search(text) and not OWNED_CONTAINER_MEMBER.search(text))
                or EXPENSIVE_API_NAME.search(text)
            )
            if expensive and not re.search(r"@cost\b", context, re.IGNORECASE):
                findings.append(
                    Finding(
                        "module-interface",
                        "missing-cost-annotation",
                        item.path,
                        item.line,
                        "public owned-container or expensive-named API needs a Doxygen @cost contract",
                    )
                )
    return findings


# Back-compat alias used by older tests / callers.
def lint_module_interface(lines: list[SourceLine], metadata: Any = None) -> list[Finding]:
    del metadata
    return lint_runtime_guards(lines)


def render(findings: list[Finding], json_output: bool) -> int:
    if json_output:
        print(json.dumps({"findings": [finding.as_dict() for finding in findings]}, ensure_ascii=False, indent=2))
    else:
        for finding in findings:
            print(f"FAIL {finding.rule} {finding.path}:{finding.line}: {finding.message}")
        if not findings:
            print("architecture contracts OK: no new high-signal violations")
    return 1 if findings else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--metadata",
        type=Path,
        help=argparse.SUPPRESS,  # retired; accepted so old wrappers do not break
    )
    parser.add_argument("--base", help="git base for changed-only mode (default: CI base or HEAD)")
    parser.add_argument("--all", action="store_true", help="lint all src C/C++ lines instead of only additions")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    if args.all:
        lines = _all_lines()
    else:
        base = args.base
        if base is None:
            base = os.environ.get("EVENGINE_ARCHITECTURE_BASE")
        lines = _changed_lines(base or "HEAD")
    findings = lint_api_shapes(lines) + lint_runtime_guards(lines)
    return render(findings, args.json)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
