#!/usr/bin/env python3
"""Compile original SCAD batches to portable OBJ (m, Y-up) for Model3D.

No third-party Python modules. OpenSCAD is an authoring dependency only.
Native .eva/.evpack export can additionally be requested with --eve.
"""
import argparse
import math
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

PARTS = ('plaster', 'oak', 'linen', 'terracotta', 'brass', 'charcoal',
         'ceramic', 'foliage', 'rug', 'shade', 'luminous', 'art', 'window-frame', 'glass', 'art-glass', 'tea-glass', 'celadon')
ROOT = Path(__file__).resolve().parents[1]


def write_obj(path, vertices):
    """Preserve STL face normals explicitly so Assimp cannot round architectural edges."""
    with path.open('w') as out:
        out.write('# Generated from living-room.scad; metres, Y-up\ns off\n')
        for v in vertices:
            out.write('v %.7g %.7g %.7g\n' % v)
        for i in range(0, len(vertices), 3):
            a, b, c = vertices[i:i+3]
            u = [b[j] - a[j] for j in range(3)]
            v = [c[j] - a[j] for j in range(3)]
            normal = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
            length = math.sqrt(sum(n*n for n in normal))
            if length <= 1e-12:
                raise ValueError(f'{path}: degenerate triangle {i//3}')
            out.write('vn %.9g %.9g %.9g\n' % tuple(n/length for n in normal))
        for i in range(0, len(vertices), 3):
            n = i//3 + 1
            out.write(f'f {i+1}//{n} {i+2}//{n} {i+3}//{n}\n')


def bake(openscad, eve, parts=PARTS):
    source = ROOT / 'assets' / 'living-room.scad'
    with tempfile.TemporaryDirectory() as tmp:
        for part in parts:
            stl = Path(tmp) / f'{part}.stl'
            subprocess.run([openscad, '--hardwarnings', '--export-format', 'asciistl',
                            '-D', f'part="{part}"', '-o', str(stl), str(source)],
                           check=True, timeout=240)
            vertices = []
            for line in stl.read_text().splitlines():
                fields = line.split()
                if fields and fields[0] == 'vertex':
                    x, y, z = map(float, fields[1:])
                    vertices.append((x / 1000, z / 1000, -y / 1000))
            if not vertices or len(vertices) % 3:
                raise ValueError(f'{part}: invalid triangle stream')
            obj = ROOT / 'assets' / f'{part}.obj'
            write_obj(obj, vertices)
            print(f'{part}: {len(vertices)//3} triangles', flush=True)
            if eve:
                # Keep each native source self-contained; STL carries no material data.
                wrapper = Path(tmp) / f'{part}.scad'
                wrapper.write_text(source.read_text().replace('part = "plaster";', f'part = "{part}";', 1))
                package = str(uuid.uuid5(uuid.NAMESPACE_URL, f'evengine:scad-living-room:{part}'))
                eva = ROOT / 'assets' / f'{part}.eva'
                subprocess.run([eve, 'asset', 'import', str(wrapper), '--from', 'scad',
                                '--openscad', openscad, '--out', str(eva),
                                '--package-id', package, '--name', f'living-room.{part}'], check=True)
                subprocess.run([eve, 'asset', 'validate', str(eva)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openscad', default=shutil.which('openscad'))
    parser.add_argument('--part', choices=PARTS, action='append', help='Rebuild only these batches (repeatable)')
    parser.add_argument('--eve', help='Also import and validate native .eva batches')
    args = parser.parse_args()
    if not args.openscad:
        parser.error('Install OpenSCAD or provide --openscad /path/to/openscad')
    bake(str(Path(args.openscad).resolve()), str(Path(args.eve).resolve()) if args.eve else None, args.part or PARTS)
