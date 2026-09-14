"""Author an original, Y-up OBJ cottage with modeled roof tiles and atlas UVs.

No procedural geometry is built at runtime. Run this script to regenerate the
editable OBJ/MTL assets. Units match the riverside map (32 units per tile).
"""
from collections import defaultdict
from pathlib import Path
import math
import zipfile

ROOT = Path(__file__).resolve().parent / "assets/house"
FACES = defaultdict(list)
MATERIALS = {
    "plaster": ((0.88, 0.77, 0.55), None),
    "timber": ((0.39, 0.24, 0.14), 12),
    "stone": ((0.70, 0.73, 0.76), 9),
    "brick": ((0.83, 0.68, 0.56), 10),
    "door": ((0.68, 0.43, 0.21), 12),
    "roof": ((0.60, 0.24, 0.14), None),
    "roof_light": ((0.65, 0.27, 0.16), None),
    "roof_dark": ((0.54, 0.20, 0.12), None),
    "glass": ((0.15, 0.34, 0.37), None),
    "brass": ((0.83, 0.59, 0.20), None),
}


def face(material, points):
    a, b, c = points[:3]
    u = [b[i]-a[i] for i in range(3)]
    v = [c[i]-a[i] for i in range(3)]
    n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
    length = math.sqrt(sum(x*x for x in n))
    assert length > 1e-6, "degenerate face"
    FACES[material].append((points, tuple(x/length for x in n)))


def box(material, x0, y0, z0, x1, y1, z1):
    face(material, [(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)])
    face(material, [(x1,y0,z0),(x0,y0,z0),(x0,y1,z0),(x1,y1,z0)])
    face(material, [(x1,y0,z1),(x1,y0,z0),(x1,y1,z0),(x1,y1,z1)])
    face(material, [(x0,y0,z0),(x0,y0,z1),(x0,y1,z1),(x0,y1,z0)])
    face(material, [(x0,y1,z1),(x1,y1,z1),(x1,y1,z0),(x0,y1,z0)])
    face(material, [(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1)])


def beam(a, b, radius=2.0):
    # A rectangular timber beam, including slanted gable braces.
    axis = [b[i]-a[i] for i in range(3)]
    length = math.sqrt(sum(x*x for x in axis))
    axis = [x/length for x in axis]
    side = [axis[1], -axis[0], 0]
    sn = math.sqrt(sum(x*x for x in side))
    if sn < 1e-6: side, sn = [1,0,0], 1
    side = [x*radius/sn for x in side]
    up = [axis[1]*side[2]-axis[2]*side[1], axis[2]*side[0]-axis[0]*side[2], axis[0]*side[1]-axis[1]*side[0]]
    p = [[tuple(end[i]+s*side[i]+t*up[i] for i in range(3)) for s,t in [(-1,-1),(1,-1),(1,1),(-1,1)]] for end in [a,b]]
    for j in range(4): face("timber", [p[0][j],p[0][(j+1)%4],p[1][(j+1)%4],p[1][j]])
    face("timber", list(reversed(p[0])))
    face("timber", p[1])


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    box("stone", -88,0,-42,88,10,42)
    box("plaster", -86,10,-40,86,78,40)
    for x in [-86, -30, 30, 86]: box("timber",x-2,8,-42,x+2,80,42)
    for y in [12,76]: box("timber",-89,y-2,-43,89,y+2,43)
    for x, flip in [(-86,False),(86,True)]:
        pts=[(x,78,-40),(x,78,40),(x,126,0)]
        face("plaster", list(reversed(pts)) if flip else pts)
        beam((x,78,-43),(x,128,0),2.4)
        beam((x,128,0),(x,78,43),2.4)
        beam((x,78,0),(x,124,0),2.1)
    # Roof slopes have individual overlapping, solid, beveled-looking slates.
    for sign in [-1,1]:
        for row in range(7):
            z0,z1 = row*7.5, (row+1)*7.5
            for col in range(16):
                x0,x1=-98+col*12.25+0.25,-98+(col+1)*12.25-0.25
                def point(x,z,h=0): return (x,130-z*0.92+h,sign*z)
                top=[point(x0,z0,1.5),point(x1,z0,1.5),point(x1,z1,1.5),point(x0,z1,1.5)]
                if sign > 0: top.reverse()
                variation=(col*17+row*29+col*row*3)%11
                material="roof_light" if variation<2 else "roof_dark" if variation>8 else "roof"
                face(material,top)
                lower=[(x,y-2,z) for x,y,z in top]
                face(material,list(reversed(lower)))
                for j in range(4): face(material,[top[j],lower[j],lower[(j+1)%4],top[(j+1)%4]])
        beam((-99,81.7,sign*52.5),(99,81.7,sign*52.5),2.5)
    box("roof_dark",-100,129,-2.5,100,134,2.5)
    # Front facade (+Z): recessed-looking door, lintel, shutters, cross bars.
    box("door",-14,10,40,14,59,43)
    for x in [-17,17]: box("timber",x-2,8,40,x+2,63,46)
    box("timber",-19,60,40,19,64,46)
    box("brass",8,31,43,11,35,46)
    for x in [-55,55]:
        box("glass",x-13,36,41,x+13,61,43)
        for sx in [-1,1]:
            box("door",x+sx*18-3,34,42,x+sx*18+3,63,45)
            box("timber",x+sx*14-1.5,34,42,x+sx*14+1.5,64,46)
        for y in [34,49,63]: box("timber",x-16,y-1,43,x+16,y+1,46)
        box("timber",x-1,34,43,x+1,64,46)
        box("stone",x-18,31,40,x+18,34,48)
    # Rear window and chimney make the model readable when orbiting.
    box("glass",-13,36,-42,13,60,-40)
    for x in [-15,0,15]: box("timber",x-1,34,-44,x+1,62,-40)
    for y in [34,48,62]: box("timber",-16,y-1,-44,16,y+1,-40)
    box("brick",42,101,-24,61,150,-6)
    box("stone",39,147,-27,64,153,-3)
    box("roof_dark",44,153,-22,59,154,-8)
    # Door canopy stays within the map footprint; collision remains map-owned.
    box("timber",-23,63,40,23,67,48)
    beam((-20,43,43),(-20,63,48),1.4)
    beam((20,43,43),(20,63,48),1.4)
    lines=["# Original EVEngine riverside cottage; Y up, front +Z, units=map units", "mtllib cottage.mtl"]
    vertex=1
    triangles=0
    for material, faces in FACES.items():
        lines += [f"o cottage_{material}",f"usemtl {material}","s off"]
        tile=MATERIALS[material][1]
        for points, normal in faces:
            uv=[(0,0),(1,0),(1,1),(0,1)][:len(points)]
            if tile:
                tx,ty=(tile-1)%4,(tile-1)//4
                uv=[((tx+0.02+u*0.96)/4,1-(ty+0.98-v*0.96)/4) for u,v in uv]
            for p,(u,v) in zip(points,uv):
                lines.append("v " + " ".join(f"{n:.6f}" for n in p))
                lines.append(f"vt {u:.6f} {v:.6f}")
                lines.append("vn " + " ".join(f"{n:.6f}" for n in normal))
            for j in range(1,len(points)-1):
                ids=[vertex,vertex+j,vertex+j+1]
                lines.append("f " + " ".join(f"{i}/{i}/{i}" for i in ids))
                triangles+=1
            vertex+=len(points)
    (ROOT/"cottage.obj").write_text("\n".join(lines)+"\n",encoding="utf-8")
    mtl=[]
    for name,(color,tile) in MATERIALS.items():
        mtl += [f"newmtl {name}","Kd " + " ".join(map(str,color)),"Ks 0 0 0","Ns 1","d 1","illum 2"]
        if tile: mtl.append("map_Kd ../terrain.png")
        mtl.append("")
    (ROOT/"cottage.mtl").write_text("\n".join(mtl),encoding="utf-8")
    with zipfile.ZipFile(ROOT/"cottage-model.zip", "w") as archive:
        for source, name in [(ROOT/"cottage.obj","house/cottage.obj"),
                             (ROOT/"cottage.mtl","house/cottage.mtl"),
                             (ROOT/"README.md","house/README.md"),
                             (ROOT.parent/"terrain.png","terrain.png")]:
            info=zipfile.ZipInfo(name,date_time=(2026,1,1,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,source.read_bytes())
    print(f"OBJ authored: {vertex-1} vertices, {triangles} triangles, {len(FACES)} material groups")


if __name__ == "__main__": main()
