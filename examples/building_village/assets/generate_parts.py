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

    def save(self, name):
        Path(__file__).with_name(name+'.obj').write_text('\n'.join(self.lines)+'\n', encoding='utf-8')


def generate():
    wall = House()
    # Window aperture with two uprights, sill and lintel; solid collision is unchanged.
    wall.box(-.5,-.31,0,1,-.5,.5)
    wall.box(.31,.5,0,1,-.5,.5)
    wall.box(-.31,.31,0,.35,-.5,.5)
    wall.box(-.31,.31,.80,1,-.5,.5)
    wall.box(-.025,.025,.35,.80,-.30,.30)
    wall.box(-.31,.31,.55,.58,-.30,.30)
    wall.save('wall-window')

    chair = House()
    chair.box(-.5,.5,.38,.48,-.5,.5)
    for x in (-.43,.43):
        for z in (-.43,.43):
            chair.box(x-.07,x+.07,0,.38,z-.07,z+.07)
    chair.box(-.5,-.36,.48,1,-.5,-.35)
    chair.box(.36,.5,.48,1,-.5,-.35)
    chair.box(-.36,.36,.70,.80,-.5,-.35)
    chair.box(-.36,.36,.90,1,-.5,-.35)
    chair.save('chair')

    post = House()
    post.box(-.35,.35,0,1,-.35,.35)
    post.box(-.5,.5,0,.06,-.5,.5)
    post.box(-.5,.5,.94,1,-.5,.5)
    post.save('post')

    roof = House()
    roof.gable(-.5,.5,-.5,.5,0,1)
    roof.save('roof-gable')


if __name__ == '__main__':
    generate()
