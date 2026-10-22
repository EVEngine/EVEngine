// Infantry mortar: circular ribbed baseplate, independent bipod and optical sight.
export function buildMortar(m,a) {
    local green=[0.29,0.37,0.25],metal=[0.17,0.21,0.19],edge=[0.42,0.47,0.32],black=[0.035,0.045,0.04];
    a.armed=true; a.gunY=0.22; a.gunZ=-0.45; a.barrel=2.1; a.elevation=1.05;
    m.setJointPivot("turret",0,0.0,0); m.setJointPivot("barrel",0,0.22,-0.45);
    m.profileTube(0,0.09,-0.45,[[-0.075,0.72],[0.035,0.78],[0.075,0.65]],green,"body",0,1.5708);
    for(local i=0;i<8;i++) {
        local t=i*0.7854;
        m.braceBeam([cos(t)*0.17,0.19,-0.45+sin(t)*0.17],[cos(t)*0.66,0.16,-0.45+sin(t)*0.66],0.065,edge);
    }
    foreach(s in [-1,1]) {
        m.braceBeam([s*0.75,0.10,0.75],[s*0.16,1.1,0.25],0.075,green,"turret");
        m.armorBox(s*0.75,0.06,0.75,0.25,0.08,0.22,metal,"turret");
        m.braceBeam([s*0.75,0.17,0.75],[0,0.64,0.25],0.038,metal,"turret");
    }
    m.armorBox(0,1.04,0.28,0.55,0.12,0.16,metal,"turret");
    m.roundTube(0,1.16,0.22,0.045,0.46,edge,"turret",1.5708);
    m.roundTube(-0.34,1.16,0.22,0.14,0.03,metal,"turret",1.5708);
    m.braceBeam([-0.36,1.16,0.08],[-0.36,1.16,0.36],0.035,edge,"turret");
    m.profileTube(0,0,0,[[0,0.14],[0.18,0.14],[0.24,0.10],[1.8,0.10],[1.85,0.135],[2.1,0.135]],green,"barrel");
    m.roundTube(0,0,2.108,0.09,0.012,black,"barrel");
    foreach(z in [0.5,1.05,1.12,1.72]) m.roundTube(0,0,z,0.13,0.045,metal,"barrel");
    m.armorBox(-0.17,0.04,1.1,0.16,0.15,0.25,edge,"barrel");
    m.roundTube(-0.25,0.12,1.13,0.045,0.28,metal,"barrel");
    m.roundTube(-0.25,0.12,1.28,0.034,0.015,[0.08,0.30,0.35],"barrel");
}
