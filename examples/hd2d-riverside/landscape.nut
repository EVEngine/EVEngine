persist riversideDressing = [];
persist riversideSolids = [];

function riverProp(mesh,x,y,z,sx,sy,sz,r,g,b) {
    local e=eve.Renderable3D();e.setMesh(mesh);
    e.setPosition(x.tofloat(),y.tofloat(),z.tofloat());e.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());
    e.setTint(r.tofloat(),g.tofloat(),b.tofloat(),1.0);e.setRoughness(0.94);
    e.setCastShadow(true);e.setReceiveShadow(true);riversideDressing.push(e);return e;
}
function riverDress() {
    local cube=gfx.newMeshCube(1.0),round=gfx.newMeshSphere(10,7);
    // Riverbank stones and reeds stop at the bridge approaches.
    for(local z=20;z<500;z+=23)if(z<180 || z>300) {
        for(local side=0;side<2;side++) {
            local x=(side==0?427.0:548.0)+sin(z*0.11)*4.0;
            riverProp(round,x,1,z,11+z%5,5+z%4,12+z%7,0.32,0.36,0.29);
            if(z%3!=0)for(local k=0;k<3;k++)
                riverProp(cube,x+(side==0?-16:16)+k*3,12+k*3,z,1.8,24+k*6,1.8,0.24,0.37,0.17);
        }
    }
    for(local z=184;z<=296;z+=112) {
        riverProp(cube,450,31,z,190,5,5,0.32,0.21,0.12);
        for(local x=360;x<550;x+=32)riverProp(cube,x,15,z,6,44,6,0.31,0.22,0.13);
    }
    // Trees sit outside the main plaza; the same authored positions own blocking.
    foreach(p in [[42,38],[132,28],[278,30],[365,36],[620,40],[665,120],
                 [665,330],[600,470],[500,478],[355,478],[230,478],[45,455],[32,330]]) {
        local x=p[0].tofloat(),z=p[1].tofloat();
        riversideSolids.push({x=x,z=z,r=17.0});
        riverProp(cube,x,28,z,9,56,9,0.29,0.20,0.11);
        riverProp(round,x,72,z,27,34,25,0.17,0.31,0.14);
        riverProp(round,x-17,61,z+7,21,24,22,0.23,0.38,0.18);
        riverProp(round,x+16,65,z-6,20,27,20,0.24,0.39,0.17);
    }
    // Benches and flower planters frame the square rather than blocking its routes.
    foreach(x in [92.0,290.0]) {
        riversideSolids.push({x=x,z=345.0,r=24.0});
        riverProp(cube,x,14,345,42,5,15,0.40,0.25,0.13);
        riverProp(cube,x,26,350,42,20,4,0.40,0.25,0.13);
        for(local side=-1;side<=1;side+=2)riverProp(cube,x+side*15,7,345,4,14,12,0.21,0.22,0.19);
    }
    for(local x=45;x<375;x+=28) {
        riverProp(round,x,3,420,10,6,9,0.31,0.36,0.23);
        if(x%2)riverProp(round,x,12,421,12,13,11,0.27,0.39,0.18);
    }
}
function riverDecorBlocked(x,z) {
    foreach(p in riversideSolids)if((x-p.x)*(x-p.x)+(z-p.z)*(z-p.z)<p.r*p.r)return true;
    return false;
}
