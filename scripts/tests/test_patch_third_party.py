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


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if check and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def checkout_patch_inputs(source: Path, destination: Path, patches: list[Path]) -> None:
    """Materialize real upstream inputs without copying unrelated files or history."""
    paths = {"CMakeLists.txt"}
    for patch in patches:
        for line in patch.read_text(encoding="utf-8").splitlines():
            if line.startswith("--- a/"):
                paths.add(line.removeprefix("--- a/"))
    destination.mkdir()
    for relative in sorted(paths):
        blob = subprocess.run(
            ["git", "-C", str(source), "show", f"HEAD:{relative}"],
            capture_output=True, check=True,
        ).stdout
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
    run(["git", "-C", str(destination), "init", "--quiet"])
    run(["git", "-C", str(destination), "add", "."])
    run(["git", "-C", str(destination), "-c", "user.name=Fixture",
         "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
         "commit", "--quiet", "-m", "Pinned patch inputs"])
    assert not run(["git", "-C", str(destination), "status", "--porcelain"]).stdout


def apply_twice(patch: Path, patch_dir: Path) -> None:
    command = [
        "cmake",
        f"-DPATCH={patch}",
        f"-DPATCH_DIR={patch_dir}",
        "-P",
        str(PATCH_SCRIPT),
    ]
    first = run(command)
    second = run(command)
    assert "Applied third-party patch" in first.stdout
    assert "already applied" in second.stdout


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="evengine-third-party-patch-") as raw:
        temporary = Path(raw)

        clean_aggregate = temporary / "aggregate-clean"
        checkout_patch_inputs(THIRD_PARTY, clean_aggregate,
                              [PATCHES / "third-party-squirrel-ssq-export.patch"])
        apply_twice(PATCHES / "third-party-squirrel-ssq-export.patch", clean_aggregate)

        # A dirty aggregate checkout is a supported state because other patches
        # and profile preparation may have changed unrelated files. The export
        # patch must remain applicable when its own target files are untouched.
        dirty_aggregate = temporary / "aggregate-dirty"
        checkout_patch_inputs(THIRD_PARTY, dirty_aggregate,
                              [PATCHES / "third-party-squirrel-ssq-export.patch"])
        with (dirty_aggregate / "CMakeLists.txt").open("a", encoding="utf-8") as output:
            output.write("\n# Unrelated local profile configuration\n")
        assert run(["git", "-C", str(dirty_aggregate), "diff", "--name-only"]).stdout.strip() == "CMakeLists.txt"
        apply_twice(PATCHES / "third-party-squirrel-ssq-export.patch", dirty_aggregate)

        clean_medialoader = temporary / "medialoader-clean"
        medialoader_patches = [
            PATCHES / "medialoader-smooth-normals.patch",
            PATCHES / "mpg123-signal-handler.patch",
            PATCHES / "medialoader-image-formats.patch",
            PATCHES / "medialoader-audio-gapless.patch",
        ]
        checkout_patch_inputs(MEDIALOADER, clean_medialoader, medialoader_patches)
        for patch in medialoader_patches:
            apply_twice(patch, clean_medialoader)

        # A genuine target drift must fail and expose git's diagnostic; it may
        # not be mistaken for an already-applied patch or silently skipped.
        drifted = temporary / "aggregate-drifted"
        checkout_patch_inputs(THIRD_PARTY, drifted,
                              [PATCHES / "third-party-squirrel-ssq-export.patch"])
        target = drifted / "squirrel" / "squirrel" / "CMakeLists.txt"
        target.write_text(
            target.read_text(encoding="utf-8").replace(
                "target_include_directories(squirrel_static PUBLIC",
                "target_include_directories(squirrel_static BROKEN",
                1,
            ),
            encoding="utf-8",
        )
        result = run(
            [
                "cmake",
                f"-DPATCH={PATCHES / 'third-party-squirrel-ssq-export.patch'}",
                f"-DPATCH_DIR={drifted}",
                "-P",
                str(PATCH_SCRIPT),
            ],
            check=False,
        )
        diagnostic = result.stdout + result.stderr
        assert result.returncode != 0
        assert "no longer applies cleanly" in diagnostic
        assert "git apply --check output" in diagnostic
        assert "patch failed" in diagnostic

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
