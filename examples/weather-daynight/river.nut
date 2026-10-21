// Scene-owned spline water. Geometry is baked once; only shader time changes.
persist riverSamples = []
persist riverShader = null
persist riverClock = 0.0
persist riverGuide = null
persist riverGuideVisible = false

function riverRequire(r) {
    if (!r.ok) throw r.status.summary;
    return ("value" in r) ? r.value : null;
}

function buildRiver() {
    local path = riverRequire(procgen.newSplinePath());
    riverRequire(path.setKind("catmullRom"));
    // x, height, z, half-width. Edit these anchors to reshape the whole waterway.
    local anchors = [[-3.0,4.95,-17.0,1.35],[-1.0,4.90,-12.0,1.5],
        [0.0,4.80,-8.8,1.45],[0.0,4.45,-7.0,1.4],
        [0.0,2.8,-5.9,1.55],[0.0,0.18,-4.7,1.9],
        [0.4,0.10,-2.3,3.0],[2.3,0.06,2.0,2.1],
        [0.5,0.02,7.0,1.9],[-2.8,-0.02,12.0,2.2],[-1.0,-0.08,17.5,2.4]];
    foreach (a in anchors) {
        riverRequire(path.addPoint(a[0],a[1],a[2],0.0,0.0,0.0,0.0,0.0,0.0));
    }
    riverSamples = [];
    local pos=[], normals=[], uv=[], indices=[], guide=[];
    local distance=0.0;
    for (local i=0;i<=240;++i) {
        local t=i/240.0;
        local s=riverRequire(path.evaluateResult(t));
        local segment=t*(anchors.len()-1);
        local k=segment.tointeger();
        if (k>=anchors.len()-1) k=anchors.len()-2;
        local blend=segment-k;
        local width=anchors[k][3]*(1.0-blend)+anchors[k+1][3]*blend;
        local x=s.getX(), y=s.getY(), z=s.getZ();
        if (i>0) {
            local p=riverSamples[i-1];
            distance+=sqrt((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)+(z-p.z)*(z-p.z));
        }
        local tx=s.getTangentX(), ty=s.getTangentY(), tz=s.getTangentZ();
        local flat=sqrt(tx*tx+tz*tz);
        local sx=tz/flat, sz=-tx/flat;
        riverSamples.push({x=x,y=y,z=z,width=width,sx=sx,sz=sz});
        guide.extend([x,y+0.12,z]);
        for (local j=0;j<=12;++j) {
            local u=j/12.0, side=(u*2.0-1.0)*width;
            pos.extend([x+sx*side,y,z+sz*side]);
            normals.extend([-ty*tx/flat,flat,-ty*tz/flat]);
            // Metres along the spline, independent of anchor spacing.
            uv.extend([u,distance]);
            if (i<240 && j<12) {
                local a=i*13+j, b=a+1, c=a+13;
                indices.extend([a,c,b,b,c,c+1]);
            }
        }
    }
    riverShader=gfx.newMeshShader(fs.readText("shaders/river.frag"));
    riverShader.declareFloat("time");
    riverShader.declareFloat("flow");
    riverShader.sendFloat("time",0.0);
    riverShader.sendFloat("flow",1.0);
    local mesh=gfx.newMeshFromArrays(pos,normals,uv,pos.len()/3,indices,indices.len());
    local object=eve.Renderable3D();
    object.setMesh(mesh);
    local material=gfx.newMaterial();
    material.setShader(riverShader);
    material.setSurfaceMode("transparent");
    material.setDepthWrite(true);
    material.setDoubleSided(true);
    object.setMaterial(material);
    object.setShader(riverShader);
    skyProps.push(object);
    riverGuide=riverRequire(gfx.newPrimitivePolyline3D(guide,false,0.95,0.65,0.12,1.0,2.0));
    riverGuide.setVisible(false);
    print("SPLINE_RIVER_READY samples=241 arcLength="+distance+"\n");
}

function updateRiver(dt) {
    riverClock+=dt;
    riverShader.sendFloat("time",riverClock);
    riverShader.sendFloat("flow",1.0+skyPresets[skyWeather].rain*0.45);
    if (key_just_pressed("g")) {
        riverGuideVisible=!riverGuideVisible;
        riverGuide.setVisible(riverGuideVisible);
    }
}
