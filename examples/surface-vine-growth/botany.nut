// Bounded render cache derived from the owning surface-crawl branch points.
persist vineBotany = [];
persist vineBotanyUsed = 0;
persist vineDebugTips = false;
persist vineLeafMesh = null;

function vineBotanicalPart(leaf,x,y,z,sx,sy,sz,yaw,pitch) {
    if(vineBotanyUsed>=900)return;
    if(vineBotanyUsed==vineBotany.len())vineBotany.push(eve.Renderable3D());
    local e=vineBotany[vineBotanyUsed++];e.setVisible(true);
    e.setMesh(leaf?vineLeafMesh:vineCube);e.setPosition(x,y,z);e.setScale(sx,sy,sz);
    e.setRotation(yaw,pitch,0.0);e.setRoughness(0.95);e.setCastShadow(true);e.setReceiveShadow(true);
    if(leaf)e.setTint(0.13+(vineBotanyUsed%3)*0.025,0.29+(vineBotanyUsed%4)*0.035,0.09,1.0);
    else e.setTint(0.22,0.25,0.10,1.0);
}
function vineSyncBotany() {
    vineBotanyUsed=0;
    if(vineSystem!=null)foreach(branch in vineSystem.getBranches()) {
        local p=branch.points;
        for(local i=9;i<p.len();i+=9) {
            local dx=p[i]-p[i-9],dy=p[i+1]-p[i-8],dz=p[i+2]-p[i-7];
            local length=sqrt(dx*dx+dy*dy+dz*dz);
            if(length<0.001)continue;
            local thickness=0.035+0.025*(1.0-i.tofloat()/p.len());
            vineBotanicalPart(false,(p[i]+p[i-9])*0.5,(p[i+1]+p[i-8])*0.5+0.018,
                (p[i+2]+p[i-7])*0.5,thickness,length,thickness,atan2(-dx,-dz),acos(dy/length));
            if(i%18==0)for(local side=-1;side<=1;side+=2) {
                local vertical=fabs(dy)>sqrt(dx*dx+dz*dz);
                vineBotanicalPart(true,p[i]+side*0.11,p[i+1]+0.04,p[i+2]+0.045,
                    0.09,vertical?0.14:0.025,vertical?0.025:0.15,side*0.6,vertical?0.0:0.25);
            }
        }
    }
    for(local i=vineBotanyUsed;i<vineBotany.len();i++)vineBotany[i].setVisible(false);
}
function vineDressCourtyard() {
    vineLeafMesh=gfx.newMeshSphere(8,6);
    // Relief stays within a few centimetres of the existing authoritative boxes.
    foreach(w in [[2.4,1.1,0.4,1.9,2.2,2.8],[3.6,2.4,0.8,1.7,2.0,2.2],[4.7,3.7,1.1,1.4,1.6,1.8]]) {
        for(local y=w[1]-w[4]*0.5+0.15;y<w[1]+w[4]*0.5;y+=0.32) {
            vinePlace(vineCube,w[0]+w[3]*0.5+0.012,y,w[2],0.018,0.023,w[5],0.18,0.21,0.18);
            vinePlace(vineCube,w[0],y,w[2]+w[5]*0.5+0.012,w[3],0.023,0.018,0.18,0.21,0.18);
            for(local x=w[0]-w[3]*0.5+0.18;x<w[0]+w[3]*0.5;x+=0.42)
                vinePlace(vineCube,x,y+0.13,w[2]+w[5]*0.5+0.014,0.018,0.25,0.02,0.21,0.24,0.20);
        }
        vinePlace(vineCube,w[0],w[1]+w[4]*0.5,w[2],w[3]+0.05,0.07,w[5]+0.05,0.27,0.30,0.25);
    }
    // Mortar courses and timber trim follow the existing collision boxes.
    for(local y=0.3;y<3.0;y+=0.34) {
        vinePlace(vineCube,-1.535,y,0.0,0.028,0.025,3.2,0.21,0.19,0.16);
        for(local z=-1.4;z<1.5;z+=0.6)
            vinePlace(vineCube,-1.53,y+0.15,z+(floor(y*3)%2)*0.28,0.035,0.28,0.025,0.27,0.25,0.20);
    }
    for(local y=0.25;y<2.1;y+=0.38)
        vinePlace(vineCube,1.44,y,0.4,0.03,0.035,2.8,0.17,0.20,0.19);
    for(local z=-1.0;z<1.1;z+=0.7)
        vinePlace(vineCube,-1.51,1.45,z,0.055,2.9,0.08,0.24,0.17,0.10);
    for(local x=-0.9;x<1.0;x+=0.5)for(local z=-1.2;z<1.3;z+=0.5)
        vinePlace(vineCube,x,0.058,z,0.46,0.018,0.46,0.30,0.32,0.26);
}
