"""Measure native OBJECT spawn profile boundaries from actual link and symbol artifacts.

This is an evidence reader, not another module manifest. Configured module identities and
library targets come from CMakeCache; expected closure comes from profile_matrix/manifest.
Only Darwin/Linux Makefiles/Ninja OBJECT builds are admitted. Unsupported/missing evidence
fails explicitly, and never becomes a successful trim claim.
"""

from __future__ import annotations

import hashlib
import json
import re
import shlex
import shutil
import subprocess
from pathlib import Path

MAP_PROFILES = {"spawn-map-rpg-runtime", "spawn-map-rts-runtime"}
PROFILES = {"spawn-core-only", "spawn-content-only", "spawn-rpg-only", *MAP_PROFILES}


def read_cache(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.split(":", 1)[0]] = value
    return values


def read_link_command(
    directory: Path, cache: dict[str, str], target: str = "eve_spawn_check", folder: str = "spawn"
) -> str:
    generator = cache.get("CMAKE_GENERATOR")
    if generator == "Unix Makefiles":
        return (
            directory / "profile" / folder / "CMakeFiles" / f"{target}.dir" / "link.txt"
        ).read_text(encoding="utf-8")
    if generator == "Ninja":
        launcher = cache.get("CMAKE_MAKE_PROGRAM", "ninja")
        commands = subprocess.check_output(
            [launcher, "-C", str(directory), "-t", "commands", target], encoding="utf-8"
        )
        candidates = []
        binary = (directory / "profile" / folder / target).resolve()
        for command in commands.splitlines():
            tokens = shlex.split(command)
            if "-o" in tokens:
                output = tokens[tokens.index("-o") + 1]
                if (directory / output).resolve() == binary:
                    candidates.append(command)
        if len(candidates) == 1:
            return candidates[0]
        raise RuntimeError(f"Cannot identify one actual Ninja link command for {target}")
    raise RuntimeError(f"Unverified generator for spawn link evidence: {generator}")


def linked_modules(
    command: str, directory: Path, cache: dict[str, str], folder: str = "spawn"
) -> list[str]:
    libraries: dict[str, str] = {}
    for key, value in cache.items():
        match = re.fullmatch(r"EVE_MODULE_(.+)_LIB", key)
        if match:
            if value in libraries and libraries[value] != match[1]:
                raise RuntimeError(f"Ambiguous configured module library: {value}")
            libraries[value] = match[1]
    tokens = shlex.split(command)
    expanded = []
    for token in tokens:
        if token.startswith("@"):
            working = (
                directory / "profile" / folder
                if cache.get("CMAKE_GENERATOR") == "Unix Makefiles"
                else directory
            )
            response = working / token[1:]
            if not response.is_file():
                raise RuntimeError(f"Missing actual linker response file: {token}")
            expanded.extend(shlex.split(response.read_text(encoding="utf-8")))
        else:
            expanded.append(token)
    targets = {
        match[1] for token in expanded if (match := re.search(r"CMakeFiles/([^/]+)\.dir/", token))
    }
    for token in expanded:
        basename = Path(token).name
        library = re.fullmatch(r"(?:lib)?(.+?)\.(?:a|lib|dylib|so(?:\.[0-9]+)*)", basename)
        if library and library[1] in libraries:
            targets.add(library[1])
        if token.startswith("-l") and token[2:] in libraries:
            targets.add(token[2:])
    measured = sorted(libraries[target] for target in targets if target in libraries)
    if not measured:
        raise RuntimeError("No configured module OBJECT inputs found in actual link command")
    return measured


def verify_symbols(profile: str, defined: str, imported: str, dependencies: str) -> list[str]:
    required = ["eve::spawn::SpawnWorld::advance("]
    forbidden = ["eve::spawn_editor::", "eve::spawn_editing::", "eve::map_rpg_spawn_editor::"]
    if profile in MAP_PROFILES:
        required.append("eve::map_spawn::PreparedMapSpawn::prepare(")
        forbidden.extend(["eve::editor::EditorSession::", "eve::editing::DocumentHost::"])
        if profile == "spawn-map-rpg-runtime":
            required.extend(
                [
                    "eve::rpg_spawn::RPGSpawnFactory::prepare(",
                    "eve::map_rpg_spawn::RPGMapRuntime::advance(",
                ]
            )
            forbidden.extend(["eve::rts::", "eve::rts_spawn::", "eve::map_rts_spawn::"])
        else:
            required.extend(
                [
                    "eve::rts_spawn::RTSSpawnFactory::prepare(",
                    "eve::map_rts_spawn::RTSMapRuntime::advance(",
                ]
            )
            forbidden.extend(["eve::rpg::", "eve::rpg_spawn::", "eve::map_rpg_spawn::"])
    else:
        forbidden.extend(
            [
                "eve::graphics::",
                "eve::map::",
                "eve::map_spawn::",
                "eve::map_rpg_spawn::",
                "eve::map_rts_spawn::",
                "eve::ui::",
                "eve::rts::",
                "eve::rts_spawn::",
                "eve::crowd::",
            ]
        )
        if profile == "spawn-content-only":
            required.append("eve::spawn::SpawnTemplateSource::resolve(")
        if profile == "spawn-rpg-only":
            required.append("eve::rpg_spawn::RPGSpawnFactory::prepare(")
        else:
            forbidden.extend(["eve::rpg::", "eve::rpg_spawn::"])
    for witness in required:
        if witness not in defined:
            raise RuntimeError(f"Missing real implementation symbol witness: {witness}")
    for marker in forbidden:
        if marker in defined or marker in imported:
            raise RuntimeError(f"Forbidden implementation/type symbol in linked artifact: {marker}")
    if profile not in MAP_PROFILES and re.search(
        r"SDL|vulkan|OpenAL|X11|OpenGL|Metal|imgui|webgpu|dawn", dependencies, re.IGNORECASE
    ):
        raise RuntimeError("Unexpected renderer/window/audio dependency in headless spawn binary")
    return required


def audit(directory: Path, profile: str, expected_modules: list[str]) -> dict:
    if profile not in PROFILES:
        raise RuntimeError(f"Spawn link audit does not admit profile {profile}")
    directory = directory.resolve()
    folder = "map_spawn" if profile in MAP_PROFILES else "spawn"
    target = "eve_map_spawn_check" if profile in MAP_PROFILES else "eve_spawn_check"
    output = directory / "profile" / folder / "boundary"
    (output / "report.json").unlink(missing_ok=True)
    cache = read_cache(directory / "CMakeCache.txt")
    if cache.get("EVENGINE_PROFILE") != profile:
        raise RuntimeError("Configured profile does not match requested evidence")
    if cache.get("EVENGINE_MODULE_LINKAGE") != "OBJECT":
        raise RuntimeError("Only native OBJECT evidence is implemented; SHARED remains unverified")
    if cache.get("BUILD_PLATFORM") not in {"macosx", "linux"}:
        raise RuntimeError(
            "Only Darwin/Linux artifacts are supported; other platforms remain unverified"
        )
    configured = sorted(filter(None, cache.get("EVE_ENABLED_MODULES", "").split(";")))
    if configured != sorted(expected_modules):
        raise RuntimeError(
            "Actual configured closure differs from canonical profile/manifest closure"
        )
    if profile in MAP_PROFILES:
        from check_module_manifest import is_authoring_facet

        leaked = [
            name
            for name in configured
            if is_authoring_facet(name, cache.get(f"EVE_MODULE_{name}_DIR"))
        ]
        if leaked:
            raise RuntimeError(f"Runtime profile links authoring modules: {leaked}")
    command = read_link_command(directory, cache, target, folder)
    measured = linked_modules(command, directory, cache, folder)
    if measured != configured:
        raise RuntimeError(
            f"Actual OBJECT link inputs differ: measured={measured}, configured={configured}"
        )
    binary = directory / "profile" / folder / target
    if not binary.is_file():
        raise RuntimeError("Real spawn test executable missing; build eve_profile_smoke first")
    nm = cache.get("CMAKE_NM")
    if not nm or nm.endswith("-NOTFOUND"):
        raise RuntimeError("Configured toolchain has no symbol reader")
    defined = subprocess.check_output(
        [nm, "--demangle", "--defined-only", str(binary)], encoding="utf-8"
    )
    imported = subprocess.check_output(
        [nm, "--demangle", "--undefined-only", str(binary)], encoding="utf-8"
    )
    if cache["BUILD_PLATFORM"] == "macosx":
        inspector = shutil.which("otool")
        if not inspector:
            raise RuntimeError("otool missing: dynamic dependencies cannot be verified")
        dependencies = subprocess.check_output([inspector, "-L", str(binary)], encoding="utf-8")
    else:
        inspector = cache.get("CMAKE_READELF") or shutil.which("readelf")
        if not inspector or inspector.endswith("-NOTFOUND"):
            raise RuntimeError("readelf missing: dynamic dependencies cannot be verified")
        dependencies = subprocess.check_output([inspector, "-d", str(binary)], encoding="utf-8")
    if cache["BUILD_PLATFORM"] == "macosx":
        needed = re.findall(r"^\s+(.+?)\s+\(compatibility", dependencies, re.MULTILINE)
    else:
        needed = re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", dependencies)
    dependency_names = [Path(name).name for name in needed]
    witnesses = verify_symbols(profile, defined, imported, "\n".join(dependency_names))
    with binary.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    output.mkdir(exist_ok=True)
    (output / "defined-symbols.txt").write_text(defined, encoding="utf-8")
    (output / "imported-symbols.txt").write_text(imported, encoding="utf-8")
    (output / "dynamic-dependencies.txt").write_text(dependencies, encoding="utf-8")
    (output / "link-command.txt").write_text(command, encoding="utf-8")
    report = {
        "schema": "eve.spawn-link-boundary",
        "version": 1,
        "profile": profile,
        "linkage": "OBJECT",
        "platform": cache["BUILD_PLATFORM"],
        "configured_modules": configured,
        "linked_modules": measured,
        "implementation_witnesses": witnesses,
        "dynamic_dependencies": dependency_names,
        "binary": str(binary),
        "binary_sha256": digest,
        "link_command_sha256": hashlib.sha256(command.encode("utf-8")).hexdigest(),
        "evidence": str(output),
        "ok": True,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report
