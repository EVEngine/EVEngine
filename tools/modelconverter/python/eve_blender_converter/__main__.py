"""CLI entry point: `python -m eve_blender_converter <command> <job.txt>`.

Commands:
    convert <job.txt>   Run a converter job and write <job>.result.txt
    check               Verify bpy availability and print status
    list <converter_dir> List converters in a directory
"""

from __future__ import annotations

import os
import sys


def _check():
    if _bpy_available():
        import bpy
        print(f"bpy OK ({bpy.app.version_string})")
        return 0
    print("bpy NOT AVAILABLE (pip install bpy)")
    return 1


def _list(converter_dir: str):
    from .job import list_matches
    for cid in list_matches(converter_dir):
        print(cid)
    return 0


def _bpy_available() -> bool:
    try:
        import bpy  # noqa: F401
        return True
    except Exception:
        return False


def _convert(job_path: str):
    from .job import Job, Result
    job = Job.from_file(job_path)
    # The plugin expects result at <job>.result.txt
    result_path = job_path + ".result.txt"
    if job.temp_dir:
        result_path = os.path.join(job.temp_dir, "modelconverter_result.txt")

    if not _bpy_available():
        result = Result()
        result.ok = False
        result.error = ("bpy is not installed/importable. Install it with: pip install bpy")
        result.write(result_path)
        if not os.path.exists(result_path):
            result.write(job_path + ".result.txt")
        return 1

    from .runtime import run
    result = run(job)
    result.write(result_path)
    if not os.path.exists(result_path):
        # Fallback next to the job file if temp_dir was unusable.
        result.write(job_path + ".result.txt")
    return 0 if result.ok else 1


def main(argv) -> int:
    if not argv:
        print(__doc__)
        return 0
    command = argv[0]
    if command == "check":
        return _check()
    if command == "list":
        return _list(argv[1] if len(argv) > 1 else ".")
    if command == "convert":
        if len(argv) < 2:
            print("usage: python -m eve_blender_converter convert <job.txt>")
            return 2
        return _convert(argv[1])
    print(f"unknown command '{command}'\n{__doc__}")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
