// Project-local authoring recipe. Native SplinePath owns sampling, mesh.rock owns geometry.
// Inputs and outputs are owning values; synchronous VM-thread calls, no retained callbacks.
local need = function(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}

export function cliffDefaults() {
    return {schema="eve.curve-cliff", version=1, seed=1847, height=5.0,
        spacing=1.4, depth=1.6, variation=0.35, closed=false,
        points=[[-12.0, -2.0], [-6.0, 1.0], [0.0, -1.0], [6.0, 3.0], [12.0, 0.0]]};
}

local bounded = function(v, lo, hi) {
    return (typeof v == "integer" || typeof v == "float") && v >= lo && v <= hi;
};

local validate = function(d) {
    if (typeof d != "table") throw "cliff document must be an object";
    local fields = ["schema", "version", "seed", "height", "spacing", "depth", "variation", "closed", "points"];
    if (d.len() != fields.len()) throw "unknown or missing cliff fields";
    foreach (key in fields) if (!(key in d)) throw "missing cliff field: " + key;
    if (d.schema != "eve.curve-cliff" || d.version != 1) throw "unsupported cliff schema/version";
    if (typeof d.seed != "integer" || !bounded(d.seed, 1, 1000000)) throw "seed must be 1..1000000";
    if (!bounded(d.height, 1.0, 12.0) || !bounded(d.spacing, 0.7, 3.0) ||
        !bounded(d.depth, 0.6, 3.0) || !bounded(d.variation, 0.0, 0.7)) throw "cliff settings out of range";
    if (typeof d.closed != "bool" || typeof d.points != "array" || d.points.len() < 3 || d.points.len() > 16)
        throw "cliff requires 3..16 control points";
    foreach (i, p in d.points) {
        if (typeof p != "array" || p.len() != 2 || !bounded(p[0], -16.0, 16.0) || !bounded(p[1], -10.0, 10.0))
            throw "point outside the 32 x 20 metre editing area";
        if (i > 0 || d.closed) {
            local q = d.points[(i + d.points.len() - 1) % d.points.len()];
            if (typeof q != "array" || q.len() != 2) throw "invalid neighbouring point";
            local dx = p[0] - q[0], dz = p[1] - q[1];
            if (dx * dx + dz * dz < 0.04) throw "neighbouring points must be 0.2 metres apart";
        }
    }
}

// Independent named channels: mesh=11, width=23, height=37, offset=53, yaw=71.
// Bounded integer arithmetic also works in 32-bit Squirrel builds. No wall clock/global RNG.
local randomAt = function(seed, index, channel) {
    local n = (seed % 65521 + (index % 8192) * 131 + channel * 719) % 65521;
    n = (n * 251 + 13849) % 65521;
    n = n ^ (n >> 7);
    n = (n * 251 + 13849) % 65521;
    return n.tofloat() / 65521.0;
};

export function cliffPlan(d) {
    validate(d);
    local path=need(procgen.newSplinePath());
    need(path.setKind("catmullRom"));
    path.setClosed(d.closed);
    foreach (p in d.points) need(path.addPoint(p[0].tofloat(),d.height.tofloat(),p[1].tofloat(),0.0,0.0,0.0,0.0,0.0,0.0));
    local length=need(path.lengthResult(32));
    local intervals=ceil(length/d.spacing).tointeger();
    if (intervals<1 || intervals>512) throw "cliff exceeds 512 placement intervals";
    local step=length/intervals, placements=[];
    local rows=ceil(d.height/1.5).tointeger();
    if (rows<2) rows=2;
    local band=d.height/rows;
    for (local row=0;row<rows;++row) {
        for (local i=0;i<intervals+(d.closed ? 0 : 1);++i) {
            local id=row*1024+i;
            local distance=(i+(row%2)*0.45)*step;
            local s=need(path.travelResult(distance,d.closed ? "loop" : "clamp",32));
            local tx=s.getTangentX(),tz=s.getTangentZ(),norm=sqrt(tx*tx+tz*tz);
            if (norm<0.00001) throw "curve has a degenerate tangent";
            // Broad correlated folds define the silhouette; small independent fractures add detail.
            local fold=sin(distance*0.48+d.seed*0.001)*0.45+sin(distance*1.13)*0.15;
            local offset=(fold+(randomAt(d.seed,id,53)-0.5)*0.35)*d.depth*d.variation;
            local spread=1.0+(rows-1-row)*0.10;
            local topVariation=row==rows-1 ? (1.0+(randomAt(d.seed,id,37)-0.5)*0.7*d.variation) : 1.0;
            placements.append({mesh=d.variation>0 ? (randomAt(d.seed,id,11)*6).tointeger() : 0,
                x=s.getX()-tz/norm*offset,y=(row+0.45)*band+sin(distance*0.65+row*0.7)*band*0.28*d.variation,z=s.getZ()+tx/norm*offset,
                sx=step*(0.82+(randomAt(d.seed,id,23)-0.5)*0.7*d.variation),
                sy=band*(1.22+(randomAt(d.seed,id,37)-0.5)*0.5*d.variation)*topVariation,
                sz=d.depth*spread*(0.90+(randomAt(d.seed,id,53)-0.5)*0.7*d.variation),
                yaw=-atan2(tz,tx)+(randomAt(d.seed,id,71)-0.5)*0.7*d.variation,
                pitch=(randomAt(d.seed,id,83)-0.5)*0.3*d.variation,
                roll=fold*0.3*d.variation, shade=0.84+row*0.035+randomAt(d.seed,id,97)*0.06});
        }
    }
    // Larger fracture masses interrupt regular horizontal seams and give the cliff its primary silhouette.
    for (local i=1;i<intervals;i+=4) {
        local s=need(path.travelResult(i*step,"clamp",32));
        local tx=s.getTangentX(),tz=s.getTangentZ(),norm=sqrt(tx*tx+tz*tz);
        local offset=d.depth*(0.12+randomAt(d.seed,i,131)*d.variation);
        placements.append({mesh=6+i%3,x=s.getX()-tz/norm*offset,
            y=d.height*0.47,z=s.getZ()+tx/norm*offset,
            sx=step*1.05,sy=d.height*(0.30+(randomAt(d.seed,i,137)-0.5)*0.09*d.variation),
            sz=d.depth*1.6,yaw=-atan2(tz,tx),pitch=0.0,roll=0.0,shade=0.94});
    }
    // Talus at the foot breaks the artificial straight base silhouette.
    for (local i=0;i<intervals;++i) {
        local s=need(path.travelResult((i+0.3)*step,"clamp",32));
        local tx=s.getTangentX(),tz=s.getTangentZ(),norm=sqrt(tx*tx+tz*tz);
        local side=i%2==0 ? 1.0 : -1.0;
        local size=0.5+(randomAt(d.seed,i,107)-0.5)*1.4*d.variation;
        placements.append({mesh=i%6,x=s.getX()-tz/norm*d.depth*side*1.1,
            y=size*0.35,z=s.getZ()+tx/norm*d.depth*side*1.1,
            sx=size*1.4,sy=size*0.75,sz=size,yaw=randomAt(d.seed,i,109)*6.28,
            pitch=0.0,roll=0.0,shade=0.76});
    }
    return {path=path,placements=placements,length=length};
}

// Presentation adapter: split indexed rock triangles so each face owns one geometric normal.
// Geometry still comes from canonical mesh.rock; only the shading topology changes.
local flatRock = function(cpu) {
    local positions=[],normals=[],uvs=[],indices=[];
    for (local t=0;t<cpu.getIndexCount();t+=3) {
        local ids=[cpu.getIndex(t),cpu.getIndex(t+1),cpu.getIndex(t+2)];
        local a=ids[0],b=ids[1],c=ids[2];
        local ax=cpu.getPositionX(a),ay=cpu.getPositionY(a),az=cpu.getPositionZ(a);
        local ux=cpu.getPositionX(b)-ax,uy=cpu.getPositionY(b)-ay,uz=cpu.getPositionZ(b)-az;
        local vx=cpu.getPositionX(c)-ax,vy=cpu.getPositionY(c)-ay,vz=cpu.getPositionZ(c)-az;
        local nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        local length=sqrt(nx*nx+ny*ny+nz*nz);
        if (length<0.000001) continue;
        foreach (id in ids) {
            indices.append(indices.len());
            positions.append(cpu.getPositionX(id)); positions.append(cpu.getPositionY(id)); positions.append(cpu.getPositionZ(id));
            normals.append(nx/length); normals.append(ny/length); normals.append(nz/length);
            uvs.append(0.0); uvs.append(0.0);
        }
    }
    if (indices.len()==0) throw "rock has no nondegenerate faces";
    local mesh=gfx.newMeshFromArrays(positions,normals,uvs,indices.len(),indices,indices.len());
    if (mesh==null) throw "flat rock upload failed";
    return mesh;
};

export function cliffPalette() {
    local meshes=[];
    for (local i=0; i<9; ++i) {
        local p=need(procgen.newParams());
        need(p.setSeed(1009+i*137));
        need(p.setInt("subdivisions", 1));
        need(p.setString("baseShape", i<6 ? "slab" : "cliff"));
        need(p.setFloat("radius", 1.0));
        need(p.setFloat("flattening", 0.0));
        need(p.setFloat("variation", 0.40));
        need(p.setFloat("angularity", 0.60));
        need(p.setFloat("erosion", 0.03));
        meshes.append(flatRock(need(procgen.buildMesh("mesh.rock", p))));
    }
    return meshes;
}

export function cliffBacking(path, d) {
    local graph=need(procgen.newMeshModifierGraph());
    need(graph.addNode("wall", "mesh.splineExtrude"));
    need(graph.addNode("out", "mesh.output"));
    need(graph.setNodeSplinePath("wall", path));
    local w=d.depth*0.32;
    need(graph.setNodeSplineProfile("wall", [-w,-d.height*0.20, w,-d.height*0.20, w,-d.height, -w,-d.height], true));
    need(graph.setNodeInt("wall", "pathSegments", 128));
    need(graph.connect("wall", "out", 0));
    return need(procgen.uploadMesh(need(graph.executeResult("out")), gfx));
}

export function cliffNeed(result) { return need(result); }
export function cliffValidate(d) { validate(d); }
