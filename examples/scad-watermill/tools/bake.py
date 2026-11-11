#!/usr/bin/env python3
"""Bake SCAD watermill parts to OBJ (and optionally native .eva via eve asset import).

Always regenerates the authoritative SCAD/rig/OBJ sources from design.py first.
When OpenSCAD is available, each part is also compiled with -D part="..." to verify
the SCAD modules match the committed OBJ batches.
"""
from __future__ import annotations

import argparse
import math
import shutil
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
sys.path.insert(0, str(REPO / "scripts"))
import utf8_stdio  # noqa: E402

PARTS = ("building", "race", "sluice", "drive", "main", "runner", "hoist")


def write_obj(path: Path, vertices):
    with path.open("w", encoding="utf-8") as out:
        out.write("# Generated from mill.scad; metres, Y-up\ns off\n")
        for v in vertices:
            out.write("v %.7g %.7g %.7g\n" % v)
        for i in range(0, len(vertices), 3):
            a, b, c = vertices[i : i + 3]
            u = [b[j] - a[j] for j in range(3)]
            v = [c[j] - a[j] for j in range(3)]
            normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            length = math.sqrt(sum(n * n for n in normal))
            if length <= 1e-12:
                raise ValueError(f"{path}: degenerate triangle {i // 3}")
            out.write("vn %.9g %.9g %.9g\n" % tuple(n / length for n in normal))
        for i in range(0, len(vertices), 3):
            n = i // 3 + 1
            out.write(f"f {i + 1}//{n} {i + 2}//{n} {i + 3}//{n}\n")


def bake_openscad(openscad: str, parts):
    source = ROOT / "assets" / "mill.scad"
    with tempfile.TemporaryDirectory() as tmp:
        for part in parts:
            stl = Path(tmp) / f"{part}.stl"
            subprocess.run(
                [
                    openscad,
                    "--hardwarnings",
                    "--export-format",
                    "asciistl",
                    "-D",
                    f'part="{part}"',
                    "-o",
                    str(stl),
                    str(source),
                ],
                check=True,
                timeout=360,
            )
            vertices = []
            for line in stl.read_text(encoding="utf-8").splitlines():
                fields = line.split()
                if fields and fields[0] == "vertex":
                    x, y, z = map(float, fields[1:])
                    vertices.append((x / 1000, z / 1000, -y / 1000))
            if not vertices or len(vertices) % 3:
                raise ValueError(f"{part}: invalid triangle stream")
            write_obj(ROOT / "assets" / f"{part}.obj", vertices)
            print(f"{part}: {len(vertices) // 3} triangles (OpenSCAD)", flush=True)


def bake_eva(eve: str, openscad: str):
    source = ROOT / "assets" / "mill.scad"
    package = str(uuid.uuid5(uuid.NAMESPACE_URL, "evengine:scad-watermill:mill"))
    eva = ROOT / "assets" / "mill.eva"
    subprocess.run(
        [
            eve,
            "asset",
            "import",
            str(source),
            "--from",
            "scad",
            "--openscad",
            openscad,
            "--out",
            str(eva),
            "--package-id",
            package,
            "--name",
            "watermill.mill",
        ],
        check=True,
    )
    subprocess.run([eve, "asset", "validate", str(eva)], check=True)
    print(f"wrote {eva}", flush=True)


def main(argv: list[str] | None = None) -> int:
    utf8_stdio.enable_utf8_stdio()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openscad", default=shutil.which("openscad"))
    parser.add_argument("--part", choices=PARTS, action="append")
    parser.add_argument("--eve", help="Also import multi-part .eva with ScadRig")
    parser.add_argument(
        "--from-design-only",
        action="store_true",
        help="Skip OpenSCAD verification and keep design.py OBJ emitters",
    )
    args = parser.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import design  # noqa: WPS433

    design.main()
    parts = tuple(args.part) if args.part else PARTS
    if not args.from_design_only:
        if not args.openscad:
            parser.error("Install OpenSCAD or pass --openscad / --from-design-only")
        bake_openscad(str(Path(args.openscad).resolve()), parts)
    if args.eve:
        if not args.openscad:
            parser.error("--eve requires OpenSCAD")
        bake_eva(str(Path(args.eve).resolve()), str(Path(args.openscad).resolve()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
