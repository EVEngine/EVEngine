"""Run the real snow example with deterministic interaction regression checks."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eve", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/snow-regression"))
    parser.add_argument("--validation", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for helper in (root / "examples/snow").glob("*.nut"):
        if helper.name not in ("main.nut", "config.nut"):
            (output / helper.name).write_text(helper.read_text(encoding="utf-8"), encoding="utf-8")
    source = (root / "examples/snow/main.nut").read_text(encoding="utf-8")
    fixture = (root / "test/snow_example.nut").read_text(encoding="utf-8")
    (output / "main.nut").write_text(source + "\n" + fixture, encoding="utf-8")
    (output / "config.nut").write_text(
        'config = { width=960 height=640 title="snow regression" hotReload=false };\n',
        encoding="utf-8",
    )
    env = os.environ.copy()
    env["EVENGINE_VULKAN_VALIDATION"] = "1" if args.validation else "0"
    with (output / "run.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(
            [str(args.eve.resolve()), "run", str(output)], cwd=root, env=env,
            stdout=log, stderr=subprocess.STDOUT, timeout=120,
        )
    text = (output / "run.log").read_text(encoding="utf-8", errors="replace")
    failures = [line for line in text.splitlines() if any(
        marker in line for marker in ("SNOW_FAIL", "frame error:", "[ERROR: Validation]"))]
    if result.returncode or "SNOW_PASS:" not in text or failures:
        print("Snow regression failed:", result.returncode, *failures[:12], sep="\n")
        print("Log:", output / "run.log")
        return 1
    print("Snow regression passed; captures and log:", output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
