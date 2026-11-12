persist castleScenery = [];
persist castleRoof = [];

function castleBox(x,y,z,sx,sy,sz,r,g,b) {
    x=x.tofloat();y=y.tofloat();z=z.tofloat();sx=sx.tofloat();sy=sy.tofloat();sz=sz.tofloat();
    r=r.tofloat();g=g.tofloat();b=b.tofloat();
    local e=eve.Renderable3D();e.setMesh(gfx.newMeshCube(1.0));
    e.setPosition(x,y,z);e.setScale(sx,sy,sz);e.setTint(r,g,b,1.0);
    e.setRoughness(0.9);e.setCastShadow(true);e.setReceiveShadow(true);
    castleScenery.push(e);return e;
}
function castleBuildLandscape() {
    castleBox(0,-1.6,0,88,2,78,0.20,0.28,0.19);
    castleBox(0,-0.45,0,57,0.7,49,0.30,0.32,0.29);
    castleBox(0,0.015,-31,6,0.05,24,0.48,0.40,0.27);
    castleBox(-18,-0.48,-29,30,0.08,7,0.08,0.20,0.22);
    castleBox(18,-0.48,-29,30,0.08,7,0.08,0.20,0.22);
    castleBox(0,-0.48,29,68,0.08,7,0.08,0.20,0.22);
    for(local side=-1;side<=1;side+=2)castleBox(side*31,-0.48,0,6,0.08,52,0.08,0.20,0.22);
    for(local row=0;row<5;row++) for(local x=-22;x<24;x+=3) {
        local xx=x+(row%2)*1.5;
        if(fabs(xx)<3.8) continue;
        castleBox(xx,0.55+row*1.1,-20.92,2.85,0.96,0.07,
                  0.39+0.025*((x+22+row)%4),0.40+row*0.008,0.36);
    }
    for(local x=-18;x<=18;x+=6) if(fabs(x)>4)
        castleBox(x,3.6,-20.99,0.20,1.0,0.05,0.06,0.10,0.11);
    for(local side=-1;side<=1;side+=2) {
        castleBox(side*7.5,9.1,-19,0.14,5,0.14,0.20,0.16,0.11);
        castleBox(side*8.1,10.1,-19,1.1,1.9,0.05,side<0?0.18:0.56,0.29,side<0?0.42:0.18);
    }
    local shrub=gfx.newMeshSphere(10,6);
    for(local i=0;i<28;i++) {
        local x=i<14?((i%2==0)?-39.0:39.0):(-27.0+((i-14)%7)*9.0);
        local z=i<14?(-27.0+(i/2).tointeger()*9.0):((i<21)?-36.0:36.0);
        local e=eve.Renderable3D();e.setMesh(shrub);
        e.setPosition(x,-0.1,z);e.setScale(1.3+(i%3)*0.4,0.8,1.2);
        e.setTint(0.17,0.27+(i%3)*0.025,0.14,1.0);e.setRoughness(1.0);castleScenery.push(e);
    }
}
function castleUpdateRoof() {
    foreach(e in castleRoof)e.setVisible(false);
    if(castleRoof.len()==0) for(local side=-1;side<=1;side+=2) {
        local e=castleBox(side*3.1,0,0,7.3,0.35,11.0,0.15,0.23,0.25);
        e.setRotation(0.0,0.0,-side*0.48);castleRoof.push(e);
    }
    foreach(i,e in castleRoof){e.setPosition(i==0?-3.1:3.1,floors*3.2+1.75,0.0);e.setVisible(true);}
}
