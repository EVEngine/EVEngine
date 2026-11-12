"""Exercise GNU Make -> MSVC wrapper -> CMake/Ninja -> real NMake on Windows."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]


def main():
    if os.name != "nt":
        raise SystemExit("This integration check requires native Windows and Visual Studio.")
    make = shutil.which("make")
    cmake = shutil.which("cmake")
    if not make or not cmake:
        raise SystemExit("GNU Make and CMake must be installed.")
    with tempfile.TemporaryDirectory(prefix="eve nmake environment ") as directory:
        root = Path(directory).resolve()
        wrapper = ROOT / "cmake/with-msvc.cmd"
        (root / "Native.mk").write_text("probe:\n\t@echo NMAKE_ENVIRONMENT_OK\n", encoding="utf-8")
        (root / "CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.21)
project(NativeMakeEnvironment NONE)
find_program(NATIVE_NMAKE NAMES nmake REQUIRED)
add_custom_target(probe
    COMMAND "${NATIVE_NMAKE}" /NOLOGO /F "${CMAKE_CURRENT_SOURCE_DIR}/Native.mk" probe
    VERBATIM)
add_custom_target(raw
    COMMAND "${CMAKE_COMMAND}" -E env "MAKEFLAGS=${RAW_MAKEFLAGS}" "MFLAGS=${RAW_MFLAGS}"
        "${NATIVE_NMAKE}" /NOLOGO /F "${CMAKE_CURRENT_SOURCE_DIR}/Native.mk" probe
    VERBATIM)
""",
            encoding="utf-8",
        )
        # This child receives flags produced by a real GNU Make invocation.
        # Reinject those flags only into the negative control, then verify the
        # normal production launch boundary and the same native command.
        (root / "probe.py").write_text(
            f"""import json
import os
from pathlib import Path
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")
sys.stderr.reconfigure(encoding="utf-8")
root = Path({str(root)!r})
flags = {{key: os.environ.get(key, "") for key in ("MAKEFLAGS", "MFLAGS")}}
(root / "parent-flags.json").write_text(json.dumps(flags), encoding="utf-8")
assert "PROBE_ASSIGNMENT" in flags["MAKEFLAGS"], flags
wrapper = {str(wrapper)!r}
cmake = {cmake!r}

def run(*args, show=True):
    result = subprocess.run([wrapper, cmake, *args], capture_output=True,
                            encoding="utf-8", errors="replace", check=False, timeout=120)
    if show:
        print(result.stdout, end="")
        print(result.stderr, end="")
    return result

configured = run("-S", str(root), "-B", str(root / "build"), "-G", "Ninja",
                 "-DRAW_MAKEFLAGS:STRING=" + flags["MAKEFLAGS"],
                 "-DRAW_MFLAGS:STRING=" + flags["MFLAGS"])
assert configured.returncode == 0, "fixture configure failed"
raw = run("--build", str(root / "build"), "--target", "raw", show=False)
assert raw.returncode != 0, "negative control unexpectedly accepted GNU flags: " + raw.stdout + raw.stderr
assert "U1065" in raw.stdout + raw.stderr, "negative control did not reproduce NMake option parsing: " + raw.stdout + raw.stderr
clean = run("--build", str(root / "build"), "--target", "probe")
assert clean.returncode == 0, "MSVC wrapper leaked GNU flags"
assert "NMAKE_ENVIRONMENT_OK" in clean.stdout, "native NMake target did not execute"
assert flags == {{key: os.environ.get(key, "") for key in flags}}, "caller environment changed"
""",
            encoding="utf-8",
        )
        (root / "GNU.mk").write_text(
            f'.PHONY: verify\nverify:\n\t@"{Path(sys.executable).as_posix()}" "{(root / "probe.py").as_posix()}"\n',
            encoding="utf-8",
        )
        env = dict(os.environ)
        for key in ("MAKEFLAGS", "MFLAGS"):
            env.pop(key, None)
        result = subprocess.run(
            [
                make,
                "--no-print-directory",
                "-f",
                str(root / "GNU.mk"),
                "verify",
                "PROBE_ASSIGNMENT=probe value",
            ],
            cwd=root,
            env=env,
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            check=False,
            timeout=240,
        )
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if result.returncode:
            return result.returncode
        flags = json.loads((root / "parent-flags.json").read_text(encoding="utf-8"))
        print(f"GNU Make caller flags: {flags!r}")
        print("PASS: negative control rejected GNU flags; isolated MSVC launch executes real NMake")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
