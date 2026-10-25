#!/usr/bin/env python3
"""Regression tests for third-party patch roots and idempotent application.

The temporary checkouts contain pinned HEAD blobs from the local sources;
this test never contacts a remote repository and never mutates the checkout
used by the engine build.
"""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PATCH_SCRIPT = ROOT / "cmake" / "patch_third_party.cmake"
PATCHES = ROOT / "cmake" / "patches"
THIRD_PARTY = ROOT / "third-party"
MEDIALOADER = THIRD_PARTY / "medialoader"
ECS_SUBMODULE = ROOT / "external" / "ECS.hpp"


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, encoding="utf-8", capture_output=True, check=False)
    if check and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def checkout_patch_inputs(source: Path, destination: Path, patches: list[Path]) -> None:
    """Materialize real upstream inputs without copying unrelated files or history.

    Both the inputs and the patch copy used below are written with LF endings.
    git apply compares bytes, and a patch file's checkout form depends on
    .gitattributes and core.autocrlf (cmake/patches/ecs-shared-default-table.patch
    is pinned to CRLF because external/ECS.hpp commits CRLF), so exercising that
    pairing is the real patch step's job -- this test covers idempotence and drift
    detection, which must hold on every host.
    """
    paths = {"CMakeLists.txt"}
    for patch in patches:
        for line in patch.read_text(encoding="utf-8").splitlines():
            if line.startswith("--- a/"):
                paths.add(line.removeprefix("--- a/"))
    destination.mkdir()
    for relative in sorted(paths):
        blob = subprocess.run(
            ["git", "-C", str(source), "show", f"HEAD:{relative}"],
            capture_output=True, check=True, text=True, encoding="utf-8",
        ).stdout
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(blob.replace("\r\n", "\n"), encoding="utf-8", newline="\n")
    run(["git", "-C", str(destination), "init", "--quiet"])
    run(["git", "-C", str(destination), "add", "."])
    run(["git", "-C", str(destination), "-c", "user.name=Fixture",
         "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
         "commit", "--quiet", "-m", "Pinned patch inputs"])
    assert not run(["git", "-C", str(destination), "status", "--porcelain"]).stdout


def apply_twice(patch: Path, patch_dir: Path, *, normalize_crlf: bool = False,
                already_applied: bool = False) -> None:
    # Apply the same patch content with LF endings, matching the inputs above.
    staged = patch_dir.parent / f"{patch.name}.lf"
    staged.write_text(patch.read_text(encoding="utf-8").replace("\r\n", "\n"),
                      encoding="utf-8", newline="\n")
    command = [
        "cmake",
        f"-DPATCH={staged}",
        f"-DPATCH_DIR={patch_dir}",
    ]
    if normalize_crlf:
        command.append("-DPATCH_NORMALIZE_CRLF=ON")
    command.extend(["-P", str(PATCH_SCRIPT)])
    first = run(command)
    second = run(command)
    assert ("already applied" if already_applied else "Applied third-party patch") in first.stdout
    assert "already applied" in second.stdout


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="evengine-third-party-patch-") as raw:
        temporary = Path(raw)

        # external/ECS.hpp uses the same script from the engine's configure step.
        clean_ecs = temporary / "ecs-clean"
        checkout_patch_inputs(ECS_SUBMODULE, clean_ecs,
                              [PATCHES / "ecs-shared-default-table.patch",
                               PATCHES / "ecs-query-and-record-lifetime.patch"])
        apply_twice(PATCHES / "ecs-shared-default-table.patch", clean_ecs)
        apply_twice(PATCHES / "ecs-view-preparation.patch", clean_ecs)
        apply_twice(PATCHES / "ecs-idle-commit.patch", clean_ecs)
        apply_twice(PATCHES / "ecs-view-scope-and-iteration.patch", clean_ecs)
        apply_twice(PATCHES / "ecs-view-live-slots.patch", clean_ecs)
        apply_twice(PATCHES / "ecs-query-and-record-lifetime.patch", clean_ecs)
        # Replaying the complete configure sequence must stay idempotent after
        # later patches have changed nearby declarations in the same header.
        for name in ("ecs-shared-default-table", "ecs-view-preparation", "ecs-idle-commit",
                     "ecs-view-scope-and-iteration", "ecs-view-live-slots",
                     "ecs-query-and-record-lifetime"):
            apply_twice(PATCHES / f"{name}.patch", clean_ecs, already_applied=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
