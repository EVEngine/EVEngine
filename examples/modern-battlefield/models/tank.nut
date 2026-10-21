// Main battle tank: angular composite turret, six road wheels, armoured skirts.
export function buildTank(m,a) {
    local hull=[0.29,0.34,0.23],edge=[0.38,0.42,0.28],dark=[0.065,0.078,0.075];
    local metal=[0.19,0.22,0.19],sand=[0.51,0.48,0.33],glass=[0.08,0.28,0.32];
    a.armed=true; a.gunY=2.18; a.gunZ=0.85; a.barrel=4.65;
    m.setJointPivot("turret",0,1.67,0); m.setJointPivot("barrel",0,0.51,0.85);
    m.hullLoft([[-3.1,1.32,0.64,1.3],[-2.65,1.64,0.45,1.5],[1.85,1.64,0.45,1.52],[3.25,1.30,0.72,1.08]],hull);
    foreach(s in [-1,1]) {
        // Closed track path, individually modelled shoes and guide teeth.
        m.armorBox(s*1.67,0.72,0,0.65,0.86,5.65,dark);
        for(local i=0;i<6;i++) {
            local z=-2.22+i*0.89;
            m.roundTube(s*1.78,0.69,z,0.43,0.43,metal,"body",1.5708);
            m.roundTube(s*2.01,0.69,z,0.28,0.055,hull,"body",1.5708);
            m.roundTube(s*2.05,0.69,z,0.085,0.07,sand,"body",1.5708);
            for(local k=0;k<6;k++) {
                local t=k*1.0472;
                m.roundTube(s*2.052,0.69+sin(t)*0.19,z+cos(t)*0.19,0.027,0.07,metal,"body",1.5708);
            }
        }
        for(local i=0;i<29;i++) foreach(y in [0.22,1.20])
            m.armorBox(s*1.74,y,-2.70+i*0.194,0.74,0.09,0.14,metal);
        foreach(z in [-2.76,2.76]) for(local k=0;k<8;k++) {
            local t=-1.5708+k*0.4488;
            m.armorBox(s*1.74,0.71+sin(t)*0.49,z+(z<0?-1:1)*cos(t)*0.33,0.74,0.13,0.13,metal);
        }
        for(local i=0;i<5;i++) {
            m.armorBox(s*1.94,1.28,-2.25+i*1.05,0.14,0.61,0.98,hull);
            m.armorBox(s*2.02,1.49,-2.25+i*1.05,0.05,0.10,0.90,edge);
        }
        m.armorBox(s*1.15,1.61,2.3,0.40,0.17,0.24,dark);
        m.armorBox(s*1.15,1.63,2.44,0.26,0.09,0.02,[0.8,0.77,0.55]);
        m.braceBeam([s*0.95,1.56,2.9],[s*0.95,1.4,3.24],0.09,metal);
    }
    // Engine grilles and rear luggage; deliberate silhouette separate from SPG.
    for(local i=0;i<12;i++) m.armorBox(0,1.53,-2.63+i*0.11,1.90,0.045,0.045,dark);
    m.armorBox(0,1.57,2.0,0.8,0.12,0.7,edge);
    m.hullLoft([[-1.65,1.02,0.02,0.58],[-0.9,1.42,0.0,0.80],[0.75,1.28,0.02,0.72],[1.6,0.45,0.12,0.48]],hull,"turret",0.24);
    foreach(s in [-1,1]) {
        m.armorBox(s*0.93,0.53,0.57,0.75,0.35,1.10,edge,"turret",s*0.31);
        for(local i=0;i<4;i++) m.armorBox(s*1.29,0.25,-0.92+i*0.32,0.16,0.23,0.26,sand,"turret");
        for(local i=0;i<4;i++) m.roundTube(s*1.24,0.59,-0.25+i*0.16,0.065,0.20,metal,"turret",s*0.8,-0.35);
        m.braceBeam([s*0.91,0.72,-1.12],[s*0.91,0.72,-0.45],0.055,metal,"turret");
    }
    m.roundTube(-0.55,0.84,-0.2,0.36,0.10,metal,"turret",0,1.5708);
    m.roundTube(0.55,0.85,-0.55,0.30,0.12,edge,"turret",0,1.5708);
    m.armorBox(-0.5,0.91,0.2,0.25,0.16,0.23,metal,"turret");
    m.armorBox(-0.5,0.93,0.32,0.19,0.08,0.02,glass,"turret");
    m.armorBox(0.38,0.90,0.35,0.32,0.26,0.31,metal,"turret");
    m.armorBox(0.38,0.94,0.515,0.24,0.12,0.025,glass,"turret");
    m.roundTube(-0.8,1.36,-0.85,0.018,1.2,metal,"turret",0,1.5708);
    m.armorBox(0.0,0.42,-1.73,1.6,0.32,0.42,metal,"turret");
    for(local i=0;i<7;i++) m.armorBox(-0.72+i*0.24,0.57,-1.95,0.045,0.34,0.045,edge,"turret");
    m.profileTube(0,0,0,[[0,0.26],[0.4,0.24],[0.55,0.14],[1.5,0.13],[1.62,0.21],[2.15,0.21],[2.3,0.12],[4.5,0.105],[4.65,0.13]],metal,"barrel");
    m.roundTube(0,0,4.658,0.082,0.012,[0.018,0.022,0.022],"barrel");
    foreach(z in [0.7,1.15,2.7,3.4]) m.roundTube(0,0,z,0.145,0.07,edge,"barrel");
}
