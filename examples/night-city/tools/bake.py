"""Bake original SCAD kit into portable OBJ batches; OpenSCAD is authoring-only."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openscad", default="openscad")
    args = parser.parse_args()
    assets = Path(__file__).resolve().parents[1] / "assets"
    for part in ["tower", "lit", "dark", "tower-high", "lit-high", "dark-high",
                 "lamp", "lamp-glow", "signal", "lens"]:
        with tempfile.TemporaryDirectory(dir=assets) as temp:
            stl = Path(temp) / "part.stl"
            subprocess.run([args.openscad, "--hardwarnings", "--export-format", "binstl",
                            "-D", f'part="{part}"', "-o", str(stl), str(assets / "city.scad")],
                           check=True, timeout=240)
            data = stl.read_bytes()
            count = struct.unpack_from("<I", data, 80)[0]
            if len(data) != 84 + 50 * count:
                raise ValueError("Invalid STL triangle stream")
            lines = ["# Original city.scad geometry; metres, Y-up", "s off"]
            for i in range(count):
                values = struct.unpack_from("<12f", data, 84 + i * 50)
                nx, ny, nz = values[:3]
                lines.append(f"vn {nx:.7g} {nz:.7g} {-ny:.7g}")
                for j in [3, 6, 9]:
                    x, y, z = values[j:j + 3]
                    lines.append(f"v {x:.7g} {z:.7g} {-y:.7g}")
                n = i * 3 + 1
                lines.append(f"f {n}//{i+1} {n+1}//{i+1} {n+2}//{i+1}")
            (assets / f"{part}.obj").write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"{part}: {count} triangles", flush=True)


if __name__ == "__main__":
    main()
