// Host-owned lit projection. Mesh slots are reused across edits, including undo.
// World-space vertices preserve parent rotation/nonuniform scale without a second TRS authority.
if (!("visuals" in sceneDemo)) sceneDemo.visuals <- [];
if (!("ground" in sceneDemo)) sceneDemo.ground <- null;
if (!("keyLight" in sceneDemo)) sceneDemo.keyLight <- null;
function sceneCross(a,b) { return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]; }
function sceneLitCube(points,selected,slot) {
    local center=points.slice(0,3), axes=[points.slice(3,6),points.slice(6,9),points.slice(9,12)];
    local positions=[], normals=[], uv=[], indices=[];
    local cross=sceneCross(axes[0],axes[1]);
    local handed=cross[0]*axes[2][0]+cross[1]*axes[2][1]+cross[2]*axes[2][2];
    for(local axis=0;axis<3;axis++) foreach(sign in [-1.0,1.0]) {
        local a=axes[(axis+1)%3],b=axes[(axis+2)%3];
        local normal=sceneCross(a,b), length=sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        local start=positions.len()/3;
        foreach(corner in [[-1,-1],[1,-1],[1,1],[-1,1]]) {
            for(local k=0;k<3;k++) {
                positions.push(center[k]+sign*axes[axis][k]+corner[0]*a[k]+corner[1]*b[k]);
                normals.push(normal[k]/length*sign*(handed<0 ? -1.0 : 1.0));
            }
            uv.extend([0.0,0.0]);
        }
        local winding=sign*(handed<0 ? -1.0 : 1.0)>0 ? [0,1,2,0,2,3] : [0,2,1,0,3,2];
        foreach(i in winding) indices.push(start+i);
    }
    if(slot==sceneDemo.visuals.len()) {
        local mesh=gfx.newMeshFromArrays(positions,normals,uv,24,indices,36);
        local entity=eve.Renderable3D();entity.setMesh(mesh);
        entity.setRoughness(0.65);entity.setCastShadow(true);entity.setReceiveShadow(true);
        sceneDemo.visuals.push({mesh=mesh,entity=entity});
    } else if(!gfx.updateMeshVertices(sceneDemo.visuals[slot].mesh,positions,normals,uv,24,indices,36))
        throw "Could not update scene preview mesh";
    local entity=sceneDemo.visuals[slot].entity;
    entity.setVisible(true);entity.setTint(selected ? 1.0 : 0.18,selected ? 0.58 : 0.55,selected ? 0.13 : 0.72,1.0);
}
function sceneSetupLighting() {
    if(sceneDemo.ground==null) {
        sceneDemo.ground=eve.Renderable3D();sceneDemo.ground.setMesh(gfx.newMeshCube(1.0));
        sceneDemo.ground.setPosition(0.0,-0.12,0.0);sceneDemo.ground.setScale(18.0,0.2,18.0);
        sceneDemo.ground.setTint(0.16,0.19,0.23,1.0);sceneDemo.ground.setRoughness(0.9);
        sceneDemo.ground.setReceiveShadow(true);
        sceneDemo.keyLight=eve.Light3D();sceneDemo.keyLight.setType("dir");
        sceneDemo.keyLight.setDirection(-0.5,1.0,0.4);sceneDemo.keyLight.setColor(1.0,0.93,0.82,1.0);
        sceneDemo.keyLight.setCastShadow(true);
    }
    sceneDemo.camera.setAmbient(0.24,0.28,0.35);
}

function sceneKeepHandle(result,fill=true) {
    if(!sceneChecked(result)) return;
    sceneChecked(result.value.setDepthMode("ignore"));
    if(fill) sceneChecked(result.value.setPaintMode("fill-stroke"));
    sceneDemo.axes.push(result.value);
}
function sceneHandleBox(center,half,r,g,b) {
    local points=sceneParentPoint(center[0],center[1],center[2]);
    foreach(axis in [[half,0.0,0.0],[0.0,half,0.0],[0.0,0.0,half]]) {
        local end=sceneParentPoint(center[0]+axis[0],center[1]+axis[1],center[2]+axis[2]);
        points.extend([end[0]-points[0],end[1]-points[1],end[2]-points[2]]);
    }
    sceneKeepHandle(gfx.newPrimitiveObb3D(points,r,g,b,1.0,1.0));
}
