"""Run the independent sky with a local external pack or built-in procedural assets."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import utf8_stdio  # noqa: E402


def prepare_game(destination: Path, pack: Path | None, seed: int, hour: float | None) -> None:
    """Prepare only scripts/config; mount external resources read-only at runtime."""
    if not 0 <= seed <= 0xFFFFFFFF:
        raise ValueError("seed must be an unsigned 32-bit integer")
    if hour is not None and not 0 <= hour < 24:
        raise ValueError("hour must be in [0,24)")
    base = ROOT / "examples/uds-sky"
    profile_path = pack / "sky.json" if pack else base / "sky-procedural.json"
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    script = (base / "main.nut").read_text(encoding="utf-8")
    if pack:
        resource = profile.get("wispsAsset")
        if not isinstance(resource, str) or not resource or "\\" in resource or ":" in resource:
            raise ValueError("pack sky.json requires a relative wispsAsset manifest")
        if resource.startswith("/") or any(part in (".", "..", "") for part in resource.split("/")):
            raise ValueError("pack manifest must stay inside the pack")
        if not (pack / resource).is_file():
            raise ValueError("pack manifest does not exist")
        profile["wispsAsset"] = "sky-pack/" + resource
        script = script.replace('persist skyPackDirectory = ""',
                                "persist skyPackDirectory = " + json.dumps(pack.resolve().as_posix(), ensure_ascii=False))
    else:
        profile["proceduralSeed"] = seed
    if hour is not None:
        profile["clock"] = {"initialHour": hour, "hoursPerSecond": 0}
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(base / "config.nut", destination / "config.nut")
    (destination / "main.nut").write_text(script, encoding="utf-8")
    (destination / "sky-procedural.json").write_text(json.dumps(profile, indent=2), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, help="External folder containing sky.json and its resources")
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--hour", type=float, help="Freeze hour for inspection; otherwise run the day cycle")
    parser.add_argument("--engine", type=Path, default=Path(os.environ.get(
        "EVE", str(ROOT / "build/win32-debug/src/engine/eve.exe"))))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="eve-sky-") as directory:
        prepare_game(Path(directory), args.pack, args.seed, args.hour)
        result = subprocess.run([str(args.engine.resolve()), "run", directory], check=False)
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
