#!/usr/bin/env python3
"""Check that critical return values produce compiler diagnostics.

This is a deliberately small compile-fail gate. It does not build the engine
or link a test binary, so it can run in the fast structural CI job. The
fixtures exercise the public Result, Squirrel Value conversion, Subscription,
and UUID parse contracts.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


FIXTURES = {
    "result": r'''
#include "common/Result.h"

void discard_result() {
    eve::Result<void>::success();
}
''',
    "squirrel_value": r'''
#include "common/SquirrelBinding.h"

void discard_squirrel_value_conversion(HSQUIRRELVM vm) {
    eve::script::valueFromSquirrel(vm, -1);
}
''',
    "subscription": r'''
#include "common/Subscription.h"

void discard_subscription() {
    eve::Observer<int> observer;
    observer.subscribe([](const int&) {});
}
''',
    "identity": r'''
#include "common/Identity.h"

void discard_identity_parse() {
    eve::AssetGuid::parse("01020304-0506-0708-090a-0b0c0d0e0f10");
}
''',
    "scene_ownership": r'''
#include "scene/SceneHost.h"

void discard_scene_ownership() {
    eve::scene::SceneHost::createHost();
}
''',
}


def compiler_command() -> list[str] | None:
    requested = os.environ.get("CXX", "").strip()
    candidates = [requested] if requested else ["c++", "g++", "clang++"]
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return [found]
        if Path(candidate).exists():
            return [candidate]
    return None


def check_fixture(compiler: list[str], source_dir: Path, name: str, source: str) -> tuple[bool, str]:
    with tempfile.TemporaryDirectory(prefix="evengine-nodiscard-") as temp:
        source_path = Path(temp) / f"{name}.cpp"
        source_path.write_text(source, encoding="utf-8")
        command = compiler + [
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror=unused-result",
            "-fsyntax-only",
            "-I",
            str(source_dir / "src"),
            "-I",
            str(source_dir / "external" / "zeroerr" / "include"),
            "-I",
            str(source_dir / "external" / "ECS.hpp" / "src"),
            "-I",
            str(source_dir / "third-party" / "glm"),
            "-I",
            str(source_dir / "third-party" / "squirrel" / "include"),
            "-I",
            str(source_dir / "third-party" / "simplesquirrel" / "include"),
            str(source_path),
        ]
        completed = subprocess.run(command, text=True, capture_output=True, check=False)
        diagnostics = completed.stdout + completed.stderr
        return completed.returncode != 0 and (
            "nodiscard" in diagnostics.lower() or "unused-result" in diagnostics.lower()
        ), diagnostics


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    source_dir = args.source_dir.resolve()
    compiler = compiler_command()
    if compiler is None:
        print(
            "warning: no C++ compiler found (c++/g++/clang++, or CXX); "
            "skipping nodiscard diagnostic gate",
            file=sys.stderr,
        )
        return 0

    failed = False
    for name, source in FIXTURES.items():
        passed, diagnostics = check_fixture(compiler, source_dir, name, source)
        if not passed:
            failed = True
            print(f"FAIL nodiscard fixture: {name}", file=sys.stderr)
            print(diagnostics, file=sys.stderr)
        else:
            print(f"nodiscard fixture OK: {name}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
