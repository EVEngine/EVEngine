"""Run the sample's native GAS adapters without a display or network access."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    args = parser.parse_args()
    engine = args.engine.resolve()
    sample = Path(__file__).resolve().parents[1]
    # The engine compiles the complete root with EveScript extensions, including
    # persist. No gameplay implementation or provider is duplicated in this test.
    source = (
        'eve._bindAllNativeClasses();\n'
        'fs <- eve.Filesystem();\nattributes <- eve.Attributes();\n'
        'eve_init <- null; eve_update <- null; eve_render <- null;\n'
        + (sample / "main.nut").read_text(encoding="utf-8")
        + "\n"
        + (sample / "tests/runtime_checks.nut").read_text(encoding="utf-8")
    )
    with tempfile.TemporaryDirectory(prefix="arena-gas-") as folder:
        root = Path(folder) / "checks.nut"
        root.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(engine), "run", "--no-window", "--root", str(root)],
            cwd=sample,
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
            check=False,
        )
    output = result.stdout + result.stderr
    print(output)
    if result.returncode or "ALL ARENA GAS API CHECKS PASSED" not in output:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
