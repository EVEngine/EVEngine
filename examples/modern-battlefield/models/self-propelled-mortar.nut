// Compact open-top tracked mortar carrier, high-angle tube and ready-round racks.
export function buildSelfPropelledMortar(m,a) {
    local green=[0.31,0.38,0.29],edge=[0.45,0.49,0.35],metal=[0.17,0.22,0.20],black=[0.04,0.055,0.045];
    a.armed=true; a.gunY=1.85; a.gunZ=-0.3; a.barrel=2.0; a.elevation=1.0;
    m.setJointPivot("turret",0,1.3,-0.6); m.setJointPivot("barrel",0,0.55,0.3);
    m.trackedRunningGear(4.7,1.3,5,green);
    m.hullLoft([[-2.4,1.2,0.55,1.35],[-1.8,1.5,0.45,1.5],[1.65,1.5,0.45,1.50],[2.5,1.15,0.72,1.05]],green);
    m.roundTube(0,0.10,0,1.02,0.20,metal,"turret",0,1.5708);
    m.roundTube(0,0.22,0,0.82,0.035,black,"turret",0,1.5708);
    foreach(s in [-1,1]) {
        m.armorBox(s*0.94,0.61,0,0.17,0.85,1.75,green,"turret");
        m.armorBox(s*0.96,1.04,0,0.24,0.08,1.83,edge,"turret");
        m.armorBox(s*0.65,0.7,-0.88,0.55,0.65,0.14,green,"turret");
        m.armorBox(s*0.92,0.46,0.65,0.25,0.32,0.22,metal,"turret");
        m.armorBox(s*1.47,1.25,0,0.13,0.35,4.3,green);
        m.armorBox(s*0.96,1.53,1.65,0.35,0.18,0.22,black);
        m.armorBox(s*0.96,1.54,1.77,0.22,0.10,0.015,[0.8,0.78,0.58]);
        for(local i=0;i<4;i++) {
            m.profileTube(s*0.66,0.20,-0.58+i*0.31,[[-0.12,0.07],[0.35,0.07],[0.55,0.02]],edge,"turret",0,-1.5708);
            m.armorBox(s*0.68,0.30,-0.58+i*0.31,0.21,0.07,0.20,metal,"turret");
        }
    }
    m.armorBox(0,0.5,-0.98,1.8,0.55,0.16,green,"turret");
    m.armorBox(0,1.53,1.0,0.72,0.12,0.6,edge);
    for(local i=0;i<7;i++) m.armorBox(-0.5+i*0.16,1.49,1.65,0.07,0.045,0.5,metal);
    m.armorBox(-0.6,1.64,1.12,0.19,0.12,0.15,metal);
    m.armorBox(-0.6,1.65,1.205,0.13,0.06,0.02,[0.09,0.28,0.34]);
    m.profileTube(0,0,0,[[-0.4,0.23],[0.2,0.23],[0.3,0.17],[1.7,0.17],[1.8,0.22],[2.0,0.22]],green,"barrel");
    m.roundTube(0,0,2.008,0.14,0.015,black,"barrel");
    foreach(z in [0.3,0.9,1.5]) m.roundTube(0,0,z,0.21,0.07,metal,"barrel");
    m.roundTube(-0.25,0.08,0.7,0.07,0.6,metal,"barrel");
}
