// Heavy siege/field piece: four wheels, deployed outriggers and massive cradle.
export function buildHeavyHowitzer(m,a) {
    local green=[0.27,0.32,0.25],edge=[0.42,0.45,0.33],metal=[0.17,0.20,0.19],black=[0.05,0.06,0.055];
    a.armed=true; a.gunY=1.85; a.gunZ=0.15; a.barrel=6.0; a.elevation=0.43;
    m.setJointPivot("turret",0,0.9,0); m.setJointPivot("barrel",0,0.95,0.15);
    m.armorBox(0,0.65,-0.6,2.55,0.42,2.4,green);
    foreach(s in [-1,1]) {
        foreach(z in [-1.35,0.35]) {
            m.profileTube(s*1.5,0.72,z,[[-0.26,0.55],[-0.22,0.71],[0.22,0.71],[0.26,0.55]],black,"body",1.5708);
            m.roundTube(s*1.78,0.72,z,0.45,0.045,green,"body",1.5708);
            m.roundTube(s*1.82,0.72,z,0.16,0.08,metal,"body",1.5708);
            for(local k=0;k<8;k++) {
                local t=k*0.7854;
                m.roundTube(s*1.81,0.72+sin(t)*0.31,z+cos(t)*0.31,0.035,0.04,edge,"body",1.5708);
            }
        }
        m.braceBeam([s*0.7,0.65,-1.1],[s*2.4,0.22,-4.0],0.36,green);
        m.armorBox(s*2.4,0.18,-4.0,1.0,0.36,0.38,metal);
        m.braceBeam([s*0.8,0.61,0.4],[s*2.5,0.17,1.65],0.25,green);
        m.armorBox(s*2.5,0.1,1.65,0.85,0.2,0.8,metal);
        m.braceBeam([s*1.5,0.74,-2.5],[s*2.1,0.35,-3.7],0.1,edge);
        m.armorBox(s*0.63,0.48,0,0.30,1.25,1.4,green,"turret");
        m.roundTube(s*0.83,0.95,0.15,0.32,0.15,metal,"turret",1.5708);
        m.braceBeam([s*0.49,0.25,-0.5],[s*0.4,0.88,0.8],0.1,edge,"turret");
    }
    m.roundTube(0,-0.04,0,0.8,0.18,metal,"turret",0,1.5708);
    m.armorBox(-0.84,0.0,-1.0,0.65,0.12,1.35,metal,"turret");
    m.armorBox(-0.99,0.4,-0.6,0.55,0.12,0.5,green,"turret");
    m.roundTube(-0.93,0.75,-0.5,0.29,0.05,edge,"turret",1.5708);
    for(local k=0;k<6;k++) { local t=k*1.0472;
        m.braceBeam([-0.96,0.75,-0.5],[-0.96,0.75+sin(t)*0.26,-0.5+cos(t)*0.26],0.035,metal,"turret"); }
    m.profileTube(0,0,0,[[-1.25,0.32],[-0.8,0.35],[0.4,0.29],[1.2,0.23],[2.5,0.20],[2.65,0.25],[3.0,0.25],[3.1,0.17],[5.6,0.14],[6.0,0.17]],metal,"barrel",0,0,0,32);
    m.roundTube(0,0,6.008,0.115,0.015,black,"barrel");
    foreach(s in [-1,1]) m.profileTube(s*0.32,0.33,0,[[-1.05,0.14],[1.65,0.14],[1.8,0.09]],green,"barrel");
    foreach(z in [0.0,1.3,4.0,4.9]) m.roundTube(0,0,z,z<2?0.32:0.18,0.09,edge,"barrel");
    m.armorBox(0,-0.20,-1.2,0.85,0.60,0.65,green,"barrel");
    m.braceBeam([0.40,-0.3,-1.25],[0.65,-0.3,-1.4],0.07,metal,"barrel");
}
