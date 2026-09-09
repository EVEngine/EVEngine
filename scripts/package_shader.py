#!/usr/bin/env python3
"""Prepare an EVEngine shader definition from validated SPIR-V stage files.

The resulting JSON is consumed by `eve asset import --from shader`. This tool does not
translate Unity ShaderLab or infer graphics binding layouts.
"""

import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vertex", type=Path, required=True)
    parser.add_argument("--fragment", type=Path, required=True)
    parser.add_argument("--interface", choices=("mesh3d", "sprite2d"), required=True)
    parser.add_argument("--parameters", type=Path, help='JSON array: [{"name":"strength","default":[1.0]}]')
    parser.add_argument("--spirv-val", default="spirv-val")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    stages = {}
    for name, path in (("vertex", args.vertex), ("fragment", args.fragment)):
        data = path.read_bytes()
        if not data or len(data) % 4 or len(data) > 1024 * 1024:
            parser.error(f"{name}: expected at most 1 MiB of SPIR-V words")
        # Validate the exact immutable bytes that are packaged, avoiding a source-file race.
        with tempfile.TemporaryDirectory(prefix="eve-shader-") as directory:
            staged = Path(directory) / "stage.spv"
            staged.write_bytes(data)
            subprocess.run([args.spirv_val, "--target-env", "vulkan1.2", str(staged)], check=True)
        stages[name] = list(struct.unpack(f"<{len(data) // 4}I", data))
    parameters = json.loads(args.parameters.read_text(encoding="utf-8")) if args.parameters else []
    definition = json.dumps({"schema": "eve.shader", "schemaVersion": 1,
                             "interface": args.interface, "format": "spirv-1.6",
                             **stages, "parameters": parameters},
                            sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    # Archive creation stays in the canonical native importer and publisher.
    with tempfile.TemporaryDirectory(prefix="eve-shader-publish-", dir=args.output.parent) as directory:
        candidate = Path(directory) / "candidate.json"
        candidate.write_bytes(definition)
        candidate.replace(args.output)
    print(args.output)


if __name__ == "__main__":
    main()
