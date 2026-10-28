"""Generate paintable planar patches with explicit world-space UV frames."""
from pathlib import Path
import json
import math

ROOT = Path(__file__).parent


def generate():
    patches = []

    def face(origin, u, v):
        patches.append([origin, u, v])

    face([-6, 0, 5], [12, 0, 0], [0, 0, -10])
    face([-6, 0, -5], [12, 0, 0], [0, 3.5, 0])
    face([-6, 0, 5], [0, 0, -10], [0, 3.5, 0])
    # Broad ramp and elevated landing.
    face([0.5, 0.02, 2], [3.5, 0, 0], [0, 1.8, -4])
    face([0.5, 1.82, -2], [3.5, 0, 0], [0, 0, -2])
    face([4, 0, -4], [0, 0, 2], [0, 1.82, 0])
    # Faceted cylinder: world projection crosses all UV seams.
    for i in range(16):
        b, a = i * math.tau / 16, (i + 1) * math.tau / 16
        p = [-2.3 + math.cos(a), 0, -1.5 + math.sin(a)]
        q = [-2.3 + math.cos(b), 0, -1.5 + math.sin(b)]
        face(p, [q[j] - p[j] for j in range(3)], [0, 2.7, 0])
    face([-3.3, 2.7, -0.5], [2, 0, 0], [0, 0, -2])
    for index, (p, u, v) in enumerate(patches):
        n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        length = math.sqrt(sum(x*x for x in n))
        n = [x / length for x in n]
        vertices = [p, [p[j]+u[j] for j in range(3)],
                    [p[j]+v[j] for j in range(3)], [p[j]+u[j]+v[j] for j in range(3)]]
        text = ''.join('v ' + ' '.join(map(str, x)) + '\n' for x in vertices)
        text += 'vt 0 1\nvt 1 1\nvt 0 0\nvt 1 0\n'
        text += 'vn ' + ' '.join(map(str, n)) + '\n'
        text += 'f 1/1/1 2/2/1 3/3/1\nf 3/3/1 2/2/1 4/4/1\n'
        if index == len(patches) - 1:
            text = ''
            for k in range(17):
                a = k * math.tau / 16
                uv = [0.5, 0.5] if k == 16 else [0.5+0.5*math.cos(a), 0.5+0.5*math.sin(a)]
                position = [p[j]+u[j]*uv[0]+v[j]*uv[1] for j in range(3)]
                text += 'v ' + ' '.join(map(str, position)) + '\n'
                text += f'vt {uv[0]} {1-uv[1]}\n'
            text += 'vn 0 1 0\n'
            for k in range(16):
                a, b = k+1, (k+1)%16+1
                text += f'f 17/17/1 {a}/{a}/1 {b}/{b}/1\n'
        (ROOT / 'assets' / f'patch-{index}.obj').write_text(text)
    (ROOT / 'scene.nut').write_text('sceneFrames <- ' + json.dumps(patches) + ';\n')


if __name__ == '__main__':
    generate()
