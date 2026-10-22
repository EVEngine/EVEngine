// Tall rear-mounted SPG turret, long 155 mm silhouette and deployed rear spade.
export function buildSelfPropelledArtillery(m,a) {
    local green=[0.34,0.36,0.24],edge=[0.48,0.48,0.32],metal=[0.18,0.21,0.18],black=[0.04,0.05,0.045];
    a.armed=true; a.gunY=2.62; a.gunZ=0.0; a.barrel=5.4; a.elevation=0.29;
    m.setJointPivot("turret",0,1.5,-0.85); m.setJointPivot("barrel",0,1.12,0.85);
    m.trackedRunningGear(6.1,1.58,7,green);
    m.hullLoft([[-3.1,1.4,0.65,1.45],[-2.6,1.7,0.5,1.6],[2.2,1.7,0.5,1.6],[3.4,1.3,0.8,1.14]],green);
    m.hullLoft([[-1.8,1.18,0,1.52],[-1.45,1.4,0,1.66],[0.75,1.3,0,1.54],[1.3,0.92,0.16,1.14]],green,"turret",0.16);
    foreach(s in [-1,1]) {
        m.armorBox(s*1.48,1.27,0.0,0.13,0.50,5.9,green);
        m.armorBox(s*1.39,0.67,-0.50,0.12,0.55,1.30,edge,"turret");
        m.armorBox(s*0.90,1.72,-0.7,0.55,0.14,0.9,metal,"turret");
        m.braceBeam([s*0.85,1.0,-1.84],[s*0.85,1.0,-2.1],0.05,metal,"turret");
        m.armorBox(s*1.20,1.68,2.3,0.35,0.16,0.25,black);
        m.armorBox(s*1.20,1.69,2.44,0.25,0.09,0.03,[0.84,0.79,0.6]);
    }
    for(local i=0;i<12;i++) m.armorBox(0,1.63,1.0+i*0.13,1.9,0.05,0.06,metal);
    m.armorBox(0,0.6,-3.5,2.6,0.8,0.18,metal,"body",0,0.25);
    foreach(s in [-1,1]) m.braceBeam([s*0.7,1.2,-2.7],[s*0.7,0.8,-3.5],0.12,edge);
    m.roundTube(0.5,1.72,0.05,0.35,0.12,edge,"turret",0,1.5708);
    m.armorBox(0.52,1.85,0.12,0.15,0.16,0.32,metal,"turret");
    m.armorBox(0.52,1.86,0.29,0.1,0.08,0.02,[0.08,0.25,0.3],"turret");
    m.roundTube(-0.7,2.04,-0.8,0.018,1.2,metal,"turret",0,1.5708);
    for(local i=0;i<5;i++) m.armorBox(-0.50,0.25+i*0.24,-1.84,0.8,0.06,0.1,metal,"turret");
    m.profileTube(0,0,0,[[-0.4,0.28],[0.45,0.27],[0.6,0.19],[1.9,0.18],[2.0,0.26],[2.5,0.26],[2.65,0.16],[5.2,0.12],[5.4,0.14]],metal,"barrel");
    m.armorBox(0,0,5.2,0.42,0.34,0.48,edge,"barrel");
    foreach(s in [-1,1]) for(local i=0;i<3;i++) m.armorBox(s*0.215,0,5.05+i*0.12,0.012,0.21,0.07,black,"barrel");
    m.roundTube(0,0,5.447,0.10,0.012,black,"barrel");
    m.roundTube(0,0.28,0.50,0.13,1.7,green,"barrel");
}
