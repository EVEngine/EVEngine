// Field howitzer: open cradle, twin recuperators and broad split trail.
export function buildHowitzer(m,a) {
    local paint=[0.38,0.40,0.25],edge=[0.52,0.51,0.32],steel=[0.21,0.23,0.20],tire=[0.065,0.07,0.065];
    a.armed=true; a.gunY=1.40; a.gunZ=0.15; a.barrel=3.6;
    m.setJointPivot("turret",0,0.75,0); m.setJointPivot("barrel",0,0.65,0.15);
    m.armorBox(0,0.55,0,2.8,0.25,0.32,steel);
    foreach(s in [-1,1]) {
        m.profileTube(s*1.37,0.65,0,[[-0.22,0.48],[-0.18,0.64],[0.16,0.64],[0.22,0.48]],tire,"body",1.5708);
        m.roundTube(s*1.61,0.65,0,0.39,0.04,paint,"body",1.5708);
        m.roundTube(s*1.66,0.65,0,0.16,0.10,steel,"body",1.5708);
        for(local i=0;i<10;i++) {
            local t=i*0.62832;
            m.roundTube(s*1.64,0.65+sin(t)*0.28,cos(t)*0.28,0.03,0.025,edge,"body",1.5708);
        }
        m.braceBeam([s*0.36,0.65,-0.25],[s*1.65,0.18,-3.25],0.28,paint);
        m.braceBeam([s*0.58,0.42,-1],[s*1.65,0.18,-3.25],0.10,edge);
        m.armorBox(s*1.65,0.17,-3.3,0.72,0.32,0.22,steel);
        m.armorBox(s*1.1,0.37,-2.1,0.06,0.25,0.8,steel,"body",s*0.4);
        m.armorBox(s*0.39,0.22,0,0.20,0.88,0.88,paint,"turret");
        m.roundTube(s*0.54,0.64,0.15,0.21,0.16,steel,"turret",1.5708);
        m.roundTube(s*0.57,0.22,-0.55,0.27,0.045,edge,"turret",1.5708);
        for(local k=0;k<5;k++) {
            local t=k*1.25664;
            m.braceBeam([s*0.60,0.22,-0.55],[s*0.60,0.22+sin(t)*0.23,-0.55+cos(t)*0.23],0.025,steel,"turret");
        }
    }
    m.roundTube(0,0.0,0,0.57,0.12,steel,"turret",0,1.5708);
    m.armorBox(-0.60,0.76,-0.45,0.10,0.28,0.13,steel,"turret");
    m.roundTube(-0.60,0.92,-0.41,0.05,0.25,steel,"turret");
    m.armorBox(0,-0.05,-0.6,0.48,0.37,0.75,steel,"barrel");
    m.profileTube(0,0,0,[[0,0.22],[0.6,0.21],[0.78,0.16],[3.25,0.11],[3.6,0.11]],steel,"barrel");
    foreach(s in [-1,1]) m.profileTube(s*0.19,0.22,0,[[-0.6,0.11],[1.05,0.11],[1.13,0.075]],paint,"barrel");
    m.armorBox(0,0,3.45,0.42,0.32,0.45,edge,"barrel");
    foreach(s in [-1,1]) for(local i=0;i<3;i++) m.armorBox(s*0.215,0,3.31+i*0.12,0.012,0.18,0.06,tire,"barrel");
    m.roundTube(0,0,3.68,0.085,0.015,tire,"barrel");
}
