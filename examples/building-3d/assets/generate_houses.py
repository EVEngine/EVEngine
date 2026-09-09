"""Deterministic, original low-poly OBJ fixtures; Python standard library only."""
from pathlib import Path
from math import sqrt


class House:
    def __init__(self):
        self.lines = ["# Original EVEngine test asset; CC0-1.0", "o house", "s off"]
        self.vertices = 0
        self.normals = 0

    def face(self, points):
        a, b, c = points[:3]
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        length = sqrt(sum(x*x for x in n))
        assert length > 1e-8
        self.normals += 1
        self.lines.append("vn " + " ".join(f"{x/length:.6f}" for x in n))
        start = self.vertices + 1
        for x, y, z in points:
            self.lines.append(f"v {x:.6f} {y-.5:.6f} {z:.6f}")
            self.vertices += 1
        for i in range(1, len(points)-1):
            self.lines.append("f " + " ".join(f"{j}//{self.normals}" for j in (start, start+i, start+i+1)))

    def box(self, x0, x1, y0, y1, z0, z1):
        v = [(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),
             (x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
        for indices in ((3,2,1,0),(4,5,6,7),(0,4,7,3),(2,6,5,1),(7,6,2,3),(0,1,5,4)):
            self.face([v[i] for i in indices])

    def gable(self, x0, x1, z0, z1, eave, ridge):
        mid = (x0+x1)/2
        self.face([(x0,eave,z0),(mid,ridge,z0),(x1,eave,z0)])
        self.face([(x1,eave,z1),(mid,ridge,z1),(x0,eave,z1)])
        self.face([(x0,eave,z1),(mid,ridge,z1),(mid,ridge,z0),(x0,eave,z0)])
        self.face([(x1,eave,z0),(mid,ridge,z0),(mid,ridge,z1),(x1,eave,z1)])
        self.face([(x0,eave,z0),(x1,eave,z0),(x1,eave,z1),(x0,eave,z1)])

    def details(self):
        # Raised door and window frames remain legible without textures/material loading.
        self.box(-.09,.09,0,.34,.401,.43)
        for x in (-.26,.26):
            self.box(x-.075,x+.075,.26,.43,.401,.42)
            self.box(x-.085,x+.085,.245,.265,.40,.44)

    def save(self, name):
        Path(__file__).with_name(name+'.obj').write_text('\n'.join(self.lines)+'\n', encoding='ascii')


def generate():
    cottage = House()
    cottage.box(-.4,.4,0,.6,-.4,.4)
    cottage.gable(-.48,.48,-.47,.47,.6,.94)
    cottage.box(.19,.29,.65,1,-.24,-.12)
    cottage.details()
    cottage.save('cottage')

    barn = House()
    barn.box(-.4,.4,0,.56,-.4,.4)
    barn.gable(-.48,.48,-.48,.48,.56,1)
    barn.box(-.22,.22,0,.43,.401,.43)
    barn.box(-.015,.015,0,.43,.43,.45)
    barn.box(-.27,.27,.43,.47,.40,.46)
    barn.save('barn')

    pavilion = House()
    pavilion.box(-.4,.4,0,.6,-.4,.4)
    peak = (0,1,0)
    corners = [(-.48,.6,-.48),(-.48,.6,.48),(.48,.6,.48),(.48,.6,-.48)]
    for i in range(4):
        pavilion.face([corners[i],corners[(i+1)%4],peak])
    pavilion.face(list(reversed(corners)))
    pavilion.details()
    pavilion.save('pavilion')


if __name__ == '__main__':
    generate()
