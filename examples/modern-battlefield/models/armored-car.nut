// 8x8 infantry carrier: V-shaped hull, high crew cabin, small remote cannon turret.
export function buildArmoredCar(m,a) {
    local green=[0.34,0.39,0.30],edge=[0.48,0.51,0.37],metal=[0.17,0.21,0.20],tire=[0.045,0.055,0.05],glass=[0.08,0.24,0.30];
    a.armed=true; a.gunY=2.70; a.gunZ=0.10; a.barrel=2.2;
    m.setJointPivot("turret",0,2.15,-0.4); m.setJointPivot("barrel",0,0.55,0.5);
    m.hullLoft([[-3.2,1.0,0.7,1.8],[-2.65,1.35,0.6,2.08],[1.45,1.35,0.6,2.08],[2.65,1.05,0.85,1.5]],green);
    foreach(s in [-1,1]) {
        for(local i=0;i<4;i++) {
            local z=-2.2+i*1.37;
            m.profileTube(s*1.37,0.66,z,[[-0.26,0.46],[-0.20,0.62],[0.20,0.62],[0.26,0.46]],tire,"body",1.5708);
            m.roundTube(s*1.66,0.66,z,0.35,0.05,green,"body",1.5708);
            m.roundTube(s*1.70,0.66,z,0.12,0.07,metal,"body",1.5708);
            for(local k=0;k<8;k++) {
                local t=k*0.7854;
                m.roundTube(s*1.70,0.66+sin(t)*0.24,z+cos(t)*0.24,0.027,0.04,edge,"body",1.5708);
            }
            m.armorBox(s*1.41,1.28,z,0.65,0.14,1.15,green);
        }
        m.armorBox(s*0.54,1.99,1.74,0.83,0.045,0.5,glass,"body",0,0.44);
        m.armorBox(s*0.54,2.02,1.73,0.90,0.055,0.05,metal);
        m.armorBox(s*1.39,1.63,0.2,0.07,0.6,1.0,edge);
        m.braceBeam([s*1.5,1.3,0.5],[s*1.5,1.3,-0.3],0.07,metal);
        m.armorBox(s*1.09,1.55,2.5,0.32,0.2,0.13,tire);
        m.armorBox(s*1.09,1.56,2.58,0.23,0.1,0.02,[0.84,0.80,0.6]);
        m.armorBox(s*0.95,0.95,-3.14,0.22,0.12,0.045,[0.45,0.07,0.035]);
        for(local i=0;i<4;i++) m.armorBox(s*1.36,1.85,-0.9-i*0.32,0.06,0.10,0.21,metal);
    }
    m.armorBox(0,0.91,2.7,2.5,0.20,0.18,metal);
    m.armorBox(0,1.35,-3.16,1.15,1.1,0.06,metal);
    m.armorBox(0,1.36,-3.205,1.02,0.96,0.04,green);
    for(local i=0;i<6;i++) m.armorBox(0,0.9+i*0.17,-3.24,0.8,0.045,0.06,edge);
    m.roundTube(0,0.02,0,0.58,0.12,metal,"turret",0,1.5708);
    m.hullLoft([[-0.65,0.5,0.05,0.50],[0.15,0.6,0.05,0.64],[0.7,0.35,0.16,0.43]],green,"turret");
    m.armorBox(-0.54,0.51,-0.10,0.24,0.26,0.36,metal,"turret");
    m.armorBox(-0.54,0.54,0.09,0.17,0.12,0.02,glass,"turret");
    m.profileTube(0,0,0,[[0,0.12],[0.5,0.10],[0.65,0.065],[1.9,0.065],[2.2,0.09]],metal,"barrel");
    m.roundTube(0,0,2.208,0.045,0.012,tire,"barrel");
    m.armorBox(0.29,-0.1,0.4,0.25,0.28,0.65,green,"turret");
    m.roundTube(-0.85,2.58,-1.8,0.014,1.1,metal,"body",0,1.5708);
}
